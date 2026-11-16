/**
 * @file test_firmware.c
 * @brief 用真实业务C代码和确定性board替身做回归。
 * @details 覆盖解析、缓存、时间、生产生命周期、网络和故障路径；不链接真实board.c。
 * @note 测试前台独占替身数据；短按由用例直接注入，不证明实际GPIO消抖、寄存器、功耗或Flash掉电行为。
 */
#include "utc_calendar.h"
#include "protocol_codec.h"
#include "record_cache.h"
#include "upload_schedule.h"
#include "at_client.h"
#include "http_service.h"
#include "report_service.h"
#include "identity_store.h"
#include "board.h"
#include "application.h"
#include "network_manager.h"
#include "location_manager.h"
#include "sampling_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** @brief 测试专用半主机退出，参数0表示成功；定义于test_exit.s。 */
void test_exit(unsigned code);
/** @brief 断言执行数和失败数；检查数包含每次模拟传输的长度校验，不是独立用例数。 */
static unsigned checks, failures;
/** @brief 不可关闭的测试断言：记录全部失败并继续执行，结尾统一以非零状态报告。 */
#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if (!(condition))                                                                          \
        {                                                                                          \
            failures++;                                                                            \
            printf("FAIL line %u: %s\n", (unsigned)__LINE__, #condition);                          \
        }                                                                                          \
    } while (0)
/* 替身只有前台，不模拟寄存器、IRQ优先级或真实电气行为。 */
/** @brief 由用例显式推进的单调毫秒；没有真实IRQ或宿主机睡眠。 */
static uint32_t clock_ms;
/** @brief 固定模拟Unix起点；NTP替身可调基值，不能代表当前真实日期。 */
static uint32_t rtc_base = 1700000000u;
/** @brief 替身串口输入工作区，head/tail为有效字节边界；超过此容量令测试失败。 */
static char input[4096];
/** @brief 注入端/消费端索引；测试单线程，不用于证明真实环并发正确性。 */
static unsigned input_head, input_tail;
/** @brief 故障溢出、自动应答模式、一次短按和模拟供电请求状态。 */
static bool overflow, automatic, short_press, modem_supply;
/** @brief 模拟身份区0..100字节；复位填0xFF，不模拟擦写寿命或掉电。 */
static uint8_t storage[101];
/** @brief 模拟睡眠、普通POST和自检POST的次数，供生命周期断言。 */
static unsigned sleeps, posts, self_posts;
/** @brief 还需故意返回ERROR的POST次数，消费后递减。 */
static unsigned fail_posts;
/** @brief 用例选择的国家、Ping失败和无GNSS fix故障开关。 */
static bool is_russia, is_ping_blocked, has_no_gnss_fix;
/** @brief 收到QGPSEND的次数，用于证明失败后确实尝试清理。 */
static unsigned gnss_close_count;
/** @brief 两运营商COPS尝试次数，检查首试/轮询而非真实注册。 */
static unsigned megafon_attempts, tele2_attempts;
/** @brief 拼接命令与当前URL的替身副本，供脚本路由应答。 */
static char command_buffer[256], current_url[256];
/** @brief 已拼命令长度与模块宣告的下个精确载荷长度，不含CRLF。 */
static size_t command_count, expected_payload;
/** @brief 待发原始载荷是URL还是POST JSON，决定替身的长度检查和响应。 */
static bool payload_is_url;

/** @brief 测试模型复位全部外部输入，生产模块通过各自init重置。 */
static void mock_reset(void)
{
    clock_ms = 0;
    input_head = input_tail = 0;
    overflow = false;
    automatic = false;
    short_press = false;
    memset(storage, 0xFF, sizeof(storage));
    command_count = expected_payload = 0;
    current_url[0] = 0;
    sleeps = posts = self_posts = fail_posts = 0;
    is_russia = is_ping_blocked = has_no_gnss_fix = false;
    gnss_close_count = 0;
    megafon_attempts = tele2_attempts = 0;
}
/** @brief 注入串口完整或片段数据，容量溢出令本用例失败。 */
static void inject(const char *text)
{
    while (*text)
    {
        if (input_head >= sizeof(input))
        {
            CHECK(false);
            return;
        }
        input[input_head++] = *text++;
    }
}
/** @brief 模块协议替身，精确验证URL/JSON长度并按命令返回可控响应。 */
static void answer(const char *command)
{
    if (!strncmp(command, "AT+QHTTPURL=", 12))
    {
        expected_payload = (size_t)strtoul(command + 12, NULL, 10);
        payload_is_url = true;
        inject("CONNECT\r\n");
    }
    else if (!strncmp(command, "AT+QHTTPPOST=", 13))
    {
        expected_payload = (size_t)strtoul(command + 13, NULL, 10);
        payload_is_url = false;
        inject("CONNECT\r\n");
    }
    else if (!strcmp(command, "AT+QHTTPGET=80"))
    {
        inject("+QHTTPGET: 0,200,40\r\n");
    }
    else if (!strcmp(command, "AT+QHTTPREAD=256"))
    {
        if (strstr(current_url, "/api/cw"))
        {
            inject("CONNECT\r\n{\"lat\":31.2,\"lon\":121.3}\r\n+QHTTPREAD: 0\r\n");
        }
        else if (strstr(current_url, "selfCheck"))
        {
            inject("CONNECT\r\n{\"status\":\"1\"}\r\n+QHTTPREAD: 0\r\n");
        }
        else
        {
            inject("CONNECT\r\n{\"uploadInterval\":180,\"moveUploadInterval\":60,"
                   "\"continuesUploadInterval\":720,\"locateType\":2}\r\n+QHTTPREAD: 0\r\n");
        }
    }
    else if (!strcmp(command, "AT+CPIN?"))
    {
        inject("+CPIN: READY\r\nOK\r\n");
    }
    else if (!strncmp(command, "AT+QENG=", 8))
    {
        if (is_russia)
        {
            inject("+QENG: "
                   "\"servingcell\",\"NOCONN\",\"LTE\",\"FDD\",250,20,1234,100,1,3,5,5,ABCD,-90,-"
                   "10,-70,20,30\r\nOK\r\n");
        }
        else
        {
            inject("+QENG: "
                   "\"servingcell\",\"NOCONN\",\"LTE\",\"FDD\",460,0,1234,100,1,3,5,5,ABCD,-90,-10,"
                   "-70,"
                   "20,30\r\nOK\r\n");
        }
    }
    else if (!strcmp(command, "AT+QCELLINFO?"))
    {
        inject(
            "+QCELLINFO: \"neighbourcell\",\"LTE\",460,0,ABCD,1234,205,28,-,2300,-103\r\nOK\r\n");
    }
    else if (!strcmp(command, "AT+CGATT?"))
    {
        inject("+CGATT: 1\r\nOK\r\n");
    }
    else if (!strcmp(command, "AT+QCCID"))
    {
        inject("+QCCID: 89860012345678901234\r\nOK\r\n");
    }
    else if (!strcmp(command, "AT+CGSN=1"))
    {
        inject("+CGSN: 123456789012345\r\nOK\r\n");
    }
    else if (!strcmp(command, "AT+CSQ"))
    {
        inject("+CSQ: 23,99\r\nOK\r\n");
    }
    else if (!strncmp(command, "AT+QNTP=", 8))
    {
        inject("+QNTP: 0,\"2026/11/16,12:00:00+00\"\r\nOK\r\n");
    }
    else if (!strcmp(command, "AT+QGPSLOC=2"))
    {
        if (has_no_gnss_fix)
        {
            inject("ERROR\r\n");
        }
        else
        {
            inject("+QGPSLOC: 123519.0,31.2,121.3,1.0,20,2,0,0,0,161126,8\r\nOK\r\n");
        }
    }
    else if (!strcmp(command, "AT+QGPSEND"))
    {
        gnss_close_count++;
        inject("OK\r\n");
    }
    else if (!strncmp(command, "AT+QPING", 8))
    {
        inject(is_ping_blocked ? "ERROR\r\n" : "+QPING: 0,8.8.8.8,32,10,64\r\n");
    }
    else if (strstr(command, "AT+COPS=1,2,\"25002\""))
    {
        megafon_attempts++;
        inject("OK\r\n");
    }
    else if (strstr(command, "AT+COPS=1,2,\"25020\""))
    {
        tele2_attempts++;
        inject("OK\r\n");
    }
    else
    {
        inject("OK\r\n");
    }
}
/** @brief 提供无寄存器访问的测试初始化替身。 */
void board_init(void)
{
}
/** @brief 返回用例显式推进的模拟毫秒，不依赖宿主机时间。 */
uint32_t board_now_ms(void)
{
    return clock_ms;
}
/** @brief 记录模拟电源状态，供休眠前断电断言使用。 */
void board_modem_supply(bool enabled)
{
    modem_supply = enabled;
}
/** @brief 接收测试中的PWRKEY调用，不模拟电气波形。 */
void board_modem_key(bool high)
{
    (void)high;
}
/** @brief 忽略测试LED电平；本组用例不验证物理LED。 */
void board_led_write(bool high)
{
    (void)high;
}
/** @brief 一次性消费用例注入的有效短按事件，不代替真实消抖验证。 */
bool board_take_short_press(void)
{
    bool value = short_press;
    short_press = false;
    return value;
}
/** @brief 本组测试不注入IRQ事件，返回空事件位。 */
unsigned board_take_wake_events(void)
{
    return 0;
}
/** @brief 核对精确载荷长度，按当前命令生成确定性应答。 */
Result board_uart_write(const char *bytes, size_t count)
{
    if (!automatic)
    {
        return RESULT_OK;
    }
    if (expected_payload)
    {
        CHECK(count == expected_payload);
        if (payload_is_url)
        {
            CHECK(count < sizeof(current_url));
            memcpy(current_url, bytes, count);
            current_url[count] = 0;
            inject("OK\r\n");
        }
        else
        {
            if (strstr(current_url, "selfCheck"))
            {
                self_posts++;
            }
            else
            {
                posts++;
            }
            if (fail_posts)
            {
                fail_posts--;
                inject("ERROR\r\n");
            }
            else
            {
                inject("+QHTTPPOST: 0,200,120\r\n");
            }
        }
        expected_payload = 0;
        return RESULT_OK;
    }
    if (command_count + count >= sizeof(command_buffer))
    {
        return RESULT_OVERFLOW;
    }
    memcpy(command_buffer + command_count, bytes, count);
    command_count += count;
    command_buffer[command_count] = 0;
    if (command_count >= 2 && !memcmp(command_buffer + command_count - 2, "\r\n", 2))
    {
        command_buffer[command_count - 2] = 0;
        answer(command_buffer);
        command_count = 0;
    }
    return RESULT_OK;
}
/** @brief 从替身输入队列取一个字节，允许测试控制分片边界。 */
bool board_uart_read(char *byte)
{
    if (input_tail == input_head)
    {
        return false;
    }
    *byte = input[input_tail++];
    if (input_tail == input_head)
    {
        input_tail = input_head = 0;
    }
    return true;
}
/** @brief 清除模拟旧输入及溢出标记，保持与板级接口消费语义一致。 */
void board_uart_flush(void)
{
    input_tail = input_head = 0;
    overflow = false;
}
/** @brief 一次性消费用例注入的RX溢出标志。 */
bool board_uart_take_overflow(void)
{
    bool result = overflow;
    overflow = false;
    return result;
}
/** @brief 从固定测试日期及模拟毫秒生成可复现墙上时间。 */
uint32_t board_rtc_epoch(void)
{
    return rtc_base + clock_ms / 1000u;
}
/** @brief 调整模拟RTC基值，不影响通信单调计时。 */
Result board_rtc_set(uint32_t epoch)
{
    rtc_base = epoch - clock_ms / 1000u;
    return RESULT_OK;
}
/** @brief 断言模块已断电后推进模拟十分钟，不验证真实WFI。 */
void board_sleep(void)
{
    CHECK(!modem_supply);
    clock_ms += 600000u;
    sleeps++;
}
/** @brief 返回确定性电池和旧温度推导值，隔离模拟电路。 */
Result board_sample_adc(float *b, float *t)
{
    *b = 4.1f;
    *t = 10.0f;
    return RESULT_OK;
}
/** @brief 允许启动替身传感器，不访问I2C。 */
Result board_motion_start(void)
{
    return RESULT_OK;
}
/** @brief 提供固定静止XYZ样本，以确定性验证方差及周期。 */
Result board_motion_sample(float axes[3])
{
    axes[0] = 0;
    axes[1] = 0;
    axes[2] = 1;
    return RESULT_OK;
}
/** @brief 关闭替身无额外动作，资源清理由状态断言检查。 */
void board_motion_stop(void)
{
}
/** @brief 读取模拟身份字节，保持越界返回0xFF约束。 */
uint8_t board_storage_read(unsigned offset)
{
    return offset <= 100 ? storage[offset] : 0xFF;
}
/** @brief 模拟仅空白或相同值可写，不证明真实Flash掉电行为。 */
Result board_storage_write(unsigned offset, uint8_t value)
{
    if (offset > 100 || (storage[offset] != 0xFF && storage[offset] != value))
    {
        return RESULT_IO;
    }
    storage[offset] = value;
    return RESULT_OK;
}
/** @brief FVP启动文件所需系统初始化；测试不访问HC32外设。 */
void SystemInit(void)
{
}
/** @brief 校验解析失败不污染输出及完整记录FIFO语义。 */
static void test_codecs_and_cache(void)
{
    DeviceConfig config = {180, 60, 720, LOCATION_CELL_FIRST}, before = config;
    Position position = {0};
    CellInfo cell;
    char text[600], digits[21];
    size_t length;
    uint32_t epoch;
    unsigned fields[6];
    TelemetryRecord record = {0};
    DeviceIdentity id = {123456, "123456789012345", "89860012345678901234"};
    CHECK(protocol_parse_config("{\"uploadInterval\":1}", &config) != RESULT_OK);
    CHECK(!memcmp(&before, &config, sizeof(config)));
    CHECK(protocol_parse_config("{\"uploadInterval\":0,\"moveUploadInterval\":60,"
                                "\"continuesUploadInterval\":720,\"locateType\":2}",
                                &config) == RESULT_INVALID);
    CHECK(protocol_parse_config("{\"uploadInterval\":\"180\",\"moveUploadInterval\":60,"
                                "\"continuesUploadInterval\":720,\"locateType\":1}",
                                &config) == RESULT_OK);
    CHECK(config.location_policy == LOCATION_GNSS_FIRST);
    CHECK(!protocol_is_approved("{\"status\":\"10\"}"));
    CHECK(protocol_is_approved("{\"status\":\"1\"}"));
    CHECK(!protocol_is_approved("{\"status\":1,"));
    CHECK(protocol_parse_position("{\"lat\":0,\"lon\":0}", &position) == RESULT_OK);
    CHECK(position.is_valid);
    CHECK(protocol_parse_position("{\"lat\":91,\"lon\":0}", &position) == RESULT_INVALID);
    CHECK(protocol_parse_position("{\"lat\":NaN,\"lon\":0}", &position) != RESULT_OK);
    CHECK(protocol_parse_gnss("+QGPSLOC: 123519,31.2,121.3,1,20,2,0,0,0,161126,8\r\n", &position) ==
          RESULT_OK);
    CHECK(protocol_parse_gnss("+QGPSLOC: 123519,31.2,121.3,1,20,0,0,0,0,161126,0\r\n", &position) ==
          RESULT_INVALID);
    CHECK(protocol_parse_cell("+QENG: "
                              "\"servingcell\",\"NOCONN\",\"LTE\",\"FDD\",460,0,1234,100,1,3,5,5,"
                              "ABCD,-90,-10,-70,20,30\r\n",
                              &cell) == RESULT_OK);
    CHECK(cell.mcc == 460);
    strcpy(text, "+QCELLINFO: \"neighbourcell\",\"LTE\",460,0,ABCD,1234,205,28,-,2300,-103\n");
    {
        char line[100];
        unsigned i;
        strcpy(line, text);
        for (i = 0; i < 3; i++)
        {
            strcat(text, line);
        }
    }
    protocol_parse_neighbors(text, &cell);
    CHECK(cell.neighbor_count == 3);
    CHECK(protocol_build_cell_url(&cell, 123456, text, sizeof(text)) == RESULT_OK);
    CHECK(protocol_parse_digits("+CGSN: 123456789012345\r\n", "+CGSN:", digits, 16, 15) ==
          RESULT_OK);
    CHECK(strlen(digits) == 15);
    CHECK(protocol_parse_digits("+CGSN: 1234567890123456\r\n", "+CGSN:", digits, 16, 15) ==
          RESULT_OVERFLOW);
    CHECK(protocol_parse_ntp("+QNTP: 1", &epoch) == RESULT_FORMAT);
    CHECK(protocol_parse_ntp("+QNTP: 0,\"2026/11/16,12:00:00+32\"", &epoch) == RESULT_OK);
    CHECK(epoch == utc_calendar_to_epoch(2026, 11, 16, 4, 0, 0));
    CHECK(utc_calendar_to_epoch(2026, 2, 29, 0, 0, 0) == 0);
    epoch = utc_calendar_to_epoch(2024, 2, 29, 23, 59, 59);
    utc_calendar_from_epoch(epoch, fields);
    CHECK(fields[0] == 2024 && fields[1] == 2 && fields[2] == 29 && fields[3] == 23);
    record.position = position;
    record.position.is_valid = true;
    record.has_timestamp = true;
    record.timestamp_s = epoch;
    record.battery_v = 4.1f;
    record.temperature_legacy = 10;
    CHECK(protocol_build_report(&record, &id, NULL, text, sizeof(text), &length) == RESULT_OK);
    CHECK(length == strlen(text));
    CHECK(strstr(text, "\"type\":\"1\"") != NULL);
    CHECK(protocol_build_report(&record, &id, NULL, text, 10, &length) == RESULT_OVERFLOW);
    record_cache_init();
    {
        unsigned i;
        for (i = 0; i < 21; i++)
        {
            record.timestamp_s = i + 1;
            record_cache_push(&record);
        }
    }
    CHECK(record_cache_count() == 20 && record_cache_dropped() == 1);
    CHECK(record_cache_peek()->timestamp_s == 2);
    record.battery_v = 0;
    CHECK(record_cache_peek()->battery_v > 4.0f);
    record_cache_pop();
    CHECK(record_cache_peek()->timestamp_s == 3);
}
/** @brief 覆盖真实AT的分片、超时、迟到排空、行错误与环溢出。 */
static void test_at(void)
{
    mock_reset();
    at_init();
    CHECK(at_start("AT", 2, false, "OK", 1000) == RESULT_OK);
    inject("O");
    at_poll();
    CHECK(at_result() == RESULT_PENDING);
    inject("K\r\n");
    at_poll();
    clock_ms += 40;
    at_poll();
    CHECK(at_result() == RESULT_OK);
    CHECK(at_start("AT", 2, false, "OK", 100) == RESULT_OK);
    clock_ms += 101;
    at_poll();
    CHECK(at_result() == RESULT_TIMEOUT);
    inject("OK\r\n");
    at_poll();
    CHECK(at_start("AT", 2, false, "OK", 100) == RESULT_BUSY);
    clock_ms += 251;
    at_poll();
    CHECK(at_start("AT", 2, false, "OK", 100) == RESULT_OK);
    inject("ERROR\r\n");
    at_poll();
    CHECK(at_result() == RESULT_IO);
    clock_ms += 251;
    at_poll();
    CHECK(at_start("AT", 2, false, "OK", 100) == RESULT_OK);
    overflow = true;
    at_poll();
    CHECK(at_result() == RESULT_OVERFLOW);
}
/** @brief 以回绕时间和提前按键唤醒验证实际间隔调度。 */
static void test_schedule(void)
{
    UploadSchedule schedule;
    DeviceConfig config = {180, 60, 720, LOCATION_CELL_FIRST};
    upload_schedule_init(&schedule, 0xFFFFFF00u);
    CHECK(!upload_schedule_due(&schedule, &config, false, 0xFFFFFF00u + 600000u));
    CHECK(upload_schedule_due(&schedule, &config, true, 0xFFFFFF00u + 3600000u));
    upload_schedule_complete(&schedule, true, 100);
    CHECK(!upload_schedule_due(&schedule, &config, false, 100 + 600000u));
}
/** @brief 运行真实APP/网络/定位/HTTP至休眠；首次失败仍可继续且缓存可补传。 */
static void test_application(void)
{
    unsigned i;
    mock_reset();
    automatic = true;
    storage[0] = 0x40;
    storage[1] = 0xE2;
    storage[2] = 1;
    storage[3] = 0;
    storage[30] = 0x55;
    storage[40] = 0x44;
    fail_posts = 1;
    application_init();
    for (i = 0; i < 30000 && sleeps < 2; i++)
    {
        clock_ms += 100;
        application_poll();
    }
    CHECK(posts >= 1);
    CHECK(sleeps >= 2);
    CHECK(record_cache_count() == 1);
    for (i = 0; i < 100000 && posts < 3; i++)
    {
        clock_ms += 100;
        application_poll();
    }
    for (i = 0; i < 1000 && record_cache_count(); i++)
    {
        clock_ms += 100;
        application_poll();
    }
    CHECK(posts >= 3);
    CHECK(record_cache_count() == 0);
}
/** @brief 从空白SN到自检服务器批准再到三按激活，未批准前不得进入普通上报。 */
static void test_production_lifecycle(void)
{
    unsigned i;
    uint32_t serial;
    mock_reset();
    automatic = true;
    application_init();
    inject("SN=12bad6\r\n");
    for (i = 0; i < 10; i++)
    {
        clock_ms += 100;
        application_poll();
    }
    CHECK(!identity_store_read(&serial));
    CHECK(posts == 0);
    inject("SN=123456\r\n");
    for (i = 0; i < 10; i++)
    {
        clock_ms += 100;
        application_poll();
    }
    CHECK(identity_store_read(&serial) && serial == 123456);
    CHECK(!identity_store_activated());
    for (i = 0; i < 3; i++)
    {
        short_press = true;
        clock_ms += 100;
        application_poll();
    }
    for (i = 0; i < 10000 && (!identity_store_checked() || modem_supply); i++)
    {
        clock_ms += 100;
        application_poll();
    }
    CHECK(self_posts == 1);
    CHECK(identity_store_checked());
    CHECK(!identity_store_activated());
    CHECK(posts == 0);
    /* 让关机完成后的状态转移稳定，再注入三个有效短按事件。 */
    clock_ms += 100;
    application_poll();
    for (i = 0; i < 3; i++)
    {
        short_press = true;
        clock_ms += 100;
        application_poll();
    }
    CHECK(identity_store_activated());
    for (i = 0; i < 10000 && posts == 0; i++)
    {
        clock_ms += 100;
        application_poll();
    }
    CHECK(posts == 1);
}

/** @brief 俄罗斯两运营商与受限探测失败必须有限结束，并能关闭模块。 */
static void test_russian_network(void)
{
    unsigned i;
    DeviceIdentity identity = {123456, "", ""};
    mock_reset();
    automatic = true;
    is_russia = true;
    at_init();
    CHECK(network_start(&identity) == RESULT_OK);
    for (i = 0; i < 10000 && network_result() == RESULT_PENDING; i++)
    {
        clock_ms += 100;
        at_poll();
        network_poll();
    }
    CHECK(network_result() == RESULT_OK);
    CHECK(network_snapshot()->data_ready);
    CHECK(megafon_attempts >= 1 && tele2_attempts >= 2);
    network_shutdown_start();
    for (i = 0; i < 500 && !network_shutdown_poll(); i++)
    {
        clock_ms += 100;
    }
    CHECK(!modem_supply);
    is_ping_blocked = true;
    at_init();
    CHECK(network_start(&identity) == RESULT_OK);
    for (i = 0; i < 10000 && network_result() == RESULT_PENDING; i++)
    {
        clock_ms += 100;
        at_poll();
        network_poll();
    }
    CHECK(network_result() == RESULT_OK);
    network_shutdown_start();
    for (i = 0; i < 500 && !network_shutdown_poll(); i++)
    {
        clock_ms += 100;
    }
    CHECK(!modem_supply);
}

/** @brief GNSS四次失败必须先尝试关闭，再独立失败或回退到基站。 */
static void test_location_failure_cleanup(void)
{
    unsigned i;
    mock_reset();
    automatic = true;
    has_no_gnss_fix = true;
    at_init();
    CHECK(location_start(LOCATION_GNSS_FIRST, LOCATION_GNSS, 123456) == RESULT_OK);
    for (i = 0; i < 2000 && location_result() == RESULT_PENDING; i++)
    {
        clock_ms += 100;
        at_poll();
        location_poll();
    }
    CHECK(location_result() != RESULT_PENDING && location_result() != RESULT_OK);
    CHECK(gnss_close_count == 1);
    CHECK(location_start(LOCATION_GNSS_FIRST, LOCATION_NONE, 123456) == RESULT_OK);
    for (i = 0; i < 2000 && location_result() == RESULT_PENDING; i++)
    {
        clock_ms += 100;
        at_poll();
        location_poll();
    }
    CHECK(location_result() == RESULT_OK);
    CHECK(location_position()->source == LOCATION_CELL);
    CHECK(gnss_close_count == 2);
}

/** @brief 测试入口由半主机退出，不访问真实设备。 */
int main(void)
{
    mock_reset();
    test_codecs_and_cache();
    test_at();
    CHECK(http_start("http://example.invalid", NULL) == RESULT_OK);
    http_cancel();
    CHECK(http_result() != RESULT_PENDING);
    CHECK(http_start("http://example.invalid", NULL) == RESULT_OK);
    http_cancel();
    test_schedule();
    test_application();
    test_production_lifecycle();
    /* 完成上一业务的关机，避免用例间留在途资源。 */
    network_shutdown_start();
    report_cancel();
    location_cancel();
    sampling_cancel();
    while (!network_shutdown_poll())
    {
        clock_ms += 100;
    }
    test_russian_network();
    test_location_failure_cleanup();
    printf("TESTS checks=%u failures=%u\n", checks, failures);
    test_exit(failures ? 1u : 0u);
    return failures ? 1 : 0;
}
