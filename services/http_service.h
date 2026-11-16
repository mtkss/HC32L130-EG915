/**
 * @file http_service.h
 * @brief 通过AT实现共享的HTTP GET/POST传输阶段。
 * @details 只处理URL、正文和定长READ正文，不解释自检批准或周期参数。
 * @note 前台单实例；调用期间不得与其他网络/定位流程争用AT，取消HTTP仍需统一出口取消AT。
 */
#ifndef HTTP_SERVICE_H
#define HTTP_SERVICE_H
#include "domain.h"
/**
 * @brief 启动GET或POST；url和body同步复制。body=NULL为GET，POST正文1..511字节。
 * @param url 非NULL
 * NUL终止串，以http://或https://开头，长度小于256；同步复制。HTTPS证书/SSL配置须另行提供，不声称已完成TLS认证适配。
 * @param body NULL表示GET；非NULL表示POST，非空且长度小于PROJECT_HTTP_CAPACITY，同步复制。
 * @return OK表示操作建立而非HTTP成功；BUSY不改变当前事务；非法协议/NULL
 * URL/空POST返回INVALID，容量不足OVERFLOW。
 * @note
 * 前台单实例；失败/取消后须由APP统一断电，禁止直接对可能遗留的数据模式重试；本软件接口无法检测物理断电。
 */
Result http_start(const char *url, const char *body);
/**
 * @brief 推进一个AT步骤；失败取消并由调用者缓存并统一关机恢复。
 * @note 每次推进当前阶段一次；调用方须先持续at_poll，且期间不得让别的组件争用AT。
 * @note
 * GET/POST要求result=0、2xx和已知非零正文长度；缺长度FORMAT，超过响应容量减96为OVERFLOW。READ要求完整正文、尾部OK与+QHTTPREAD:
 * 0。
 */
void http_poll(void);
/** @brief 查询完成；OK代表模块HTTP 2xx且完整READ结束，非业务批准。 */
Result http_result(void);
/**
 * @brief 响应借用到下一次http_start；调用者立即解析。
 * @return 静态NUL终止HTTP正文，只读借用到下一次有效http_start；仅http_result()==OK时有完成意义。
 */
const char *http_response(void);
/**
 * @brief 中止HTTP软件状态，物理模块仍须统一关机；调用者同时取消AT事务。
 * @note 可重复取消，立即退出软件PENDING并清步骤资格；不主动at_cancel，也不撤回模块已接受的HTTP。
 */
void http_cancel(void);
#endif
