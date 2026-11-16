/**
 * @file upload_schedule.c
 * @brief 按单调时间和运动锁存选择上报周期。
 * @details 不访问RTC、传感器或Flash；不自行上传，数据缓存和失败重试由APP负责。
 * @note 上下文由APP独占；now_ms由调用者传入，必须与上次起点使用同一单调时基。
 */
#include "upload_schedule.h"
/** @brief 初始化本轮周期起点与运动锁存，首次上报由APP单独触发。 */
void upload_schedule_init(UploadSchedule *s, uint32_t now)
{
    if (s)
    {
        s->last_attempt_ms = now;
        s->still_report_count = 0;
        s->has_motion = false;
    }
}
/** @brief 锁存已观察到的运动，按实际毫秒差值与选定周期判断是否到期。 */
bool upload_schedule_due(UploadSchedule *s, const DeviceConfig *config, bool moving, uint32_t now)
{
    uint32_t interval;
    if (!s || !config)
    {
        return false;
    }
    /* 锁存整个上报周期曾出现的运动，后续静止样本不会提前清除。 */
    if (moving)
    {
        s->has_motion = true;
    }
    /* 曾有静止上报后再次观察到运动，立即要求一次新上报；否则按运动周期判断。 */
    if (s->has_motion)
    {
        if (s->still_report_count > 0)
        {
            return true;
        }
        interval = config->motion_interval_min;
    }
    else
    {
        interval = s->still_report_count >= 3 ? config->continuous_interval_min
                                              : config->still_interval_min;
    }
    /* 分钟先提升到32位再乘60000；合法配置最大30天，小于完整uint32毫秒回绕周期。 */
    return (uint32_t)(now - s->last_attempt_ms) >= interval * 60000u;
}
/** @brief 一次上报尝试完成后更新周期起点和静止次数，缓存策略由APP负责。 */
void upload_schedule_complete(UploadSchedule *s, bool moving, uint32_t now)
{
    if (s)
    {
        s->last_attempt_ms = now;
        if (moving || s->has_motion)
        {
            s->still_report_count = 0;
        }
        else if (s->still_report_count < 3)
        {
            s->still_report_count++;
        }
        s->has_motion = false;
    }
}
