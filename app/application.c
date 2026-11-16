/**
 * @file application.c
 * @brief 编排SN、自检、激活、普通采样上传及统一关机。
 * @details
 * 依赖采样/网络/定位组件、上报/缓存/身份服务和board事件接口；不编码报文、不直接访问寄存器。
 * @note application_init只在启动装配时调用一次；其余接口在单一前台上下文使用，不可重入。
 */
#include "application.h"
#include "board.h"
#include "at_client.h"
#include "identity_store.h"
#include "record_cache.h"
#include "protocol_codec.h"
#include "report_service.h"
#include "sampling_service.h"
#include "network_manager.h"
#include "location_manager.h"
#include "upload_schedule.h"
#include "project_config.h"
#include <string.h>
#include <stdlib.h>

/** @brief APP唯一拥有的生命周期上下文；只由前台读写，IRQ不得直接访问。 */
static struct
{
    ApplicationState state,
        after_shutdown; ///< 当前阶段与统一关机完成后的目标阶段；仅begin_shutdown绑定后者。
    Result last_error;  ///< 本轮诊断失败码，开始新采样时清零；不是持久故障日志。
    bool is_self_check, is_first_report; ///< 当前业务模式及本次启动是否尚未完成首轮普通上报尝试。
    bool has_motion,
        is_reporting_cache; ///< 周期内运动锁存；缓存标志区分“尚未启动发送”和“等待当前队首”。
    unsigned confirm_count; ///< 确认窗口中的有效短按次数，达到3即消费并清零。
    uint32_t confirm_ms, idle_ms,
        led_ms;               ///< 单调毫秒：首个确认按键、最近有效短按/状态空闲、最近LED翻转。
    bool led_high;            ///< APP记忆的输出电平，不推断LED的真实亮灭极性。
    bool is_sn_dropping;      ///< 超长/溢出SN行隔离标志；一直丢弃到CR或LF。
    DeviceIdentity identity;  ///< 本机SN与当前模块身份；借给network更新IMEI/ICCID。
    DeviceConfig config;      ///< 当前整组有效配置；远程解析成功后才整体替换。
    TelemetryRecord record;   ///< 当前普通/自检工作快照；缓存使用按值副本。
    SelfCheckResult check;    ///< 仅本轮自检项目及服务器批准；不干扰周期调度。
    UploadSchedule schedule;  ///< 正常工作周期上下文，自检流程不调用其决策。
    CommandStep time_command; ///< NTP步骤独占的AT等待资格，不与其他组件共用。
    char sn_line[48];         ///< 前台组装的单行SN字节，sn_count为有效长度；超长后丢整行。
    size_t sn_count;          ///< 当前行已保存字节数；解析依赖精确长度而非未补NUL的字符串。
} app;
/** @brief 启动采样，即使失败也能进入故障处理，不留下PENDING假状态。 */
static void begin_sample(bool self_check)
{
    app.is_self_check = self_check;
    memset(&app.record, 0, sizeof(app.record));
    memset(&app.check, 0, sizeof(app.check));
    app.last_error = RESULT_OK;
    {
        Result result = sampling_start();
        if (result != RESULT_OK)
        {
            app.last_error = result;
        }
    }
    app.state = APP_SAMPLE;
}
/**
 * @brief 统一撤销AT及各子流程等待，再启动有界模块断电序列。
 * @param next 关机完成后的合法APP目标状态，由当前业务分支选择。
 * @note 前台；此函数不等待物理断电，APP_SHUTDOWN持续轮询才完成资源退出。
 */
static void begin_shutdown(ApplicationState next)
{
    /* 先取消底层AT，再撤销各拥有者的等待资格，避免下轮start永久BUSY；最后才等待电源序列。 */
    network_shutdown_start();
    sampling_cancel();
    location_cancel();
    report_cancel();
    app.after_shutdown = next;
    app.state = APP_SHUTDOWN;
}
/**
 * @brief 将有效短按事件聚合为有限确认窗中的三按确认。
 * @param pressed 本轮由board消费的短按释放事件，不是GPIO原始电平。
 * @param now 同一单调时基的uint32毫秒；窗口差值按无符号回绕比较。
 * @return 第三次有效短按为true并清计数；超时重新开始，不推进Flash状态。
 */
static bool confirm_press(bool pressed, uint32_t now)
{
    if (app.confirm_count && (uint32_t)(now - app.confirm_ms) >= PROJECT_KEY_CONFIRM_WINDOW_MS)
    {
        app.confirm_count = 0;
    }
    if (!pressed)
    {
        return false;
    }
    if (!app.confirm_count)
    {
        app.confirm_ms = now;
    }
    if (++app.confirm_count >= 3)
    {
        app.confirm_count = 0;
        return true;
    }
    return false;
}
/** @brief 前台接收完整SN行，只接受SN=后恰好6位数字，不对外回显额外调试文字。 */
static void receive_serial_number(void)
{
    char byte;
    unsigned budget = 64;
    if (board_uart_take_overflow())
    {
        app.sn_count = 0;
        app.is_sn_dropping = true;
    }
    while (budget-- && board_uart_read(&byte))
    {
        if (byte == '\r' || byte == '\n')
        {
            if (!app.is_sn_dropping && app.sn_count == 9 && !memcmp(app.sn_line, "SN=", 3))
            {
                unsigned i;
                bool valid = true;
                uint32_t serial = 0;
                for (i = 3; i < 9; i++)
                {
                    if (app.sn_line[i] < '0' || app.sn_line[i] > '9')
                    {
                        valid = false;
                        break;
                    }
                    serial = serial * 10u + (unsigned)(app.sn_line[i] - '0');
                }
                if (valid && identity_store_serial(serial) == RESULT_OK)
                {
                    app.identity.serial_number = serial;
                    app.state = APP_WAIT_CHECK;
                }
            }
            app.sn_count = 0;
            app.is_sn_dropping = false;
        }
        else if (app.is_sn_dropping)
        {
            continue;
        }
        else if (app.sn_count + 1 < sizeof(app.sn_line))
        {
            app.sn_line[app.sn_count++] = byte;
        }
        else
        {
            app.sn_count = 0;
            app.is_sn_dropping = true;
        }
    }
}
/** @brief 根据前台状态更新LED；IRQ不输出日志、不控制业务。 */
static void update_led(uint32_t now)
{
    bool active = app.state >= APP_SAMPLE && app.state <= APP_CACHE;
    if (active && (uint32_t)(now - app.led_ms) >= PROJECT_LED_TOGGLE_MS)
    {
        app.led_ms = now;
        app.led_high = !app.led_high;
        board_led_write(app.led_high);
    }
    else if (!active)
    {
        app.led_high = false;
        board_led_write(false);
    }
}
/** @brief 复制定位成功值，保留当前样本的其他字段不被全局数据覆盖。 */
static void accept_location(void)
{
    if (location_result() == RESULT_OK)
    {
        if (app.is_self_check)
        {
            app.check.cell = *location_position();
        }
        else
        {
            app.record.position = *location_position();
        }
    }
    app.record.rsrp_dbm = location_rsrp_dbm();
}
/** @brief 启动发送快照；若编码不合法，普通有效记录留缓存，自检保持未批准。 */
static void begin_report(void)
{
    Result result = report_start(&app.record, &app.identity, app.is_self_check ? &app.check : NULL);
    app.is_reporting_cache = false;
    if (result == RESULT_OK)
    {
        app.state = APP_REPORT;
    }
    else
    {
        app.last_error = result;
        if (!app.is_self_check && app.record.position.is_valid && app.record.has_timestamp)
        {
            record_cache_push(&app.record);
        }
        begin_shutdown(app.is_self_check ? APP_WAIT_CHECK : APP_SLEEP);
    }
}
/** @brief 初始化APP拥有的状态与配置，根据已提交身份选择生产入口或常规采集。 */
void application_init(void)
{
    memset(&app, 0, sizeof(app));
    app.config.still_interval_min = PROJECT_DEFAULT_STILL_MIN;
    app.config.motion_interval_min = PROJECT_DEFAULT_MOTION_MIN;
    app.config.continuous_interval_min = PROJECT_DEFAULT_CONTINUOUS_MIN;
    app.config.location_policy = LOCATION_CELL_FIRST;
    app.is_first_report = true;
    at_init();
    record_cache_init();
    upload_schedule_init(&app.schedule, board_now_ms());
    app.idle_ms = board_now_ms();
    if (!identity_store_read(&app.identity.serial_number))
    {
        app.state = APP_SN;
    }
    else if (identity_store_activated())
    {
        begin_sample(false);
    }
    else
    {
        app.state = identity_store_checked() ? APP_WAIT_ACTIVATE : APP_WAIT_CHECK;
    }
}
/** @brief 在前台消费AT进展与按键事件，按当前生命周期状态推进一步。 */
void application_poll(void)
{
    uint32_t now = board_now_ms();
    bool pressed = board_take_short_press();
    Result result;
    const NetworkSnapshot *network;
    /* 先消费串口才能使本轮组件查询到最新终态；按键也在长网络等待期间持续推进。 */
    at_poll();
    update_led(now);
    if (pressed)
    {
        app.idle_ms = now;
    }
    switch (app.state)
    {
    case APP_SN:
        receive_serial_number();
        return;
    case APP_WAIT_CHECK:
        if (confirm_press(pressed, now))
        {
            begin_sample(true);
        }
        return;
    /* 激活是持久化业务提交：只有三按确认且标记写成功才允许常规采样。 */
    case APP_WAIT_ACTIVATE:
        if (confirm_press(pressed, now) && identity_store_mark_activated() == RESULT_OK)
        {
            upload_schedule_init(&app.schedule, now);
            app.is_first_report = true;
            begin_sample(false);
        }
        else if (!app.confirm_count &&
                 (uint32_t)(now - app.idle_ms) >= PROJECT_KEY_CONFIRM_WINDOW_MS)
        {
            board_take_wake_events();
            board_sleep();
            board_take_wake_events();
            app.idle_ms = board_now_ms();
        }
        return;
    /* 自检只消费采样结果，不调用普通周期决策；非首轮普通采样失败不伪装静止。 */
    case APP_SAMPLE:
        sampling_poll();
        if (sampling_result() == RESULT_PENDING)
        {
            return;
        }
        if (sampling_result() != RESULT_OK)
        {
            app.last_error = sampling_result();
        }
        sampling_copy(&app.record);
        app.check.has_sensor_sample = app.record.has_sensor_sample;
        if (!app.is_self_check)
        {
            if (app.record.has_sensor_sample && app.record.is_moving)
            {
                app.has_motion = true;
            }
            if (!app.is_first_report &&
                (!app.record.has_sensor_sample ||
                 !upload_schedule_due(&app.schedule, &app.config, app.record.is_moving, now)))
            {
                app.state = APP_SLEEP;
                return;
            }
            app.record.is_moving = app.has_motion;
        }
        result = network_start(&app.identity);
        if (result == RESULT_OK)
        {
            app.state = APP_NETWORK;
        }
        else
        {
            begin_shutdown(app.is_self_check ? APP_WAIT_CHECK : APP_SLEEP);
        }
        return;
    /* modem_ready是继续发送AT的门槛；data_ready仅供诊断，不挡住可离线获得的GNSS位置。 */
    case APP_NETWORK:
        network_poll();
        result = network_result();
        if (result == RESULT_PENDING)
        {
            return;
        }
        network = network_snapshot();
        app.record.rssi = network->rssi;
        app.check.has_network = network->data_ready;
        if (!network->modem_ready)
        {
            app.last_error = result;
            begin_shutdown(app.is_self_check ? APP_WAIT_CHECK : APP_SLEEP);
            return;
        }
        result = location_start(app.config.location_policy,
                                app.is_self_check ? LOCATION_CELL : LOCATION_NONE,
                                app.identity.serial_number);
        if (result == RESULT_OK)
        {
            app.state = APP_LOCATE;
        }
        else
        {
            begin_shutdown(app.is_self_check ? APP_WAIT_CHECK : APP_SLEEP);
        }
        return;
    case APP_LOCATE:
        location_poll();
        if (location_result() == RESULT_PENDING)
        {
            return;
        }
        if (location_result() != RESULT_OK)
        {
            app.last_error = location_result();
        }
        accept_location();
        if (app.is_self_check)
        {
            location_start(LOCATION_GNSS_FIRST, LOCATION_GNSS, app.identity.serial_number);
            app.state = APP_SELF_GNSS;
        }
        else
        {
            app.time_command.is_waiting = false;
            app.state = APP_TIME;
        }
        return;
    /* 单项自检独立保存两种来源，不能让第二次定位覆盖第一次自检结果。 */
    case APP_SELF_GNSS:
        location_poll();
        if (location_result() == RESULT_PENDING)
        {
            return;
        }
        if (location_result() == RESULT_OK)
        {
            app.check.gnss = *location_position();
        }
        app.time_command.is_waiting = false;
        app.state = APP_TIME;
        return;
    /* 网络授时尽力而为：解析/RTC设置失败仍读取RTC；这里未向last_error传播Rtc_SetTime返回值。 */
    case APP_TIME:
        result = at_command_step(&app.time_command, "AT+QNTP=1,\"ntp.aliyun.com\",123",
                                 "+QNTP:", 10000u);
        if (result == RESULT_PENDING)
        {
            return;
        }
        if (result == RESULT_OK)
        {
            uint32_t epoch;
            if (protocol_parse_ntp(at_response(), &epoch) == RESULT_OK)
            {
                board_rtc_set(epoch);
            }
        }
        app.record.timestamp_s = board_rtc_epoch();
        app.record.has_timestamp = app.record.timestamp_s != 0;
        begin_report();
        return;
    /* HTTP终态、服务器批准和Flash提交是三个不同结果，只有后两项均满足才推进激活入口。 */
    case APP_REPORT:
        report_poll();
        result = report_result();
        if (result == RESULT_PENDING)
        {
            return;
        }
        if (result != RESULT_OK)
        {
            app.last_error = result;
        }
        if (app.is_self_check)
        {
            app.check.has_server_approval = result == RESULT_OK && report_outcome()->is_approved;
            if (!app.check.has_server_approval && result == RESULT_OK)
            {
                app.last_error = RESULT_REJECTED;
            }
            if (app.check.has_server_approval && identity_store_mark_checked() == RESULT_OK)
            {
                app.idle_ms = now;
                begin_shutdown(APP_WAIT_ACTIVATE);
            }
            else
            {
                begin_shutdown(APP_WAIT_CHECK);
            }
            return;
        }
        /* 周期以一次已完成的尝试为边界；失败快照单独缓存，首轮失败也不进入永久停机。 */
        upload_schedule_complete(&app.schedule, app.has_motion, now);
        app.has_motion = false;
        app.is_first_report = false;
        if (result != RESULT_OK)
        {
            record_cache_push(&app.record);
            begin_shutdown(APP_SLEEP);
            return;
        }
        if (report_outcome()->has_config)
        {
            app.config = report_outcome()->config;
        }
        app.state = APP_CACHE;
        app.is_reporting_cache = false;
        return;
    /* peek不出队；请求/发送失败保持队首，只有服务确认后pop，避免重排或丢掉失败记录。 */
    case APP_CACHE:
        if (!app.is_reporting_cache)
        {
            const TelemetryRecord *cached = record_cache_peek();
            if (!cached)
            {
                begin_shutdown(APP_SLEEP);
                return;
            }
            result = report_start(cached, &app.identity, NULL);
            if (result != RESULT_OK)
            {
                begin_shutdown(APP_SLEEP);
                return;
            }
            app.is_reporting_cache = true;
            return;
        }
        report_poll();
        result = report_result();
        if (result == RESULT_PENDING)
        {
            return;
        }
        if (result == RESULT_OK)
        {
            record_cache_pop();
            if (report_outcome()->has_config)
            {
                app.config = report_outcome()->config;
            }
            app.is_reporting_cache = false;
        }
        else
        {
            begin_shutdown(APP_SLEEP);
        }
        return;
    case APP_SHUTDOWN:
        if (network_shutdown_poll())
        {
            app.state = app.after_shutdown;
            app.idle_ms = now;
        }
        return;
    /* 此处消费已有唤醒位是本轮事件已处理的边界；board仍原子检查新事件和按键实际电平。 */
    case APP_SLEEP:
        board_take_wake_events();
        board_sleep();
        board_take_wake_events();
        begin_sample(false);
        return;
    /* 非法状态不能无条件绕过生产门槛；按持久身份选择合法恢复入口。 */
    default:
        app.last_error = RESULT_INVALID;
        begin_shutdown(identity_store_activated()                         ? APP_SLEEP
                       : identity_store_read(&app.identity.serial_number) ? APP_WAIT_CHECK
                                                                          : APP_SN);
        return;
    }
}

/** @brief 按值导出诊断信息，调用者不能借此修改内部状态。 */
ApplicationSnapshot application_snapshot(void)
{
    ApplicationSnapshot snapshot;
    snapshot.state = app.state;
    snapshot.last_error = app.last_error;
    snapshot.is_self_check = app.is_self_check;
    snapshot.cached_records = record_cache_count();
    return snapshot;
}
