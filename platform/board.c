/**
 * @file board.c
 * @brief 独占HC32板级寄存器、时钟、引脚、UART环及中断事件。
 * @details 引脚仅来自board_pins.h，寄存器访问使用原DDL；UTC转换来自core，不依赖上层业务服务。
 * @note 除明示IRQ处理器外仅前台使用；UART/时基/事件共享读写以PRIMASK临界区交接，volatile不充当锁。
 */
#include "board.h"
#include "board_pins.h"
#include "project_config.h"
#include "utc_calendar.h"
#include "ddl.h"
#include "gpio.h"
#include "uart.h"
#include "bt.h"
#include "rtc.h"
#include "adc.h"
#include "bgr.h"
#include "i2c.h"
#include "flash.h"
#include "lpm.h"
#include <string.h>
/* UART0 ISR为唯一生产者，前台唯一消费者；索引及事件均在临界区交接。 */
/** @brief ISR生产/前台消费的字节环；有效容量N-1，满时丢本字节并置溢出。 */
static volatile char uart_bytes[PROJECT_UART_RX_CAPACITY];
/** @brief head由UART0 ISR发布，tail由前台推进；前台交接处用PRIMASK保护。 */
static volatile uint16_t uart_head, uart_tail;
/** @brief UART ISR置位、前台原子消费；表示原始串流存在丢字节。 */
static volatile bool uart_overflow;
/** @brief TIM0 ISR累计的单调毫秒；仅休眠返回时在临界区追加RTC差值。 */
static volatile uint32_t elapsed_ms;
/** @brief TIM0 ISR私有分数余量，单位为1/22120毫秒；范围0..22119。 */
static uint32_t tick_remainder;
/** @brief RTC/PortB ISR发布的事件位，前台在临界区快照并清零。 */
static volatile unsigned wake_events;
/** @brief 前台已确认状态与待稳定候选电平，true为低有效按下。 */
static bool key_is_down, key_candidate;
/** @brief 候选电平改变与确认按下时的单调毫秒，供消抖/长按判定。 */
static uint32_t key_changed_ms, key_pressed_ms;
/** @brief 保存/恢复PRIMASK，允许嵌套调用者保持原中断屏蔽状态。 */
static uint32_t enter_critical(void)
{
    uint32_t state = __get_PRIMASK();
    __disable_irq();
    return state;
}
/** @brief 还原进入前的中断状态。 */
static void leave_critical(uint32_t state)
{
    __set_PRIMASK(state);
}
/**
 * @brief 用完整零初始化配置设置一个板级GPIO，消除旧端口枚举适配歧义。
 * @param port 本地DDL端口枚举，来自board_pins确认映射。
 * @param pin 本地DDL引脚枚举，须与port组成已确认连接。
 * @param output true为输出，否则输入；high为实际输出电平，不是抽象亮灭。
 * @param pull_up 是否启用内部上拉，不推断外部电阻。
 * @param pull_down 是否启用内部下拉，不推断外部电阻。
 * @note 仅前台；Gpio_Init返回值当前不向调用者传播，不声称可检测配置失败。
 */
static void configure_pin(en_gpio_port_t port, en_gpio_pin_t pin, bool output, bool high,
                          bool pull_up, bool pull_down)
{
    stc_gpio_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enDir = output ? GpioDirOut : GpioDirIn;
    cfg.bOutputVal = high;
    cfg.enPu = pull_up ? GpioPuEnable : GpioPuDisable;
    cfg.enPd = pull_down ? GpioPdEnable : GpioPdDisable;
    cfg.enCtrlMode = GpioAHB;
    Gpio_Init(port, pin, &cfg);
}
/** @brief 根据当前PCLK重启原1000计数TIM0，约11.573ms；IRQ内精确累计余数。 */
static void start_timer(void)
{
    stc_bt_mode0_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    Sysctrl_SetPeripheralGate(SysctrlPeripheralBaseTim, TRUE);
    cfg.enWorkMode = BtWorkMode0;
    cfg.enCT = BtTimer;
    cfg.enPRS = BtPCLKDiv256;
    cfg.enCntMode = Bt16bitArrMode;
    Bt_Mode0_Init(TIM0, &cfg);
    /* 64536=65536-1000；PCLK22.12MHz/256，1000计数约11.5732ms，不是固定10ms中断。 */
    Bt_M0_ARRSet(TIM0, 64536);
    Bt_M0_Cnt16Set(TIM0, 64536);
    Bt_ClearIntFlag(TIM0, BtUevIrq);
    Bt_Mode0_EnableIrq(TIM0);
    EnableNvic(TIM0_IRQn, IrqLevel3, TRUE);
    Bt_M0_Run(TIM0);
}
/** @brief 配置原UART0 PA9/10；RX环在开中断前已经清空。 */
static void start_uart(void)
{
    stc_uart_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    Sysctrl_SetPeripheralGate(SysctrlPeripheralUart0, TRUE);
    configure_pin(BOARD_UART_TX_PORT, BOARD_UART_TX_PIN, true, true, false, false);
    configure_pin(BOARD_UART_RX_PORT, BOARD_UART_RX_PIN, false, false, true, false);
    Gpio_SetAfMode(BOARD_UART_TX_PORT, BOARD_UART_TX_PIN, GpioAf1);
    Gpio_SetAfMode(BOARD_UART_RX_PORT, BOARD_UART_RX_PIN, GpioAf1);
    cfg.enRunMode = UartMskMode1;
    cfg.enStopBit = UartMsk1bit;
    cfg.enMmdorCk = UartMskDataOrAddr;
    cfg.stcBaud.u32Baud = 115200;
    cfg.stcBaud.enClkDiv = UartMsk8Or16Div;
    cfg.stcBaud.u32Pclk = Sysctrl_GetPClkFreq();
    Uart_Init(M0P_UART0, &cfg);
    Uart_ClrStatus(M0P_UART0, UartRC);
    Uart_ClrStatus(M0P_UART0, UartTC);
    Uart_EnableIrq(M0P_UART0, UartRxIrq);
    EnableNvic(UART0_IRQn, IrqLevel3, TRUE);
}
/** @brief 外部XTL和RTC初始时间沿用旧工程，NTP成功后才设置UTC。 */
static void start_rtc(void)
{
    stc_rtc_initstruct_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    Sysctrl_SetPeripheralGate(SysctrlPeripheralRtc, TRUE);
    Sysctrl_ClkSourceEnable(SysctrlClkXTL, TRUE);
    cfg.rtcAmpm = RtcPm;
    cfg.rtcClksrc = RtcClkXtl;
    cfg.rtcPrdsel.rtcPrdsel = RtcPrds;
    cfg.rtcPrdsel.rtcPrds = RtcNone;
    cfg.rtcTime.u8Year = 0x19;
    cfg.rtcTime.u8Month = 0x04;
    cfg.rtcTime.u8Day = 0x17;
    cfg.rtcTime.u8DayOfWeek = 4;
    cfg.rtcTime.u8Hour = 0x10;
    cfg.rtcTime.u8Minute = 0x01;
    cfg.rtcTime.u8Second = 0x55;
    Rtc_Init(&cfg);
    Rtc_Cmd(TRUE);
    EnableNvic(RTC_IRQn, IrqLevel3, TRUE);
}
/** @brief 先建立时基与接收能力，再开放按键并设置板级初态。 */
void board_init(void)
{
    Sysctrl_SetRCHTrim(SysctrlRchFreq22_12MHz);
    Sysctrl_ClkSourceEnable(SysctrlClkRCH, TRUE);
    Sysctrl_SysClkSwitch(SysctrlClkRCH);
    Sysctrl_SetHCLKDiv(SysctrlHclkDiv1);
    Sysctrl_SetPCLKDiv(SysctrlPclkDiv1);
    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE);
    /* 接收环在UART开IRQ之前准备；先有TIM0时基，后续有界硬件操作才可使用毫秒期限。 */
    board_uart_flush();
    start_timer();
    start_uart();
    start_rtc();
    configure_pin(BOARD_KEY_PORT, BOARD_KEY_PIN, false, false, true, false);
    Gpio_ClearIrq(BOARD_KEY_PORT, BOARD_KEY_PIN);
    Gpio_EnableIrq(BOARD_KEY_PORT, BOARD_KEY_PIN, GpioIrqFalling);
    EnableNvic(PORTB_IRQn, IrqLevel3, TRUE);
    configure_pin(BOARD_LED_PORT, BOARD_LED_PIN, true, false, false, true);
    board_modem_key(true);
    board_modem_supply(false);
    configure_pin(BOARD_SAMPLE_ENABLE_PORT, BOARD_SAMPLE_ENABLE_PIN, true, false, false, true);
    /* Flash初始化/加锁返回值当前未向APP传播，保留此已知边界，不能据void返回判定硬件就绪。 */
    Flash_Init(1, TRUE);
    Flash_LockAll();
}
/** @brief 在短临界区内读取IRQ拥有的单调计数，保持访问约束一致。 */
uint32_t board_now_ms(void)
{
    uint32_t state = enter_critical(), value = elapsed_ms;
    leave_critical(state);
    return value;
}
/** @brief 通过已声明的PB4电源使能引脚输出请求电平。 */
void board_modem_supply(bool enabled)
{
    configure_pin(BOARD_MODEM_SUPPLY_PORT, BOARD_MODEM_SUPPLY_PIN, true, enabled, false, true);
}
/** @brief 配置PA4并输出PWRKEY请求电平，脉冲时序由网络组件负责。 */
void board_modem_key(bool high)
{
    configure_pin(BOARD_MODEM_KEY_PORT, BOARD_MODEM_KEY_PIN, true, high, false, true);
}
/** @brief 只更新LED输出电平，不在板级决定提示策略。 */
void board_led_write(bool high)
{
    Gpio_WriteOutputIO(BOARD_LED_PORT, BOARD_LED_PIN, high);
}
/** @brief 原子读取并消费RTC/按键事件，避免前台清零覆盖IRQ新事件。 */
unsigned board_take_wake_events(void)
{
    uint32_t state = enter_critical();
    unsigned result = wake_events;
    wake_events = 0;
    leave_critical(state);
    return result;
}
/** @brief 用稳定电平时间确认按下/释放，仅为不足长按阈值的释放生成短按。 */
bool board_take_short_press(void)
{
    uint32_t now = board_now_ms();
    bool down = !Gpio_GetInputIO(BOARD_KEY_PORT, BOARD_KEY_PIN);
    bool pressed = false;
    if (down != key_candidate)
    {
        key_candidate = down;
        key_changed_ms = now;
    }
    /* 电平变化先稳定24ms再进入状态；长按由确认按下后的3000ms判定，不按主循环次数计时。 */
    if (down != key_is_down && (uint32_t)(now - key_changed_ms) >= 24u)
    {
        key_is_down = down;
        if (down)
        {
            key_pressed_ms = now;
        }
        else
        {
            pressed = (uint32_t)(now - key_pressed_ms) < 3000u;
        }
    }
    return pressed;
}
/** @brief 有界逐字节发送指定长度；任一字节失败立即退出，已发字节不回滚。 */
Result board_uart_write(const char *bytes, size_t count)
{
    size_t i;
    if (!bytes && count)
    {
        return RESULT_INVALID;
    }
    for (i = 0; i < count; i++)
    {
        if (Uart_SendDataPollTimeOut(M0P_UART0, (uint8_t)bytes[i], 0xFFFFu) != Ok)
        {
            return RESULT_IO;
        }
    }
    return RESULT_OK;
}
/** @brief 从IRQ环安全取走一个字节，读出后推进唯一消费者索引。 */
bool board_uart_read(char *byte)
{
    uint32_t state;
    if (!byte)
    {
        return false;
    }
    state = enter_critical();
    if (uart_head == uart_tail)
    {
        leave_critical(state);
        return false;
    }
    *byte = uart_bytes[uart_tail];
    uart_tail = (uart_tail + 1u) % PROJECT_UART_RX_CAPACITY;
    leave_critical(state);
    return true;
}
/** @brief 原子丢弃当前环内历史字节并清溢出标志，不清外部模块状态。 */
void board_uart_flush(void)
{
    uint32_t state = enter_critical();
    uart_tail = uart_head;
    uart_overflow = false;
    leave_critical(state);
}
/** @brief 原子消费接收溢出标志，交由AT层终止不完整事务。 */
bool board_uart_take_overflow(void)
{
    uint32_t state = enter_critical();
    bool result = uart_overflow;
    uart_overflow = false;
    leave_critical(state);
    return result;
}
/** @brief 转换RTC的有效BCD字段。 */
static unsigned from_bcd(uint8_t value)
{
    return (value >> 4) * 10u + (value & 15u);
}
/** @brief 生成硬件RTC BCD值。 */
static uint8_t to_bcd(unsigned value)
{
    return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}
/** @brief 读取RTC BCD并通过纯算法生成UTC秒；读取/日期非法返回0。 */
uint32_t board_rtc_epoch(void)
{
    stc_rtc_time_t value;
    if (Rtc_ReadDateTime(&value) != Ok)
    {
        return 0;
    }
    return utc_calendar_to_epoch(2000u + from_bcd(value.u8Year), from_bcd(value.u8Month),
                                 from_bcd(value.u8Day), from_bcd(value.u8Hour),
                                 from_bcd(value.u8Minute), from_bcd(value.u8Second));
}
/** @brief 把有效UTC日历转换为RTC字段，不改动单调超时计数。 */
Result board_rtc_set(uint32_t epoch)
{
    unsigned f[6];
    stc_rtc_time_t value;
    utc_calendar_from_epoch(epoch, f);
    if (f[0] < 2000 || f[0] > 2099)
    {
        return RESULT_INVALID;
    }
    memset(&value, 0, sizeof(value));
    value.u8Year = to_bcd(f[0] - 2000);
    value.u8Month = to_bcd(f[1]);
    value.u8Day = to_bcd(f[2]);
    value.u8Hour = to_bcd(f[3]);
    value.u8Minute = to_bcd(f[4]);
    value.u8Second = to_bcd(f[5]);
    value.u8DayOfWeek = (uint8_t)((epoch / 86400u + 4u) % 7u);
    return Rtc_SetTime(&value) == Ok ? RESULT_OK : RESULT_IO;
}
/** @brief 配置RTC闹钟并协调外设关闭，唤醒后恢复时基及UART。 */
void board_sleep(void)
{
    stc_rtc_alarmtime_t alarm;
    unsigned f[6];
    uint32_t before, after, state;
    before = board_rtc_epoch();
    utc_calendar_from_epoch(before + PROJECT_WAKE_INTERVAL_MIN * 60u, f);
    memset(&alarm, 0, sizeof(alarm));
    alarm.RtcAlarmMinute = to_bcd(f[4]);
    alarm.RtcAlarmHour = to_bcd(f[3]);
    alarm.RtcAlarmWeek = 0x7F;
    /* 更新闹钟前先停计数、清旧事件并禁用旧闹钟，写后重新使能；0x7F为每周全部日期掩码。 */
    Rtc_Cmd(FALSE);
    Rtc_ClearAlmfItStatus();
    Rtc_AlmEnCmd(FALSE);
    Rtc_SetAlarmTime(&alarm);
    Rtc_AlmEnCmd(TRUE);
    Rtc_AlmIeCmd(TRUE);
    Rtc_Cmd(TRUE);
    /* 不允许把UART关闭当作AT取消：调用者应先统一取消；此处只停接收与时钟并设置休眠引脚。 */
    Uart_DisableIrq(M0P_UART0, UartRxIrq);
    EnableNvic(UART0_IRQn, IrqLevel3, FALSE);
    Sysctrl_SetPeripheralGate(SysctrlPeripheralUart0, FALSE);
    configure_pin(BOARD_UART_TX_PORT, BOARD_UART_TX_PIN, true, true, false, false);
    configure_pin(BOARD_UART_RX_PORT, BOARD_UART_RX_PIN, false, false, true, false);
    board_motion_stop();
    configure_pin(BOARD_SAMPLE_ENABLE_PORT, BOARD_SAMPLE_ENABLE_PIN, false, false, false, true);
    configure_pin(BOARD_BATTERY_PORT, BOARD_BATTERY_PIN, false, false, true, false);
    configure_pin(BOARD_TEMPERATURE_PORT, BOARD_TEMPERATURE_PIN, false, false, true, false);
    board_led_write(false);
    Bt_M0_Stop(TIM0);
    Bt_Mode0_DisableIrq(TIM0);
    Bt_ClearIntFlag(TIM0, BtUevIrq);
    EnableNvic(TIM0_IRQn, IrqLevel3, FALSE);
    /* 关中断检查事件后WFI；pending IRQ仍会唤醒，恢复PRIMASK后派发，避免检查与休眠间丢事件。 */
    state = enter_critical();
    if (!wake_events && Gpio_GetInputIO(BOARD_KEY_PORT, BOARD_KEY_PIN))
    {
        SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
        SCB->SCR &= ~SCB_SCR_SLEEPONEXIT_Msk;
        __DSB();
        __WFI();
    }
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    leave_critical(state);
    Sysctrl_SysClkSwitch(SysctrlClkRCH);
    after = board_rtc_epoch();
    state = enter_critical();
    /* 睡眠补偿依赖RTC连续推进，只有非负差才追加；秒级量化包含准备/恢复的一部分，不是精确休眠计量。
     */
    if (after >= before)
    {
        elapsed_ms += (after - before) * 1000u;
    }
    leave_critical(state);
    start_timer();
    start_uart();
}
/** @brief 在原模拟稳定窗口后有界采两路ADC，成功才发布输出并始终收尾。 */
Result board_sample_adc(float *battery, float *temperature)
{
    stc_adc_cfg_t cfg;
    stc_adc_jqr_cfg_t queue;
    uint32_t start;
    uint16_t b, t;
    Result result = RESULT_TIMEOUT;
    if (!battery || !temperature)
    {
        return RESULT_INVALID;
    }
    memset(&cfg, 0, sizeof(cfg));
    memset(&queue, 0, sizeof(queue));
    configure_pin(BOARD_SAMPLE_ENABLE_PORT, BOARD_SAMPLE_ENABLE_PIN, true, true, false, true);
    Gpio_SetAnalogMode(BOARD_BATTERY_PORT, BOARD_BATTERY_PIN);
    Gpio_SetAnalogMode(BOARD_TEMPERATURE_PORT, BOARD_TEMPERATURE_PIN);
    Sysctrl_SetPeripheralGate(SysctrlPeripheralAdcBgr, TRUE);
    Bgr_BgrEnable();
    delay1ms(10);
    /* 保留原ADC初始化后的100ms模拟稳定时间；本函数只在无在途AT时调用。 */
    delay1ms(100);
    cfg.enAdcMode = AdcScanMode;
    cfg.enAdcClkDiv = AdcMskClkDiv1;
    cfg.enAdcSampCycleSel = AdcMskSampCycle12Clk;
    cfg.enAdcRefVolSel = AdcMskRefVolSelAVDD;
    cfg.enAdcAlign = AdcAlignRight;
    Adc_Init(&cfg);
    /* 扫描顺序固定：队列0=PA2电池，队列1=PA6旧温度通路；没有ADC DMA/IRQ回调交接。 */
    queue.u8JqrCnt = 2;
    Adc_JqrModeCfg(&queue);
    Adc_CfgJqrChannel(AdcJQRCH0MUX, AdcExInputCH2);
    Adc_CfgJqrChannel(AdcJQRCH1MUX, AdcExInputCH6);
    Adc_ClrIrqStatus(AdcMskIrqJqr);
    Adc_JQR_Start();
    start = board_now_ms();
    while ((uint32_t)(board_now_ms() - start) < 100u)
    {
        if (Adc_GetIrqStatus(AdcMskIrqJqr))
        {
            b = Adc_GetJqrResult(AdcJQRCH0MUX);
            t = Adc_GetJqrResult(AdcJQRCH1MUX);
            if (b < 4096 && t < 4096)
            {
                /* 旧12位ADC/固定3.3V/3倍分压公式；温度变量仅为10*raw/(4096-raw)，不是NTC摄氏换算。
                 */
                *battery = (float)b / 4096.0f * 3.3f * 3.0f;
                *temperature = 10.0f * (float)t / (4096.0f - (float)t);
                result = RESULT_OK;
            }
            else
            {
                result = RESULT_INVALID;
            }
            break;
        }
    }
    /* 成功、非法结果和超时都到此收尾；不能只停止转换后继续无限等待完成标志。 */
    Adc_JQR_Stop();
    Adc_ClrIrqStatus(AdcMskIrqJqr);
    Adc_Disable();
    Bgr_BgrDisable();
    Sysctrl_SetPeripheralGate(SysctrlPeripheralAdcBgr, FALSE);
    configure_pin(BOARD_SAMPLE_ENABLE_PORT, BOARD_SAMPLE_ENABLE_PIN, true, false, false, true);
    return result;
}
/**
 * @brief 完成一次SC7A20单寄存器读/写，任何失败都停止本次总线事务。
 * @param address 8位寄存器地址，由调用者提供本器件有效地址。
 * @param value 非NULL；read=true为单字节输出，false为待写字节输入。
 * @param read true执行写寄存器地址+重复START+读；false写寄存器地址和一个数据字节。
 * @return OK表示收到末尾数据/ACK；IO为非预期状态；TIMEOUT为20ms预算耗尽。
 * @note 前台、不可重入；需先初始化I2C并保证TIM0 IRQ能推进。失败发STOP且关闭控制器，不隐式无限重发。
 */
static Result sensor_transfer(uint8_t address, uint8_t *value, bool read)
{
    uint32_t start = board_now_ms();
    /* 写事务phase=1等待寄存器地址ACK，=2等待唯一数据字节ACK；不越界发送额外字节。 */
    unsigned phase = 0;
    Result result = RESULT_TIMEOUT;
    I2C_SetFunc(M0P_I2C0, I2cStart_En);
    while ((uint32_t)(board_now_ms() - start) < 20u)
    {
        uint8_t status;
        if (!I2C_GetIrq(M0P_I2C0))
        {
            continue;
        }
        status = I2C_GetState(M0P_I2C0);
        switch (status)
        {
        /* 以下状态码沿用本地DDL/原I2C状态流程；未预期状态统一IO退出，不无限重发START。 */
        case 0x08:
            I2C_ClearFunc(M0P_I2C0, I2cStart_En);
            /* 0x32/0x33是原器件含R/W位的总线地址编码，不另猜测其他传感器地址。 */
            I2C_WriteByte(M0P_I2C0, 0x32);
            break;
        case 0x18:
            I2C_WriteByte(M0P_I2C0, address);
            phase = 1;
            break;
        case 0x28:
            if (read)
            {
                I2C_SetFunc(M0P_I2C0, I2cStart_En);
            }
            else if (phase == 1)
            {
                I2C_WriteByte(M0P_I2C0, *value);
                phase = 2;
            }
            else
            {
                result = RESULT_OK;
            }
            break;
        case 0x10:
            I2C_ClearFunc(M0P_I2C0, I2cStart_En);
            I2C_WriteByte(M0P_I2C0, 0x33);
            break;
        case 0x40:
            I2C_ClearFunc(M0P_I2C0, I2cAck_En);
            break;
        case 0x58:
            *value = I2C_ReadByte(M0P_I2C0);
            result = RESULT_OK;
            break;
        default:
            result = RESULT_IO;
            break;
        }
        I2C_ClearIrq(M0P_I2C0);
        if (result != RESULT_TIMEOUT)
        {
            break;
        }
    }
    I2C_SetFunc(M0P_I2C0, I2cStop_En);
    if (result != RESULT_OK)
    {
        I2C_ClearFunc(M0P_I2C0, I2cModule_En);
    }
    return result;
}
/** @brief 配置I2C及SC7A20原寄存器值，不把未核实ID当作认证结果。 */
Result board_motion_start(void)
{
    stc_gpio_cfg_t pin;
    stc_i2c_cfg_t cfg;
    uint8_t value;
    Result result;
    memset(&pin, 0, sizeof(pin));
    pin.enDir = GpioDirOut;
    pin.enOD = GpioOdEnable;
    pin.enPu = GpioPuEnable;
    pin.bOutputVal = TRUE;
    Gpio_Init(BOARD_SENSOR_SCL_PORT, BOARD_SENSOR_SCL_PIN, &pin);
    Gpio_Init(BOARD_SENSOR_SDA_PORT, BOARD_SENSOR_SDA_PIN, &pin);
    Gpio_SetAfMode(BOARD_SENSOR_SCL_PORT, BOARD_SENSOR_SCL_PIN, GpioAf1);
    Gpio_SetAfMode(BOARD_SENSOR_SDA_PORT, BOARD_SENSOR_SDA_PIN, GpioAf1);
    memset(&cfg, 0, sizeof(cfg));
    Sysctrl_SetPeripheralGate(SysctrlPeripheralI2c0, TRUE);
    cfg.u32Pclk = Sysctrl_GetPClkFreq();
    cfg.u32Baud = 100000;
    cfg.enMode = I2cMasterMode;
    I2C_Init(M0P_I2C0, &cfg);
    result = sensor_transfer(0x0F, &value, true);
    if (result != RESULT_OK)
    {
        return result; /* 旧代码未核验ID，型号/期望值待器件资料确认。 */
    }
    /* 寄存器0x20=0x57、0x21=0、0x23=0沿用原初始化；ODR/量程的真实生效须以器件资料及实测确认。 */
    value = 0x57;
    result = sensor_transfer(0x20, &value, false);
    if (result != RESULT_OK)
    {
        return result;
    }
    value = 0;
    result = sensor_transfer(0x21, &value, false);
    if (result != RESULT_OK)
    {
        return result;
    }
    return sensor_transfer(0x23, &value, false);
}
/** @brief 有界读取六个轴数据字节，按原12位缩放生成XYZ。 */
Result board_motion_sample(float axes[3])
{
    uint8_t bytes[6];
    unsigned i;
    if (!axes)
    {
        return RESULT_INVALID;
    }
    for (i = 0; i < 6; i++)
    {
        Result result = sensor_transfer((uint8_t)(0x28u + i), &bytes[i], true);
        if (result != RESULT_OK)
        {
            return result;
        }
    }
    /* XYZ各两字节按低在前拼接为int16，再按旧12位标度转换；逐寄存器读不提供整组六字节锁存保证。 */
    for (i = 0; i < 3; i++)
    {
        int16_t raw = (int16_t)((uint16_t)bytes[2 * i] | ((uint16_t)bytes[2 * i + 1] << 8));
        axes[i] = (float)(raw / 16) / 2048.0f * 2.0f;
    }
    return RESULT_OK;
}
/** @brief 尝试关闭传感器后强制关闭I2C门控并释放引脚复用。 */
void board_motion_stop(void)
{
    uint8_t value = 0;
    if (Sysctrl_GetPeripheralGate(SysctrlPeripheralI2c0))
    {
        /* 关闭寄存器尽力而为，当前忽略各ACK失败；随后仍关闭控制器/时钟，防止资源遗留。 */
        sensor_transfer(0x20, &value, false);
        sensor_transfer(0x23, &value, false);
        sensor_transfer(0x24, &value, false);
        I2C_ClearFunc(M0P_I2C0, I2cModule_En);
        Sysctrl_SetPeripheralGate(SysctrlPeripheralI2c0, FALSE);
    }
    configure_pin(BOARD_SENSOR_SCL_PORT, BOARD_SENSOR_SCL_PIN, false, false, true, false);
    configure_pin(BOARD_SENSOR_SDA_PORT, BOARD_SENSOR_SDA_PIN, false, false, true, false);
}
/** @brief 只读预留身份区有限偏移，越界以擦除态字节返回。 */
uint8_t board_storage_read(unsigned offset)
{
    return offset <= 100u ? *(volatile uint8_t *)(PROJECT_FLASH_ID_ADDRESS + offset) : 0xFF;
}
/** @brief 在PRIMASK保护区内执行单字节写和重锁，不执行扇区擦除。 */
Result board_storage_write(unsigned offset, uint8_t value)
{
    Result result;
    uint32_t state;
    if (offset > 100u)
    {
        return RESULT_INVALID;
    }
    if (board_storage_read(offset) == value)
    {
        return RESULT_OK;
    }
    if (board_storage_read(offset) != 0xFF)
    {
        return RESULT_INVALID;
    }
    /* 沿用原解锁掩码0x20000000；只写身份单字节，禁止此处隐式擦扇区影响激活标记。 */
    state = enter_critical();
    result = Flash_LockSet(0x20000000) == Ok && Flash_OpModeConfig(FlashWriteMode) == Ok &&
                     Flash_Write8(PROJECT_FLASH_ID_ADDRESS + offset, &value, 1) == Ok
                 ? RESULT_OK
                 : RESULT_IO;
    /* 写失败也恢复读模式并请求全锁；这两次恢复返回值当前未纳入result，需实板确认异常保护。 */
    Flash_OpModeConfig(FlashReadMode);
    Flash_LockAll();
    leave_critical(state);
    return result;
}
/** @brief TIM0仅累计时基；没有业务、日志或浮点。 */
void Tim0_IRQHandler(void)
{
    if (Bt_GetIntFlag(TIM0, BtUevIrq))
    {
        /* 每IRQ对应1000*256个PCLK；除以22120时钟/毫秒，余数跨IRQ保留，避免每tick取整漂移。 */
        tick_remainder += 256000u;
        elapsed_ms += tick_remainder / 22120u;
        tick_remainder %= 22120u;
        Bt_ClearIntFlag(TIM0, BtUevIrq);
    }
}
/** @brief UART0只搬字节；满时丢本字节并置溢出，前台终止当前事务。 */
void Uart0_IRQHandler(void)
{
    if (Uart_GetStatus(M0P_UART0, UartRC))
    {
        uint16_t next;
        char byte;
        Uart_ClrStatus(M0P_UART0, UartRC);
        byte = (char)Uart_ReceiveData(M0P_UART0);
        next = (uart_head + 1u) % PROJECT_UART_RX_CAPACITY;
        if (next == uart_tail)
        {
            uart_overflow = true;
        }
        else
        {
            uart_bytes[uart_head] = byte;
            /* 先写槽内容再发布head；前台读取同时用临界区保护，不把volatile误当完整同步。 */
            __DMB();
            uart_head = next;
        }
    }
}
/** @brief GPIO只记录唤醒原因，按键消抖由前台按真实毫秒处理。 */
void PortB_IRQHandler(void)
{
    if (Gpio_GetIrqStatus(BOARD_KEY_PORT, BOARD_KEY_PIN))
    {
        wake_events |= BOARD_WAKE_KEY;
        Gpio_ClearIrq(BOARD_KEY_PORT, BOARD_KEY_PIN);
    }
}
/** @brief RTC只清标志与记录唤醒事件。 */
void Rtc_IRQHandler(void)
{
    if (Rtc_GetAlmfItStatus())
    {
        Rtc_ClearAlmfItStatus();
        wake_events |= BOARD_WAKE_RTC;
    }
}
