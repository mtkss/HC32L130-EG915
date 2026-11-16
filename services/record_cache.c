/**
 * @file record_cache.c
 * @brief 保存前台独占的固定容量RAM定位记录FIFO。
 * @details 存储完整TelemetryRecord快照，不恢复/覆盖当前采集全局变量，不访问Flash。
 * @note 单实例前台；复位丢失；peek返回借用指针，其有效期结束于下次队列修改。
 */
#include "record_cache.h"
#include "project_config.h"
#include <string.h>
/** @brief 前台拥有的固定数组，逻辑有效区从head起连续count项，按模容量回绕。 */
static TelemetryRecord records[PROJECT_CACHE_CAPACITY];
/** @brief 最旧项索引、有效数量和因满覆盖累计数；不由IRQ访问。 */
static unsigned head, count, dropped;
/** @brief 在启动阶段清空RAM队列和覆盖计数，不操作Flash。 */
void record_cache_init(void)
{
    head = count = dropped = 0;
    memset(records, 0, sizeof(records));
}
/** @brief 复制独立记录到FIFO；只与最新条目去重，满时覆盖最旧记录。 */
void record_cache_push(const TelemetryRecord *record)
{
    unsigned last;
    if (!record || !record->position.is_valid)
    {
        return;
    }
    if (count)
    {
        last = (head + count - 1u) % PROJECT_CACHE_CAPACITY;
        if (records[last].timestamp_s == record->timestamp_s &&
            records[last].position.latitude_deg == record->position.latitude_deg &&
            records[last].position.longitude_deg == record->position.longitude_deg)
        {
            return;
        }
    }
    /* 容量满时先推进最旧索引再写新队尾；保留有界RAM占用并累计可观测丢弃数。 */
    if (count == PROJECT_CACHE_CAPACITY)
    {
        head = (head + 1u) % PROJECT_CACHE_CAPACITY;
        count--;
        dropped++;
    }
    records[(head + count) % PROJECT_CACHE_CAPACITY] = *record;
    count++;
}
/** @brief 借用最旧记录而不出队，确保上传失败时仍保留原始快照。 */
const TelemetryRecord *record_cache_peek(void)
{
    return count ? &records[head] : NULL;
}
/** @brief 只在发送已确认后移除队首，空队列调用安全。 */
void record_cache_pop(void)
{
    if (count)
    {
        head = (head + 1u) % PROJECT_CACHE_CAPACITY;
        count--;
    }
}
/** @brief 读取当前有效项数，不改变队列顺序。 */
unsigned record_cache_count(void)
{
    return count;
}
/** @brief 读取因容量满而覆盖的累计记录数。 */
unsigned record_cache_dropped(void)
{
    return dropped;
}
