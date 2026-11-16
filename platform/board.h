/**
 * @file board.h
 * @brief 独占HC32板级寄存器、时钟、引脚、UART环及中断事件。
 * @details 引脚仅来自board_pins.h，寄存器访问使用原DDL；UTC转换来自core，不依赖上层业务服务。
 * @note 除明示IRQ处理器外仅前台使用；UART/时基/事件共享读写以PRIMASK临界区交接，volatile不充当锁。
 */
#ifndef BOARD_H
#define BOARD_H
#include "domain.h"
/** @brief 唤醒事件位；读取采用临界区快照并消费。 */
typedef enum
{
    BOARD_WAKE_NONE = 0, ///< 无已发布唤醒事件，不代表物理输入始终未变化。
    BOARD_WAKE_KEY = 1,  ///< PB11下降沿事件位；有效短按仍需前台消抖确认。
    BOARD_WAKE_RTC = 2   ///< RTC闹钟事件位；唤醒后在原调用栈继续。
} BoardWake;
/**
 * @brief 启动时钟、RTC、UART、按键和TIM0；不自动开启模块电源。
 * @note 复位后前台调用一次；先清接收状态并启动时基，再开放外设中断。
 * @note 此void接口不向APP传播所调用DDL的初始化返回值；时钟、RTC和Flash就绪仍需实板确认。
 */
void board_init(void);
/**
 * @brief 单调毫秒计数，TIM0余数累计；睡眠后以RTC经过时间补偿。
 * @return uint32毫秒，约49.7天回绕；TIM0累计分数余量，深睡由RTC秒差补偿。
 * @note 前台读取使用PRIMASK快照；活动超时不得在TIM0不能推进的关中断上下文中等待。
 */
uint32_t board_now_ms(void);
/**
 * @brief 设置PB4模块供电使能电平；前台非阻塞。
 * @param enabled true请求PB4高使能，false请求低；重复调用重建该引脚配置，不反馈电源实测状态。
 */
void board_modem_supply(bool enabled);
/**
 * @brief 设置PA4电平；实际PWRKEY时间由network_manager编排。
 * @param high true请求PA4高、false请求低；前台同步设置，不在此函数内等待开/关机完成。
 */
void board_modem_key(bool high);
/**
 * @brief LED输出；极性沿用原代码，true输出高。
 * @param high 实际GPIO电平，非抽象亮灭；LED有效极性尚需电路确认。
 */
void board_led_write(bool high);
/**
 * @brief 前台轮询按键消抖/LED之外事件；返回并消费一次有效短按。
 * @return 一次确认的短按释放事件；约24ms稳定消抖，持续>=3000ms释放不生成短按。
 * @note 前台应持续轮询；休眠/长停顿期间的电平历史不由本函数重建。
 */
bool board_take_short_press(void);
/**
 * @brief 原子消费唤醒事件位。
 * @return RTC/KEY位组合，读取后清零；PRIMASK保护快照，ISR新写入不会被前台分步覆盖。
 */
unsigned board_take_wake_events(void);
/**
 * @brief 发送精确字节，不追加换行；有界轮询，失败返回IO。
 * @param bytes 至少count字节可读；count=0时允许NULL，非0时NULL返回INVALID。
 * @param count 精确发送字节数，不包括隐式终止符或换行。
 * @return OK表示每字节轮询发送完成；IO表示首个失败，之前已发字节不可回滚。
 * @note 前台有界轮询，每字节DDL迭代预算0xFFFF；并非固定毫秒上界，不可在IRQ中使用。
 */
Result board_uart_write(const char *bytes, size_t count);
/**
 * @brief 从IRQ环形队列取一个字节；false表示空。
 * @param byte 非NULL单字节输出；空队列或NULL返回false且不改输出。
 * @note 临界区只覆盖索引/字节交接；有效容量为配置值减1。
 */
bool board_uart_read(char *byte);
/**
 * @brief 丢弃旧接收并消费溢出状态；前台调用，受临界区保护。
 * @note 前台临界区内移动消费者索引并消费溢出状态，不承诺清除外设尚未读出的字节。
 */
void board_uart_flush(void);
/**
 * @brief 原子读取并消费接收溢出标志；true要求当前AT事务失败。
 * @return 是否丢失过RX字节；读取清零，true要求上层终止/丢弃不完整事务。
 */
bool board_uart_take_overflow(void);
/**
 * @brief RTC UTC秒读写；读取失败返回0；设置仅接受2000..2099。
 * @return 可解析的2000..2099 UTC秒，否则0；非0不代表网络授时成功或日期准确。
 */
uint32_t board_rtc_epoch(void);
/**
 * @brief 设置有效UTC日历；不修改单调通信计时。
 * @param epoch_s 转换后年份须为2000..2099；非法返回INVALID。
 * @return DDL设置成功为OK，失败为IO；不改变board_now_ms活动计时。
 */
Result board_rtc_set(uint32_t epoch_s);
/**
 * @brief 设分钟字段+10闹钟并深睡；有待处理按键时不睡，返回后恢复时基/UART。
 * @note 前台且没有在途AT/采样时调用；APP须先统一关机，不在此函数代发PWRKEY序列。
 * @note 关闭UART/TIM0/I2C并设置闹钟；若有唤醒位或按键仍低，不执行WFI；返回仍恢复外设。
 * @note 无软件唤醒超时，返回依赖硬件中断/事件；秒差补偿有量化误差，不能当作精确600000ms睡眠。
 */
void board_sleep(void);
/**
 * @brief 两路ADC采集；稳定110ms加轮询上限100ms，失败不改输出并关闭资源。
 * @param battery_v 非NULL电池电压输出，单位V；只有采样成功时更新。
 * @param temperature_legacy 非NULL旧温度推导量输出；无摄氏度校准声明。
 * @return INVALID表示参数/结果不合法，TIMEOUT表示100ms完成等待到期。
 * @note 前台：稳定110ms另加轮询100ms预算及收尾；需TIM0 IRQ持续推进。温度输出沿用旧分压推导量。
 */
Result board_sample_adc(float *battery_v, float *temperature_legacy);
/**
 * @brief SC7A20启动/单次XYZ采样/停止；I2C单事务20ms，失败关闭控制器。
 * @return I2C读取/配置操作结果；中间失败立即返回，由拥有者再调用stop关闭资源。
 * @note 前台I2C单事务20ms；读取WHO_AM_I但未核验具体型号ID，不声称已验证器件身份。
 */
Result board_motion_start(void);
/**
 * @brief 读取一次三轴g值；单寄存器事务最长20ms，总6字节，失败不供算法使用。
 * @param axes_g 非NULL三元素输出，顺序X/Y/Z，沿原12位/±2g标度。
 * @return 任一字节失败立即返回；该对象仅在OK后整体作为新样本使用。
 * @note 6个独立寄存器读，不保证硬件同一采样时刻锁存；比例及符号需实板核对。
 */
Result board_motion_sample(float axes_g[3]);
/**
 * @brief 有界请求传感器停机并关闭I2C，重复调用安全。
 * @note
 * 仅I2C门控开启时尝试3次有界寄存器写，再关闭I2C模块/门控与引脚复用；void不证明关停命令均获ACK。
 */
void board_motion_stop(void);
/**
 * @brief 只读旧格式SN/标记；offset限定0..100。
 * @param offset 相对身份基址字节偏移，0..100；越界返回0xFF且不读目标地址。
 * @note 0xFF既可能是擦除态也可能是越界返回；本接口不提供单独错误状态。
 */
uint8_t board_storage_read(unsigned offset);
/**
 * @brief 仅将空白字节写入一次，或验证已有相同字节；无自动擦除。
 * @param offset 身份区内字节偏移0..100，不允许越界。
 * @return OK表示同值或写/校验成功；INVALID为越界/不兼容旧值，IO为DDL操作失败。
 * @note 前台且AT空闲；禁止擦除；PRIMASK覆盖解锁/写/重锁，部分失败可能已写入，调用者须据返回核实。
 * @param value 待提交单字节；只允许空白0xFF或已为同值，不执行擦除。
 */
Result board_storage_write(unsigned offset, uint8_t value);
#endif
