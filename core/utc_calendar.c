/**
 * @file utc_calendar.c
 * @brief 转换UTC Unix秒和公历字段。
 * @details 只依赖标准整数/布尔类型，不访问RTC、单调时基或运行库时区。
 * @note 前台纯算法；输入和输出由调用者拥有，不保存指针。
 */
#include "utc_calendar.h"
#include <stdbool.h>
/** @brief 公历闰年，仅用于2000..2099及Unix转换。 */
static bool leap(unsigned year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
/** @brief 返回公历月份天数；调用方先校验month。 */
static unsigned month_days(unsigned year, unsigned month)
{
    static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1] + (month == 2 && leap(year) ? 1u : 0u);
}
/** @brief 将已校验公历日期转换为UTC Unix秒，不读取RTC或修改时区。 */
uint32_t utc_calendar_to_epoch(unsigned y, unsigned m, unsigned d, unsigned h, unsigned min,
                               unsigned s)
{
    unsigned year, month;
    uint32_t days = 0;
    if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1 || d > month_days(y, m) || h > 23 ||
        min > 59 || s > 59)
    {
        return 0;
    }
    for (year = 1970; year < y; year++)
    {
        days += leap(year) ? 366u : 365u;
    }
    for (month = 1; month < m; month++)
    {
        days += month_days(y, month);
    }
    return ((days + d - 1u) * 24u + h) * 3600u + min * 60u + s;
}
/** @brief 把UTC Unix秒分解为日历字段，供RTC BCD适配使用。 */
void utc_calendar_from_epoch(uint32_t epoch, unsigned fields[6])
{
    uint32_t days = epoch / 86400u;
    unsigned year = 1970, month = 1;
    if (!fields)
    {
        return;
    }
    fields[3] = (epoch / 3600u) % 24u;
    fields[4] = (epoch / 60u) % 60u;
    fields[5] = epoch % 60u;
    while (days >= (leap(year) ? 366u : 365u))
    {
        days -= leap(year) ? 366u : 365u;
        year++;
    }
    while (days >= month_days(year, month))
    {
        days -= month_days(year, month);
        month++;
    }
    fields[0] = year;
    fields[1] = month;
    fields[2] = (unsigned)days + 1u;
}
