# EG800Z-EU（带GNSS）适配说明

## 版本与确认边界

分支：feature/eg800z-eu-gnss；基线提交：d54f2f1（原EG915工程保持在main）。

用户确认：APN仍为sxzcat1，供电使能、PWRKEY和UART接线沿用原板，EG800Z-CN与EU的GNSS命令兼容。用户补充字符串为EG800ZEZ；与最初目标EG800ZEU的写法一并保留在本记录，不据此伪造完整订货号。没有取得ATI/AT+CGMR实机输出，也没有连接或烧录设备。

本次以附件为协议依据；CN手册用于EU的适用性来自用户确认，而不是文件标题本身。额外官网检索只用于检查型号混用风险，没有用EG800Q/K或QuecOpen硬件参数替代本工程的AT协议和板卡事实。

## 附件证据与代码映射

以下页码为PDF页序；印刷页码通常比PDF页序少1。原始PDF和全文提取放在用户本地资料中，不随公开源码仓库分发。

| 附件 | 章节 / PDF页 | 本次落实 |
|---|---|---|
| EG800Z-CN GNSS V1.0.0 Preliminary 20260211 | §2.3.3–2.3.5 / 23–26 | QGPS能力/状态探测、QGPS=1、QGPSLOC=2的11字段、QGPSEND；fix只接受2/3，卫星为1..99整数，空COG允许 |
| 同上 | §2.3.1.8 / 17–18、§2.3.7 / 28–29 | 不再强写gnssconfig=5和北斗输出配置；不每轮修改自动保存的AGNSS开关；保持模块已配置的默认星座/辅助策略 |
| HTTP(S) V1.4 | §1.1、§2.3.1 / 8、12–16 | 显式绑定contextid=1、requestheader=0、responseheader=0、rspout/auto=0、contenttype=4 |
| 同上 | §2.3.2–2.3.5 / 17–22 | URL/POST精确字节数，等待CONNECT后发送；POST CONNECT最大125s，主机预算130s；等待结果URC而非最初OK |
| 同上 | §2.3.9 / 27–28 | QHTTPREAD=80的80为包间隔秒数；正文按结果URC声明长度接收，再校验尾部OK和QHTTPREAD: 0 |
| QuecCell V1.4 | §1.1、§2.3 / 7、9–10 | EG800Z系列适用；18字段QENG servingcell继续使用；MCC/MNC整数与RSRP/RSSI手册范围检查 |
| 同上 | §2.4 / 11–12 | QCELL?提供RX_lev，不能冒充旧QCELLINFO的RSRP。本分支运行链仅上传主小区定位参数；旧邻区解析器保留用于基线兼容测试，不被目标定位链调用 |
| UniRTOS AT V1.0 | §2.9、§5.7、§6.1 / 18–19、63、68–69 | CGSN执行命令读取裸IMEI，QCCID不变；首次COPS=0自动选网，180s手册上限加5s主机余量 |

## 实际执行变化

### 网络

保留原板上电/PWRKEY顺序。ATE0 → CPIN READY → QICSGP（APN沿用）→ COPS=0 → 有界等待CGATT=1 → QIACT=1 → QENG判断国家 → 身份/CSQ。

只有MCC确认250时，才进入原两俄罗斯运营商选择、附着、Ping与恢复策略；取消了首次在未知国家强制注册25020的行为。APN、SIM、自动选网、附着或PDP关键失败不再仅凭modem_ready继续定位。身份/CSQ等非关键元数据仍尽力更新。换运营商后清除先前data_ready，防止沿用旧链路就绪位。

QIACT和QNTP的完整TCP/IP应用指导未出现在这四份附件中。QIACT主机等待120s、附着窗口60s属于软件策略预算；原QNTP命令与Ping业务保持基线实现，不声称已由本批附件完整校核其全部行为。

### GNSS

QGPS=?探测 → QGPS?查询。已开启时直接查询位置，未开启才发QGPS=1。不重写持久化星座、AGNSS服务器或自动启动设置。首次启动后5s查询，后续每5s尝试一次，在120s产品策略窗口内等待有效fix；单次AT等待可能使退出延迟至约125s，再加关闭/AT排空时间，不把120s当成严格端到端上界或TTFF保证。

成功和失败共用QGPSEND清理阶段。能力不支持可回退主小区；单项GNSS自检明确失败。取消仍由APP统一关机断电收尾。默认普通定位改为GNSS优先，服务器整组配置仍可覆盖该偏好。

### HTTP和错误恢复

显式配置 → URL CONNECT → URL精确载荷 → GET结果URC / POST CONNECT、精确载荷、结果URC → READ定长正文 → 尾部OK和READ结果 → 发布正文。

HTTP结果的错误码、HTTP状态、长度分别解析。0,2000不再因200前缀被误判成功；仅2xx且正文长度符合约束才继续。正文中的ERROR、OK、+QHTTPREAD: 0均作为载荷消费，不参与AT完成匹配。业务只得到正文，不能从AT包装或无效片段中提取配置/批准。

URL/POST等失败可能遗留模块数据模式。APP在网络关键失败或首个定位阶段失败时立即走统一关机，禁止继续GNSS/NTP等新AT命令。普通上报失败仍缓存记录后关机，保留原补传策略。不能仅依赖250ms串口排空证明模块退出数据模式。

### 基站定位兼容边界

保留旧服务端主小区URL编码和固定空邻区槽，未猜测QCELL RX_lev到RSRP的转换。RSSI低于-110dBm时旧加110编码会无符号下溢，本分支返回INVALID，避免发送FFFFFFxx。当前未实现基于RX_lev的新版多小区服务端协议；主小区定位效果须用实际服务验收。

## 状态机与资源选择

继续使用原裸机前台、单AT拥有者、enum + switch-case。网络/定位/HTTP职责没有变，新增的是顺序能力探测和帧边界，取消/断电仍由APP统一编排；无需引入RTOS或QP。IRQ、UART环、Flash布局、采样算法和板级接线未改。新定长READ只增加少量状态/计数，不增加正文工作区或动态分配。

## 明确限制与实板验收

- 当前HTTP业务只支持已声明非零Content-Length且正文不超过672字节；缺长度/零长度返回FORMAT，超长返回OVERFLOW。未实现chunked/未知长度响应、文件下载及任意二进制业务解析。
- HTTPS的证书、SSL上下文和认证策略未在本次配置；现有业务URL仍为HTTP。不能把接受https://字符串解释为已完成TLS安全适配。
- AGNSS沿用模块当前配置，未新增辅助数据下载、账号或凭据；断电热启动、GNSS备电、天线和功耗未验证。
- 保留原PWRKEY关机电平链（低3s、高30s、断供电）。AT手册§1.5/§11.1另给QPOWD正常关机及POWERED DOWN后的等待建议；本次没有在可能仍为数据模式的UART上冒险插入QPOWD。实际板级关机时序仍需对模块硬件设计和实测核对。
- UART511字节有效环容量和768字节AT累计缓冲未扩大；真实115200串流、URC夹杂、溢出、中断延迟和栈水位须实板测试。
- 必须记录ATI、CGMR、QGPS=?、QGPS?、QGPSLOC=2、QENG以及完整HTTP原始收发。验证实际服务器Content-Length、数据模式失败后断电恢复、首次上报失败缓存补传、自检批准和激活、GNSS无信号/已开启/不支持及主小区换坐标。

## 核验记录

本分支最终编译、指令模型回归、资源与注释审查结果见[EG800ZEU_验证记录.md](EG800ZEU_验证记录.md)。既有注释规范核验文件描述的是main基线补注释过程，不能作为本分支“执行逻辑未改”的证据。本次是有意的模块行为适配。

## 源码入口

| 职责 | 入口 |
|---|---|
| 默认定位策略 | [app/application.c:208](../app/application.c#L208) |
| 业务失败统一退出 | [app/application.c:235](../app/application.c#L235) |
| 自动网络及MCC250策略 | [components/network_manager.c:131](../components/network_manager.c#L131) |
| GNSS与主小区状态机 | [components/location_manager.c:109](../components/location_manager.c#L109) |
| HTTP事务及正文发布 | [services/http_service.c:143](../services/http_service.c#L143) |
| 固定长度READ接收 | [services/at_client.c:280](../services/at_client.c#L280) |
| 定位字段校验 | [services/protocol_codec.c:199](../services/protocol_codec.c#L199) |
| 正文控制字回归 | [tests/test_firmware.c:747](../tests/test_firmware.c#L747) |
