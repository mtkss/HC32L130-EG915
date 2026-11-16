# HC32L130 + EG800Z-EU（带GNSS）适配分支

当前分支：`feature/eg800z-eu-gnss`。APN与板级接线沿用用户确认，先读[适配说明](docs/EG800ZEU_适配说明.md)及[分支验证记录](docs/EG800ZEU_验证记录.md)。`main`保留原EG915基线；以下工程目录/构建方法继续有效。

这是由D:\download\hc32l130f8ua_template复制基础依赖后重构的独立项目。源目录未修改，原业务文件保存在baseline，不参与新工程编译。

## 打开与构建

- MDK入口：[HC32L130_Refactored.uvprojx](MDK/HC32L130_Refactored.uvprojx)，唯一目标HC32L130_Refactored，选本机ARMCC5.06 update7 build960。
- 离线构建：在工程根目录执行PowerShell的tools/build.ps1；可用-KeilRoot指定安装根目录。默认C:\Keil_v5。不下载芯片包，不烧录。
- 产物：build/HC32L130_Refactored.axf、.hex、.map和.htm调用图。MDK构建产物在build/mdk。
- 芯片：HC32L130F8UA / Cortex-M0+；Flash64KiB、RAM8KiB。链接允许代码占0..0xEDFF，0xEE00起为身份区保留。栈2KiB，堆0。

## 模块

| 目录 | 职责 |
|---|---|
| core | 纯数据类型和UTC转换，不访问硬件 |
| app | SN、自检、激活、正常上报与故障出口 |
| components | 网络、定位、采样、周期决策 |
| services | AT、HTTP、报文、完整记录缓存和身份存储 |
| platform | 引脚、时钟、IRQ、有限硬件事务和睡眠 |
| config | 业务默认值、超时预算和服务器地址 |
| vendor | 原DDL/系统代码、CMSIS和保留RTT，许可证沿用来源 |
| tests | 运行真实业务C代码的板级替身测试 |
| docs | 架构、迁移差异和实际验证结果 |

## 主要入口

main→board_init→application_init→application_poll。APP内没有硬件寄存器和协议编码。自检与正常上传共用network/location/report服务；不同报文保持不同字段。调用application_snapshot可读取当前阶段、最后错误和缓存条数。

## 测试及代码格式

先执行tools/setup_tests.ps1（需要网络，将锁定依赖装到本工程tools/test_runtime），再执行tools/test.ps1。测试使用ARMCC编译相同业务C模块，在Unicorn Cortex-M指令模型运行；board替身提供可控模块应答、时间和Flash。它验证纯逻辑及模块协作，不验证HC32寄存器、中断时序、物理供电、Flash掉电或功耗。

tools/format.ps1 -Check核验自有C/H格式；去掉-Check才写入格式。vendor和baseline不会被格式化。

## 使用前阅读

- [新工程运行说明](docs/新工程运行说明.md)
- [架构与兼容边界](docs/架构与兼容边界.md)
- [重构迁移与行为变更](docs/重构迁移与行为变更.md)
- [验证记录](docs/验证记录.md)

本工程提供可编译、可模型回归的重构基线。真实蜂窝模块型号、AT版本、板卡版本、温度换算及最低功耗条件仍未确认；未经实板验收，不能视为量产固件。没有执行烧录或对设备写Flash。
