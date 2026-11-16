/**
 * @file record_cache.h
 * @brief 保存前台独占的固定容量RAM定位记录FIFO。
 * @details 存储完整TelemetryRecord快照，不恢复/覆盖当前采集全局变量，不访问Flash。
 * @note 单实例前台；复位丢失；peek返回借用指针，其有效期结束于下次队列修改。
 */
#ifndef RECORD_CACHE_H
#define RECORD_CACHE_H
#include "domain.h"
/**
 * @brief 清空队列及丢弃计数，仅启动调用。
 * @note 前台启动时调用；重复调用会丢弃全部RAM缓存与覆盖历史。
 */
void record_cache_init(void);
/**
 * @brief 按值入队；满时覆盖最旧并累计丢弃数，重复最新记录忽略。
 * @param record NULL或位置无效时忽略；有效对象同步按值复制，不借用原指针。
 * @note 与最新项经纬度/时间戳完全相同视为重复；不比较电池等其他字段；满队列覆盖计数加1。
 */
void record_cache_push(const TelemetryRecord *record);
/**
 * @brief 借用最旧记录到下一次队列修改；发送成功才调用pop。
 * @return 空队列NULL，否则只读借用队首到下一次push/pop/init；发送失败不要pop。
 */
const TelemetryRecord *record_cache_peek(void);
/**
 * @brief 删除已成功发送的最旧项；空队列无副作用。
 * @note 前台唯一拥有者调用；仅在发送确认后移除，函数本身不验证服务端是否接收。
 */
void record_cache_pop(void);
/** @brief 返回当前缓存条数，范围0..20，前台读取。 */
unsigned record_cache_count(void);
/** @brief 返回覆盖丢弃累计数，复位归零。 */
unsigned record_cache_dropped(void);
#endif
