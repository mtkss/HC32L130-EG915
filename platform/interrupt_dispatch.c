/**
 * @file interrupt_dispatch.c
 * @brief 装配本工程实际开启的向量与board中断处理器。
 * @details 不保存业务状态、不解析串口文本；未启用向量继续使用启动文件默认处理。
 * @note 运行于ISR；只分派或请求HardFault复位，不执行日志、Flash写入或阻塞等待。
 */
#include "ddl.h"
#include "hc32l13x.h"
#include "interrupts_hc32l13x.h"
/** @brief 业务IRQ处理器定义于board.c，禁止在分派层操作业务数据。 */
void PortB_IRQHandler(void);
void Uart0_IRQHandler(void);
void Tim0_IRQHandler(void);
void Rtc_IRQHandler(void);
/** @brief 设置NVIC优先级和开关；沿用原DDL公开接口。 */
void EnableNvic(IRQn_Type irq, en_irq_level_t level, boolean_t enabled)
{
    NVIC_ClearPendingIRQ(irq);
    NVIC_SetPriority(irq, level);
    if (enabled)
    {
        NVIC_EnableIRQ(irq);
    }
    else
    {
        NVIC_DisableIRQ(irq);
    }
}
/** @brief PB11唤醒分派。 */
void PORTB_IRQHandler(void)
{
    PortB_IRQHandler();
}
/** @brief UART0接收分派。 */
void UART0_IRQHandler(void)
{
    Uart0_IRQHandler();
}
/** @brief TIM0时基分派。 */
void TIM0_IRQHandler(void)
{
    Tim0_IRQHandler();
}
/** @brief RTC闹钟分派。 */
void RTC_IRQHandler(void)
{
    Rtc_IRQHandler();
}
/** @brief 不可恢复内核故障请求系统复位；没有看门狗和故障持久化保证。 */
void HardFault_Handler(void)
{
    NVIC_SystemReset();
}
