/**
 * @file location_manager.c
 * @brief 按EG800Z命令能力推进GNSS/主小区定位和有界来源回退。
 * @details GNSS命令依据用户确认可用于EU的CN V1.0.0手册；不配置持久化星座/AGNSS参数。
 * @note 单实例前台，AT/HTTP互斥；HTTP失败可能遗留数据模式，须先统一断电，不能立即发GNSS命令。
 */
#include "location_manager.h"
#include "at_client.h"
#include "http_service.h"
#include "protocol_codec.h"
#include "project_config.h"
#include "board.h"
#include <string.h>
/** @brief 顺序定位阶段；清理共享一个终态，避免成功和失败重复关机链。 */
typedef enum
{
    LOC_CELL_WAIT,
    LOC_CELL_QUERY,
    LOC_CELL_HTTP, ///< 主小区换坐标；不误用QCELL RX_lev为RSRP。
    LOC_GNSS_PROBE,
    LOC_GNSS_QUERY,
    LOC_GPS, ///< 探测能力/当前状态，再决定是否启动。
    LOC_FIX_WAIT,
    LOC_FIX,
    LOC_GPS_CLOSE ///< 策略窗口内查询，最后无论成败均关闭GNSS。
} LocationState;
/** @brief 定位前台私有上下文；start清零，位置仅在整体成功后可借用。 */
static struct
{
    LocationState state;
    Result result, failure; ///< failure保存原始定位结果，清理错误不覆盖已有有效坐标。
    CommandStep step;
    LocationSource source, only_source;
    bool tried_gnss, tried_cell, has_gnss_request; ///< 防止跨来源循环；已发启用请求即需要清理。
    uint32_t deadline_ms, fix_started_ms;          ///< 下次探测期限与GNSS窗口起点，单调毫秒。
    uint32_t serial_number;
    CellInfo cell;
    Position position;
} location;
/** @brief 建立来源初态，不强行更改模块NVRAM中的星座组合。 */
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
        location.state = LOC_GNSS_PROBE;
    }
}
/** @brief 若已发GNSS请求先清理；每个来源最多一次，HTTP不确定退出模式不在此回退。 */
static void fallback(Result error)
{
    if (location.source == LOCATION_GNSS && location.has_gnss_request &&
        location.state != LOC_GPS_CLOSE)
    {
        location.failure = error;
        location.state = LOC_GPS_CLOSE;
        location.step.is_waiting = false;
        return;
    }
    if (location.only_source != LOCATION_NONE)
    {
        location.result = error;
    }
    else if (!location.tried_gnss)
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
/** @brief 接受定位请求，保存参数并选择首次来源。 */
Result location_start(LocationPolicy policy, LocationSource only_source, uint32_t serial_number)
{
    if (location.result == RESULT_PENDING)
    {
        return RESULT_BUSY;
    }
    if ((policy != LOCATION_GNSS_FIRST && policy != LOCATION_CELL_FIRST) ||
        (only_source != LOCATION_NONE && only_source != LOCATION_GNSS &&
         only_source != LOCATION_CELL) ||
        serial_number > 999999u)
    {
        return RESULT_INVALID;
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
/** @brief 推进单步GNSS或HTTP，所有等待返回主循环；只提交经过校验的完整位置。 */
void location_poll(void)
{
    Result result = RESULT_PENDING;
    uint32_t now = board_now_ms();
    char url[256];
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
        result = at_command_step(&location.step, "AT+QENG=\"servingcell\"", "OK",
                                 PROJECT_COMMAND_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            result = protocol_parse_cell(at_response(), &location.cell);
            if (result == RESULT_OK)
            {
                result = protocol_build_cell_url(&location.cell, location.serial_number, url,
                                                 sizeof(url));
            }
            if (result == RESULT_OK)
            {
                result = http_start(url, NULL);
            }
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
        /* URL/POST超时可能还在数据模式；不能把下一条GNSS命令当成URL/正文发送。APP将统一关机。 */
        if (result != RESULT_PENDING && result != RESULT_OK)
        {
            location.result = result;
        }
        return;
    case LOC_GNSS_PROBE:
        result = at_command_step(&location.step, "AT+QGPS=?", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            if (strstr(at_response(), "+QGPS:"))
            {
                location.state = LOC_GNSS_QUERY;
            }
            else
            {
                result = RESULT_FORMAT;
            }
        }
        break;
    case LOC_GNSS_QUERY:
        result = at_command_step(&location.step, "AT+QGPS?", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            const char *p = strstr(at_response(), "+QGPS:");
            if (!p)
            {
                result = RESULT_FORMAT;
                break;
            }
            p += 6;
            while (*p == ' ')
            {
                p++;
            }
            if ((p[0] == '0' || p[0] == '1') && (p[1] == '\r' || p[1] == '\n'))
            {
                if (*p == '1')
                {
                    location.has_gnss_request = true;
                    location.fix_started_ms = now;
                    location.deadline_ms = now;
                    location.state = LOC_FIX_WAIT;
                }
                else
                {
                    location.state = LOC_GPS;
                }
            }
            else
            {
                result = RESULT_FORMAT;
            }
        }
        break;
    case LOC_GPS:
        location.has_gnss_request = true;
        result = at_command_step(&location.step, "AT+QGPS=1", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            location.fix_started_ms = now;
            location.deadline_ms = now + PROJECT_GNSS_POLL_INTERVAL_MS;
            location.state = LOC_FIX_WAIT;
        }
        break;
    case LOC_FIX_WAIT:
        if ((uint32_t)(now - location.fix_started_ms) >= PROJECT_GNSS_FIX_TIMEOUT_MS)
        {
            fallback(RESULT_TIMEOUT);
            return;
        }
        if ((int32_t)(now - location.deadline_ms) >= 0)
        {
            location.state = LOC_FIX;
        }
        return;
    case LOC_FIX:
        result = at_command_step(&location.step, "AT+QGPSLOC=2", "OK", PROJECT_COMMAND_TIMEOUT_MS);
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
            location.failure = RESULT_OK;
            location.state = LOC_GPS_CLOSE;
            return;
        }
        /* 未定位/CME 516等仍可在产品120秒窗口内重试；窗口不是模块TTFF保证。 */
        if ((uint32_t)(now - location.fix_started_ms) < PROJECT_GNSS_FIX_TIMEOUT_MS)
        {
            location.deadline_ms = now + PROJECT_GNSS_POLL_INTERVAL_MS;
            location.state = LOC_FIX_WAIT;
            return;
        }
        break;
    case LOC_GPS_CLOSE:
        result = at_command_step(&location.step, "AT+QGPSEND", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            location.has_gnss_request = false;
            if (location.failure == RESULT_OK)
            {
                location.result = RESULT_OK;
            }
            else
            {
                fallback(location.failure);
            }
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
