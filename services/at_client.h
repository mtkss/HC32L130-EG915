/**
 * @file at_client.h
 * @brief 在UART字节环上实现单一异步AT响应事务。
 * @details board独占IRQ与字节环，本模块只在前台处理行、超时和迟到应答排空。
 * @note 必须由单一前台串行调用，不可重入；无命令ID的外部URC只能尽力隔离，不保证精确关联。
 */
#ifndef AT_CLIENT_H
#define AT_CLIENT_H
#include "domain.h"
/** @brief 一个调用流程的步骤等待标志；不能被两个业务流程共用。 */
typedef struct
{
    bool is_waiting; ///< 本拥有者是否已提交AT步骤；只由对应前台流程更新，不跨流程复用。
} CommandStep;
/**
 * @brief 初始化静态事务；调用时不得有在途命令。
 * @note
 * 仅启动或确认无在途事务时使用。重复调用会清响应、排空资格和接收队列，不能代替运行中有序取消。
 */
void at_init(void);
/**
 * @brief 推进接收与期限检查；每主循环调用，消费至多一个RX环容量。
 * @note 仅前台，需持续调用且TIM0时基可推进；一次至多消费PROJECT_UART_RX_CAPACITY个字节。
 * @note 成功先匹配行，再等约35ms接收静默；失败排空250ms静默，连续噪声最多排空2s。
 */
void at_poll(void);
/**
 * @brief 启动命令或原始载荷；启动同步发送后不保留text指针。
 * @param text 发送源；非NULL，调用者保证至少length字节可读。
 * @param length 精确发送字节数；raw=false时实现另加CRLF，不计入此长度。
 * @param raw true发送原始载荷；false发送AT命令并追加CRLF。
 * @param expected 非NULL、非空、长度小于64的行前缀；非空由调用者保证。
 * @param timeout_ms 响应期限，1..120000毫秒，使用board单调时基。
 * @return OK表示已发出并进入异步等待；BUSY不接受新事务；其他错误可能已有部分字节发出。
 * @note 仅前台、不可重入；发送为有界轮询，等待为poll驱动；start不保留text指针。
 * @note 完成须查询at_result；响应借用到下一次有效start/init，错误/取消后的部分文本不得作成功使用。
 */
Result at_start(const char *text, size_t length, bool raw, const char *expected,
                uint32_t timeout_ms);
/**
 * @brief 查询完成状态；成功响应保留到下一次启动；PENDING不表示完成。
 * @return PENDING为在途；其他为最近完成结果。初始化后的OK不代表曾执行过命令。
 */
Result at_result(void);
/**
 * @brief 借用只读响应到下次at_start；只在事务完成后解析。
 * @return 静态NUL终止缓冲，只读借用；失败可包含部分应答，不授予解析成功资格。
 * @note 指针不可释放/修改；仅在at_result()==OK后由当前事务拥有者解析。
 */
const char *at_response(void);
/**
 * @brief 取消当前事务并静默排空250ms；下一次启动可能返回BUSY。
 * @note 可重复调用，但每次都会重置排空起点；只取消软件事务，不向模块发送撤销命令。
 */
void at_cancel(void);
/**
 * @brief 可复用步骤适配器；首次发命令，后续查询结果；完成时清等待标志。
 * @param step 当前组件独占、初始is_waiting=false的步骤对象，非NULL。
 * @param command 非NULL NUL终止AT命令，首次提交同步发送。
 * @param expected 非NULL非空期望前缀，长度小于64，约束同at_start。
 * @param timeout_ms 1..120000毫秒；只在首次成功提交时绑定该事务。
 * @return PENDING表示步骤正在提交/等待（也可能AT暂忙）；终态会清step等待标志。
 * @note 同一step必须串行推进同一命令；不可在is_waiting时换参数冒充另一个事务。
 */
Result at_command_step(CommandStep *step, const char *command, const char *expected,
                       uint32_t timeout_ms);
/**
 * @brief 推进精确载荷发送步骤，不追加CRLF，缓冲在启动返回后可复用。
 * @param step 单个流程独占的步骤对象，初始is_waiting=false。
 * @param bytes 可读源缓冲，至少提供count字节；按接口约定在同步发送返回后可复用。
 * @param count 精确载荷字节数，不包含NUL，不自动追加CRLF。
 * @param expected 非空期望行前缀，长度小于64；约束同at_start。
 * @return PENDING表示等待提交/完成；终态会清等待标志，精确字节不追加CRLF。
 * @param timeout_ms 响应期限1..120000毫秒，仅首次提交绑定。
 */
Result at_payload_step(CommandStep *step, const char *bytes, size_t count, const char *expected,
                       uint32_t timeout_ms);
#endif
