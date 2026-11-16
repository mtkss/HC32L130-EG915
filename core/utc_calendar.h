/**
 * @file utc_calendar.h
 * @brief 转换UTC Unix秒和公历字段。
 * @details 只依赖标准整数/布尔类型，不访问RTC、单调时基或运行库时区。
 * @note 前台纯算法；输入和输出由调用者拥有，不保存指针。
 */
#ifndef UTC_CALENDAR_H
#define UTC_CALENDAR_H
#include <stdint.h>
/**
 * @brief 有效公历转Unix秒；支持2000..2099，字段非法返回0，不做时区转换。
 * @param year 公历年份，2000..2099。
 * @param month 公历月份，1..12。
 * @param day 月份内有效日序，考虑闰年。
 * @param hour UTC小时，0..23。
 * @param minute UTC分钟，0..59。
 * @param second UTC秒，0..59。
 * @return 2000..2099且日期合法时返回UTC秒，否则0；不读取时区配置。
 */
uint32_t utc_calendar_to_epoch(unsigned year, unsigned month, unsigned day, unsigned hour,
                               unsigned minute, unsigned second);
/**
 * @brief Unix秒转年月日时分秒；fields提供6元素，NULL无副作用。
 * @param epoch_s uint32 UTC秒。
 * @param fields 六个可写unsigned，依次年/月/日/时/分/秒；NULL不写入。
 * @note NULL输出无副作用；不限制转换结果年份到2099，RTC写入方须另查硬件范围。
 */
void utc_calendar_from_epoch(uint32_t epoch_s, unsigned fields[6]);
#endif
