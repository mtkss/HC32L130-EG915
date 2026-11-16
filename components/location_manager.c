/**
 * @file location_manager.c
 * @brief 推进GNSS/基站定位、最多一次来源回退及GNSS关闭。
 * @details 独占本轮定位结果，依赖AT、HTTP和协议编解码；不决定上报周期、LED或Flash激活。
 * @note 单实例前台；等待通过poll推进；退出定位不等于模块已断电，统一关机由APP完成。
 */
#include "location_manager.h"
#include "at_client.h"
#include "http_service.h"
#include "protocol_codec.h"
#include "project_config.h"
#include "board.h"
#include <string.h>
/** @brief 定位子状态；等待时间单独保存，主循环继续处理按键。 */
typedef enum
{
    LOC_CELL_WAIT,          ///< 保留原基站查询前15s等待，不占用AT。
    LOC_CELL_QUERY,         ///< 取得主小区；失败不能以空cell继续请求换坐标。
    LOC_NEIGHBORS,          ///< 邻区查询尽力而为，主小区有效时仍可构造固定槽位URL。
    LOC_CELL_HTTP,          ///< 等待换坐标HTTP并解析，成功才发布位置。
    LOC_GNSS_CONFIG_BD,     ///< 尝试北斗NMEA配置；失败仍按原兼容策略继续。
    LOC_GNSS_CONFIG_SYSTEM, ///< 尝试GNSS系统配置；配置应答不作为最终定位成功。
    LOC_AGPS,               ///< 尝试开启辅助定位；失败不阻止基本GNSS尝试。
    LOC_GPS,                ///< 请求开启GNSS，失败进入清理/回退。
    LOC_FIX_WAIT,           ///< 保存首次21s/后续11s等待截止，前台继续处理其他事件。
    LOC_FIX,                ///< 请求一次位置并校验；最多4次，不把部分字段发布。
    LOC_AGPS_CLOSE,         ///< 已有有效位置后尝试关闭辅助定位。
    LOC_GPS_CLOSE,          ///< 尝试关闭GPS后完成定位；关闭失败由最终模块断电收尾。
    LOC_FAILURE_AGPS_CLOSE, ///< 保留原失败原因，先尝试清理AGPS再决定回退。
    LOC_FAILURE_GPS_CLOSE   ///< 清理GPS完成后使用原失败原因回退/退出。
} LocationState;
/** @brief 前台独占定位上下文；start清零，输出只读借用到下一次start。 */
static struct
{
    LocationState state;                 ///< 当前命令/等待/清理阶段，不能用result单独重建阶段。
    Result result;                       ///< 本轮整体结果；PENDING保持步骤驱动。
    Result failure;                      ///< GNSS失败前的原因，清理命令结束后仍以原失败决定回退。
    CommandStep step;                    ///< 定位子流程唯一的AT步骤标志，阶段切换前须完成或取消。
    LocationSource source, only_source;  ///< 当前执行来源；only_source为NONE才允许跨来源回退。
    bool tried_gnss, tried_cell;         ///< 本轮已尝试来源，保证回退不会在两种定位之间无限循环。
    unsigned attempts;                   ///< 已失败的取GNSS定位次数；总尝试上限4。
    uint32_t deadline_ms, serial_number; ///< 当前等待截止单调毫秒；SN按值保存用于基站URL。
    CellInfo cell;                       ///< 主/邻区快照；只有对应成功解析后才代表本轮数据。
    Position position; ///< 成功解析后发布的坐标；取消不主动清旧内容，消费者须检查result。
} location;
/** @brief 按指定来源进入独立定位分支。 */
static void choose_source(LocationSource source)
{
    location.source = source;
    location.step.is_waiting = false;
    if (source == LOCATION_CELL)
    {
        location.tried_cell = true;
        location.state = LOC_CELL_WAIT;
        location.deadline_ms = board_now_ms() + 15000u;
    }
    else
    {
        location.tried_gnss = true;
        location.state = LOC_GNSS_CONFIG_BD;
    }
}
/**
 * @brief 在保留原始失败原因的前提下先清理GNSS，再决定回退或失败完成。
 * @param error 本轮定位失败码；清理应答失败不能覆盖这个原因。
 * @note 单项自检不回退；普通流程每种来源最多选一次，失败清理末态避免再次递归清理。
 */
static void fallback(Result error)
{
    /* GNSS操作失败也先关闭其资源，再按策略回退或完成失败。 */
    if (location.source == LOCATION_GNSS && location.state != LOC_FAILURE_GPS_CLOSE)
    {
        location.failure = error;
        location.state = LOC_FAILURE_AGPS_CLOSE;
        location.step.is_waiting = false;
        return;
    }

    if (location.only_source != LOCATION_NONE)
    {
        location.result = error;
        return;
    }
    if (!location.tried_gnss)
    {
        choose_source(LOCATION_GNSS);
    }
    else if (!location.tried_cell)
    {
        choose_source(LOCATION_CELL);
    }
    else
    {
        location.result = error;
    }
}
/** @brief 清理本轮定位上下文并选择来源；只提交操作，坐标由后续poll产生。 */
Result location_start(LocationPolicy policy, LocationSource only_source, uint32_t serial_number)
{
    if (location.result == RESULT_PENDING)
    {
        return RESULT_BUSY;
    }
    memset(&location, 0, sizeof(location));
    location.only_source = only_source;
    location.serial_number = serial_number;
    location.result = RESULT_PENDING;
    choose_source(only_source != LOCATION_NONE    ? only_source
                  : policy == LOCATION_CELL_FIRST ? LOCATION_CELL
                                                  : LOCATION_GNSS);
    return RESULT_OK;
}
/** @brief 推进定位命令、等待、解析和关闭阶段，失败按限定策略回退。 */
void location_poll(void)
{
    Result result = RESULT_PENDING;
    char url[256];
    uint32_t now = board_now_ms();
    if (location.result != RESULT_PENDING)
    {
        return;
    }
    switch (location.state)
    {
    case LOC_CELL_WAIT:
        if ((int32_t)(now - location.deadline_ms) >= 0)
        {
            location.state = LOC_CELL_QUERY;
        }
        return;
    case LOC_CELL_QUERY:
        result = at_command_step(&location.step, "AT+QENG=\"servingcell\"",
                                 "+QENG:", PROJECT_COMMAND_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            result = protocol_parse_cell(at_response(), &location.cell);
            if (result == RESULT_OK)
            {
                location.state = LOC_NEIGHBORS;
            }
        }
        break;
    /* 邻区缺失不否定已取得的主小区，URL保留固定三槽兼容格式；不把邻区失败伪装成有邻区。 */
    case LOC_NEIGHBORS:
        result = at_command_step(&location.step, "AT+QCELLINFO?", "OK", 10000u);
        if (result == RESULT_PENDING)
        {
            return;
        }
        if (result == RESULT_OK)
        {
            protocol_parse_neighbors(at_response(), &location.cell);
        }
        result = protocol_build_cell_url(&location.cell, location.serial_number, url, sizeof(url));
        if (result == RESULT_OK)
        {
            result = http_start(url, NULL);
            if (result == RESULT_OK)
            {
                location.state = LOC_CELL_HTTP;
                return;
            }
        }
        break;
    case LOC_CELL_HTTP:
        http_poll();
        result = http_result();
        if (result == RESULT_OK)
        {
            result = protocol_parse_position(http_response(), &location.position);
            if (result == RESULT_OK)
            {
                location.result = RESULT_OK;
            }
        }
        break;
    /* 两项配置与AGPS均为尽力请求，失败继续GPS是当前兼容策略，不能据函数名声称配置必然成功。 */
    case LOC_GNSS_CONFIG_BD:
        result = at_command_step(&location.step, "AT+QGPSCFG=\"beidounmeatype\",1", "OK",
                                 PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            location.state = LOC_GNSS_CONFIG_SYSTEM;
            return;
        }
        break;
    case LOC_GNSS_CONFIG_SYSTEM:
        result = at_command_step(&location.step, "AT+QGPSCFG=\"gnssconfig\",5", "OK",
                                 PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            location.state = LOC_AGPS;
            return;
        }
        break;
    case LOC_AGPS:
        result = at_command_step(&location.step, "AT+QAGPS=1", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            location.state = LOC_GPS;
            return;
        }
        break;
    case LOC_GPS:
        result = at_command_step(&location.step, "AT+QGPS=1", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            location.state = LOC_FIX_WAIT;
            location.deadline_ms = now + 21000u;
        }
        break;
    case LOC_FIX_WAIT:
        if ((int32_t)(now - location.deadline_ms) >= 0)
        {
            location.state = LOC_FIX;
        }
        return;
    case LOC_FIX:
        result = at_command_step(&location.step, "AT+QGPSLOC=2",
                                 "+QGPSLOC:", PROJECT_COMMAND_TIMEOUT_MS);
        if (result == RESULT_PENDING)
        {
            return;
        }
        if (result == RESULT_OK)
        {
            result = protocol_parse_gnss(at_response(), &location.position);
        }
        if (result == RESULT_OK)
        {
            location.state = LOC_AGPS_CLOSE;
            return;
        }
        /* 首次等待21s、后续11s分别来自旧1s+20s和1s+10s；最多4次，不无限等待fix。 */
        if (++location.attempts < 4)
        {
            location.deadline_ms = now + 11000u;
            location.state = LOC_FIX_WAIT;
            return;
        }
        break;
    case LOC_AGPS_CLOSE:
        result = at_command_step(&location.step, "AT+QAGPS=0", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            location.state = LOC_GPS_CLOSE;
            return;
        }
        break;
    /* 坐标已有效，关闭应答失败不撤销坐标；APP随后统一断电，负责最终资源收尾。 */
    case LOC_GPS_CLOSE:
        result = at_command_step(&location.step, "AT+QGPSEND", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            location.result = RESULT_OK;
            return;
        }
        break;
    case LOC_FAILURE_AGPS_CLOSE:
        result = at_command_step(&location.step, "AT+QAGPS=0", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            location.state = LOC_FAILURE_GPS_CLOSE;
            return;
        }
        break;
    /* 清理失败不覆盖原定位失败原因，也不再次进入清理循环；fallback最多再选另一来源一次。 */
    case LOC_FAILURE_GPS_CLOSE:
        result = at_command_step(&location.step, "AT+QGPSEND", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            fallback(location.failure);
            return;
        }
        break;
    default:
        result = RESULT_INVALID;
        break;
    }
    if (result != RESULT_PENDING && result != RESULT_OK)
    {
        fallback(result);
    }
}
/** @brief 查询定位事务状态，不把正在定位当作成功。 */
Result location_result(void)
{
    return location.result;
}
/** @brief 提供本轮只读位置快照；读取者仍须检查事务和is_valid。 */
const Position *location_position(void)
{
    return &location.position;
}
/** @brief 返回本轮缓存的主小区RSRP，不额外向模块查询。 */
int16_t location_rsrp_dbm(void)
{
    return location.cell.rsrp_dbm;
}

/** @brief 撤销软件定位/HTTP等待状态；物理模块断电仍由APP统一关机处理。 */
void location_cancel(void)
{
    location.result = RESULT_IO;
    location.step.is_waiting = false;
    http_cancel();
}
