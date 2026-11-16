/**
 * @file http_service.c
 * @brief 依据EG800Z HTTP V1.4推进显式配置、URL、GET/POST和定长正文读取。
 * @details 独占HTTP缓冲和AT步骤，不解释业务JSON；只接受有Content-Length的非空2xx正文。
 * @note 单实例前台，失败/取消后必须由APP统一断电恢复，不能直接重试未知数据模式。
 */
#include "http_service.h"
#include "at_client.h"
#include "project_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
/** @brief HTTP顺序阶段；配置受控后才允许URL和正文进入数据模式。 */
typedef enum
{
    HTTP_CONFIG, ///< 显式绑定PDP1、自动请求头、仅响应体、关闭自动输出并选JSON。
    HTTP_URL_PROMPT,
    HTTP_URL_PAYLOAD, ///< CONNECT后才能发送精确URL。
    HTTP_REQUEST,
    HTTP_POST_PAYLOAD, ///< POST先等CONNECT，GET/POST均等结果URC。
    HTTP_READ          ///< 按结果URC的长度读取，包间隔80秒不代表256字节。
} HttpStage;
/** @brief 前台事务和全部缓冲的唯一拥有者；start同步复制输入，不保存调用者指针。 */
static struct
{
    HttpStage stage;
    unsigned config_index; ///< 配置数组索引，失败立即结束而非带旧配置继续。
    bool is_post;
    Result result;
    CommandStep command;
    size_t body_length;                          ///< 服务器声明正文长度，成功解析URC后才有效。
    char url[256];                               ///< 最多255字节URL加NUL，本模块按值持有。
    char body[PROJECT_HTTP_CAPACITY];            ///< POST正文最多511字节加NUL；不包含自动请求头。
    char response[PROJECT_AT_RESPONSE_CAPACITY]; ///< 只保存正文，不向业务暴露AT控制行。
} http;
/** @brief 从完整结果行读取无符号十进制字段，拒绝符号、溢出和非数字前缀。 */
static bool read_unsigned(const char **cursor, unsigned long *value)
{
    const char *p = *cursor;
    unsigned long parsed = 0;
    while (*p == ' ')
    {
        p++;
    }
    if (!isdigit((unsigned char)*p))
    {
        return false;
    }
    while (isdigit((unsigned char)*p))
    {
        unsigned digit = (unsigned)(*p++ - '0');
        if (parsed > (UINT32_MAX - digit) / 10ul)
        {
            return false;
        }
        parsed = parsed * 10ul + digit;
    }
    while (*p == ' ')
    {
        p++;
    }
    *cursor = p;
    *value = parsed;
    return true;
}
/** @brief 校验结果URC；缺长度/空正文不猜测帧界，错误HTTP代码不进入业务解析。 */
static Result accept_result(const char *prefix)
{
    const char *p = strstr(at_response(), prefix);
    unsigned long result, code, length;
    if (!p)
    {
        return RESULT_FORMAT;
    }
    p += strlen(prefix);
    if (!read_unsigned(&p, &result))
    {
        return RESULT_FORMAT;
    }
    if (result != 0)
    {
        return RESULT_IO;
    }
    if (*p++ != ',' || !read_unsigned(&p, &code))
    {
        return RESULT_FORMAT;
    }
    if (code < 200 || code > 299)
    {
        return RESULT_REJECTED;
    }
    if (*p++ != ',' || !read_unsigned(&p, &length) || (*p != '\r' && *p != '\n'))
    {
        return RESULT_FORMAT;
    }
    if (!length)
    {
        return RESULT_FORMAT;
    }
    if (length > PROJECT_AT_RESPONSE_CAPACITY - 96u)
    {
        return RESULT_OVERFLOW;
    }
    http.body_length = (size_t)length;
    return RESULT_OK;
}
/** @brief 验证并复制请求，再开启单实例HTTP流程。 */
Result http_start(const char *url, const char *body)
{
    if (http.result == RESULT_PENDING)
    {
        return RESULT_BUSY;
    }
    if (!url || (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)))
    {
        return RESULT_INVALID;
    }
    if (strlen(url) >= sizeof(http.url) || (body && strlen(body) >= sizeof(http.body)))
    {
        return RESULT_OVERFLOW;
    }
    if (body && !*body)
    {
        return RESULT_INVALID; /* 手册POST最短1字节。 */
    }
    strcpy(http.url, url);
    http.body[0] = 0;
    if (body)
    {
        strcpy(http.body, body);
    }
    http.is_post = body != NULL;
    http.stage = HTTP_CONFIG;
    http.config_index = 0;
    http.command.is_waiting = false;
    http.result = RESULT_PENDING;
    http.response[0] = 0;
    http.body_length = 0;
    return RESULT_OK;
}
/** @brief 每轮只提交/查询当前阶段，正文和完成URC分别校验后才发布成功。 */
void http_poll(void)
{
    /* HTTP V1.4 §2.3.1：不能依赖模块此前持有的请求头/自动输出配置。 */
    static const char *const configuration[] = {
        "AT+QHTTPCFG=\"contextid\",1", "AT+QHTTPCFG=\"requestheader\",0",
        "AT+QHTTPCFG=\"responseheader\",0", "AT+QHTTPCFG=\"rspout/auto\",0",
        "AT+QHTTPCFG=\"contenttype\",4"};
    Result result = RESULT_PENDING;
    char command[64];
    if (http.result != RESULT_PENDING)
    {
        return;
    }
    switch (http.stage)
    {
    case HTTP_CONFIG:
        result = at_command_step(&http.command, configuration[http.config_index], "OK",
                                 PROJECT_COMMAND_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            if (++http.config_index == sizeof(configuration) / sizeof(configuration[0]))
            {
                http.stage = HTTP_URL_PROMPT;
            }
            return;
        }
        break;
    case HTTP_URL_PROMPT:
        snprintf(command, sizeof(command), "AT+QHTTPURL=%u,80", (unsigned)strlen(http.url));
        result = at_command_step(&http.command, command, "CONNECT", PROJECT_HTTP_TIMEOUT_MS);
        break;
    case HTTP_URL_PAYLOAD:
        result = at_payload_step(&http.command, http.url, strlen(http.url), "OK",
                                 PROJECT_COMMAND_TIMEOUT_MS);
        break;
    case HTTP_REQUEST:
        if (http.is_post)
        {
            snprintf(command, sizeof(command), "AT+QHTTPPOST=%u,60,80",
                     (unsigned)strlen(http.body));
        }
        else
        {
            strcpy(command, "AT+QHTTPGET=80");
        }
        result = at_command_step(&http.command, command, http.is_post ? "CONNECT" : "+QHTTPGET:",
                                 http.is_post ? PROJECT_HTTP_CONNECT_TIMEOUT_MS
                                              : PROJECT_HTTP_TIMEOUT_MS);
        if (result == RESULT_OK && !http.is_post)
        {
            result = accept_result("+QHTTPGET:");
            if (result == RESULT_OK)
            {
                http.stage = HTTP_READ;
                return;
            }
        }
        break;
    case HTTP_POST_PAYLOAD:
        result = at_payload_step(&http.command, http.body, strlen(http.body),
                                 "+QHTTPPOST:", PROJECT_HTTP_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            result = accept_result("+QHTTPPOST:");
        }
        break;
    case HTTP_READ:
        result = at_http_read_step(&http.command, http.body_length, PROJECT_HTTP_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            const char *p = at_response();
            /* 只接受独立CONNECT行，避免URC文本中的同名子串改变正文起点。 */
            if (strncmp(p, "CONNECT\r\n", 9))
            {
                p = strstr(p, "\nCONNECT\r\n");
                if (p)
                {
                    p++;
                }
            }
            if (!p || strlen(p + 9) < http.body_length || memchr(p + 9, 0, http.body_length))
            {
                result = RESULT_FORMAT;
            }
            else
            {
                memcpy(http.response, p + 9, http.body_length);
                http.response[http.body_length] = 0;
                http.result = RESULT_OK;
                return;
            }
        }
        break;
    default:
        result = RESULT_INVALID;
        break;
    }
    if (result == RESULT_OK)
    {
        http.stage = (HttpStage)(http.stage + 1);
    }
    else if (result != RESULT_PENDING)
    {
        http.result = result;
        at_cancel();
    }
}
/** @brief 查询传输终态，OK仍不代表服务器业务批准。 */
Result http_result(void)
{
    return http.result;
}
/** @brief 借用已成功接收的正文，下一次start前有效。 */
const char *http_response(void)
{
    return http.response;
}
/** @brief 取消软件等待；模块数据模式可能仍在途，调用方须统一断电后再发AT。 */
void http_cancel(void)
{
    http.result = RESULT_IO;
    http.command.is_waiting = false;
}
