# EG800Z-EU适配分支验证记录

对象：feature/eg800z-eu-gnss，基线d54f2f1，日期2026-11-16（Asia/Hong_Kong）。

## 实际结果

| 验证 | 结果与证据 |
|---|---|
| 修改前基线 | ARM模型123项检查、0失败 |
| 最终ARM模型回归 | 174项检查、0失败；build/tests/unicorn.log和test.log。执行真实业务C代码，board由确定性替身提供 |
| 独立ARMCC构建 | 成功，0错误；build/build.log。沿用厂商诊断66抑制 |
| Keil MDK工程 | HC32L130_Refactored.uvprojx / HC32L130_Refactored，0错误、2个既有厂商ADC枚举告警；build/uv4_build.log |
| 格式/差异检查 | tools/format.ps1 -Check、git diff --check通过 |
| 注释与参数结构 | 32个自有C/H、167个函数定义均有用途，86个公开头文件接口参数标签与声明匹配；EG800ZEU_注释核验.json |
| 人工注释审查 | 已核对改动函数的前台上下文、AT独占/缓冲归属、秒/毫秒单位、正文有效长度、错误/取消/超时出口、用户确认与手册依据；机械统计不代替本项审阅 |
| 模块边界 | 无新增依赖环、导出可变全局；修改均在已构建C文件内，无遗漏新文件；模块边界核验.json |
| 外部代码 | 101个vendor/baseline文件SHA-256仍与main基线相同；平台引脚/IRQ/ADC/I2C/Flash布局未修改 |

## 覆盖的适配场景

- 实际GET/POST结果URC长度与正文严格匹配，自检/正常上传和缓存补传共用HTTP服务。
- CONNECT分片；正文包含ERROR、OK、QHTTPREAD字样仍不能提前完成；非零READ结果立即失败。
- 非空期望串校验；130秒CONNECT预算在126秒时仍在途，超过预算才超时。
- HTTP 2000/503拒绝、缺长度FORMAT、超长OVERFLOW、正文与AT包装分离。
- GNSS负经纬、3D、空COG、无定位/非法fix/负速度/非整数卫星数；失败不覆盖旧位置。
- GNSS已开启不重复启动；不支持能力时普通流程回退主小区、单项自检失败；总窗口无fix先关闭再回退。
- 自动附着后MCC250仍保留原运营商策略；APN拒绝明确失败且data_ready为false。
- 原基线配置/FIFO/SN/自检批准/激活/周期回绕/AT迟到和溢出等回归保留。

测试替身对未列出的AT命令报错，避免未知命令默认OK掩盖适配错误。测试中曾定位并修正替身命令长度、故障标志复位和测试初始化位置问题；上述结果来自修正后重新执行，不引用失败前的旧产物。

## 资源

ARMCC5.06 update7 build960，C99、Cortex-M0+、microlib；既有2KiB栈、0堆。

独立构建ROM27568字节，RAM7440字节（含2048字节栈），8KiB RAM剩余752字节。MDK因对象排列为7432字节。相较main独立基线，ROM增加896字节、RAM增加24字节。链接器可追踪主链静态最大栈1464字节，仍报告不可追踪函数指针/循环，不能声称真实最坏栈已证明。需实板栈水位及IRQ峰值验证。

## 未验证与使用约束

没有烧录、串口联调或设备Flash操作；未证明GNSS天线/TTFF/功耗、电源时序、实际AT固件、SIM漫游、服务器Content-Length和主小区定位质量。HTTP正文须已知非零长度且不超过672字节，未知长度/chunked、RX_lev到旧邻区信号编码、HTTPS认证配置未实现。详见[适配说明](EG800ZEU_适配说明.md)。

旧注释核验记录中的token/HEX一致结论属于main补注释历史；本分支有意更改协议行为，不能套用该结论。最终文件/产物SHA-256见[交付清单.json](交付清单.json)。
