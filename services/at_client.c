/**
 * @file at_client.c
 * @brief 在UART字节环上实现单一异步AT响应事务。
 * @details board独占IRQ与字节环，本模块只在前台处理行、超时和迟到应答排空。
 * @note 必须由单一前台串行调用，不可重入；无命令ID的外部URC只能尽力隔离，不保证精确关联。
 */
#include "at_client.h"
#include "board.h"
#include "project_config.h"
#include <string.h>
/** @brief AT前台唯一事务与响应所有者；UART ISR不直接写此对象。 */
static struct
{
    Result result;             ///< 最近事务结果，初始化OK不代表执行过命令。
    bool is_active, has_match; ///< 是否正在接收；是否已有符合边界的期望完成行。
    uint32_t started_ms, last_byte_ms, timeout_ms, quarantine_started_ms,
        quarantine_last_byte_ms; ///< 单调毫秒：事务起点/最近字节、响应预算、排空起点/排空最近字节。
    bool is_quarantined;         ///< 取消/失败后的隔离窗，期间新start返回BUSY。
    size_t count, line_start;    ///< response已用字节数（不含NUL）与当前未处理行的起始偏移。
    bool is_http_read, has_connect, has_read_ok; ///< 固定长度READ模式；只有正文外才解释结果行。
    size_t http_remaining; ///< 已知HTTP正文还需接收的字节数，不含CONNECT和结束行。
    char expected[64];     ///< 按值保存的非空期望前缀，最多63字符加NUL。
    char response
        [PROJECT_AT_RESPONSE_CAPACITY]; ///< 累计响应含NUL；保留多行，满时失败，不覆盖历史拼出伪帧。
} client;
/** @brief 结束操作；错误进入静默排空窗，成功保留完整响应给业务消费。 */
static void finish(Result result)
{
    client.is_active = false;
    client.result = result;
    if (result != RESULT_OK)
    {
        client.is_quarantined = true;
        client.quarantine_started_ms = client.quarantine_last_byte_ms = board_now_ms();
    }
}
/** @brief 重置AT事务和UART历史输入；只允许启动阶段或无在途事务时调用。 */
void at_init(void)
{
    memset(&client, 0, sizeof(client));
    client.result = RESULT_OK;
    board_uart_flush();
}
/** @brief 同步发送命令/精确载荷后建立异步响应事务，不保留发送指针。 */
Result at_start(const char *text, size_t length, bool raw, const char *expected,
                uint32_t timeout_ms)
{
    Result result;
    if (!text || !expected || !*expected || !timeout_ms || timeout_ms > PROJECT_AT_MAX_TIMEOUT_MS ||
        strlen(expected) >= sizeof(client.expected))
    {
        return RESULT_INVALID;
    }
    if (client.is_active || client.is_quarantined)
    {
        return RESULT_BUSY;
    }
    /* 只有确认无在途/排空事务后才清历史；后续响应只归本轮拥有者使用。 */
    board_uart_flush();
    memset(client.response, 0, sizeof(client.response));
    client.count = client.line_start = 0;
    client.has_match = false;
    client.is_http_read = client.has_connect = client.has_read_ok = false;
    strcpy(client.expected, expected);
    client.started_ms = client.last_byte_ms = board_now_ms();
    client.timeout_ms = timeout_ms;
    result = board_uart_write(text, length);
    if (result == RESULT_OK && !raw)
    {
        result = board_uart_write("\r\n", 2);
    }
    client.result = RESULT_PENDING;
    client.is_active = true;
    if (result != RESULT_OK)
    {
        finish(result);
    }
    return result;
}
/** @brief 有界消费IRQ接收环，识别行结果、溢出、静默期与超时。 */
void at_poll(void)
{
    char byte;
    unsigned budget = PROJECT_UART_RX_CAPACITY;
    uint32_t now = board_now_ms();
    /* 失败后丢弃迟到应答；安静250ms退出，持续噪声到2s也退出以避免软件永久BUSY。 */
    if (client.is_quarantined)
    {
        while (budget-- && board_uart_read(&byte))
        {
            client.quarantine_last_byte_ms = now;
        }
        if (((uint32_t)(now - client.quarantine_last_byte_ms) >= PROJECT_AT_QUIET_MS ||
             (uint32_t)(now - client.quarantine_started_ms) >= 2000u))
        {
            client.is_quarantined = false;
            board_uart_flush();
        }
        return;
    }
    if (!client.is_active)
    {
        return;
    }
    if (board_uart_take_overflow())
    {
        finish(RESULT_OVERFLOW);
        return;
    }
    while (budget-- && board_uart_read(&byte))
    {
        const char *line;
        size_t length;
        if (client.count + 1 >= sizeof(client.response))
        {
            finish(RESULT_OVERFLOW);
            return;
        }
        client.response[client.count++] = byte;
        client.response[client.count] = 0;
        client.last_byte_ms = now;
        /* HTTP手册§2.3.9：CONNECT后按结果URC给出的字节数读取；正文中的ERROR/OK不是AT结果。 */
        if (client.is_http_read && client.has_connect && client.http_remaining)
        {
            client.http_remaining--;
            if (!client.http_remaining)
            {
                client.line_start = client.count;
            }
            continue;
        }
        if (byte != '\n')
        {
            continue;
        }
        line = client.response + client.line_start;
        while (*line == '\r' || *line == '\n' || *line == ' ')
        {
            line++;
        }
        length = strcspn(line, "\r\n");
        if (client.is_http_read && !client.has_connect && length == 7 &&
            !strncmp(line, "CONNECT", 7))
        {
            client.has_connect = true;
            client.line_start = client.count;
            continue;
        }
        if (client.is_http_read && client.has_connect && length == 2 && !strncmp(line, "OK", 2))
        {
            client.has_read_ok = true;
        }
        /* ERROR及CME/CMS错误须先于期望成功前缀处理，避免错误行包含关键字而误成功。 */
        if ((length == 5 && !strncmp(line, "ERROR", 5)) || !strncmp(line, "+CME ERROR:", 11) ||
            !strncmp(line, "+CMS ERROR:", 11))
        {
            finish(RESULT_IO);
            return;
        }
        /* READ非零结果是明确失败；不等满主机超时，也不把前缀0,额外字段当作成功。 */
        if (client.is_http_read && client.has_connect && !client.http_remaining &&
            !strncmp(line, "+QHTTPREAD:", 11))
        {
            const char *code = line + 11;
            while (*code == ' ')
            {
                code++;
            }
            if (code[0] != '0' || (code[1] != '\r' && code[1] != '\n'))
            {
                finish(RESULT_IO);
                return;
            }
            if (!client.has_read_ok)
            {
                finish(RESULT_FORMAT);
                return;
            }
        }
        /* 除以冒号结束的字段前缀外，成功关键字后必须是行界或分隔符；OKAY不等于OK。 */
        if (!strncmp(line, client.expected, strlen(client.expected)))
        {
            if ((strcmp(client.expected, "OK") || length == 2) &&
                (client.expected[strlen(client.expected) - 1] == ':' ||
                 line[strlen(client.expected)] == '\r' || line[strlen(client.expected)] == '\n' ||
                 line[strlen(client.expected)] == ',' || line[strlen(client.expected)] == ' '))
            {
                client.has_match =
                    !client.is_http_read ||
                    (client.has_connect && client.has_read_ok && !client.http_remaining);
            }
        }
        client.line_start = client.count;
    }
    /* 已命中后继续吸收约35ms静默前的尾部OK/URC，保留多行文本；不声称具备模块事务ID。 */
    if (client.has_match && (uint32_t)(now - client.last_byte_ms) >= 35u)
    {
        finish(RESULT_OK);
        return;
    }
    if ((uint32_t)(now - client.started_ms) >= client.timeout_ms)
    {
        finish(RESULT_TIMEOUT);
    }
}
/** @brief 返回上一次启动/推进后的事务状态，不消费响应。 */
Result at_result(void)
{
    return client.result;
}
/** @brief 借用AT内部接收文本；内容的有效期结束于下一次启动或初始化。 */
const char *at_response(void)
{
    return client.response;
}
/** @brief 终止事务并进入有界排空窗，防止旧回复直接进入下一事务。 */
void at_cancel(void)
{
    client.is_active = false;
    client.result = RESULT_IO;
    client.is_quarantined = true;
    client.quarantine_started_ms = client.quarantine_last_byte_ms = board_now_ms();
}
/**
 * @brief 将某组件的步骤等待标志与唯一AT事务绑定，避免重复发送。
 * @param step 非NULL且只归一个前台流程拥有，初次使用is_waiting=false。
 * @param text 非NULL源文本，首次提交时至少length字节可读。
 * @param length 精确发送字节数，首次调用同步消费，不含自动CRLF。
 * @param raw true精确载荷，false追加命令CRLF。
 * @param expected 非NULL非空期望前缀，长度小于64。
 * @param timeout 响应期限，毫秒，1..240000。
 * @return 暂忙或在途为PENDING；终态清is_waiting后返回原AT结果。
 * @note 同一step在等待期间不得切换命令身份，也不能据PENDING判断模块已接受。
 */
static Result execute_step(CommandStep *step, const char *text, size_t length, bool raw,
                           const char *expected, uint32_t timeout)
{
    Result result;
    if (!step)
    {
        return RESULT_INVALID;
    }
    if (!step->is_waiting)
    {
        result = at_start(text, length, raw, expected, timeout);
        /* 暂忙并不消耗本步骤的提交资格；此拥有者继续poll，待排空窗结束再发一次。 */
        if (result == RESULT_BUSY)
        {
            return RESULT_PENDING;
        }
        if (result != RESULT_OK)
        {
            return result;
        }
        step->is_waiting = true;
        return RESULT_PENDING;
    }
    result = at_result();
    if (result != RESULT_PENDING)
    {
        step->is_waiting = false;
    }
    return result;
}
/** @brief 把拥有者的步骤标志与AT命令事务关联，避免每次poll重复发命令。 */
Result at_command_step(CommandStep *step, const char *command, const char *expected,
                       uint32_t timeout)
{
    return command ? execute_step(step, command, strlen(command), false, expected, timeout)
                   : RESULT_INVALID;
}
/** @brief 复用步骤驱动发送精确载荷，不为URL或JSON额外附加换行。 */
Result at_payload_step(CommandStep *step, const char *bytes, size_t count, const char *expected,
                       uint32_t timeout)
{
    return execute_step(step, bytes, count, true, expected, timeout);
}

/** @brief 用同一AT拥有者提交固定长度HTTP读取，保持正文和控制行边界。 */
Result at_http_read_step(CommandStep *step, size_t body_length, uint32_t timeout_ms)
{
    bool was_waiting;
    Result result;
    if (!step || !body_length || body_length > PROJECT_AT_RESPONSE_CAPACITY - 96u)
    {
        return RESULT_INVALID;
    }
    was_waiting = step->is_waiting;
    result = execute_step(step, "AT+QHTTPREAD=80", strlen("AT+QHTTPREAD=80"), false,
                          "+QHTTPREAD: 0", timeout_ms);
    if (!was_waiting && step->is_waiting)
    {
        client.is_http_read = true;
        client.http_remaining = body_length;
    }
    return result;
}
