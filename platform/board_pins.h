/**
 * @file board_pins.h
 * @brief 集中保存从原工程读取的引脚连接约定。
 * @details 只定义端口/引脚绑定，不证明电路电平、外部上下拉或模块电源时序正确。
 * @note 修改后须核对原理图、复用和休眠配置；不得仅按芯片可复用能力猜测板上连接。
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#include "gpio.h"
/** @brief 模块PWRKEY请求PA4；脉冲高低时序归network_manager，不推断实测极性。 */
#define BOARD_MODEM_KEY_PORT GpioPortA
#define BOARD_MODEM_KEY_PIN GpioPin4
/** @brief 模块电源使能PB4；软件true输出高、false输出低，未提供电压检测反馈。 */
#define BOARD_MODEM_SUPPLY_PORT GpioPortB
#define BOARD_MODEM_SUPPLY_PIN GpioPin4
/** @brief UART0发送PA9，初始化AF1；休眠改GPIO高，未证明无反向供电。 */
#define BOARD_UART_TX_PORT GpioPortA
#define BOARD_UART_TX_PIN GpioPin9
/** @brief UART0接收PA10，初始化AF1；休眠保留输入上拉。 */
#define BOARD_UART_RX_PORT GpioPortA
#define BOARD_UART_RX_PIN GpioPin10
/** @brief I2C0时钟PB6，AF1开漏/上拉；退出采样后改GPIO输入上拉。 */
#define BOARD_SENSOR_SCL_PORT GpioPortB
#define BOARD_SENSOR_SCL_PIN GpioPin6
/** @brief I2C0数据PB7，AF1开漏/上拉；外部上拉值尚无原理图证据。 */
#define BOARD_SENSOR_SDA_PORT GpioPortB
#define BOARD_SENSOR_SDA_PIN GpioPin7
/** @brief PB11按键，软件低有效且下降沿唤醒；消抖由前台完成。 */
#define BOARD_KEY_PORT GpioPortB
#define BOARD_KEY_PIN GpioPin11
/** @brief PA7 LED输出；接口传实际电平，亮灭极性待电路核对。 */
#define BOARD_LED_PORT GpioPortA
#define BOARD_LED_PIN GpioPin7
/** @brief PA5模拟采样通路使能，按原代码高请求；ADC结束后拉低。 */
#define BOARD_SAMPLE_ENABLE_PORT GpioPortA
#define BOARD_SAMPLE_ENABLE_PIN GpioPin5
/** @brief PA2 ADC外部通道2，旧电压公式含3倍分压和固定3.3V参考假设。 */
#define BOARD_BATTERY_PORT GpioPortA
#define BOARD_BATTERY_PIN GpioPin2
/** @brief PA6 ADC外部通道6，只输出旧分压推导量，无NTC温度换算。 */
#define BOARD_TEMPERATURE_PORT GpioPortA
#define BOARD_TEMPERATURE_PIN GpioPin6
#endif
