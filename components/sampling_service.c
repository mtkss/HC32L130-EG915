/**
 * @file sampling_service.c
 * @brief 采集ADC并以在线三轴方差判断运动。
 * @details 依赖board有界ADC/I2C接口；只生成采样快照，不推进上报状态或累计上报周期。
 * @note 单实例前台；启动含模拟稳定和ADC等待，poll含有限I2C事务，不得在在途AT接收阶段调度。
 */
#include "sampling_service.h"
#include "board.h"
#include "project_config.h"
#include <string.h>
/** @brief 前台独占ADC/运动采样事务；只复制结果，不修改APP状态。 */
static struct
{
    Result result;          ///< 本轮终态/在途标志；取消改IO但不自动擦record。
    unsigned count;         ///< 已成功参与方差统计的XYZ组数，范围0..50。
    float mean[3], m2[3];   ///< 轴顺序XYZ；均值单位g，m2为离均差平方和g²，完成时除count。
    uint32_t next_ms;       ///< 下一组采样截止单调毫秒；错过周期只采当前一组。
    TelemetryRecord record; ///< 本轮ADC和运动快照，完整成功才置has_sensor_sample。
} sampling;
/** @brief 重新建立采样统计，先采ADC，再启动运动传感器。 */
Result sampling_start(void)
{
    Result result;
    if (sampling.result == RESULT_PENDING)
    {
        return RESULT_BUSY;
    }
    memset(&sampling, 0, sizeof(sampling));
    result = board_sample_adc(&sampling.record.battery_v, &sampling.record.temperature_legacy);
    if (result == RESULT_OK)
    {
        result = board_motion_start();
    }
    sampling.result = result == RESULT_OK ? RESULT_PENDING : result;
    sampling.next_ms = board_now_ms();
    if (result != RESULT_OK)
    {
        board_motion_stop();
    }
    return result;
}
/** @brief 到采样期限时采一个XYZ样本，使用在线方差累计并在满足样本数后关闭传感器。 */
void sampling_poll(void)
{
    float axes[3];
    unsigned i;
    Result result;
    uint32_t now = board_now_ms();
    if (sampling.result != RESULT_PENDING || (int32_t)(now - sampling.next_ms) < 0)
    {
        return;
    }
    result = board_motion_sample(axes);
    if (result != RESULT_OK)
    {
        sampling.result = result;
        board_motion_stop();
        return;
    }
    /* Welford在线更新：mean += delta/n，M2 += delta*(x-new_mean)。三轴总体方差为sum(M2)/n，单位g²。
     */
    sampling.count++;
    for (i = 0; i < 3; i++)
    {
        float delta = axes[i] - sampling.mean[i];
        sampling.mean[i] += delta / (float)sampling.count;
        sampling.m2[i] += delta * (axes[i] - sampling.mean[i]);
    }
    /* 以本次实际采样时间安排下一次，前台超期不突发补采多个同一时刻的样本。 */
    sampling.next_ms = now + PROJECT_SAMPLE_INTERVAL_MS;
    if (sampling.count >= PROJECT_SAMPLE_COUNT)
    {
        sampling.record.is_moving =
            (sampling.m2[0] + sampling.m2[1] + sampling.m2[2]) / (float)sampling.count >
            PROJECT_MOTION_VARIANCE_G2;
        /* 只有ADC与50组三轴样本全部成功后发布有效标志，避免通信故障被当作零噪声静止。 */
        sampling.record.has_sensor_sample = true;
        sampling.result = RESULT_OK;
        board_motion_stop();
    }
}
/** @brief 读取采样事务结果；取消/失败与成功快照分开表达。 */
Result sampling_result(void)
{
    return sampling.result;
}
/** @brief 按值复制当前采样快照，不改变统计或采样状态。 */
void sampling_copy(TelemetryRecord *record)
{
    if (record)
    {
        *record = sampling.record;
    }
}

/** @brief 取消采样资格；仍在采样时先有界关闭传感器，不主动擦除旧快照。 */
void sampling_cancel(void)
{
    if (sampling.result == RESULT_PENDING)
    {
        board_motion_stop();
    }
    sampling.result = RESULT_IO;
}
