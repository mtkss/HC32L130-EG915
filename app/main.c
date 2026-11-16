/**
 * @file main.c
 * @brief 装配板级资源和应用，运行裸机协作主循环。
 * @details 板级资源须先初始化，随后应用读取身份；本入口不直接实现协议或生产写入格式。
 * @note 执行上下文为复位后的前台；中断只通过board私有事件与前台交接。
 */
#include "board.h"
#include "application.h"
/** @brief 初始化平台和应用后持续推进协作状态机，不执行设备烧录。 */
int main(void)
{
    board_init();
    application_init();
    for (;;)
    {
        application_poll();
    }
}
