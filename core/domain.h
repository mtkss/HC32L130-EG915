/**
 * @file domain.h
 * @brief 定义跨模块交换的业务值对象。
 * @details 不包含硬件访问、编码逻辑或可变全局对象；字段有效性与单位在声明处定义。
 * @note 各对象由所属模块持有；跨模块传递时遵循调用接口的按值复制或只读借用契约。
 */
#ifndef DOMAIN_H
#define DOMAIN_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/** @brief 操作状态；PENDING为在途，OK在start/结果查询中的具体含义见接口契约。 */
typedef enum
{
    RESULT_OK,       ///< 操作已完成；异步start的OK仅表示提交成功，完成仍查询result。
    RESULT_PENDING,  ///< 正在执行，结果数据尚不可按成功使用。
    RESULT_TIMEOUT,  ///< 超过当前操作期限；是否已释放硬件由接口另行约定。
    RESULT_IO,       ///< 外设/模块应答失败；取消流程也以此表示未完成成功。
    RESULT_FORMAT,   ///< 目标文本格式不匹配或必要字段缺失。
    RESULT_OVERFLOW, ///< 容量不足、接收溢出或输出会被截断。
    RESULT_BUSY,     ///< 单实例正忙，本次启动未被接受。
    RESULT_INVALID,  ///< 参数、状态或数值超出接口约束。
    RESULT_REJECTED  ///< 传输可成功，但业务确认被服务器拒绝。
} Result;
/** @brief 定位来源沿用线上编码；0表示尚未获得有效坐标。 */
typedef enum
{
    LOCATION_NONE = 0, ///< 无已确认来源；也用于请求按偏好选择/回退。
    LOCATION_GNSS = 1, ///< 线上type=1，来自GNSS有效定位。
    LOCATION_CELL = 2  ///< 线上type=2，来自基站HTTP换算坐标。
} LocationSource;
/** @brief 定位策略沿用原服务器编码。 */
typedef enum
{
    LOCATION_GNSS_FIRST = 1, ///< 线上locateType=1，先GNSS，允许时再回退基站。
    LOCATION_CELL_FIRST = 2  ///< 线上locateType=2，先基站，允许时再回退GNSS。
} LocationPolicy;
/** @brief 坐标及GNSS扩展字段；is_valid为唯一有效性判断，不用0坐标充当错误。 */
typedef struct
{
    double latitude_deg;   ///< 纬度，单位度，合法范围[-90,90]；0不是失败标记。
    double longitude_deg;  ///< 经度，单位度，合法范围[-180,180]。
    float speed_kmh;       ///< GNSS地面速度，km/h；基站定位默认0，不代表已测得静止。
    float altitude_m;      ///< GNSS海拔，m；基站结果默认0，不代表真实海拔。
    float bearing_deg;     ///< GNSS方向字段，度；未提供时保留默认0。
    LocationSource source; ///< 坐标成功解析后的实际来源，不能用请求偏好替代。
    bool is_valid;         ///< 只有解析通过后置true；其他字段即便非0也不能自行判成功。
} Position;
/** @brief 一次工作/缓存快照；按值存储，时间字段为上报前生成的UTC Unix秒。 */
typedef struct
{
    Position position;        ///< 本轮有效位置；只允许is_valid=true的记录进入普通上报。
    uint32_t timestamp_s;     ///< 上报前生成的UTC Unix秒；补传保留该值，不是硬件采样瞬间。
    float battery_v;          ///< 按原分压公式推导的电压V，尚无实际参考/分压校准。
    float temperature_legacy; ///< 旧10*ADC/(4096-ADC)推导量，非已校准摄氏温度；电阻标度待电路确认。
    bool is_moving;           ///< 组件给当前采样结果；APP可将本周期曾运动状态锁存到本记录。
    bool has_sensor_sample;   ///< ADC与完整运动采样成功标志；取消后的旧快照仍需结合事务结果使用。
    bool has_timestamp;       ///< 可生成日历时间戳；不代表RTC已被可信网络授时。
    uint8_t rssi;             ///< AT+CSQ数值编码；正常0..31、99通常未知；默认0不代表查询成功。
    int16_t rsrp_dbm;         ///< 主小区RSRP，dBm；无有效主小区结果时默认0，不可解释为实际0dBm。
} TelemetryRecord;
/** @brief 出厂/服务器配置；时间统一为分钟，配置仅整组提交。 */
typedef struct
{
    uint16_t still_interval_min; ///< 普通静止上报周期，分钟，本实现校验1..43200，待服务端契约确认。
    uint16_t
        motion_interval_min; ///< 本周期有运动时的周期，分钟，本实现校验1..43200，待服务端契约确认。
    uint16_t
        continuous_interval_min; ///< 连续3次静止上报后的周期，分钟，本实现校验1..43200，待服务端契约确认。
    LocationPolicy location_policy; ///< 下一次定位的优先策略；仅整组配置验证后生效。
} DeviceConfig;
/** @brief 设备身份；SN为六位十进制数，ICCID最多20字符。 */
typedef struct
{
    uint32_t serial_number; ///< 0..999999整数，线上按十进制输出；Flash存4字节小端。
    char imei[16];          ///< 最多15位数字加NUL；读取失败不应假定当前内容已更新。
    char iccid[21];         ///< 最多20位数字加NUL，当前身份属于发送时设备/SIM。
} DeviceIdentity;
/** @brief 蜂窝定位输入；邻区容量固定3，count必须不超过3。 */
typedef struct
{
    uint16_t mcc;     ///< 主小区十进制移动国家码；解析仅接受1..999。
    uint16_t mnc;     ///< 主小区十进制网络码，0..999；URL按兼容格式编码。
    char tac[16];     ///< 主小区TAC文本，最多15字符加NUL；不在此转换进制。
    char cell_id[16]; ///< 小区ID文本，最多15字符加NUL；主/邻区各自拥有副本。
    int16_t rssi_dbm; ///< QENG主小区RSSI，dBm；构造旧URL时加110编码。
    int16_t rsrp_dbm; ///< 主小区RSRP，dBm；无有效主小区结果时默认0，不可解释为实际0dBm。
    /** @brief 一个邻区条目，仅在有效数量范围内使用，不借用原串口文本。 */
    struct
    {
        char tac[8];        ///< 邻区TAC文本，最多7字符加NUL，实际长度由解析边界控制。
        char cell_id[16];   ///< 小区ID文本，最多15字符加NUL；主/邻区各自拥有副本。
        int16_t rsrp_dbm;   ///< 邻区RSRP，dBm；仅neighbor_count覆盖的槽位有效，不原地加偏移。
    } neighbors[3];         ///< 固定三邻区槽；neighbor_count表示有效项，编码仍沿用三个槽位。
    uint8_t neighbor_count; ///< 有效邻区数0..3；第4个及以后不写入数组。
} CellInfo;
/** @brief 自检独立结果，不改变正常上报调度计数。 */
typedef struct
{
    Position gnss;            ///< 单项GNSS自检结果；失败保持无效默认位置。
    Position cell;            ///< 单项基站自检结果，与GNSS分别保存。
    bool has_sensor_sample;   ///< ADC与完整运动采样成功标志；取消后的旧快照仍需结合事务结果使用。
    bool has_network;         ///< 本轮数据网络探测标志，不等同服务器可访问或HTTP成功。
    bool has_server_approval; ///< HTTP结束并解析status=1后的业务批准；Flash写入还需单独成功。
} SelfCheckResult;
#endif
