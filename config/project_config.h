/**
 * @file project_config.h
 * @brief 集中管理容量、默认周期、协议地址和超时预算。
 * @details 原始硬件事实与线上兼容值保持来源约束；新的毫秒预算不冒充原迭代计数的精确换算。
 * @note 编译期配置；容量改变须重查RAM/栈，周期与超时改变须重跑边界/回绕及实板时序验证。
 */
#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H
/** @brief RX字节环总槽数，当前512；留一槽区分满/空，可容纳511字节，满时丢字节并置溢出。 */
#define PROJECT_UART_RX_CAPACITY 512u
/** @brief 单事务累计响应容量，当前768字节含NUL；满则事务失败，不截断后继续解析。 */
#define PROJECT_AT_RESPONSE_CAPACITY 768u
/** @brief HTTP请求正文工作区容量，当前512字节含NUL；影响报文长度上限和静态RAM。 */
#define PROJECT_HTTP_CAPACITY 512u
/** @brief RAM完整记录槽数，当前20；满时覆盖最旧，复位丢失，不引入Flash擦写。 */
#define PROJECT_CACHE_CAPACITY 20u
/** @brief 默认周期单位为分钟；与旧工程出厂值一致。 */
/** @brief 默认静止周期180分钟，来自原main.c；仅启动装载，后续允许服务器整组更新。 */
#define PROJECT_DEFAULT_STILL_MIN 180u
/** @brief 默认运动周期60分钟，来自原main.c；指上报周期而非传感器采样周期。 */
#define PROJECT_DEFAULT_MOTION_MIN 60u
/** @brief 默认连续静止周期720分钟，来自原main.c；连续3次静止上报后采用。 */
#define PROJECT_DEFAULT_CONTINUOUS_MIN 720u
/** @brief 服务器周期校验上限43200分钟（30天），为本重构有界默认，不是原协议已确认上限。 */
#define PROJECT_MAX_INTERVAL_MIN 43200u
/** @brief 以下毫秒数来自旧流程显式时序或新的有界默认预算，须实板确认。 */
/** @brief 三次有效短按确认窗10500ms，近似保留原909个TIM0节拍；未作实板测量。 */
#define PROJECT_KEY_CONFIRM_WINDOW_MS 10500u
/** @brief 工作提示翻转间隔116ms，近似原双计数翻转周期；实际亮灭极性仍由硬件决定。 */
#define PROJECT_LED_TOGGLE_MS 116u
/** @brief 普通AT响应预算5000ms，新毫秒默认；原迭代计数无法直接换算，需真实模块核验。 */
#define PROJECT_COMMAND_TIMEOUT_MS 5000u
/** @brief HTTP每一阶段响应预算85000ms；不是整个上报操作总期限，模块命令内部另有80s/60s/20s参数。 */
#define PROJECT_HTTP_TIMEOUT_MS 85000u
/** @brief 手动/自动COPS等待上限120000ms，来自原显式Tele2锁网预算；非整轮网络总时长。 */
#define PROJECT_NETWORK_TIMEOUT_MS 120000u
/** @brief 失败/取消后要求250ms无新字节；连续噪声另有2s排空上限，不能提供URC完美事务关联。 */
#define PROJECT_AT_QUIET_MS 250u
/** @brief 运动样本名义间隔200ms，沿用原SC7A20任务；前台延迟时不追赶补采。 */
#define PROJECT_SAMPLE_INTERVAL_MS 200u
/** @brief 运动方差统计50组XYZ，沿用原实现；在线统计不保存整组数组。 */
#define PROJECT_SAMPLE_COUNT 50u
/** @brief 三轴总体方差和阈值0.015g²，沿用原经验值，未重新标定。 */
#define PROJECT_MOTION_VARIANCE_G2 0.015f
/** @brief RTC闹钟分钟字段增加10，来自原唤醒策略；不是保证精确睡满600秒。 */
#define PROJECT_WAKE_INTERVAL_MIN 10u
/** @brief 旧SN/标记基址0xEE00；链接器显式将该地址起的区域排除在代码之外。 */
#define PROJECT_FLASH_ID_ADDRESS 0xEE00u
/** @brief 普通位置上报的原兼容地址；不在本轮重构修改服务端路径或协议。 */
#define PROJECT_POSITION_URL "http://182.92.135.10:5055/receData/position"
/** @brief 自检独立接口的原兼容地址；与普通上报共用传输但业务字段/批准语义不同。 */
#define PROJECT_SELF_CHECK_URL "http://182.92.135.10:5055/receData/selfCheck"
/** @brief 原基站换坐标服务地址；参数布局在protocol_codec中按旧格式编码。 */
#define PROJECT_CELL_URL "http://gps.qianxingtu.cn/api/cw"
/** @brief 沿用旧APN sxzcat1；适用SIM/运营商需实网确认，不依据Ping结果自动改写。 */
#define PROJECT_APN "sxzcat1"
#endif
