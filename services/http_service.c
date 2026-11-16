/**
 * @file http_service.c
 * @brief 通过AT实现共享的HTTP GET/POST传输阶段。
 * @details 只处理URL、正文和完整READ响应，不解释自检批准或周期参数。
 * @note 前台单实例；调用期间不得与其他网络/定位流程争用AT，取消HTTP仍需统一出口取消AT。
 */
#include "http_service.h"
#include "at_client.h"
#include "project_config.h"
#include <stdio.h>
#include <string.h>
/** @brief 前台单实例HTTP事务；URL/正文/响应均由本对象拥有，不借用业务缓冲。 */
static struct
{
    unsigned stage;      ///< 阶段0设URL长度、1发URL、2发GET/POST、3发POST正文、4读响应。
    bool is_post;        ///< body参数非NULL即POST；空字符串正文仍是POST。
    Result result;       ///< 整体HTTP完成状态；PENDING期间不得覆盖请求。
    CommandStep command; ///< 当前阶段AT步骤标志，与其他组件互斥。
    char url[256];       ///< 复制的NUL终止URL，最大255字节，不含额外CRLF。
    char body[PROJECT_HTTP_CAPACITY]; ///< 复制的NUL终止POST正文；发送长度为strlen，不包含NUL。
    char
        response[PROJECT_AT_RESPONSE_CAPACITY]; ///< HTTP成功后保存AT READ文本，业务解释层只读借用。
} http;
/** @brief 复制URL和可选POST正文，再建立共用HTTP阶段；参数失败不启动传输。 */
Result http_start(const char *url, const char *body)
{
    if (http.result == RESULT_PENDING)
    {
        return RESULT_BUSY;
    }
    if (!url || strlen(url) >= sizeof(http.url) || (body && strlen(body) >= sizeof(http.body)))
    {
        return RESULT_OVERFLOW;
    }
    strcpy(http.url, url);
    http.body[0] = 0;
    if (body)
    {
        strcpy(http.body, body);
    }
    http.is_post = body != NULL;
    http.stage = 0;
    http.command.is_waiting = false;
    http.result = RESULT_PENDING;
    http.response[0] = 0;
    return RESULT_OK;
}
/** @brief 依次推进URL提交、HTTP请求、正文与读取，不解释业务JSON。 */
void http_poll(void)
{
    Result result = RESULT_PENDING;
    char command[64];
    if (http.result != RESULT_PENDING)
    {
        return;
    }
    switch (http.stage)
    {
    /* 阶段0仅宣告URL长度；收到CONNECT后才允许提交精确URL。 */
    case 0:
        snprintf(command, sizeof(command), "AT+QHTTPURL=%u,80", (unsigned)strlen(http.url));
        result = at_command_step(&http.command, command, "CONNECT", PROJECT_HTTP_TIMEOUT_MS);
        break;
    /* URL不是AT命令，payload_step不追加CRLF，避免宣告长度与实际串流不符。 */
    case 1:
        result = at_payload_step(&http.command, http.url, strlen(http.url), "OK",
                                 PROJECT_COMMAND_TIMEOUT_MS);
        break;
    /* GET在本阶段等HTTP结果，POST先等数据输入提示；两条路径之后汇合到READ。 */
    case 2:
        if (http.is_post)
        {
            snprintf(command, sizeof(command), "AT+QHTTPPOST=%u,60,20",
                     (unsigned)strlen(http.body));
        }
        else
        {
            strcpy(command, "AT+QHTTPGET=80");
        }
        result =
            at_command_step(&http.command, command, http.is_post ? "CONNECT" : "+QHTTPGET: 0,200",
                            PROJECT_HTTP_TIMEOUT_MS);
        break;
    /* POST以模块已报告0,200为上传阶段成功；GET无正文，显式空步骤保持共用阶段编号。 */
    case 3:
        if (http.is_post)
        {
            result = at_payload_step(&http.command, http.body, strlen(http.body),
                                     "+QHTTPPOST: 0,200", PROJECT_HTTP_TIMEOUT_MS);
        }
        else
        {
            result = RESULT_OK;
        }
        break;
    /* 等完整读取终结行后保存响应副本；这里不将缺少配置字段当作传输失败。 */
    case 4:
        result = at_command_step(&http.command, "AT+QHTTPREAD=256", "+QHTTPREAD: 0",
                                 PROJECT_HTTP_TIMEOUT_MS);
        if (result == RESULT_OK)
        {
            strcpy(http.response, at_response());
            http.result = RESULT_OK;
        }
        break;
    default:
        result = RESULT_INVALID;
        break;
    }
    if (result == RESULT_OK)
    {
        http.stage++;
    }
    else if (result != RESULT_PENDING)
    {
        http.result = result;
        at_cancel();
    }
}
/** @brief 查询HTTP传输结果，业务批准和配置更新另由report解析。 */
Result http_result(void)
{
    return http.result;
}
/** @brief 返回HTTP内部保存的完整AT读取文本，由调用者立即解释。 */
const char *http_response(void)
{
    return http.response;
}

/** @brief 清除HTTP等待资格，允许下一次start；在途AT须由统一出口另行取消。 */
void http_cancel(void)
{
    http.result = RESULT_IO;
    http.command.is_waiting = false;
}
