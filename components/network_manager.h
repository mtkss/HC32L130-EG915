/**
 * @file network_manager.h
 * @brief 推进EG800Z上电、AT/SIM就绪、自动选网及PDP激活。
 * @details 依赖AT与板级电源接口；只拥有本轮网络快照，不修改定位或上传配置。
 * @note
 * 单实例前台；关机也异步推进；非俄罗斯自动选网；MCC250保留两运营商RAM历史；板级接线沿用用户确认。
 */
#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H
#include "domain.h"
/** @brief 本轮模块及数据网络探测结果；data_ready不等同HTTP业务成功。 */
typedef struct
{
    bool modem_ready; ///< 本轮ATE0收到OK；不代表SIM/注册/HTTP均成功。
    bool data_ready;  ///< 数据上下文/探测流程给出的可用性，不能证明业务服务可访问。
    uint8_t rssi;     ///< 本轮CSQ原始数值编码；查询未成功时仍为默认0。
    CellInfo cell;    ///< 国家判断阶段解析的主小区副本，可能没有有效字段。
} NetworkSnapshot;
/**
 * @brief 开始上电和注册；identity为借用输出，必须存活到完成。
 * @param identity 非NULL可写对象，由本服务借用到network_result()!=PENDING且不再推进该操作。
 * @return OK表示上电流程已开始；BUSY保持原流程；NULL返回INVALID。
 * @note 前台非重入；会逐项更新IMEI/ICCID，单项失败不回滚其他已成功字段，失败字段可能保留旧内容。
 */
Result network_start(DeviceIdentity *identity);
/**
 * @brief 推进一个非阻塞步骤；先自动附着/PDP，MCC250才保留原俄罗斯运营商业务策略。
 * @note 与其他AT拥有者互斥，需持续at_poll；每步只提交/查询一次，PWRKEY/重试间隔由毫秒期限控制。
 * @note
 * APN/自动选网/附着/PDP失败返回原错误；身份/CSQ/小区元数据尽力查询。俄罗斯探测的可用位仍按原策略；最终须检查data_ready，且不证明业务服务器可访问。
 */
void network_poll(void);
/** @brief 查询本轮网络流程结果；OK表示模块流程结束，数据是否可用另见snapshot。 */
Result network_result(void);
/**
 * @brief 借用最新网络快照，只读有效到下一次network_start。
 * @return 静态只读快照到下一次network_start；默认零值本身不证明相关查询成功。
 */
const NetworkSnapshot *network_snapshot(void);
/**
 * @brief 请求关机：PA4低3s/高30s后PB4断电，异步且幂等。
 * @note 可重复调用，已在关机时不重置期限；取消AT不撤回模块已执行动作。无供电时立即具备完成条件。
 */
void network_shutdown_start(void);
/**
 * @brief 推进关机并报告是否已断电；调用期间继续轮询AT和按键。
 * @return true表示软件已请求PB4断电或本来未供电；不证明电源实际降为0V。
 * @note 前台周期调用；PA4低至少3000ms，再高至少30000ms，poll滞后会延长而不会缩短等待。
 */
bool network_shutdown_poll(void);
#endif
