/**
 * @file protocol_codec.h
 * @brief 解析目标AT/JSON格式并编码兼容上报报文。
 * @details 依赖core值对象与UTC转换及集中配置，不访问UART/Flash，不持有调用者缓冲。
 * @note 前台同步解析；只覆盖工程已采用的格式，不能替代通用JSON语法/外部模块版本验证。
 */
#ifndef PROTOCOL_CODEC_H
#define PROTOCOL_CODEC_H
#include "domain.h"
/**
 * @brief 从完整响应提取整组配置；失败不改变output，范围1..43200分钟。
 * @param response 非NULL NUL终止完整响应，目标键和值须符合当前轻量解析格式。
 * @param output 非NULL可写配置；仅全部四项通过解析/整数及范围校验才覆盖。
 * @return FORMAT为字段/文本问题，INVALID为数值范围问题；失败保持旧output。
 * @note 非通用JSON解析器，不声称拒绝所有重复键、嵌套或语法变体。
 */
Result protocol_parse_config(const char *response, DeviceConfig *output);
/**
 * @brief 解析字符串/数字status；只有精确整数1通过。
 * @param response 非NULL NUL终止完整响应；字段格式受轻量解析器支持范围约束。
 * @return 仅成功解析到整数1时为true；不修改响应。
 */
bool protocol_is_approved(const char *response);
/**
 * @brief 解析QGPSLOC并检查fix、字段及经纬范围；成功才提交output。
 * @param response 非NULL NUL终止串，含+QGPSLOC:的原11字段响应。
 * @param output 非NULL；只在字段完整、数值有限、坐标范围及fix>=2/卫星>=1通过后提交。
 * @note 不使用GNSS日期给RTC授时；本接口只发布Position。
 */
Result protocol_parse_gnss(const char *response, Position *output);
/**
 * @brief 解析基站坐标JSON；允许赤道和本初子午线的0值。
 * @param response 非NULL NUL终止目标响应，调用期间不可被修改。
 * @param output 非NULL可写位置对象；成功才提交完整Position，失败保持旧值。
 * @note 0纬度/0经度合法；使用轻量数值提取，不提供通用JSON schema验证。
 */
Result protocol_parse_position(const char *response, Position *output);
/**
 * @brief 解析QENG的18字段格式，成功才提交，未知格式拒绝。
 * @param response 非NULL NUL终止目标响应，调用期间不可被修改。
 * @param output 非NULL可写主小区对象；成功才整体提交，失败保持旧值。
 * @note MCC/MNC/RSRP/RSSI做当前范围检查；TAC/CellID保留文本，不声称完整十六进制语法验证。
 */
Result protocol_parse_cell(const char *response, CellInfo *output);
/**
 * @brief 解析逐行QCELLINFO；最多3个邻区，超出安全忽略。
 * @param response 非NULL NUL终止多行QCELLINFO文本。
 * @note 先清邻区和count，再逐条写入；没有合格项时count=0，非失败回滚接口。
 * @param cell 非NULL可写主/邻区对象，调用先清邻区及数量，再保存合格项。
 */
void protocol_parse_neighbors(const char *response, CellInfo *cell);
/**
 * @brief 构造旧协议基站URL；失败不发送，不修改cell。
 * @param cell 非NULL，neighbor_count<=3；三个槽的字符串均须NUL终止。
 * @param serial_number 0..999999设备SN，调用者保证范围。
 * @return OK表示完整URL，OVERFLOW表示不足；失败时output可能已有截断前缀。
 * @note 同步借用输入；旧信号偏移与固定槽位格式沿用原实现，不代表已校准无线测量。
 * @param output 非NULL可写URL缓冲，失败可能包含截断前缀，不得发送。
 * @param capacity output总字节容量，必须>0且预留NUL。
 */
Result protocol_build_cell_url(const CellInfo *cell, uint32_t serial_number, char *output,
                               size_t capacity);
/**
 * @brief 编码普通/自检JSON；output不含CRLF，length仅在成功时有效。
 * @param record 非NULL只读工作快照，has_timestamp=true；普通模式要求有效位置。
 * @param identity 非NULL只读身份，其字符串为受控NUL终止数字串，不提供任意字符串转义。
 * @param check NULL编码普通报文，非NULL编码独立自检两种坐标；不保留指针。
 * @param output 非NULL可写JSON缓冲，必须提供capacity字节；失败可能留下截断前缀。
 * @param capacity 输出总容量，单位字节，含NUL空间；长度上限另见本接口。
 * @param length 非NULL，仅成功时更新；失败缓冲可能已有截断内容，不得发送。
 * @note 身份字符串须由受控数字接口产生；字段沿用原协议，不新增JSON字符串转义器。
 */
Result protocol_build_report(const TelemetryRecord *record, const DeviceIdentity *identity,
                             const SelfCheckResult *check, char *output, size_t capacity,
                             size_t *length);
/**
 * @brief 从指定前缀后提取有界数字标识，仅成功返回时允许使用输出。
 * @param response 非NULL NUL终止目标响应，调用期间不可被修改。
 * @param prefix 非NULL NUL终止前缀，指明要读取的数字段。
 * @return OK表示位数不少于minimum且未越界；OVERFLOW将output[0]清零；FORMAT可能留下短数字串。
 * @note 只有OK时才能使用输出；minimum不是精确位数校验，最大长度由capacity-1决定。
 * @param output 非NULL可写字符缓冲；只有OK时才可作为有效数字标识使用。
 * @param capacity output字节容量，至少2，数字最大长度capacity-1。
 * @param minimum 允许最少数字位数，调用者提供不超过capacity-1的期望。
 */
Result protocol_parse_digits(const char *response, const char *prefix, char *output,
                             size_t capacity, size_t minimum);
/**
 * @brief 解析成功QNTP日历，按返回时区后缀换算UTC；缺失时区按UTC处理。
 * @param response 非NULL NUL终止目标响应，调用期间不可被修改。
 * @param epoch_s 非NULL；成功才更新UTC秒输出。
 * @note 时区后缀解释为每单位15分钟，缺失时区按UTC；此约定需真实模块版本确认。
 */
Result protocol_parse_ntp(const char *response, uint32_t *epoch_s);

#endif
