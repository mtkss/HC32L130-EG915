/**
 * @file identity_store.h
 * @brief 兼容原SN与自检/激活标记的持久化语义。
 * @details Flash地址及单字节操作归board；本服务不擦除、不更新已有不同SN、不包含生产交互。
 * @note 前台且无在途AT时调用；写入有硬件临界区，失败不得当作标记已经提交。
 */
#ifndef IDENTITY_STORE_H
#define IDENTITY_STORE_H
#include "domain.h"
/**
 * @brief 读取六位SN；失败保持输出不变，true表示已提交且范围合法。
 * @param serial_number 非NULL可写输出；只在成功时修改。
 * @return true表示第4字节已提交且拼接值不超过999999；false不修改输出。
 */
bool identity_store_read(uint32_t *serial_number);
/**
 * @brief 空白记录写六位SN，最后第4字节提交；重复相同值成功，禁止改已有SN。
 * @param serial_number 0..999999；按4字节小端保存，第4字节最后提交。
 * @return 相同已提交SN返回OK；不同已存在SN/越界返回INVALID；写入错误透传。
 * @note 无擦除；未提交但已写的低字节只允许相同SN续写，不承诺部分失败时字节恢复为空白。
 */
Result identity_store_serial(uint32_t serial_number);
/**
 * @brief 精确识别旧自检0x55/+30和激活0x44/+40标记。
 * @return 仅+30精确为0x55时true；不单独验证SN。
 */
bool identity_store_checked(void);
/**
 * @brief 读取激活标记且要求自检标记已存在；不访问UART。
 * @return +30为0x55且+40为0x44时true；调用方仍须先确认SN有效。
 */
bool identity_store_activated(void);
/**
 * @brief 只追加完成标记，不清已有标记；失败必须保留待确认业务状态。
 * @note 前台且AT空闲；重复相同标记可返回OK，失败不应推进生产阶段。
 */
Result identity_store_mark_checked(void);
/**
 * @brief 自检已提交后追加激活标记；失败仍等待用户确认。
 * @return 无已提交自检标记返回INVALID；其他按底层单字节写结果，不自动更正旧异常组合。
 */
Result identity_store_mark_activated(void);
#endif
