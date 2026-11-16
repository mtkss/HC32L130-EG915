/**
 * @file http_service.h
 * @brief 通过AT实现共享的HTTP GET/POST传输阶段。
 * @details 只处理URL、正文和完整READ响应，不解释自检批准或周期参数。
 * @note 前台单实例；调用期间不得与其他网络/定位流程争用AT，取消HTTP仍需统一出口取消AT。
 */
#ifndef HTTP_SERVICE_H
#define HTTP_SERVICE_H
#include "domain.h"
/**
 * @brief 启动GET或POST；url和body同步复制。body=NULL为GET，最大511字节。
 * @param url 非NULL NUL终止串，长度小于256；同步复制。
 * @param body NULL表示GET；非NULL表示POST，长度小于PROJECT_HTTP_CAPACITY，同步复制。
 * @return OK表示操作建立而非HTTP成功；BUSY不改变当前事务；空URL或容量不满足返回OVERFLOW。
 * @note 前台单实例；result非PENDING时可再次启动。发送等待由http_poll推进。
 */
Result http_start(const char *url, const char *body);
/**
 * @brief 推进一个AT步骤；失败取消并由调用者决定重试/缓存。
 * @note 每次推进当前阶段一次；调用方须先持续at_poll，且期间不得让别的组件争用AT。
 * @note GET/POST均要求模块200结果和READ终结；READ终结行假设为+QHTTPREAD: 0，待真实模块验证。
 */
void http_poll(void);
/** @brief 查询完成；OK代表模块HTTP200且完整READ结束，非业务批准。 */
Result http_result(void);
/**
 * @brief 响应借用到下一次http_start；调用者立即解析。
 * @return 静态AT读取文本，只读借用到下一次有效http_start；仅http_result()==OK时有完成意义。
 */
const char *http_response(void);
/**
 * @brief 中止HTTP状态并释放下次启动资格；调用者同时取消AT事务。
 * @note 可重复取消，立即退出软件PENDING并清步骤资格；不主动at_cancel，也不撤回模块已接受的HTTP。
 */
void http_cancel(void);
#endif
