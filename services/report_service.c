/**
 * @file report_service.c
 * @brief 将自检/普通记录编码后交给共用HTTP，并解释业务响应。
 * @details 依赖codec和HTTP；不修改采集对象、不自行提交配置或Flash标记。
 * @note 单实例前台；传输成功与服务器批准/配置合法性分别表达，取消不修改原记录。
 */
#include "report_service.h"
#include "protocol_codec.h"
#include "http_service.h"
#include "project_config.h"
#include <string.h>
/** @brief 前台编码工作区；report_start完成复制后HTTP拥有独立副本。 */
static char payload[PROJECT_HTTP_CAPACITY];
/** @brief 本轮业务解释结果；start清零，poll成功后更新，APP只读。 */
static ReportOutcome outcome;
/** @brief 上报整体事务状态；不替代outcome中的服务器批准位。 */
static Result status;
/** @brief 本次报文的业务类型，start确定后保持到结束/取消。 */
static bool is_self_check;
/** @brief 编码指定业务报文并启动共享HTTP，发送数据与采集对象解耦。 */
Result report_start(const TelemetryRecord *record, const DeviceIdentity *identity,
                    const SelfCheckResult *check)
{
    size_t length;
    Result result;
    if (status == RESULT_PENDING)
    {
        return RESULT_BUSY;
    }
    memset(&outcome, 0, sizeof(outcome));
    result = protocol_build_report(record, identity, check, payload, sizeof(payload), &length);
    if (result != RESULT_OK)
    {
        status = result;
        return result;
    }
    is_self_check = check != NULL;
    result = http_start(is_self_check ? PROJECT_SELF_CHECK_URL : PROJECT_POSITION_URL, payload);
    status = result == RESULT_OK ? RESULT_PENDING : result;
    return result;
}
/** @brief 先等待HTTP完整结束，再分别提取自检批准或整组配置结果。 */
void report_poll(void)
{
    if (status != RESULT_PENDING)
    {
        return;
    }
    http_poll();
    status = http_result();
    if (status != RESULT_OK)
    {
        return;
    }
    /* 服务器自检批准与普通远程配置是独立业务结果；不在传输层提交Flash或全局配置。 */
    if (is_self_check)
    {
        outcome.is_approved = protocol_is_approved(http_response());
    }
    else
    {
        outcome.has_config = protocol_parse_config(http_response(), &outcome.config) == RESULT_OK;
    }
}
/** @brief 读取上报传输状态，不把服务器自检拒绝混同为链路失败。 */
Result report_result(void)
{
    return status;
}
/** @brief 借用本轮业务解释结果，由APP决定是否提交配置或Flash标记。 */
const ReportOutcome *report_outcome(void)
{
    return &outcome;
}

/** @brief 撤销上报及其HTTP子事务资格，不修改调用者采集记录。 */
void report_cancel(void)
{
    status = RESULT_IO;
    http_cancel();
}
