/**
 * @file sampling_service.h
 * @brief 采集ADC并以在线三轴方差判断运动。
 * @details 依赖board有界ADC/I2C接口；只生成采样快照，不推进上报状态或累计上报周期。
 * @note 单实例前台；启动含模拟稳定和ADC等待，poll含有限I2C事务，不得在在途AT接收阶段调度。
 */
#ifndef SAMPLING_SERVICE_H
#define SAMPLING_SERVICE_H
#include "domain.h"
/**
 * @brief 启动50点运动采样；启动ADC有界轮询，后续按200ms推进。
 * @return OK表示ADC/传感器启动成功且开始异步样本采集，不代表已获得50个有效样本。
 * @note 前台不可重入；ADC稳定110ms加转换轮询上限100ms，I2C单事务20ms；不得在在途AT时调用。
 * @note 重复在途start返回BUSY不改变旧采样；其他失败会尝试关闭传感器，错误由result保留。
 */
Result sampling_start(void);
/**
 * @brief 到200ms期限时采一个样本；50个有效样本后完成，不追赶积压。
 * @note 前台到期采一个样本，不补采积压点；一次XYZ含6个有界寄存器读，随后可能执行关闭事务。
 */
void sampling_poll(void);
/** @brief 查询采样事务结果；旧快照有效位不能覆盖取消/失败终态。 */
Result sampling_result(void);
/**
 * @brief 按值复制当前采样快照；输出是否可作为本轮结果须结合事务状态。
 * @param record 可写输出；NULL无副作用；按值复制包括当前无效/旧结果。
 * @note 调用者须先检查sampling_result及has_sensor_sample；取消只改事务结果，不擦除已完成旧快照。
 */
void sampling_copy(TelemetryRecord *record);
/**
 * @brief 取消采样并在必要时关闭传感器，旧快照不主动擦除。
 * @note 可重复调用；仅PENDING时执行传感器关闭，最终result=IO；不清旧record内容。
 */
void sampling_cancel(void);
#endif
