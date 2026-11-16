/**
 * @file protocol_codec.c
 * @brief 解析目标AT/JSON格式并编码兼容上报报文。
 * @details 依赖core值对象与UTC转换及集中配置，不访问UART/Flash，不持有调用者缓冲。
 * @note 前台同步解析；只覆盖工程已采用的格式，不能替代通用JSON语法/外部模块版本验证。
 */
#include "protocol_codec.h"
#include "utc_calendar.h"
#include "project_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
/** @brief 判断有限浮点数，避免NaN绕过范围比较。 */
static bool finite_value(double value)
{
    return value == value && value > -1e30 && value < 1e30;
}
/**
 * @brief 在当前目标JSON文本中查找完整引号键并定位冒号后的值。
 * @param text 非NULL源文本，首次提交时至少length字节可读。
 * @param key 本模块受控目标键名，不是任意外部超长文本。
 * @return 值在原text中的只读借用指针；缺键或冒号不匹配返回NULL。
 * @note 这是轻量目标格式定位，不维护嵌套/字符串转义状态，不能声称完整JSON语法验证。
 */
static const char *find_value(const char *text, const char *key)
{
    char pattern[48];
    const char *cursor;
    if (!text || !key || snprintf(pattern, sizeof(pattern), "\"%s\"", key) < 0)
    {
        return NULL;
    }
    cursor = strstr(text, pattern);
    if (!cursor)
    {
        return NULL;
    }
    cursor += strlen(pattern);
    while (isspace((unsigned char)*cursor))
    {
        cursor++;
    }
    if (*cursor++ != ':')
    {
        return NULL;
    }
    while (isspace((unsigned char)*cursor))
    {
        cursor++;
    }
    return cursor;
}
/**
 * @brief 读取目标键的完整有限数值，避免将数字前缀加垃圾接受为合法字段。
 * @param text 非NULL源文本，首次提交时至少length字节可读。
 * @param key 本模块受控目标键名，不是任意外部超长文本。
 * @return true为数字和随后分隔符通过；false时value可能已改，不能提交到业务对象。
 * @note 可接受带双引号数值；调用者仍需检查整数、物理范围和业务枚举。
 */
static bool read_number(const char *text, const char *key, double *value)
{
    const char *cursor = find_value(text, key);
    char *end;
    bool quoted;
    if (!cursor)
    {
        return false;
    }
    quoted = (*cursor == '"');
    if (quoted)
    {
        cursor++;
    }
    *value = strtod(cursor, &end);
    if (end == cursor || !finite_value(*value))
    {
        return false;
    }
    if (quoted)
    {
        if (*end != '"')
        {
            return false;
        }
        end++;
    }
    while (isspace((unsigned char)*end))
    {
        end++;
    }
    return *end == ',' || *end == '}';
}
/** @brief 在临时对象中验证四个参数，全部合法后才一次性发布配置。 */
Result protocol_parse_config(const char *response, DeviceConfig *output)
{
    static const char *keys[] = {"uploadInterval", "moveUploadInterval", "continuesUploadInterval",
                                 "locateType"};
    double values[4];
    DeviceConfig parsed;
    unsigned i;
    if (!response || !output || !strchr(response, '}'))
    {
        return RESULT_FORMAT;
    }
    for (i = 0; i < 4; i++)
    {
        if (!read_number(response, keys[i], &values[i]) || values[i] != floor(values[i]))
        {
            return RESULT_FORMAT;
        }
    }
    for (i = 0; i < 3; i++)
    {
        if (values[i] < 1 || values[i] > PROJECT_MAX_INTERVAL_MIN)
        {
            return RESULT_INVALID;
        }
    }
    if (values[3] != 1 && values[3] != 2)
    {
        return RESULT_INVALID;
    }
    parsed.still_interval_min = (uint16_t)values[0];
    parsed.motion_interval_min = (uint16_t)values[1];
    parsed.continuous_interval_min = (uint16_t)values[2];
    parsed.location_policy = (LocationPolicy)(int)values[3];
    /* 所有必要字段检查通过后才提交临时对象，失败不会留下半组新配置/位置。 */
    *output = parsed;
    return RESULT_OK;
}
/** @brief 从目标JSON文本提取status，只有完整数值1表示服务器批准。 */
bool protocol_is_approved(const char *response)
{
    double status;
    return response && strchr(response, '}') && read_number(response, "status", &status) &&
           status == 1.0;
}
/**
 * @brief 将目标AT的一行CSV复制为有界字段，支持字段外围双引号。
 * @param text 非NULL NUL终止响应行；遇CR/LF终止。
 * @param fields 调用者提供capacity行、每行40字符的可写数组。
 * @param capacity 字段行数，调用者按目标格式提供；每字段有效字符最多39。
 * @return 解析字段数；0表示过长、过多或引号未闭合，失败时部分fields可能已写。
 * @note 私有前台解析，不支持转义双引号和通用CSV方言；只有调用者要求的完整字段数才有提交资格。
 */
static unsigned split_csv(const char *text, char fields[][40], unsigned capacity)
{
    unsigned count = 0, index = 0;
    bool quoted = false;
    while (*text && *text != '\r' && *text != '\n')
    {
        char c = *text++;
        if (c == '"')
        {
            quoted = !quoted;
            continue;
        }
        if (c == ',' && !quoted)
        {
            if (count >= capacity)
            {
                return 0;
            }
            fields[count++][index] = 0;
            index = 0;
            continue;
        }
        if (count >= capacity || index >= 39)
        {
            return 0;
        }
        fields[count][index++] = c;
    }
    if (quoted || count >= capacity)
    {
        return 0;
    }
    fields[count++][index] = 0;
    return count;
}
/** @brief 解析完整数字字段并校验有限性。 */
static bool parse_scalar(const char *field, double *value)
{
    char *end;
    *value = strtod(field, &end);
    if (end == field || !finite_value(*value))
    {
        return false;
    }
    while (isspace((unsigned char)*end))
    {
        end++;
    }
    return !*end;
}
/** @brief 把QGPSLOC目标格式解析到临时位置，字段和有效性校验通过后提交。 */
Result protocol_parse_gnss(const char *response, Position *output)
{
    /* 栈上11字段，每项含39字符+NUL；GNSS文本过长/字段缺失拒绝，不借用原响应指针发布。 */
    char fields[11][40];
    double values[9];
    const char *start;
    unsigned i;
    Position parsed = {0};
    static const unsigned indices[] = {0, 1, 2, 3, 4, 5, 7, 8, 10};
    if (!response || !output || ((start = strstr(response, "+QGPSLOC:")) == NULL))
    {
        return RESULT_FORMAT;
    }
    if (split_csv(start + 10, fields, 11) != 11)
    {
        return RESULT_FORMAT;
    }
    for (i = 0; i < 9; i++)
    {
        if (!parse_scalar(fields[indices[i]], &values[i]))
        {
            return RESULT_FORMAT;
        }
    }
    if (values[1] < -90 || values[1] > 90 || values[2] < -180 || values[2] > 180 ||
        (values[5] != 2 && values[5] != 3) || values[8] < 1 || values[8] > 99 ||
        values[8] != floor(values[8]) || values[3] < 0 || values[6] < 0 || values[7] < 0)
    {
        return RESULT_INVALID;
    }
    parsed.latitude_deg = values[1];
    parsed.longitude_deg = values[2];
    parsed.altitude_m = (float)values[4];
    parsed.speed_kmh = (float)values[6];
    if (*fields[6])
    {
        double bearing;
        if (!parse_scalar(fields[6], &bearing) || bearing < 0 || bearing >= 360)
        {
            return RESULT_FORMAT;
        }
        parsed.bearing_deg = (float)bearing;
    }
    parsed.source = LOCATION_GNSS;
    parsed.is_valid = true;
    *output = parsed;
    return RESULT_OK;
}
/** @brief 解析基站服务的经纬度并校验范围，保留0坐标的合法物理含义。 */
Result protocol_parse_position(const char *response, Position *output)
{
    Position parsed = {0};
    if (!response || !output)
    {
        return RESULT_INVALID;
    }
    if (!read_number(response, "lat", &parsed.latitude_deg) ||
        !read_number(response, "lon", &parsed.longitude_deg))
    {
        return RESULT_FORMAT;
    }
    if (parsed.latitude_deg < -90 || parsed.latitude_deg > 90 || parsed.longitude_deg < -180 ||
        parsed.longitude_deg > 180)
    {
        return RESULT_INVALID;
    }
    parsed.source = LOCATION_CELL;
    parsed.is_valid = true;
    *output = parsed;
    return RESULT_OK;
}
/** @brief 按原18字段QENG格式提取主小区信息，失败不污染调用者对象。 */
Result protocol_parse_cell(const char *response, CellInfo *output)
{
    /* 18字段临时数组占720字节栈，保留在预算内；格式不符不能猜测MCC/小区字段位置。 */
    char fields[18][40];
    const char *start;
    CellInfo parsed = {0};
    double mcc, mnc, rsrp, rssi;
    if (!response || !output || ((start = strstr(response, "+QENG:")) == NULL))
    {
        return RESULT_FORMAT;
    }
    if (split_csv(start + 6, fields, 18) != 18 || !strstr(fields[0], "servingcell"))
    {
        return RESULT_FORMAT;
    }
    if (!parse_scalar(fields[4], &mcc) || !parse_scalar(fields[5], &mnc) ||
        !parse_scalar(fields[13], &rsrp) || !parse_scalar(fields[15], &rssi))
    {
        return RESULT_FORMAT;
    }
    if (mcc < 1 || mcc > 999 || mcc != floor(mcc) || mnc < 0 || mnc > 999 || mnc != floor(mnc) ||
        rsrp < -156 || rsrp > -44 || rsrp != floor(rsrp) || rssi < -120 || rssi > -25 ||
        rssi != floor(rssi))
    {
        return RESULT_INVALID;
    }
    if (strlen(fields[6]) >= sizeof(parsed.cell_id) || strlen(fields[12]) >= sizeof(parsed.tac))
    {
        return RESULT_OVERFLOW;
    }
    parsed.mcc = (uint16_t)mcc;
    parsed.mnc = (uint16_t)mnc;
    parsed.rsrp_dbm = (int16_t)rsrp;
    parsed.rssi_dbm = (int16_t)rssi;
    strcpy(parsed.cell_id, fields[6]);
    strcpy(parsed.tac, fields[12]);
    *output = parsed;
    return RESULT_OK;
}
/** @brief 清除旧邻区并最多保留三个合格条目，额外条目不越界写入。 */
void protocol_parse_neighbors(const char *response, CellInfo *cell)
{
    const char *cursor = response;
    if (!cursor || !cell)
    {
        return;
    }
    cell->neighbor_count = 0;
    memset(cell->neighbors, 0, sizeof(cell->neighbors));
    while (cell->neighbor_count < 3 && (cursor = strstr(cursor, "+QCELLINFO:")) != NULL)
    {
        char fields[16][40];
        unsigned count = split_csv(cursor + 11, fields, 16);
        double signal;
        if (count >= 11 && strstr(fields[0], "neighbourcell") && !strcmp(fields[1], "LTE") &&
            parse_scalar(fields[10], &signal) && signal >= -150 && signal <= -40 &&
            strlen(fields[4]) < 8 && strlen(fields[5]) < 16)
        {
            unsigned index = cell->neighbor_count++;
            strcpy(cell->neighbors[index].tac, fields[4]);
            strcpy(cell->neighbors[index].cell_id, fields[5]);
            cell->neighbors[index].rsrp_dbm = (int16_t)signal;
        }
        cursor = strchr(cursor, '\n');
        if (!cursor)
        {
            break;
        }
        cursor++;
    }
}
/** @brief 使用固定三邻区槽位构造兼容URL，不原地修改信号或小区输入。 */
Result protocol_build_cell_url(const CellInfo *cell, uint32_t serial_number, char *output,
                               size_t capacity)
{
    int length;
    if (!cell || !output || !capacity || cell->neighbor_count > 3)
    {
        return RESULT_INVALID;
    }
    /* 旧接口信号加110再编码为无符号十六进制；低于-110dBm会下溢，拒绝而非发送FFFFFFxx。 */
    if (cell->rssi_dbm < -110 || cell->rssi_dbm > 0)
    {
        return RESULT_INVALID;
    }
    /* 信号加110和十六进制输出沿用旧服务参数，固定三邻区槽；不原地改写cell，也不声称信号标度已标定。
     */
    length = snprintf(
        output, capacity,
        PROJECT_CELL_URL "?x=%X-%02X-%s-%s-%X-%s-%s-%X-%s-%s-%X-%s-%s-%X&p=1&mt=0&id=%lu",
        (unsigned)cell->mcc, (unsigned)cell->mnc, cell->tac, cell->cell_id,
        (unsigned)(cell->rssi_dbm + 110), cell->neighbors[0].tac, cell->neighbors[0].cell_id,
        (unsigned)(cell->neighbor_count > 0 ? cell->neighbors[0].rsrp_dbm + 110 : 0),
        cell->neighbors[1].tac, cell->neighbors[1].cell_id,
        (unsigned)(cell->neighbor_count > 1 ? cell->neighbors[1].rsrp_dbm + 110 : 0),
        cell->neighbors[2].tac, cell->neighbors[2].cell_id,
        (unsigned)(cell->neighbor_count > 2 ? cell->neighbors[2].rsrp_dbm + 110 : 0),
        (unsigned long)serial_number);
    return length < 0 || (size_t)length >= capacity ? RESULT_OVERFLOW : RESULT_OK;
}
/** @brief 根据自检或普通模式编码不同JSON，校验容量后发布有效长度。 */
Result protocol_build_report(const TelemetryRecord *r, const DeviceIdentity *id,
                             const SelfCheckResult *check, char *out, size_t capacity,
                             size_t *length)
{
    int count;
    if (!r || !id || !out || !capacity || !length || !r->has_timestamp)
    {
        return RESULT_INVALID;
    }
    /* 自检报告两个独立来源；普通报告一条有效位置。两者只是报文不同，传输交给同一HTTP服务。 */
    if (check)
    {
        count = snprintf(out, capacity,
                         "{\"id\":\"%lu\",\"timestamp\":\"%lu\",\"imei\":\"%s\",\"gnssLat\":\"%."
                         "6f\",\"gnssLon\":\"%.6f\",\"baseStationLat\":\"%.6f\",\"baseStationLon\":"
                         "\"%.6f\",\"batt\":\"%.2f\",\"temperature\":\"%.1f\",\"gyroscope\":\"0."
                         "0\",\"rssi\":\"%u\",\"iccid\":\"%s\"}",
                         (unsigned long)id->serial_number, (unsigned long)r->timestamp_s, id->imei,
                         check->gnss.latitude_deg, check->gnss.longitude_deg,
                         check->cell.latitude_deg, check->cell.longitude_deg, r->battery_v,
                         r->temperature_legacy, (unsigned)r->rssi, id->iccid);
    }
    else
    {
        if (!r->position.is_valid)
        {
            return RESULT_INVALID;
        }
        count = snprintf(out, capacity,
                         "{\"id\":\"%lu\",\"timestamp\":\"%lu\",\"lat\":\"%.6f\",\"lon\":\"%.6f\","
                         "\"speed\":\"%.1f\",\"altitude\":\"%.1f\",\"acceleration\":\"%.1f\","
                         "\"batt\":\"%.3f\",\"temperature\":\"%.1f\",\"type\":\"%u\",\"isMove\":\"%"
                         "u\",\"rssi\":\"%u,%d\",\"iccid\":\"%s\"}",
                         (unsigned long)id->serial_number, (unsigned long)r->timestamp_s,
                         r->position.latitude_deg, r->position.longitude_deg, r->position.speed_kmh,
                         r->position.altitude_m, r->position.bearing_deg, r->battery_v,
                         r->temperature_legacy, (unsigned)r->position.source,
                         (unsigned)r->is_moving, (unsigned)r->rssi, (int)r->rsrp_dbm, id->iccid);
    }
    if (count < 0 || (size_t)count >= capacity)
    {
        return RESULT_OVERFLOW;
    }
    *length = (size_t)count;
    return RESULT_OK;
}
/** @brief 从指定前缀后提取受容量限制的数字标识，失败内容不得当有效身份使用。 */
Result protocol_parse_digits(const char *response, const char *prefix, char *output,
                             size_t capacity, size_t minimum)
{
    const char *cursor;
    size_t count = 0;
    if (!response || !prefix || !output || capacity < 2)
    {
        return RESULT_INVALID;
    }
    output[0] = 0;
    cursor = strstr(response, prefix);
    if (!cursor)
    {
        return RESULT_FORMAT;
    }
    cursor += strlen(prefix);
    while (isspace((unsigned char)*cursor) || *cursor == '"')
    {
        cursor++;
    }
    while (isdigit((unsigned char)*cursor))
    {
        if (count + 1 >= capacity)
        {
            output[0] = 0;
            return RESULT_OVERFLOW;
        }
        output[count++] = *cursor++;
    }
    output[count] = 0;
    return count >= minimum ? RESULT_OK : RESULT_FORMAT;
}
/** @brief 只接受成功QNTP时间格式，并按返回时区后缀计算UTC秒。 */
Result protocol_parse_ntp(const char *response, uint32_t *epoch)
{
    const char *start;
    unsigned y, m, d, h, min, s;
    int zone = 0;
    int used = 0;
    uint32_t value;
    if (!response || !epoch || ((start = strstr(response, "+QNTP: 0,\"")) == NULL))
    {
        return RESULT_FORMAT;
    }
    start += 10;
    if (sscanf(start, "%u/%u/%u,%u:%u:%u%n", &y, &m, &d, &h, &min, &s, &used) != 6)
    {
        return RESULT_FORMAT;
    }
    value = utc_calendar_to_epoch(y, m, d, h, min, s);
    if (!value)
    {
        return RESULT_INVALID;
    }
    if (start[used] == '+' || start[used] == '-')
    {
        char *end;
        long parsed = strtol(start + used, &end, 10);
        if (*end != '"' || parsed < -48 || parsed > 56)
        {
            return RESULT_FORMAT;
        }
        zone = (int)parsed;
    }
    else if (start[used] != '"')
    {
        return RESULT_FORMAT;
    }
    /* 返回后缀按15分钟单位解释，向UTC减去偏移；采用有符号64位中间量避免负偏移无符号回绕。 */
    *epoch = (uint32_t)((int64_t)value - (int64_t)zone * 900);
    return RESULT_OK;
}
