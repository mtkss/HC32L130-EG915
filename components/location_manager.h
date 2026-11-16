/**
 * @file location_manager.h
 * @brief 推进GNSS/基站定位、最多一次来源回退及GNSS关闭。
 * @details 独占本轮定位结果，依赖AT、HTTP和协议编解码；不决定上报周期、LED或Flash激活。
 * @note 单实例前台；等待通过poll推进；退出定位不等于模块已断电，统一关机由APP完成。
 */
#ifndef LOCATION_MANAGER_H
#define LOCATION_MANAGER_H
#include "domain.h"
/**
 * @brief 启动一次定位，按调用方指定来源或偏好选择分支。
 * @param policy 调用者须传GNSS_FIRST或CELL_FIRST。
 * @param only_source NONE允许按偏好回退；GNSS/CELL仅测该来源，不回退到另一项目。
 * @param serial_number 0..999999设备SN，按值保存供基站URL使用。
 * @return OK表示启动被接受，BUSY保持当前定位不变；只有location_result()==OK才有有效结果。
 * @note 前台不可重入；参数范围由调用者保证，start没有完整枚举范围校验。
 */
Result location_start(LocationPolicy policy, LocationSource only_source, uint32_t serial_number);
/**
 * @brief 每主循环推进定位一步；等待不阻塞，失败按启动策略回退。
 * @note 每轮先推进at_poll；基站分支调用HTTP，GNSS分支最多4次取定位，长等待均保存期限后返回。
 * @note 关闭AGPS/GPS失败仍会结束/回退，最终断电由APP负责；软件关闭完成不证明模块物理功能已停。
 */
void location_poll(void);
/** @brief 读取定位完成状态；只有OK才可使用position。 */
Result location_result(void);
/**
 * @brief 借用只读坐标到下次启动；失败不发布部分结果。
 * @return
 * 静态Position，只读到下一次location_start；取消不擦除旧位置，必须结合result与is_valid使用。
 */
const Position *location_position(void);
/**
 * @brief 读取最新主小区信号，供本条记录使用。
 * @return 当前cell副本的RSRP；0也可能是本轮没有成功取得主小区，接口不另外返回有效位。
 */
int16_t location_rsrp_dbm(void);
/**
 * @brief 取消定位及HTTP子流程；模块断电由统一关机出口负责。
 * @note 重复调用安全；退出软件等待并取消HTTP状态，不发送GNSS关闭命令，须由统一断电出口收尾。
 */
void location_cancel(void);
#endif
