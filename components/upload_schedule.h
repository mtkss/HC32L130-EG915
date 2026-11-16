/**
 * @file upload_schedule.h
 * @brief 按单调时间和运动锁存选择上报周期。
 * @details 不访问RTC、传感器或Flash；不自行上传，数据缓存和失败重试由APP负责。
 * @note 上下文由APP独占；now_ms由调用者传入，必须与上次起点使用同一单调时基。
 */
#ifndef UPLOAD_SCHEDULE_H
#define UPLOAD_SCHEDULE_H
#include "domain.h"
/** @brief 调度上下文由APP唯一持有；连续静止定义为连续3次静止上报。 */
typedef struct
{
    uint32_t last_attempt_ms;    ///< 上次完成上报尝试时的单调毫秒；回绕用无符号差值处理。
    unsigned still_report_count; ///< 连续静止上报次数，饱和于3；包括失败但已完成的尝试。
    bool has_motion;             ///< 自上次尝试以来观察到的运动锁存，complete后清除。
} UploadSchedule;
/**
 * @brief 初始化调度；首次上报由APP单独触发。
 * @param schedule APP独占的上下文，NULL时无副作用。
 * @param now_ms 当前单调毫秒，成为本次周期计时起点。
 * @note 重复初始化重置周期历史；首次上报不由本组件的due强制触发。
 */
void upload_schedule_init(UploadSchedule *schedule, uint32_t now_ms);
/**
 * @brief 记录运动并判断到期；传感器失败时不得调用本函数。
 * @param schedule APP独占的可写周期上下文；允许NULL时按本接口说明无副作用。
 * @param config 非NULL只读、整组已校验配置，三个周期均为1..43200分钟。
 * @param moving 当前有效运动采样结果；传感器失败时不能用false伪装静止。
 * @param now_ms 与last_attempt_ms同源的uint32单调毫秒，观察间隔不得跨完整2^32ms。
 * @return 是否达到当前周期；本函数会锁存运动，不能当无副作用查询。
 */
bool upload_schedule_due(UploadSchedule *schedule, const DeviceConfig *config, bool moving,
                         uint32_t now_ms);
/**
 * @brief 完成本次上报尝试后重设周期；失败数据由缓存保留。
 * @param schedule 可写独占上下文，NULL无副作用。
 * @param moving 此周期运动状态，决定连续静止计数清零或递增。
 * @param now_ms 本次尝试完成时的单调毫秒，作为下一周期起点。
 * @note 成功或失败但已完成的尝试都可重置周期；失败记录保留由APP/缓存另行负责。
 */
void upload_schedule_complete(UploadSchedule *schedule, bool moving, uint32_t now_ms);
#endif
