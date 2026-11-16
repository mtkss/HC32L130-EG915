/**
 * @file application.h
 * @brief 编排SN、自检、激活、普通采样上传及统一关机。
 * @details
 * 依赖采样/网络/定位组件、上报/缓存/身份服务和board事件接口；不编码报文、不直接访问寄存器。
 * @note application_init只在启动装配时调用一次；其余接口在单一前台上下文使用，不可重入。
 */
#ifndef APPLICATION_H
#define APPLICATION_H
#include "domain.h"
/** @brief 应用状态仅本模块写入；自检模式与普通模式共享工作阶段。 */
typedef enum
{
    APP_SN,            ///< 尚无有效SN，只接收完整SN行，不进入普通业务。
    APP_WAIT_CHECK,    ///< 等待三次有效短按以触发自检；失败后也回到这里。
    APP_WAIT_ACTIVATE, ///< 已自检但未激活；三按提交标记，空闲可休眠。
    APP_SAMPLE,        ///< 等待ADC和50点运动采样，尚未决定是否开启模块。
    APP_NETWORK,       ///< 等待共享模块/网络准备，不直接据此认定上报成功。
    APP_LOCATE,        ///< 普通模式按偏好定位；自检模式只测基站来源。
    APP_SELF_GNSS,     ///< 自检第二个独立定位项目，不改变普通周期。
    APP_TIME,          ///< 尝试NTP并回退RTC，形成此条记录时间。
    APP_REPORT,        ///< 等待当前记录/自检报文传输及业务响应解释。
    APP_CACHE,         ///< 普通当前记录成功后补传FIFO；成功才移除队首。
    APP_SHUTDOWN,      ///< 统一取消各流程并等待模块断电，完成后去after_shutdown。
    APP_SLEEP          ///< 设闹钟并深睡；唤醒后返回调用点再采样。
} ApplicationState;
/** @brief 应用诊断快照，读取不消费状态，前台调用。 */
typedef struct
{
    ApplicationState state;  ///< 读取时APP所在阶段，只读副本，不可通过快照切状态。
    Result last_error;       ///< 本轮记录的失败码；下一次采样开始可清零，非完整故障历史。
    bool is_self_check;      ///< 当前工作流程是否属于自检，不是Flash永久激活状态。
    unsigned cached_records; ///< 读取时的RAM队列条数，范围0..PROJECT_CACHE_CAPACITY。
} ApplicationSnapshot;
/**
 * @brief 获取只读值快照；不暴露应用可变上下文。
 * @return APP状态、最近失败、自检标志及队列条数的按值副本，不含可写内部指针。
 * @note 仅前台调用，无IRQ并发快照保证，不改变或消费APP状态。
 */
ApplicationSnapshot application_snapshot(void);
/**
 * @brief 初始化上下文并读取身份，板级资源必须先就绪。
 * @note board_init之后、主循环之前调用一次；会重置APP/AT/缓存/周期，不能用于运行中无序重初始化。
 */
void application_init(void);
/**
 * @brief 每主循环推进一个产品阶段，非重入；硬件短事务预算见board.h。
 * @note 单一前台不可重入；先at_poll/按键再调度当前阶段。短硬件操作可同步等待，预算见board.h。
 * @note APP_SLEEP中可执行WFI，返回后在原前台继续；不是所有调用都保证立刻返回。
 */
void application_poll(void);
#endif
