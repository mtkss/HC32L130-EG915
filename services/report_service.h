/**
 * @file report_service.h
 * @brief 将自检/普通记录编码后交给共用HTTP，并解释业务响应。
 * @details 依赖codec和HTTP；不修改采集对象、不自行提交配置或Flash标记。
 * @note 单实例前台；传输成功与服务器批准/配置合法性分别表达，取消不修改原记录。
 */
#ifndef REPORT_SERVICE_H
#define REPORT_SERVICE_H
#include "domain.h"
/** @brief 上传结果；HTTP/响应完成与服务器批准、配置有效性相互独立。 */
typedef struct
{
    bool is_approved;    ///< 只供自检：收到服务器status=1；仍未包含Flash标记写入结果。
    bool has_config;     ///< 只供普通上报：四项配置均解析且范围合法。
    DeviceConfig config; ///< 仅has_config=true时可整体提交；否则是清零默认副本。
} ReportOutcome;
/**
 * @brief 编码并复制请求后启动；不修改record/identity/check，check=NULL表示普通。
 * @param record 非NULL只读工作快照；需has_timestamp，普通上报还需有效位置；不保留指针。
 * @param identity 非NULL只读设备身份，IMEI/ICCID须为NUL终止数字串；不修改、不保留。
 * @param check NULL为普通上报；非NULL为自检，两种定位分别编码。
 * @return OK仅表示编码/复制并启动HTTP；BUSY表示在途；失败不修改输入记录。
 * @note 前台单实例，不可重入；调用返回后不保留参数指针，状态由poll/result查询。
 */
Result report_start(const TelemetryRecord *record, const DeviceIdentity *identity,
                    const SelfCheckResult *check);
/**
 * @brief 推进共享HTTP并独立解析服务器批准/整组配置。
 * @note 仅前台；HTTP完成后才解释业务内容。普通配置缺失不将已确认HTTP重新判成传输失败。
 */
void report_poll(void);
/**
 * @brief 查询传输结果；服务器是否批准自检由outcome另行表示。
 * @return 返回传输结果；自检status非1时仍可能为OK，APP须另查outcome.is_approved。
 */
Result report_result(void);
/**
 * @brief 借用只读上传结果到下次启动；失败不提交远程配置。
 * @return 只读静态结果到下一次report_start；只有has_config=true时config有提交资格。
 */
const ReportOutcome *report_outcome(void);
/**
 * @brief 取消上报及底层HTTP；不修改原始记录，调用者同时取消AT。
 * @note 重复调用安全；立即清除上报/HTTP等待，但不撤回已经发送的服务端数据；APP须同时取消AT。
 */
void report_cancel(void);
#endif
