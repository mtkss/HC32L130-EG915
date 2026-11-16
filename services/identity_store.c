/**
 * @file identity_store.c
 * @brief 兼容原SN与自检/激活标记的持久化语义。
 * @details Flash地址及单字节操作归board；本服务不擦除、不更新已有不同SN、不包含生产交互。
 * @note 前台且无在途AT时调用；写入有硬件临界区，失败不得当作标记已经提交。
 */
#include "identity_store.h"
#include "board.h"
/** @brief 从旧布局按字节拼装已提交SN，验证范围后才写调用者输出。 */
bool identity_store_read(uint32_t *serial_number)
{
    uint32_t value;
    unsigned i;
    /* 第4字节是旧布局提交判据；未提交时不拼出看似有效的部分SN。 */
    if (!serial_number || board_storage_read(3) == 0xFF)
    {
        return false;
    }
    value = 0;
    for (i = 0; i < 4; i++)
    {
        value |= (uint32_t)board_storage_read(i) << (i * 8u);
    }
    if (value > 999999u)
    {
        return false;
    }
    *serial_number = value;
    return true;
}
/** @brief 在不擦除旧数据的前提下逐字节提交SN，允许相同值续写或重入。 */
Result identity_store_serial(uint32_t serial_number)
{
    uint32_t old;
    unsigned i;
    if (serial_number > 999999u)
    {
        return RESULT_INVALID;
    }
    if (identity_store_read(&old))
    {
        return old == serial_number ? RESULT_OK : RESULT_INVALID;
    }
    /* 低3字节先写，最后提交高字节；不擦除旧身份，同值可继续，失败不保证字节回滚。 */
    for (i = 0; i < 4; i++)
    {
        uint8_t byte = (uint8_t)(serial_number >> (8u * i));
        Result result = board_storage_write(i, byte);
        if (result != RESULT_OK)
        {
            return result;
        }
    }
    return identity_store_read(&old) && old == serial_number ? RESULT_OK : RESULT_IO;
}
/** @brief 精确识别旧自检完成标记，任意其他字节均视为未提交。 */
bool identity_store_checked(void)
{
    return board_storage_read(30) == 0x55;
}
/** @brief 同时核对自检与激活标记；SN合法性由上层读取身份时检查。 */
bool identity_store_activated(void)
{
    return identity_store_checked() && board_storage_read(40) == 0x44;
}
/** @brief 仅追加自检完成字节，由底层验证已有相同值或空白可写状态。 */
Result identity_store_mark_checked(void)
{
    return board_storage_write(30, 0x55);
}
/** @brief 先确认自检已提交，再追加激活完成字节。 */
Result identity_store_mark_activated(void)
{
    return identity_store_checked() ? board_storage_write(40, 0x44) : RESULT_INVALID;
}
