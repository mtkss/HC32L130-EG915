/**
 * @file network_manager.c
 * @brief 推进模块上电、AT/SIM就绪、国家判断和运营商选择。
 * @details 依赖AT与板级电源接口；只拥有网络历史和本轮快照，不修改定位或上传配置。
 * @note 单实例前台；关机也异步推进；RAM历史跨普通睡眠保留、冷启动清除。
 */
#include "network_manager.h"
#include "at_client.h"
#include "protocol_codec.h"
#include "project_config.h"
#include "board.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/** @brief 网络步骤与设备电源阶段；数字不对应外部协议。 */
typedef enum
{
    NET_SUPPLY,     ///< 供电高后等待1s，保证原上电顺序。
    NET_KEY_HIGH,   ///< PWRKEY高至少100ms后开始低脉冲。
    NET_KEY_LOW,    ///< 低至少2500ms后拉高并开始AT探测。
    NET_AT_READY,   ///< 最多60次ATE0探测，失败间隔100ms。
    NET_SIM_READY,  ///< 最多150次CPIN READY探测，失败不伪造SIM就绪。
    NET_APN,        ///< 尝试原APN配置，失败仍继续国家/网络判断。
    NET_DEREGISTER, ///< 先停止自动注册，成功后才做Tele2首试。
    NET_TELE2,
    NET_COUNTRY,       ///< QENG确定MCC，缺新结果时保留RAM历史判断。
    NET_AUTO,          ///< 恢复自动选网，随后再有限等待附着。
    NET_AUTO_WAIT,     ///< 最多60s附着窗口，不把恢复COPS的OK等同数据已就绪。
    NET_AUTO_ACTIVATE, ///< 尝试激活PDP，并更新本轮data_ready。
    NET_RU_SELECT,     ///< 选择本轮尚需测试的俄罗斯运营商。
    NET_RU_ATTACH,     ///< 确认CGATT=1，失败只结束该运营商探测。
    NET_RU_ACTIVATE,   ///< 尝试激活后仍以Ping进一步探测，不单凭QIACT失败判封锁。
    NET_RU_PING,       ///< 探测当前链路，失败仅为本软件分类，不推断运营商政策。
    NET_RU_RESTORE,    ///< 轮询后回到首个可用目标，再尝试PDP激活。
    NET_ICCID,         ///< 查询当前SIM标识；失败仍继续其他元数据。
    NET_IMEI,          ///< 查询模块标识，不根据旧字段推断本次查询成功。
    NET_CSQ,           ///< 保存CSQ数值编码，不转换为未经验证的dBm。
    NET_CONTENT,       ///< 设置兼容内容类型，失败仍结束准备流程。
    NET_DONE           ///< 流程已结束；modem_ready/data_ready分别说明就绪程度。
} NetworkState;
/** @brief 前台独占的本轮网络流程；identity借用调用方对象，其余字段由本模块管理。 */
static struct
{
    NetworkState state; ///< 当前电源/AT/搜网/元数据阶段，零初始化进入NET_SUPPLY。
    Result result;      ///< 整体流程状态；OK只表示已走完，不要求data_ready为真。
    CommandStep step;   ///< 本网络流程独占AT步骤标志。
    uint32_t deadline_ms, registration_ms; ///< 下一等待截止与自动注册窗口起点，单位单调毫秒。
    unsigned attempts, poll_pos;           ///< 当前阶段失败探测次数；order中的当前位置0或1。
    uint8_t order[2], target; ///< 运营商顺序：0=25002、1=25020；target=0xFF表示尚无可用目标。
    DeviceIdentity *identity; ///< 只借用至本轮结束的可写身份对象，不分配/释放。
    NetworkSnapshot snapshot; ///< 本轮模块/数据就绪及查询结果；初始清零不等于各查询成功。
} network;
/* RAM网络历史仅跨睡眠保留；复位清除，不宣称运营商冷却计时。 */
/** @brief 仅RAM保存的最近确认国家码；0表示无历史，不等同非俄罗斯。 */
static uint16_t last_mcc;
/** @brief 位0/1为两运营商曾附着历史；last_success为索引，0xFF无成功者；冷启动重置。 */
static uint8_t registered_mask, last_success = 0xFF;
/** @brief 软件供电请求状态与关机流程是否在途，不是电压检测结果。 */
static bool supply_is_on, shutdown_active;
/** @brief 0等待PA4低3s，1等待高30s；仅关机前台更新。 */
static unsigned shutdown_stage;
/** @brief 当前关机电平阶段起点，单调毫秒；重复请求不重置。 */
static uint32_t shutdown_ms;
/** @brief 期限比较仅用于小于2^31ms的间隔。 */
static bool due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}
/** @brief 当前操作改为下一阶段，不保留上一阶段等待标志。 */
static void advance(NetworkState state)
{
    network.state = state;
    network.step.is_waiting = false;
    network.attempts = 0;
}
/**
 * @brief 结束当前俄罗斯运营商探测，保留首个可用者并决定是否继续轮询。
 * @param attached 已确认附着，或已走到Ping阶段；用于置历史位，不表示HTTP成功。
 * @param ready 本次Ping成功的软件分类，不保证业务服务器可达。
 * @note 前台，poll_pos必须为0或1；注册历史仅RAM，不是24小时限制计时器。
 */
static void next_operator(bool attached, bool ready)
{
    uint8_t idx = network.order[network.poll_pos];
    if (attached)
    {
        registered_mask |= (uint8_t)(1u << idx);
    }
    if (ready && network.target == 0xFF)
    {
        network.target = idx;
    }
    if (ready && idx == last_success && registered_mask == 3u)
    {
        network.target = idx;
        advance(NET_RU_RESTORE);
        return;
    }
    network.poll_pos++;
    if (network.poll_pos >= 2)
    {
        advance(network.target == 0xFF ? NET_AUTO : NET_RU_RESTORE);
    }
    else
    {
        advance(NET_RU_SELECT);
    }
}
/** @brief 取得身份输出对象的借用权，开始本轮模块上电和网络准备。 */
Result network_start(DeviceIdentity *identity)
{
    if (network.result == RESULT_PENDING || shutdown_active)
    {
        return RESULT_BUSY;
    }
    if (!identity)
    {
        return RESULT_INVALID;
    }
    memset(&network, 0, sizeof(network));
    network.identity = identity;
    network.result = RESULT_PENDING;
    network.target = 0xFF;
    network.order[0] = last_success < 2 ? last_success : 0;
    network.order[1] = (uint8_t)(1u - network.order[0]);
    board_modem_supply(true);
    supply_is_on = true;
    board_modem_key(true);
    network.deadline_ms = board_now_ms() + 1000u;
    return RESULT_OK;
}
/** @brief 推进电源、AT就绪、注册及身份查询，网络失败不直接修改上报策略。 */
void network_poll(void)
{
    char command[96];
    Result result = RESULT_PENDING;
    uint32_t now = board_now_ms();
    const char *response;
    if (network.result != RESULT_PENDING)
    {
        return;
    }
    switch (network.state)
    {
    case NET_SUPPLY:
        if (due(now, network.deadline_ms))
        {
            network.deadline_ms = now + 100u;
            advance(NET_KEY_HIGH);
        }
        return;
    case NET_KEY_HIGH:
        if (due(now, network.deadline_ms))
        {
            board_modem_key(false);
            network.deadline_ms = now + 2500u;
            advance(NET_KEY_LOW);
        }
        return;
    case NET_KEY_LOW:
        if (due(now, network.deadline_ms))
        {
            board_modem_key(true);
            network.deadline_ms = now;
            advance(NET_AT_READY);
        }
        return;
    case NET_AT_READY:
        if (!due(now, network.deadline_ms))
        {
            return;
        }
        result = at_command_step(&network.step, "ATE0", "OK", 1000u);
        if (result == RESULT_OK)
        {
            network.snapshot.modem_ready = true;
            advance(NET_SIM_READY);
        }
        else if (result != RESULT_PENDING)
        {
            if (++network.attempts >= 60)
            {
                network.result = result;
            }
            else
            {
                network.deadline_ms = now + 100u;
            }
        }
        return;
    case NET_SIM_READY:
        if (!due(now, network.deadline_ms))
        {
            return;
        }
        result = at_command_step(&network.step, "AT+CPIN?", "+CPIN: READY", 1000u);
        if (result == RESULT_OK)
        {
            advance(NET_APN);
        }
        else if (result != RESULT_PENDING)
        {
            if (++network.attempts >= 150)
            {
                advance(NET_ICCID);
            }
            else
            {
                network.deadline_ms = now + 100u;
            }
        }
        return;
    /* APN配置失败仍保留后续查询机会；当前不根据运营商猜测替代APN。 */
    case NET_APN:
        result = at_command_step(&network.step, "AT+QICSGP=1,1,\"" PROJECT_APN "\",\"\",\"\"", "OK",
                                 PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            advance(NET_DEREGISTER);
        }
        return;
    case NET_DEREGISTER:
        result = at_command_step(&network.step, "AT+COPS=2", "OK", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            advance(result == RESULT_OK ? NET_TELE2 : NET_ICCID);
        }
        return;
    /* 无论锁网成功与否都查询QENG；不能把Tele2未注册直接当作非俄罗斯。 */
    case NET_TELE2:
        result = at_command_step(&network.step, "AT+COPS=1,2,\"25020\",7", "OK",
                                 PROJECT_NETWORK_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            advance(NET_COUNTRY);
        }
        return;
    /* 本轮主小区可确认国家；查询失败保留RAM历史，首次无历史不擅自认定“其他国家”。 */
    case NET_COUNTRY:
        result = at_command_step(&network.step, "AT+QENG=\"servingcell\"",
                                 "+QENG:", PROJECT_COMMAND_TIMEOUT_MS);
        if (result == RESULT_PENDING)
        {
            return;
        }
        if (result == RESULT_OK &&
            protocol_parse_cell(at_response(), &network.snapshot.cell) == RESULT_OK)
        {
            last_mcc = network.snapshot.cell.mcc;
        }
        advance(last_mcc == 250 ? NET_RU_SELECT : last_mcc ? NET_AUTO : NET_ICCID);
        return;
    case NET_AUTO:
        result = at_command_step(&network.step, "AT+COPS=0", "OK", PROJECT_NETWORK_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            network.registration_ms = now;
            network.deadline_ms = now;
            advance(NET_AUTO_WAIT);
        }
        return;
    case NET_AUTO_WAIT:
        if (!due(now, network.deadline_ms))
        {
            return;
        }
        result = at_command_step(&network.step, "AT+CGATT?", "+CGATT: 1", 2000u);
        if (result == RESULT_OK)
        {
            advance(NET_AUTO_ACTIVATE);
        }
        else if (result != RESULT_PENDING)
        {
            if ((uint32_t)(now - network.registration_ms) >= 60000u)
            {
                advance(NET_ICCID);
            }
            else
            {
                network.deadline_ms = now + 1000u;
            }
        }
        return;
    case NET_AUTO_ACTIVATE:
        result = at_command_step(&network.step, "AT+QIACT=1", "OK", 30000u);
        if (result != RESULT_PENDING)
        {
            network.snapshot.data_ready = result == RESULT_OK;
            advance(NET_ICCID);
        }
        return;
    /* 0=25002，1=25020；已找到目标时可跳过曾附着运营商，历史不是冷却倒计时。 */
    case NET_RU_SELECT:
        if (network.target != 0xFF && (registered_mask & (1u << network.order[network.poll_pos])))
        {
            next_operator(false, false);
            return;
        }
        snprintf(command, sizeof(command), "AT+COPS=1,2,\"%s\",7",
                 network.order[network.poll_pos] == 0 ? "25002" : "25020");
        result = at_command_step(&network.step, command, "OK", PROJECT_NETWORK_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            if (result == RESULT_OK)
            {
                advance(NET_RU_ATTACH);
            }
            else
            {
                next_operator(false, false);
            }
        }
        return;
    case NET_RU_ATTACH:
        result =
            at_command_step(&network.step, "AT+CGATT?", "+CGATT: 1", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            if (result == RESULT_OK)
            {
                advance(NET_RU_ACTIVATE);
            }
            else
            {
                next_operator(false, false);
            }
        }
        return;
    /* 旧兼容策略不以QIACT的单一应答裁决受限，继续Ping；最终业务连通性仍以HTTP为准。 */
    case NET_RU_ACTIVATE:
        result = at_command_step(&network.step, "AT+QIACT=1", "OK", 30000u);
        if (result != RESULT_PENDING)
        {
            advance(NET_RU_PING);
        }
        return;
    case NET_RU_PING:
        result = at_command_step(&network.step, "AT+QPING=1,\"8.8.8.8\",4,1", "+QPING: 0", 10000u);
        if (result != RESULT_PENDING)
        {
            next_operator(true, result == RESULT_OK);
        }
        return;
    case NET_RU_RESTORE:
        snprintf(command, sizeof(command), "AT+COPS=1,2,\"%s\",7",
                 network.target == 0 ? "25002" : "25020");
        result = at_command_step(&network.step, command, "OK", PROJECT_NETWORK_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            if (result == RESULT_OK)
            {
                last_success = network.target;
                advance(NET_AUTO_ACTIVATE);
            }
            else
            {
                advance(NET_ICCID);
            }
        }
        return;
    /* 身份/CSQ为逐项尽力更新，解析失败返回值未阻止后续阶段，也未整体回滚已有身份字段。 */
    case NET_ICCID:
        result = at_command_step(&network.step, "AT+QCCID", "+QCCID:", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            if (result == RESULT_OK)
            {
                protocol_parse_digits(at_response(), "+QCCID:", network.identity->iccid,
                                      sizeof(network.identity->iccid), 18);
            }
            advance(NET_IMEI);
        }
        return;
    case NET_IMEI:
        result = at_command_step(&network.step, "AT+CGSN=1", "+CGSN:", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            if (result == RESULT_OK)
            {
                protocol_parse_digits(at_response(), "+CGSN:", network.identity->imei,
                                      sizeof(network.identity->imei), 15);
            }
            advance(NET_CSQ);
        }
        return;
    case NET_CSQ:
        result = at_command_step(&network.step, "AT+CSQ", "+CSQ:", PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            unsigned rssi, ber;
            response = at_response();
            if (result == RESULT_OK && ((response = strstr(response, "+CSQ:")) != NULL) &&
                sscanf(response + 5, "%u,%u", &rssi, &ber) == 2 && rssi <= 99)
            {
                network.snapshot.rssi = (uint8_t)rssi;
            }
            advance(NET_CONTENT);
        }
        return;
    /* 内容类型设置失败也结束准备流程；因此整体OK只表流程结束，不能替代就绪位。 */
    case NET_CONTENT:
        result = at_command_step(&network.step, "AT+QHTTPCFG=\"contenttype\",4", "OK",
                                 PROJECT_COMMAND_TIMEOUT_MS);
        if (result != RESULT_PENDING)
        {
            advance(NET_DONE);
            network.result = RESULT_OK;
        }
        return;
    case NET_DONE:
        network.result = RESULT_OK;
        return;
    default:
        network.result = RESULT_INVALID;
        return;
    }
}
/** @brief 查询流程结束与否；数据网络可用性单独保存在snapshot中。 */
Result network_result(void)
{
    return network.result;
}
/** @brief 借用网络状态快照，供APP复制本条记录所需字段。 */
const NetworkSnapshot *network_snapshot(void)
{
    return &network.snapshot;
}
/** @brief 取消在途AT并开始有界PWRKEY关机序列，重复请求不重置关机期限。 */
void network_shutdown_start(void)
{
    if (shutdown_active)
    {
        return;
    }
    at_cancel();
    network.result = RESULT_IO;
    network.step.is_waiting = false;
    shutdown_stage = 0;
    shutdown_active = supply_is_on;
    shutdown_ms = board_now_ms();
    if (shutdown_active)
    {
        board_modem_key(false);
    }
}
/** @brief 按毫秒期限推进关机，只有PB4断电后才报告完成。 */
bool network_shutdown_poll(void)
{
    uint32_t now = board_now_ms();
    if (!shutdown_active)
    {
        return true;
    }
    if (shutdown_stage == 0 && (uint32_t)(now - shutdown_ms) >= 3000u)
    {
        board_modem_key(true);
        shutdown_stage = 1;
        shutdown_ms = now;
    }
    else if (shutdown_stage == 1 && (uint32_t)(now - shutdown_ms) >= 30000u)
    {
        board_modem_supply(false);
        supply_is_on = false;
        shutdown_active = false;
    }
    return !shutdown_active;
}
