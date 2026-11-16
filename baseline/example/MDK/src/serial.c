#include "serial.h"
#include "ddl.h"
#include "uart.h"
#include "bt.h"
#include "lpm.h"
#include "gpio.h"
#include "SEGGER_RTT.h"
#include "rtc.h"
#include "time.h"
#include <stdlib.h>
#include "hardware.h"
extern void Network_Record_Confirmed_MCC(uint16_t mcc);
extern uint16_t Network_Get_Last_Confirmed_MCC(void);
#include "base_types.h"

// 绂佺敤鍗婁富鏈猴細
#pragma import(__use_no_semihosting)     //鍏抽棴鍗婁富鏈烘ā寮忥紝鍙渶瑕佸湪浠绘剰涓€涓狢鏂囦欢涓姞鍏ュ嵆鍙€?

#define     T1_PORT                 (0)
#define     T1_PIN                  (3)


uint8_t cyccnt=0;
uint8_t flg=0;
uint16_t loadInterval = 0;
stc_rtc_time_t  stcReadTime;
extern uint8_t loation_type;
extern uint8_t is_move;
extern uint16_t  still_cycle;
extern uint16_t  motion_cycle;
extern uint16_t  continuous_cycle;
extern uint8_t ext_loc_enable;

extern uint16_t  still_cycle_backup;
extern uint16_t  motion_cycle_backup;
extern uint16_t  continuous_cycle_backup;

time_t timestamp_now = 0;
GpsData_t   Gps_Data = {0};
ServingCellData cell_data = {0};
// 鍦ㄧ幇鏈夌粨鏋勪綋闄勮繎娣诲姞
NeighborCellData neighbor_cells[3]; // 鏈€澶у瓨鍌?涓偦鍖?
uint8_t neighbor_count = 0;         // 瀹為檯閭诲尯鏁伴噺
char rx_buffer[BUFFER_SIZE];
uint16_t rx_index = 0;
char post_data[256];
uint8_t g_iccid[21] = {0};
uint8_t g_rssi = 0;
uint8_t g_ber = 0;
// 锟斤拷锟斤拷状态
typedef enum {
    RX_STATE_IDLE,
    RX_STATE_RECEIVING,
    RX_STATE_COMPLETE
} RxState_t;
RxState_t rx_state = RX_STATE_IDLE;



//uint8_t u8RxData[2]={0x55,0x00};
uint8_t volatile u8RxFlg=0;

//static volatile uint8_t frame_ready = FALSE;  //鎺ユ敹鏁版嵁瀹屾垚


//sn鏁版嵁
extern uint8_t sn_data[6];
extern uint32_t eraser_addr;



//#define     T1_PORT                 (3)
//#define     T1_PIN                  (3)


void SystemClock_Init(void) {
    // 锟叫伙拷锟斤拷 HRC锟斤拷4MHz锟斤拷锟斤拷锟斤拷锟斤拷为系统时锟斤拷
//  	Clk_SetRCHFreq(ClkFreq4Mhz);
//    Clk_SwitchTo(ClkRCH);
//	   en_result_t enRet = Ok;

    //閰嶇疆RCH涓哄唴閮?Hz
    Sysctrl_SetRCHTrim(SysctrlRchFreq22_12MHz);

    //鏃堕挓婧愪娇鑳?
    Sysctrl_ClkSourceEnable(SysctrlClkRCH, TRUE);

    //鏃堕挓婧愬垏鎹?
    Sysctrl_SysClkSwitch(SysctrlClkRCH);

    //鍏跺畠鏃堕挓婧愪娇鑳藉叧闂?
    Sysctrl_ClkSourceEnable(SysctrlClkXTH, FALSE);
    Sysctrl_ClkSourceEnable(SysctrlClkRCL, FALSE);
    Sysctrl_ClkSourceEnable(SysctrlClkXTL, FALSE);
    Sysctrl_ClkSourceEnable(SysctrlClkPLL, FALSE);

    //鏃堕挓鍒嗛璁剧疆
    Sysctrl_SetHCLKDiv(SysctrlHclkDiv1);
    Sysctrl_SetPCLKDiv(SysctrlPclkDiv1);

//    return enRet;
			
}


void Uart0_IRQHandler(void)
{
    if(Uart_GetStatus(M0P_UART0, UartRC))         //UART1鏁版嵁鎺ユ敹
    {
        Uart_ClrStatus(M0P_UART0, UartRC);        //娓呬腑鏂姸鎬佷綅
        uint8_t ch = Uart_ReceiveData(M0P_UART0);   //鎺ユ敹鏁版嵁瀛楄妭
				 switch (rx_state) {
        case RX_STATE_IDLE:
            if (ch == '\r' || ch == '\n') {
                // 鍙€夛細澶勭悊绌鸿鎴栦綔涓鸿捣濮嬬
                // rx_buffer[0] = ch;
                // rx_index = 1;
                // rx_state = RX_STATE_RECEIVING;
                // 鎴栧拷鐣ワ紝鏍规嵁鍗忚
            } else if (rx_index < BUFFER_SIZE - 1) {
                rx_buffer[rx_index++] = ch;
                rx_state = RX_STATE_RECEIVING;
            } else {
                Clear_RX_Buffer(); // 缂撳啿鍖烘弧锛屾棤娉曞紑濮嬫柊鏁版嵁
            }
            break;

        case RX_STATE_RECEIVING:
            if (rx_index < BUFFER_SIZE - 1) {
                rx_buffer[rx_index++] = ch;
                if (ch == '\n') {
                    rx_buffer[rx_index] = '\0'; // 瀹夊叏锛屽洜rx_index < BUFFER_SIZE-1
                    rx_state = RX_STATE_COMPLETE;
//                    SEGGER_RTT_printf(0, "Received: %s, Len: %d\n", rx_buffer, rx_index);
                    // 鍙€夛細绔嬪嵆閲嶇疆鐘舵€佹垨绛夊緟澶栭儴澶勭悊
                }
            } else {
                // 澶勭悊婧㈠嚭鍓嶅皾璇曠粨鏉熷瓧绗︿覆
                if (ch == '\n') {
                    rx_buffer[BUFFER_SIZE - 1] = '\0'; // 鎴柇骞剁粨鏉?
                    rx_index = BUFFER_SIZE;
                    rx_state = RX_STATE_COMPLETE;
//                    SEGGER_RTT_printf(0, "Truncated: %s\n", rx_buffer);
                } else {
                    Clear_RX_Buffer(); // 鏁版嵁鏃犳晥锛岄噸缃?
                    // 鍙€夛細璁剧疆婧㈠嚭閿欒鏍囧織
                }
                rx_state = RX_STATE_IDLE;

            }
            break;

        case RX_STATE_COMPLETE:
            // 鑷姩閲嶇疆鎴栫瓑寰呭閮ㄥ鐞?
            // Clear_RX_Buffer();
//            rx_state = RX_STATE_IDLE;
//            frame_ready = TRUE;
            break;
    }
	}


}

void ErrIntCallback(void)
{
  
}

void UartInit(uint32_t baudRate)
{
    stc_gpio_cfg_t stcGpioCfg;

    DDL_ZERO_STRUCT(stcGpioCfg);
	
		stc_uart_cfg_t    stcCfg;

    DDL_ZERO_STRUCT(stcCfg);

    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio,TRUE); //浣胯兘GPIO妯″潡鏃堕挓

    ///<TX
    stcGpioCfg.enDir = GpioDirOut;
    stcGpioCfg.bOutputVal = TRUE;
    Gpio_Init(GpioPortA, GpioPin9, &stcGpioCfg);
    Gpio_SetAfMode(GpioPortA, GpioPin9, GpioAf1);          //閰嶇疆PA09 绔彛涓篣RART1_TX

    ///<RX
    stcGpioCfg.enDir = GpioDirIn;
    Gpio_Init(GpioPortA, GpioPin10, &stcGpioCfg);
    Gpio_SetAfMode(GpioPortA, GpioPin10, GpioAf1);          //閰嶇疆PA10 绔彛涓篣RART1_RX
	

    ///< 寮€鍚璁炬椂閽?
    Sysctrl_SetPeripheralGate(SysctrlPeripheralUart0,TRUE);///<浣胯兘uart1妯″潡鏃堕挓

    ///<UART Init
    ///<UART Init锛堟棤鏍￠獙浣嶇殑姝ｇ‘閰嶇疆锛?
			stcCfg.enRunMode        = UartMskMode1;          ///<妯″紡1锛?浣嶆暟鎹綅+鏃犳牎楠岋紝鏍稿績淇敼锛?
			stcCfg.enStopBit        = UartMsk1bit;           ///<1bit鍋滄浣嶏紙淇濇寔涓嶅彉锛?
			stcCfg.enMmdorCk        = UartMskDataOrAddr;           ///<鏃犳牎楠岋紙鏍稿績淇敼锛屾浛鎹artMskEven锛?
			stcCfg.stcBaud.u32Baud  = 115200;                ///<娉㈢壒鐜囷紙鎸夐渶璋冩暣锛?
			stcCfg.stcBaud.enClkDiv = UartMsk8Or16Div;       ///<閫氶亾閲囨牱鍒嗛閰嶇疆锛堜繚鎸佷笉鍙橈級
			stcCfg.stcBaud.u32Pclk  = Sysctrl_GetPClkFreq(); ///<鑾峰緱澶栬鏃堕挓棰戠巼锛堜繚鎸佷笉鍙橈級
			Uart_Init(M0P_UART0, &stcCfg);                   ///<涓插彛鍒濆鍖?

    ///<UART涓柇浣胯兘
    Uart_ClrStatus(M0P_UART0,UartRC);                ///<娓呮帴鏀惰姹?
    Uart_ClrStatus(M0P_UART0,UartTC);                ///<娓呮帴鏀惰姹?
    Uart_EnableIrq(M0P_UART0,UartRxIrq);             ///<浣胯兘涓插彛鎺ユ敹涓柇
    EnableNvic(UART0_IRQn, IrqLevel3, TRUE);       ///<绯荤粺涓柇浣胯兘
	
	
}
void Rtc_IRQHandler(void)
{
    if (Rtc_GetAlmfItStatus() == TRUE) //闂归搩涓柇
    {
        Rtc_ClearAlmfItStatus();       //娓呬腑鏂爣蹇椾綅
    }
}
                
void Rtc_Init_Frist(void)
{
	
		stc_rtc_initstruct_t RtcInitStruct;
    stc_rtc_alarmtime_t RtcAlmStruct;
		stc_rtc_time_t   RtcTime_now;

    DDL_ZERO_STRUCT(RtcInitStruct);                      //鍙橀噺鍒濆鍊肩疆闆?
    DDL_ZERO_STRUCT(RtcAlmStruct);
		DDL_ZERO_STRUCT(RtcTime_now);

    Sysctrl_SetPeripheralGate(SysctrlPeripheralRtc,TRUE);//RTC妯″潡鏃堕挓鎵撳紑

    Sysctrl_ClkSourceEnable(SysctrlClkXTL, TRUE);        //浣胯兘澶栭儴XTL鏃堕挓浣滀负RTC鏃堕挓

    RtcInitStruct.rtcAmpm = RtcPm;                       //24灏忔椂鍒?
    RtcInitStruct.rtcClksrc = RtcClkXtl;                 //澶栭儴浣庨€熸椂閽?
    RtcInitStruct.rtcPrdsel.rtcPrdsel = RtcPrds;         //鍛ㄦ湡涓柇绫诲瀷PRDS
    RtcInitStruct.rtcPrdsel.rtcPrds = RtcNone;           //涓嶄骇鐢熷懆鏈熶腑鏂?
    RtcInitStruct.rtcTime.u8Second = 0x55;               //閰嶇疆RTC鏃堕棿2019骞?鏈?7鏃?0:01:55
    RtcInitStruct.rtcTime.u8Minute = 0x01;
    RtcInitStruct.rtcTime.u8Hour   = 0x10;
    RtcInitStruct.rtcTime.u8Day    = 0x17;
    RtcInitStruct.rtcTime.u8DayOfWeek = 0x04;
    RtcInitStruct.rtcTime.u8Month  = 0x04;
    RtcInitStruct.rtcTime.u8Year   = 0x19;
    RtcInitStruct.rtcCompen = RtcCompenDisable;           // 浣胯兘鏃堕挓璇樊琛ュ伩
    RtcInitStruct.rtcCompValue = 0;                      //琛ュ伩鍊? 鏍规嵁瀹為檯鎯呭喌杩涜琛ュ伩
    Rtc_Init(&RtcInitStruct);

    RtcAlmStruct.RtcAlarmMinute = 0x02;
    RtcAlmStruct.RtcAlarmHour = 0x12;
    RtcAlmStruct.RtcAlarmWeek = 0x7f;                    //浠庡懆涓€鍒板懆鏃ワ紝姣忓ぉ10:02:00鍚姩涓€娆￠椆閾?
    Rtc_SetAlarmTime(&RtcAlmStruct);                     //閰嶇疆闂归搩鏃堕棿
    Rtc_AlmIeCmd(TRUE);                                  //浣胯兘闂归挓涓柇

    EnableNvic(RTC_IRQn, IrqLevel3, TRUE);               //浣胯兘RTC涓柇鍚戦噺
    Rtc_Cmd(TRUE);                                       //浣胯兘RTC寮€濮嬭鏁?

		Rtc_ReadDateTime(&RtcTime_now);
		SEGGER_RTT_printf(0, "Current time: %02x:%02x:%02x:%02x:%02x:%02x\n", RtcInitStruct.rtcTime.u8Year,RtcInitStruct.rtcTime.u8Month,RtcInitStruct.rtcTime.u8Day,RtcTime_now.u8Hour, RtcTime_now.u8Minute);
}

void Rtc_Alarm_Int_Init(void)
{
 
		stc_rtc_alarmtime_t RtcAlmStruct;
    stc_rtc_time_t stcCurrentTime;
	
		DDL_ZERO_STRUCT(RtcAlmStruct);                      //鍙橀噺鍒濆鍊肩疆闆?
    DDL_ZERO_STRUCT(stcCurrentTime);
	
		

    // 鑾峰彇褰撳墠鏃堕棿
    Rtc_ReadDateTime(&stcCurrentTime);
		SEGGER_RTT_printf(0, "Alarm set for %02x:%02x:%02x\n", stcCurrentTime.u8Hour, stcCurrentTime.u8Minute,stcCurrentTime.u8Day);

    // 灏?BCD 鏍煎紡鐨勬椂闂村瓧娈佃浆鎹负鍗佽繘鍒?
    uint8_t currentHour = (stcCurrentTime.u8Hour >> 4) * 10 + (stcCurrentTime.u8Hour & 0x0F);
    uint8_t currentMinute = (stcCurrentTime.u8Minute >> 4) * 10 + (stcCurrentTime.u8Minute & 0x0F);

//		uint8_t nextMinute = currentMinute + 10;
		uint8_t nextMinute = currentMinute + 10;
		uint8_t nextHour = currentHour;
		//    // 濡傛灉涓嬩竴涓垎閽熺殑鏃堕棿鐐硅秴杩?9鍒嗛挓锛屽垯杩涗綅鍒颁笅涓皬鏃?
    if (nextMinute >= 60) {
        nextHour = (nextHour + 1) % 24;
				nextMinute = nextMinute % 60;
    }
//    // 璁＄畻涓嬩竴涓?鍒嗛挓鐨勬椂闂寸偣
//    uint8_t nextMinute = (currentMinute + 10) % 60;
//    uint8_t nextHour = currentHour;

    // 灏嗗崄杩涘埗鏃堕棿瀛楁杞崲鍥?BCD 鏍煎紡
    RtcAlmStruct.RtcAlarmMinute = ((nextMinute / 10) << 4) | (nextMinute % 10);
    RtcAlmStruct.RtcAlarmHour = ((nextHour / 10) << 4) | (nextHour % 10);
//		stcAlarm.u8Minute = 0x59;
//		stcAlarm.u8Hour = 0x12;
		RtcAlmStruct.RtcAlarmWeek = 0x7F;
		Rtc_Cmd(FALSE);                                       //浣胯兘RTC寮€濮嬭鏁?
    Rtc_ClearAlmfItStatus();                      // 娓呴櫎鏃ч椆閽熸爣蹇楋紙蹇呴』锛侊級
    Rtc_AlmEnCmd(FALSE);              // 涓存椂鍏抽棴闂归挓锛堜慨鏀归厤缃級
    Rtc_SetAlarmTime(&RtcAlmStruct);  // 璁剧疆鏂伴椆閽?
    Rtc_AlmEnCmd(TRUE);               // 閲嶆柊寮€鍚椆閽?
    Rtc_AlmIeCmd(TRUE);               // 寮哄埗寮€鍚椆閽熶腑鏂紙閬垮厤璇叧锛?
		Rtc_Cmd(TRUE); 
		Rtc_StartWait();
		
    SEGGER_RTT_printf(0, "Alarm set for %02x:%02x:%02x\n", RtcAlmStruct.RtcAlarmHour, RtcAlmStruct.RtcAlarmMinute,RtcAlmStruct.RtcAlarmWeek);
//    SEGGER_RTT_printf(0, "alarm start\n");
		delay1ms(1000);
		goto_sleep_mode();		
//    SEGGER_RTT_printf(0, "success锛乗n");
   

		
}
void goto_sleep_mode(void)
{
	 Lpm_GotoDeepSleep(FALSE);
}

void AT_SendCmd(const char *cmd) {
    while (*cmd) {
        Uart_SendDataPollTimeOut(M0P_UART0, *cmd++,0xFF);
//        Uart_SendDataTimeOut(UARTCH1, *cmd++, 100);
    }
    // 锟斤拷锟酵回筹拷锟斤拷锟叫ｏ拷锟斤拷锟斤拷模锟斤拷要锟斤拷锟斤拷锟斤拷锟?
    Uart_SendDataPollTimeOut(M0P_UART0, '\r',0xFF);
//    Uart_SendDataTimeOut(UARTCH1, '\r', 100);
    Uart_SendDataPollTimeOut(M0P_UART0, '\n',0xFF);
//    Uart_SendDataTimeOut(UARTCH1, '\n', 100);
}

// 锟斤拷锟斤拷锟接︼拷欠锟斤拷锟斤拷指锟斤拷锟街凤拷锟斤拷
int Check_Response(const char *str)
{
//		SEGGER_RTT_printf(0, "Response: %s\n", rx_buffer);
    return strstr(rx_buffer, str) != NULL;
		
}

// 锟斤拷战锟斤拷栈锟斤拷锟斤拷锟?
void Clear_RX_Buffer(void)
{
    memset(rx_buffer, 0, BUFFER_SIZE);  // 娓呯┖缂撳啿鍖哄唴瀹?
    rx_index = 0;
    rx_state = RX_STATE_IDLE;
}

// 绛夊緟鍝嶅簲
int Wait_For_Response(const char *expected_response, uint32_t timeout)
{
     uint32_t count = 0;
    while (1)
    {
        if (rx_state == RX_STATE_COMPLETE)
        {
						rx_state = RX_STATE_IDLE;
            // 鍦ㄦ暣涓紦鍐插尯涓悳绱㈢洰鏍囧搷搴旓紙鍖呮嫭鍘嗗彶鏁版嵁锛?
            if (strstr(rx_buffer, expected_response) != NULL)
            {
								
                return 1;  // 鍖归厤鎴愬姛
            }
            else
            {
                // 浠呴噸缃姸鎬佹満锛屼繚鐣欑紦鍐插尯鍐呭
               
            }
        }

        if (count++ > timeout)
        {
            Clear_RX_Buffer();  // 瓒呮椂鍚庢竻绌?
            return 0;           // 瓒呮椂
        }
    }
}
int Wait_For_Easy_Response(const char *expected_response, uint32_t timeout)
{
        uint32_t count = 0;
    while (1) {
			
          if (rx_state == RX_STATE_COMPLETE) {
						rx_state = RX_STATE_IDLE;
            if (Check_Response(expected_response)) {
               
                return 1; // 鎴愬姛鎺ユ敹鍒版湡鏈涚殑鍝嶅簲
            }
            Clear_RX_Buffer();
        }
        if (count++ > timeout) {
            return 0; // 瓒呮椂
        }
    }
}
uint8_t Wait_Module_AT_Ready(uint8_t retry)
{
    uint8_t total = retry;
    uint8_t attempt = 0;

    while (retry--) {
        attempt++;
        Clear_RX_Buffer();
        AT_SendCmd("ATE0");

        if (Wait_For_Easy_Response("OK", TIMEOUT_SHORT)) {
            SEGGER_RTT_printf(0,"[AT_READY] ATE0 OK, try=%d/%d, approx=%d ms\r\n",
                              attempt, total, (uint16_t)((attempt - 1u) * 100u));
            SEGGER_RTT_printf(0,"[AT_READY] pre-lock rx: %s\r\n", rx_buffer);
            return STATUS_OK;
        }

        if ((attempt == 1u) || ((attempt % 10u) == 0u)) {
            SEGGER_RTT_printf(0,"[AT_READY] waiting, try=%d/%d\r\n", attempt, total);
        }
        delay1ms(100);
    }

    SEGGER_RTT_printf(0,"[AT_READY] failed after %d probes\r\n", total);
    Clear_RX_Buffer();
    return STATUS_FAILED;
}

#define TELE2_SIM_READY_RETRY   150u
#define TELE2_SIM_READY_GAP_MS  100u

static uint8_t Wait_SIM_Ready_For_Tele2(void)
{
    uint16_t attempt;

    for (attempt = 1u; attempt <= TELE2_SIM_READY_RETRY; attempt++) {
        Clear_RX_Buffer();
        AT_SendCmd("AT+CPIN?");

        if (Wait_For_Easy_Response("+CPIN: READY", TIMEOUT_SHORT)) {
            SEGGER_RTT_printf(0,"[TELE2] SIM READY, try=%u, approx=%u ms\r\n",
                              attempt, (uint16_t)((attempt - 1u) * TELE2_SIM_READY_GAP_MS));
            return STATUS_OK;
        }

        if ((attempt == 1u) || ((attempt % 10u) == 0u)) {
            SEGGER_RTT_printf(0,"[TELE2] Waiting SIM READY, try=%u/%u\r\n",
                              attempt, TELE2_SIM_READY_RETRY);
        }
        delay1ms(TELE2_SIM_READY_GAP_MS);
    }

    SEGGER_RTT_printf(0,"[TELE2] SIM not ready after %u probes\r\n",
                      TELE2_SIM_READY_RETRY);
    Clear_RX_Buffer();
    return STATUS_FAILED;
}
static int8_t Query_Cereg_Stat(void)
{
    char *p;
    char *comma;

    Clear_RX_Buffer();
    AT_SendCmd("AT+CEREG?");
    if (!Wait_For_Response("+CEREG:", TIMEOUT_SHORT)) {
        return -1;
    }

    p = strstr(rx_buffer, "+CEREG:");
    if (p == NULL) {
        return -1;
    }

    comma = strchr(p, ',');
    if (comma == NULL) {
        return -1;
    }

    return (int8_t)atoi(comma + 1);
}

static uint8_t Query_Serving_Cell_By_Qeng(int16_t *mcc, int16_t *mnc)
{
    *mcc = -1;
    *mnc = -1;

    Clear_RX_Buffer();
    AT_SendCmd("AT+QENG=\"servingcell\"");
    if (Wait_For_Response("+QENG:", TIMEOUT_COUNT)) {
        if (Parse_QENG_Response(rx_buffer, &cell_data) == STATUS_OK) {
            *mcc = (int16_t)cell_data.mcc;
            *mnc = (int16_t)cell_data.mnc;
            SEGGER_RTT_printf(0,"[TELE2] QENG state=%s MCC=%d MNC=%d\r\n",
                              cell_data.connection_status, cell_data.mcc, cell_data.mnc);
            return STATUS_OK;
        }
    }

    SEGGER_RTT_printf(0,"[TELE2] QENG read failed\r\n");
    return STATUS_FAILED;
}


#define TELE2_COPS_TIMEOUT_MS 120000UL
#define TELE2_COPS_POLL_MS    10UL

static uint8_t Lock_Tele2_PLMN(void)
{
    uint32_t elapsed_ms = 0;
    uint16_t logged_len = 0;

    Clear_RX_Buffer();
    AT_SendCmd("AT+COPS=1,2,\"25020\",7");
    SEGGER_RTT_printf(0,"[TELE2] Sent: AT+COPS=1,2,\"25020\",7, timeout=%lu ms\r\n",
                      TELE2_COPS_TIMEOUT_MS);

    while (elapsed_ms < TELE2_COPS_TIMEOUT_MS) {
        if (rx_state == RX_STATE_COMPLETE) {
            rx_state = RX_STATE_IDLE;

            if (rx_index < logged_len) {
                logged_len = 0;
            }
            if (rx_index > logged_len) {
                SEGGER_RTT_printf(0,"[TELE2] COPS RX @%lu ms: %s",
                                  elapsed_ms, &rx_buffer[logged_len]);
                logged_len = rx_index;
            }

            if (strstr(rx_buffer, "+CME ERROR:") != NULL) {
                SEGGER_RTT_printf(0,"[TELE2] COPS rejected with CME ERROR @%lu ms\r\n",
                                  elapsed_ms);
                Clear_RX_Buffer();
                return STATUS_FAILED;
            }
            if (strstr(rx_buffer, "ERROR") != NULL) {
                SEGGER_RTT_printf(0,"[TELE2] COPS rejected with ERROR @%lu ms\r\n",
                                  elapsed_ms);
                Clear_RX_Buffer();
                return STATUS_FAILED;
            }
            if (strstr(rx_buffer, "OK") != NULL) {
                SEGGER_RTT_printf(0,"[TELE2] COPS 25020 OK @%lu ms\r\n", elapsed_ms);
                Clear_RX_Buffer();
                return STATUS_OK;
            }
        }

        delay1ms(TELE2_COPS_POLL_MS);
        elapsed_ms += TELE2_COPS_POLL_MS;

        if ((elapsed_ms % 10000UL) == 0UL) {
            SEGGER_RTT_printf(0,"[TELE2] COPS still waiting, elapsed=%lu ms, rx_len=%u\r\n",
                              elapsed_ms, rx_index);
        }
    }

    SEGGER_RTT_printf(0,"[TELE2] COPS timeout after %lu ms, raw=%s\r\n",
                      elapsed_ms, rx_buffer);
    Clear_RX_Buffer();
    return STATUS_FAILED;
}
static uint8_t Stop_Auto_Network_Before_Tele2(void)
{
    Clear_RX_Buffer();
    AT_SendCmd("AT+COPS=2");
    SEGGER_RTT_printf(0,"[TELE2] Sent AT+COPS=2 to stop automatic registration\r\n");

    if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT)) {
        SEGGER_RTT_printf(0,"[TELE2] COPS=2 OK, modem deregistered\r\n");
        Clear_RX_Buffer();
        return STATUS_OK;
    }

    SEGGER_RTT_printf(0,"[TELE2] COPS=2 failed, raw=%s\r\n", rx_buffer);
    Clear_RX_Buffer();
    return STATUS_FAILED;
}

static void Log_Current_COPS(void)
{
    char *cops;

    Clear_RX_Buffer();
    SEGGER_RTT_printf(0,"[TELE2] Verify selection: AT+COPS?\r\n");
    AT_SendCmd("AT+COPS?");

    if (!Wait_For_Response("+COPS:", TIMEOUT_COUNT)) {
        SEGGER_RTT_printf(0,"[TELE2] Post-lock COPS query failed\r\n");
        Clear_RX_Buffer();
        return;
    }

    SEGGER_RTT_printf(0,"[TELE2] Post-lock COPS: %s\r\n", rx_buffer);
    cops = strstr(rx_buffer, "+COPS:");

    if ((cops != NULL) && (strstr(cops, "25020") != NULL)) {
        SEGGER_RTT_printf(0,"[TELE2] VERIFY: Tele2 25020 is selected\r\n");
    } else if ((cops != NULL) &&
               ((strstr(cops, "+COPS: 1") != NULL) ||
                (strstr(cops, "+COPS:1") != NULL))) {
        SEGGER_RTT_printf(0,"[TELE2] VERIFY: manual mode active, but no operator registered\r\n");
    } else {
        SEGGER_RTT_printf(0,"[TELE2] VERIFY: Tele2 25020 is not selected\r\n");
    }

    Clear_RX_Buffer();
}
void Restore_Auto_Network(void)
{
    Clear_RX_Buffer();
    AT_SendCmd("AT+COPS=0");
    SEGGER_RTT_printf(0,"[TELE2] Sent AT+COPS=0 to restore auto network\r\n");

    if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT)) {
        SEGGER_RTT_printf(0,"[TELE2] Restore auto network OK\r\n");
    } else {
        SEGGER_RTT_printf(0,"[TELE2] Restore auto network failed, raw=%s\r\n", rx_buffer);
    }
    Clear_RX_Buffer();
}


Tele2StartupResult_t Try_Register_Tele2_First(void)
{
    int8_t cereg_stat;
    int16_t mcc;
    int16_t mnc;
    uint16_t last_mcc;
    uint8_t lock_status;

    if (Wait_SIM_Ready_For_Tele2() != STATUS_OK) {
        SEGGER_RTT_printf(0,"[TELE2] SIM unavailable; country unknown\r\n");
        return TELE2_STARTUP_UNKNOWN;
    }

    /* Stop automatic registration immediately when SIM becomes ready. */
    if (Stop_Auto_Network_Before_Tele2() != STATUS_OK) {
        SEGGER_RTT_printf(0,"[TELE2] Cannot stop automatic registration\r\n");
        return TELE2_STARTUP_UNKNOWN;
    }

    /* The first permitted registration attempt is always Tele2 25020. */
    lock_status = Lock_Tele2_PLMN();
    Log_Current_COPS();

    cereg_stat = Query_Cereg_Stat();
    SEGGER_RTT_printf(0,"[TELE2] After first 25020 attempt: CEREG=%d lock=%d\r\n",
                      cereg_stat, lock_status);

    /* QENG can expose MCC in LIMSRV without successful registration. */
    if (Query_Serving_Cell_By_Qeng(&mcc, &mnc) == STATUS_OK) {
        if (mcc == 250) {
            Network_Record_Confirmed_MCC(250u);
            SEGGER_RTT_printf(0,"[TELE2] Russia confirmed after 25020-first attempt, MNC=%d stat=%d\r\n",
                              mnc, cereg_stat);
            return TELE2_STARTUP_RUSSIA;
        }

        Network_Record_Confirmed_MCC((uint16_t)mcc);
        SEGGER_RTT_printf(0,"[TELE2] Non-Russia MCC=%d confirmed; restore automatic selection\r\n",
                          mcc);
        Restore_Auto_Network();
        return TELE2_STARTUP_OTHER;
    }

    /* No current cell: never treat UNKNOWN as non-Russia. */
    last_mcc = Network_Get_Last_Confirmed_MCC();
    if (last_mcc == 250u) {
        SEGGER_RTT_printf(0,"[TELE2] No serving cell; use last confirmed Russia state\r\n");
        return TELE2_STARTUP_RUSSIA;
    }
    if (last_mcc != 0u) {
        SEGGER_RTT_printf(0,"[TELE2] No serving cell; use last confirmed non-Russia MCC=%u\r\n",
                          last_mcc);
        Restore_Auto_Network();
        return TELE2_STARTUP_OTHER;
    }

    SEGGER_RTT_printf(0,"[TELE2] No serving cell and no country history; remain deregistered\r\n");
    return TELE2_STARTUP_UNKNOWN;
}
uint8_t Check_SIM_Ready(void)
{


				Clear_RX_Buffer();
				AT_SendCmd("AT+CPIN?");
				if (Wait_For_Easy_Response("+CPIN: READY", TIMEOUT_SHORT))
        {
            // 锟斤拷锟斤拷锟缴癸拷锟斤拷应
            SEGGER_RTT_printf(0,"1\n");
        }
        else
        {
            // 锟斤拷锟斤拷失锟斤拷锟斤拷应
             SEGGER_RTT_printf(0,"3!\n");
							return STATUS_FAILED;
        }	
//				AT_SendCmd("AT+CREG?");
				delay1ms(50);
				AT_SendCmd("AT+CREG?");
				if (Wait_For_Response("+CREG:", TIMEOUT_SHORT) &&
                    (strstr(rx_buffer, "+CREG: 0,1") != NULL ||
                     strstr(rx_buffer, "+CREG: 0,5") != NULL))
        {

            SEGGER_RTT_printf(0,"reg is ok!\n");
        }
        else
        {
							 SEGGER_RTT_printf(0, " bf %s!\n",rx_buffer);
							return STATUS_FAILED;

//            SEGGER_RTT_printf(0,"reg is ok IS failed!\n");
        }
				AT_SendCmd("AT+QICSGP=1,1,\"sxzcat1\",\"\",\"\"");
//				AT_SendCmd("AT+QICSGP=1,1,\"m2m.mts.ru\",\"\",\"\"");
				 AT_SendCmd("AT+CGDCONT?");
    if (Wait_For_Response("+CGDCONT:", TIMEOUT_SHORT)) {
//        SEGGER_RTT_printf(0, "Current PDP context: %s\n", rx_buffer);
    } else {
//        SEGGER_RTT_printf(0, "Failed to get current PDP context\n");
        return STATUS_FAILED;
    }

//    // 鍘绘縺娲?PDP 涓婁笅鏂囷紙濡傛灉宸叉縺娲伙級
//    AT_SendCmd("AT+CGACT=0,1");
//    if (Wait_For_Easy_Response("OK", TIMEOUT_SHORT)) {
//        SEGGER_RTT_printf(0, "PDP context deactivated\n");
//    } else {
//        SEGGER_RTT_printf(0, "Failed to deactivate PDP context\n");
//        return STATUS_FAILED;
//    }

//    // 淇敼 APN
//    char cmd[50];
//    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\",\"\",\"\"", "scuiot");
//    AT_SendCmd(cmd);
//    if (Wait_For_Easy_Response("OK", TIMEOUT_SHORT)) {
//        SEGGER_RTT_printf(0, "APN changed to %s\n", "scuiot");
//    } else {
//        SEGGER_RTT_printf(0, "Failed to change APN\n");
//        return STATUS_FAILED;
//    }

//    // 閲嶆柊婵€娲?PDP 涓婁笅鏂?
//    AT_SendCmd("AT+CGACT=1,1");
//    if (Wait_For_Easy_Response("OK", TIMEOUT_SHORT)) {
//        SEGGER_RTT_printf(0, "PDP context reactivated\n");
//        return STATUS_OK;
//    } else {
//        SEGGER_RTT_printf(0, "Failed to reactivate PDP context\n");
//        return STATUS_FAILED;
//				AT_SendCmd("AT+CGDCONT=1,\"IP\",\"scuiot\",\"\",\"\"");
//				 if (Wait_For_Easy_Response("OK", TIMEOUT_SHORT))
//        {
//            // 锟斤拷锟斤拷锟缴癸拷锟斤拷应
//            SEGGER_RTT_printf(0,"2\n");
//        }
//        else
//        {
//            // 锟斤拷锟斤拷失锟斤拷锟斤拷应
//             SEGGER_RTT_printf(0,"AT command failed!\n");
//							return STATUS_FAILED;
//        }	
//		}
				delay1ms(50);

//				delay1ms(100);
				return STATUS_OK;
}





void close_echo(void)
{
	AT_SendCmd("ATE0");
	if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT))
	{
			SEGGER_RTT_printf(0,"echo closed\r\n");
	}
}



// ... 鍏朵粬浠ｇ爜 ...

static int Parse_Response(const char *response, GpsData_t *gps) {
    const char *p = response;
    const char *end = p + strlen(response);
    int field_count = 0;

    // 澶勭悊鎸囦护澶?
    if ((p = strstr(p, "+QGPSLOC:"))) {
        p += 10; // 璺宠繃鎸囦护澶?
    } else {
//        SEGGER_RTT_printf(0, "ERR: Missing +QGPSLOC: header\n");
        return STATUS_FAILED;
    }

    while (p < end && field_count < 11) {
        const char *comma = strchr(p, ',');
				delay100us(20);
        size_t field_len = comma ? (size_t)(comma - p) : strlen(p);
				delay100us(20);	
        // 澶勭悊绌哄瓧娈?
        if (field_len == 0) {
            p++;
            field_count++;
            continue;
        }


        // 瀹屾暣瀛楁瑙ｆ瀽
        switch (field_count) {
            case 0:  // UTC鏃堕棿锛?41640.00
                gps->time = atof(p);
                SEGGER_RTT_printf(0, "UTC:%.2f\n", gps->time);
                break;

            case 1:  // 绾害锛?1.36298
                gps->latitude = atof(p);
                SEGGER_RTT_printf(0, "Lat:%.5f\n", gps->latitude);
                break;

            case 2:  // 缁忓害锛?21.53123
                gps->longitude = atof(p);
                SEGGER_RTT_printf(0, "Lon:%.5f\n", gps->longitude);
                break;

            case 3:  // HDOP锛?.00
                gps->hdop = atof(p);
                SEGGER_RTT_printf(0, "HDOP:%.2f\n", gps->hdop);
                break;

            case 4:  // 娴锋嫈锛?4.7
                gps->altitude = atof(p);
                SEGGER_RTT_printf(0, "Alt:%.1f\n", gps->altitude);
                break;

            case 5:  // 瀹氫綅鐘舵€侊細0
                gps->status = (uint8_t)atoi(p);
                SEGGER_RTT_printf(0, "Fix:%d\n", gps->status);
                break;

            case 6:  // 鏂瑰悜锛堢┖瀛楁锛?
                // 淇濈暀瀛楁锛岀ず渚嬪搷搴斾腑涓虹┖
							
                break;

            case 7:  // 鍦伴潰閫熷害锛坘m/h锛夛細0.000
                gps->speed_km = atof(p);
                SEGGER_RTT_printf(0, "SpeedKM:%.3f\n", gps->speed_km);
                break;

            case 8:  // 鍦伴潰閫熷害锛堟捣閲?灏忔椂锛夛細0.000
                gps->speed_kn = atof(p);
                SEGGER_RTT_printf(0, "SpeedKN:%.3f\n", gps->speed_kn);
                break;

            case 9:  // 鏃ユ湡锛?70325
                strncpy(gps->date, p, MIN(field_len, sizeof(gps->date)-1));
                gps->date[MIN(field_len, sizeof(gps->date)-1)] = '\0';
                SEGGER_RTT_printf(0, "Date:%s\n", gps->date);
                break;

            case 10: // 鍗槦鏁伴噺锛?6
                gps->nsat = (uint8_t)atoi(p);
                SEGGER_RTT_printf(0, "Sat:%d\n", gps->nsat);
                break;
        }

        // 绉诲姩鎸囬拡鍒颁笅涓€涓瓧娈?
        p = comma ? comma + 1 : end;
        field_count++;
    }

    // 瀛楁鏁伴噺鏍￠獙
    if (field_count != 11) {
//        SEGGER_RTT_printf(0, "ERR: Field count %d/11\n", field_count);
        return STATUS_FAILED;
    }

    // 绾害鏈夋晥鎬ф牎楠?
    if (gps->latitude < -90.0f || gps->latitude > 90.0f) {
//        SEGGER_RTT_printf(0, "ERR: Lat %f\n", gps->latitude);
        return STATUS_FAILED;
    }

    return STATUS_OK;
}


// ... 鍏朵粬浠ｇ爜 ...



static uint8_t Check_AGPS_Status(void)
{
    AT_SendCmd("AT+QAGPS?");
    if (Wait_For_Response("+QAGPS: 1", TIMEOUT_SHORT)) {
        SEGGER_RTT_printf(0, "AGPS already OPEN OK!\n");
        return STATUS_OK;
    } else {
//        SEGGER_RTT_printf(0, "response is %s!\n", rx_buffer);
        Clear_RX_Buffer();
        AT_SendCmd("AT+QAGPS=1");
        if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT)) {
            SEGGER_RTT_printf(0, "AGPS OPEN OK!\n");
						AT_SendCmd("AT+QGPSCFG=\"urc\",0x4");
						if (Wait_For_Response("OK", TIMEOUT_SHORT)) {
//								SEGGER_RTT_printf(0, "QGPSCFG already OPEN OK!\n");
								return STATUS_OK;
						} else {
										return STATUS_FAILED;
								
						}
        } else {
//            SEGGER_RTT_printf(0, "AGPS OPEN failed!\n");
            return STATUS_FAILED;
        }
    }

}

static uint8_t Check_GPS_Status(void)
{
    AT_SendCmd("AT+QGPS?");
    if (Wait_For_Response("+QGPS: 1", TIMEOUT_SHORT)) {
        SEGGER_RTT_printf(0, "GPS already OPEN OK!\n");
        return STATUS_OK;
    } else {
//        SEGGER_RTT_printf(0, "response is %s!\n", rx_buffer);
        Clear_RX_Buffer();
        AT_SendCmd("AT+QGPS=1");
        if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT)) {
            SEGGER_RTT_printf(0, "GPS OPEN OK!\n");
            return STATUS_OK;
        } else {
            SEGGER_RTT_printf(0, "GPS OPEN failed!\n");
            return STATUS_FAILED;
        }
    }
}
static uint8_t GPS_Close(void)
{

				AT_SendCmd("AT+QGPSEND");
        if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT)) {
            SEGGER_RTT_printf(0, "GPS CLOSE OK!\n");
            return STATUS_OK;
        } else {
            SEGGER_RTT_printf(0, "GPS CLOSE failed!\n");
            return STATUS_FAILED;
        }
}
static uint8_t AGPS_Close(void)
{
    AT_SendCmd("AT+QAGPS=0");
    if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT)) {
        SEGGER_RTT_printf(0, "AGPS CLOSE OK!\n");
        return STATUS_OK;
    } else {
        SEGGER_RTT_printf(0, "AGPS CLOSE failed!\n");
        return STATUS_FAILED;
    }
}


 //浼樺寲鍚庣殑瀹氫綅鑾峰彇鍑芥暟
static uint8_t Get_GPS_Location(uint32_t timeout) {
		delay1ms(timeout);
    AT_SendCmd("AT+QGPSLOC=2");
    if (Wait_For_Response("+QGPSLOC:", TIMEOUT_COUNT)) {
  
             SEGGER_RTT_printf(0, "Wait_For_Response: %s\n", rx_buffer);   
            // 瑙ｆ瀽鏁版嵁
            int parse_result = Parse_Response(rx_buffer, &Gps_Data);

            if (parse_result == STATUS_OK) {
							 Clear_RX_Buffer();
							 AGPS_Close();
							 GPS_Close();
							return STATUS_OK;
            } else {
//                SEGGER_RTT_printf(0, "[ERROR] Parse failed\n");
            }
        } 
    else {
				delay1ms(50);
//				if (GPS_Close() != STATUS_OK) {
//					SEGGER_RTT_printf(0, "GPS closed\n");
//					return STATUS_FAILED;
//				}
//				delay1ms(50);
//				SEGGER_RTT_printf(0, "Checking GPS status...\n");
//				if (Check_GPS_Status() != STATUS_OK) {
//					SEGGER_RTT_printf(0, "GPS check failed\n");
//					return STATUS_FAILED;
//				}	
//        SEGGER_RTT_printf(0, "[ERROR] Response timeout\n");
    }

    Clear_RX_Buffer();
    return STATUS_FAILED;
}
uint8_t Get_Gnss_Message(void) {
		loation_type = 1; 
		GNSS_Config_Beidou_GPS_WithCheck();
		delay1ms(50);
    SEGGER_RTT_printf(0, "Checking AGPS status...\n");
    if (Check_AGPS_Status() != STATUS_OK) {
        SEGGER_RTT_printf(0, "AGPS check failed\n");
//        return STATUS_FAILED;
    }

    SEGGER_RTT_printf(0, "Checking GPS status...\n");
    if (Check_GPS_Status() != STATUS_OK) {
        SEGGER_RTT_printf(0, "GPS check failed\n");
        return STATUS_FAILED;
    }

    uint8_t retry;  // 灏?retry 鍙橀噺澹版槑绉诲埌澶栭儴

    // 鑾峰彇 GPS 瀹氫綅淇℃伅锛屽鍔犻噸璇曟満鍒?
    for (retry = 0; retry < MAX_RETRIES; retry++) {
        SEGGER_RTT_printf(0, "Attempt %d to get GPS location...\n", retry + 1);

        // 寤舵椂 1000 姣
        delay1ms(1000);

        if (Get_GPS_Location(retry == 0 ? INITIAL_TIMEOUT : RETRY_TIMEOUT) == STATUS_OK) {
            SEGGER_RTT_printf(0, "Successfully got GPS location\n");    
            return STATUS_OK;
        }

        // ... 閲嶈瘯閫昏緫淇濇寔涓嶅彉 ...
    }

    SEGGER_RTT_printf(0, "All retries failed to get GPS location\n");
    return STATUS_FAILED;
}


time_t ConvertToTimestamp(void) {
    struct tm time_info = {0};
    char *time_str;
    stc_rtc_time_t rtc_time;
    uint8_t ntp_success = 0;
    // 灏濊瘯鑾峰彇NTP鏃堕棿
    Clear_RX_Buffer();
    AT_SendCmd("AT+QNTP=1,\"ntp.aliyun.com\",123");
    if (Wait_For_Easy_Response("+QNTP: ", TIMEOUT_COUNT)) {
        SEGGER_RTT_printf(0, "time is %s  \r\n", rx_buffer);

        // 瑙ｆ瀽NTP鏃堕棿
        time_str = strstr(rx_buffer, "+QNTP: 0,\"") + 10;
        if (time_str == NULL || strlen(time_str) < 19) {
            SEGGER_RTT_printf(0, "Invalid NTP time format\n");
            goto USE_RTC_TIME;
        }

        // 瑙ｆ瀽鏃堕棿瀛楁
        time_info.tm_year = (time_str[0]-'0')*1000 + (time_str[1]-'0')*100 + 
                          (time_str[2]-'0')*10 + (time_str[3]-'0') - 1900;
        time_info.tm_mon  = (time_str[5]-'0')*10 + (time_str[6]-'0') - 1;
        time_info.tm_mday = (time_str[8]-'0')*10 + (time_str[9]-'0');
        time_info.tm_hour = (time_str[11]-'0')*10 + (time_str[12]-'0'); // UTC+8
        time_info.tm_min  = (time_str[14]-'0')*10 + (time_str[15]-'0');
        time_info.tm_sec  = (time_str[17]-'0')*10 + (time_str[18]-'0');

        // 鏃堕棿鏈夋晥鎬ф牎楠?
        if(time_info.tm_mon >= 12 || time_info.tm_mday > 31 || 
           time_info.tm_hour >= 24 || time_info.tm_min >= 60 || 
           time_info.tm_sec >= 60) {
//            SEGGER_RTT_printf(0, "Invalid NTP time values\n");
            goto USE_RTC_TIME;
        }

        // 鏇存柊RTC鏃堕棿
				//        Rtc_DisableFunc(RtcCount);   //涓嶉渶瑕?
        
        // 鍗佽繘鍒惰浆BCD鏍煎紡
				int year_last_two = (time_info.tm_year + 1900) % 100;
				int week = ZellerWeek(time_info.tm_year + 1900,time_info.tm_mon + 1,time_info.tm_mday);
        rtc_time.u8Year   = ((year_last_two / 10) << 4) | (year_last_two % 10);
        rtc_time.u8Month  = ((time_info.tm_mon + 1)/10)<<4 | ((time_info.tm_mon + 1)%10);
        rtc_time.u8Day    = (time_info.tm_mday/10)<<4 | (time_info.tm_mday%10);
        rtc_time.u8Hour   = (time_info.tm_hour/10)<<4 | (time_info.tm_hour%10);
        rtc_time.u8Minute = (time_info.tm_min/10)<<4 | (time_info.tm_min%10);
        rtc_time.u8Second = (time_info.tm_sec/10)<<4 | (time_info.tm_sec%10);
        rtc_time.u8DayOfWeek = week;
        Rtc_SetTime(&rtc_time);
				ntp_success = 1;
//        Rtc_EnableFunc(RtcCount);
        
//        SEGGER_RTT_printf(0, "RTC updated: %04d-%02d-%02d %02d:%02d:%02d\n",
//                        time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday,
//                        time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
				//娴嬭瘯鏃堕棿寮€濮?
//				 Rtc_ReadDateTime(&rtc_time);
//        
//        // BCD杞崄杩涘埗
//        time_info.tm_year = (rtc_time.u8Year >> 4)*10 + (rtc_time.u8Year & 0xF) + 2000 -1900;
//        time_info.tm_mon  = (rtc_time.u8Month >> 4)*10 + (rtc_time.u8Month & 0xF) - 1;
//        time_info.tm_mday = (rtc_time.u8Day >> 4)*10 + (rtc_time.u8Day & 0xF);
//        time_info.tm_hour = (rtc_time.u8Hour >> 4)*10 + (rtc_time.u8Hour & 0xF);
//        time_info.tm_min  = (rtc_time.u8Minute >> 4)*10 + (rtc_time.u8Minute & 0xF);
//        time_info.tm_sec  = (rtc_time.u8Second >> 4)*10 + (rtc_time.u8Second & 0xF);
//				 SEGGER_RTT_printf(0, "Using RTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
//                        time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday,
//                        time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
//				time_t timestamp = mktime(&time_info);
//				SEGGER_RTT_printf(0,"timemap = %ld\n",timestamp);
				//娴嬭瘯鏃堕棿缁撴潫
    } else {
        USE_RTC_TIME:
        // 鑾峰彇RTC鏃堕棿
        Rtc_ReadDateTime(&rtc_time);
        
        // BCD杞崄杩涘埗
        time_info.tm_year = (rtc_time.u8Year >> 4)*10 + (rtc_time.u8Year & 0xF) + 2000 -1900;
        time_info.tm_mon  = (rtc_time.u8Month >> 4)*10 + (rtc_time.u8Month & 0xF) - 1;
        time_info.tm_mday = (rtc_time.u8Day >> 4)*10 + (rtc_time.u8Day & 0xF);
        time_info.tm_hour = (rtc_time.u8Hour >> 4)*10 + (rtc_time.u8Hour & 0xF);
        time_info.tm_min  = (rtc_time.u8Minute >> 4)*10 + (rtc_time.u8Minute & 0xF);
        time_info.tm_sec  = (rtc_time.u8Second >> 4)*10 + (rtc_time.u8Second & 0xF);
				
//        SEGGER_RTT_printf(0, "Using RTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
//                        time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday,
//                        time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
    }

    // 鐢熸垚鏃堕棿鎴?
    time_t timestamp = mktime(&time_info);
    if (timestamp == -1) {
//        SEGGER_RTT_printf(0, "Failed to convert to timestamp\n");
        return STATUS_FAILED;
    }
    
    SEGGER_RTT_printf(0, "Final timestamp: %ld\n", timestamp);
    return timestamp;
}



uint8_t Format_Http_Post_Data(GpsData_t *gps, char *buffer, size_t buffer_size, size_t *data_length, float batt, time_t timestamp) 
{
//    SEGGER_RTT_printf(0, "Entering Format_Http_Post_Data\n");
		uint32_t id = 0;
		eraser_addr = ERASE_ADDR_IS;
		// 璇诲彇4瀛楄妭灏忕鏁版嵁锛堢ず渚嬶細07 B2 01 00 -> 0x0001B207 = 111111锛?
        volatile uint32_t *p_flash = (volatile uint32_t*)eraser_addr;
        id = *p_flash;  // 鐩存帴璇诲彇32浣嶅皬绔暟鎹?
    // const char *url_template = "id=%d&timestamp=%ld&lat=%.6f&lon=%.6f&speed=%.1f&altitude=%.1f&acceleration=%.1f&batt=%.2f&temperature=%.1f&type=%d";
    const char *url_template =  "{"
        "\"id\":\"%d\","
        "\"timestamp\":\"%ld\","
        "\"lat\":\"%.6f\","
        "\"lon\":\"%.6f\","
        "\"speed\":\"%.1f\","
        "\"altitude\":\"%.1f\","
        "\"acceleration\":\"%.1f\","
        "\"batt\":\"%.3f\","
        "\"temperature\":\"%.1f\","
        "\"type\":\"%d\","
        "\"isMove\":\"%d\","
				"\"rssi\":\"%d,%d\","
				"\"iccid\":\"%s\""
        "}";
//    SEGGER_RTT_printf(0, "Before ConvertToTimestamp\n");
    // timestamp_now = ConvertToTimestamp();
//    SEGGER_RTT_printf(0, "After ConvertToTimestamp\n");

    if (timestamp == STATUS_FAILED) {
        SEGGER_RTT_printf(0, "[ERROR] Invalid timestamp\n");
        return STATUS_FAILED;
    }

     
		
    SEGGER_RTT_printf(0, "Before snprintf\n");
    int len = snprintf(buffer, buffer_size, url_template,
						 id,	
             timestamp,
             gps->latitude,
             gps->longitude,
             gps->speed_km,
             gps->altitude,
             gps->bearing,  // 浣跨敤缁撴瀯浣撲腑鐨勬柟鍚戝€?
             battery,         // 浠庡弬鏁拌幏鍙栫數姹犵數閲?
             tem_res,
            loation_type,
            is_move,
						g_rssi,
						cell_data.rsrp,
						g_iccid);        // 绮惧害鍙傛暟鏆傛椂淇濈暀纭紪鐮?
    SEGGER_RTT_printf(0, "After snprintf\n");

    if (len < 0 || (size_t)len >= buffer_size) {
        SEGGER_RTT_printf(0, "[ERROR] Buffer overflow (%d/%zu)\n", len, buffer_size);
        return STATUS_FAILED;
    }
//		memset(&Gps_Data,0,sizeof(Gps_Data));
    *data_length = (size_t)len;
    SEGGER_RTT_printf(0, "Exiting Format_Http_Post_Data\n");
    return STATUS_OK;
}

uint8_t Send_Http_URL(void)
{
		Clear_RX_Buffer();
     char url[] = "http://182.92.135.10:5055/receData/position";
    char at_cmd[64];
    
    // 鍔ㄦ€佺敓鎴怉T鎸囦护
    snprintf(at_cmd, sizeof(at_cmd), "AT+QHTTPURL=%d,30", (int)strlen(url));
    AT_SendCmd(at_cmd);
    
    if (Wait_For_Easy_Response("CONNECT", TIMEOUT_COUNT)) {
        SEGGER_RTT_printf(0, "http connect!\n");
        AT_SendCmd(url);   
        if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT))
        {
            SEGGER_RTT_printf(0, "connect ok!\n");
            return STATUS_OK;
        }
        else
        {
            SEGGER_RTT_printf(0, "connect is failed!\n");
            return STATUS_FAILED;
        }
    }
    else
    {
        SEGGER_RTT_printf(0, "connect is failed!\n");
        return STATUS_FAILED;
    }
}

uint8_t Send_Http_Data(size_t data_len)
{
    char cmd[50];
    snprintf(cmd, sizeof(cmd), "AT+QHTTPPOST=%zu,60,20", data_len);
//    SEGGER_RTT_printf(0, "cmd is %s\n", cmd);
		Clear_RX_Buffer();
    AT_SendCmd(cmd);
//			AT_SendCmd("AT+QHTTPPOST=135,60,20");
    if (Wait_For_Response("CONNECT", HTTP_TIMEOUT_COUNT))
    {
        SEGGER_RTT_printf(0, "Connect ok!\n");
        // 鍙戦€佸疄闄呯殑 POST 鏁版嵁
					Clear_RX_Buffer();
				SEGGER_RTT_printf(0, "post_data is %s \n",post_data);
        AT_SendCmd(post_data);
        if (Wait_For_Response("+QHTTPPOST: 0,200", HTTP_TIMEOUT_COUNT))
        {
            SEGGER_RTT_printf(0, "Data sent successfully!\n");
            return STATUS_OK;
        }
        else
        {
            SEGGER_RTT_printf(0, "Data send failed!\n");
            return STATUS_FAILED;
        }
    }
    else
    {
        SEGGER_RTT_printf(0, "Connect failed!\n");
        return STATUS_FAILED;
    }
}










uint8_t Http_post(time_t use_timestamp) 
{
   
    size_t data_len = 0;
		timestamp_now = use_timestamp;
    if (Send_Http_URL() != STATUS_OK)
    {
        return STATUS_FAILED;
    }

    delay1ms(100);
    if (Format_Http_Post_Data(&Gps_Data, post_data, sizeof(post_data), &data_len, battery,timestamp_now) != STATUS_OK)
    {
        return STATUS_FAILED;
    }

//    SEGGER_RTT_printf(0, "post_data = %s\n", post_data);  // 淇鏍煎紡瀛楃涓?
//    SEGGER_RTT_printf(0, "data_len = %zu\n", data_len);  // 鎵撳嵃鏁版嵁闀垮害

    if (Send_Http_Data(data_len) != STATUS_OK)
    {
        SEGGER_RTT_printf(0, "post failed\n");
        return STATUS_FAILED;
    }
    SEGGER_RTT_printf(0, "bk 1\n");
    delay1ms(100);
    if (Read_Http_Response() != STATUS_OK)
    {
			

        SEGGER_RTT_printf(0, "bk 4\n");
        still_cycle = still_cycle_backup;
        motion_cycle = motion_cycle_backup;
        continuous_cycle = continuous_cycle_backup;
        return STATUS_FAILED;
    }

    return STATUS_OK;
}

uint8_t Read_Http_Response(void)
{
    Clear_RX_Buffer();
    AT_SendCmd("AT+QHTTPREAD=256");
    if (Wait_For_Response("{", HTTP_TIMEOUT_COUNT)) 
    {

        return parse_upload_intervals(rx_buffer);
//        SEGGER_RTT_printf(0, "[ERROR] JSON format error\n");
//        SEGGER_RTT_printf(0, "Raw buffer:\n%s\n", rx_buffer);
    }
    return STATUS_FAILED;
}


uint8_t parse_upload_intervals(const char *response) {
    uint8_t parsed_fields = 0;
    const char* target_fields[] = {
        "uploadInterval", 
        "moveUploadInterval",
        "continuesUploadInterval",
        "locateType"
    };
		SEGGER_RTT_printf(0, "Raw JSON: %s\n", response);
    for (uint8_t i = 0; i < 4; i++) {
        const char *key = target_fields[i];
        const char *key_pos = strstr(response, key);  // 澶у皬鍐欎笉鏁忔劅鏌ユ壘

        if (!key_pos) {
            SEGGER_RTT_printf(0, "[ERROR] Missing field: %s\n", key);
            continue;
        }

        // 璺宠繃閿悕骞跺畾浣嶅埌鍐掑彿鍚?
        const char *value_start = strchr(key_pos, ':');
        if (!value_start) {
            SEGGER_RTT_printf(0, "[ERROR] Invalid format for field: %s\n", key);
            continue;
        }
        value_start++;  // 绉诲姩鍒板啋鍙峰悗

        // 璺宠繃鍓嶅绌烘牸鍜屽紩鍙?
        while (*value_start == ' ' || *value_start == '"') {
            value_start++;
        }

        // 鎻愬彇鏁板€奸儴鍒?
        char num_str[16] = {0};
        int j = 0;
        while (*value_start != ',' && *value_start != '}' && *value_start != '"' && j < 15) {
            num_str[j++] = *value_start++;
        }

        // 杞崲涓烘暟鍊?
        long value = strtol(num_str, NULL, 10);

        // 鏍规嵁瀛楁绱㈠紩璧嬪€?
        switch (i) {
            case 0:
                still_cycle = (uint16_t)value;
                parsed_fields |= 0x01;
                break;
            case 1:
                motion_cycle = (uint16_t)value;
                parsed_fields |= 0x02;
                break;
            case 2:
                continuous_cycle = (uint16_t)value;
                parsed_fields |= 0x04;
                break;
            case 3:
                ext_loc_enable = (uint8_t)value;
                parsed_fields |= 0x08;
                break;
        }
				continuous_cycle_backup = continuous_cycle;
				still_cycle_backup = still_cycle;
				motion_cycle_backup = motion_cycle;
				

        SEGGER_RTT_printf(0, "Parsed %s: %s\n", key, num_str);
    }

    SEGGER_RTT_printf(0, "Final parsed fields mask: 0x%02X\n", parsed_fields);
    return (parsed_fields == 0x0F) ? STATUS_OK : STATUS_FAILED;
}

uint8_t get_loca_data(void)
{
	 loation_type = 2;
	 Clear_RX_Buffer();
	 AT_SendCmd("AT+QENG=\"servingcell\"");
    if (Wait_For_Response("+QENG:", TIMEOUT_COUNT))
    {
//				SEGGER_RTT_printf(0, "Received valid QENG response: %s\n", rx_buffer);
        if(Parse_Serving_Cell_Info() == STATUS_OK)
				{
					if (Parse_QENG_Response(rx_buffer, &cell_data) == STATUS_OK) {
//						Print_Serving_Cell_Data(&cell_data);
						Clear_RX_Buffer();
						return  STATUS_OK;
            // 鎵撳嵃鎴栦娇鐢?cell_data 涓殑鏁版嵁
					} else {
							Clear_RX_Buffer();
//							SEGGER_RTT_printf(0, "Failed to parse QENG data.\n");
							return  STATUS_FAILED;
						//娌℃湁鑾峰彇鍒伴偦鍖哄熀绔欙紝涓嶈繑鍥炲け璐ワ紝缁х画鎵ц
					}
				}
				else
				{
							Clear_RX_Buffer();
//							SEGGER_RTT_printf(0, "Failed to Parse_Serving_Cell_Info\n");
							return  STATUS_FAILED;
							
				}
			
    }
		else
		{
							Clear_RX_Buffer();
//							SEGGER_RTT_printf(0, "Failed to Parse_Serving_Cell_Info\n");
							return  STATUS_FAILED;
		}
}

uint8_t Parse_Serving_Cell_Info(void) {
    const char *keyword = "+QENG: \"servingcell\"";
    char *start = strstr(rx_buffer, keyword);
    if (start == NULL) {
//        SEGGER_RTT_printf(0, "鏃犳硶鎵惧埌 servingcell 鏁版嵁\n");
        return STATUS_FAILED;
    }
		SEGGER_RTT_printf(0, "found servingcell data\n");
    return STATUS_OK;
}


int Parse_QENG_Response(const char *response, ServingCellData *cell_data) {
    const char *p = response;
    int field_count = 0;

    // 璺宠繃澶撮儴瀛楁
    if ((p = strstr(p, "+QENG:"))) {
        p += 6; // 璺宠繃鎸囦护澶?
    } else {
//        SEGGER_RTT_printf(0, "[ERROR] Missing +QENG prefix\n");
        return STATUS_FAILED;
    }

    // 瀹屾暣瀛楁瑙ｆ瀽
    while (*p && field_count < 18) {  // 鎵╁睍涓?8涓瓧娈?
        const char *comma = strchr(p, ',');
        size_t field_len = comma ? (size_t)(comma - p) : strlen(p);

        // 澶勭悊绌哄瓧娈?
        if (field_len == 0) {
            p++;
            field_count++;
            continue;
        }

        // 瀛楁鏄犲皠琛?
        switch (field_count) {
            case 0:  // "servingcell" 宸茶烦杩?
                break;
                
            case 1:  // 杩炴帴鐘舵€?
                strncpy(cell_data->connection_status, p, 
                        MIN(field_len, sizeof(cell_data->connection_status)-1));
                cell_data->connection_status[MIN(field_len, sizeof(cell_data->connection_status)-1)] = '\0';
                break;

            case 2:  // 缃戠粶鍒跺紡
                strncpy(cell_data->network_type, p,
                        MIN(field_len, sizeof(cell_data->network_type)-1));
                cell_data->network_type[MIN(field_len, sizeof(cell_data->network_type)-1)] = '\0';
                break;

            case 3:  // 鍙屽伐妯″紡
                strncpy(cell_data->fdd_tdd, p,
                        MIN(field_len, sizeof(cell_data->fdd_tdd)-1));
                cell_data->fdd_tdd[MIN(field_len, sizeof(cell_data->fdd_tdd)-1)] = '\0';
                break;

            case 4:  // MCC
                cell_data->mcc = atoi(p);
                break;

            case 5:  // MNC
                cell_data->mnc = atoi(p);
                break;

            case 6:  // 灏忓尯ID (eCID)
                strncpy(cell_data->cell_id, p,
                        MIN(field_len, sizeof(cell_data->cell_id)-1));
                cell_data->cell_id[MIN(field_len, sizeof(cell_data->cell_id)-1)] = '\0';
                break;

            case 7:  // 棰戠偣鍙?
                cell_data->earfcn = atoi(p);
                break;

            case 8:  // PCI
                cell_data->pci = atoi(p);
                break;

            case 9:  // 棰戞
                cell_data->band = atoi(p);
                break;

            case 10: // 涓婅甯﹀
                cell_data->ul_bandwidth = atoi(p);
                break;

            case 11: // 涓嬭甯﹀
                cell_data->dl_bandwidth = atoi(p);
                break;

            case 12: // TAC
                strncpy(cell_data->tac, p,
                        MIN(field_len, sizeof(cell_data->tac)-1));
                cell_data->tac[MIN(field_len, sizeof(cell_data->tac)-1)] = '\0';
                break;

            case 13: // RSRP
                cell_data->rsrp = atoi(p);
                break;

            case 14: // RSRQ
                cell_data->rsrq = atoi(p);
                break;

            case 15: // RSSI
                cell_data->rssi = atoi(p);
                break;

            case 16: // SINR
                cell_data->sinr = atoi(p);
                break;

            case 17: // 淇″彿寮哄害
                cell_data->srxlev = atoi(p);
                break;
        }

        // 绉诲姩鍒颁笅涓€涓瓧娈?
        p = comma ? comma + 1 : p + field_len;
        field_count++;
    }

    // 瀛楁鏁伴噺鏍￠獙
    if (field_count != 18) {
//        SEGGER_RTT_printf(0, "[ERROR] ziduanshucuowu: %d/18\n", field_count);
        return STATUS_FAILED;
    }

    return STATUS_OK;
}

uint8_t Build_GPS_CW_Get_URL(char *url, size_t url_size) {
		
			uint32_t id = 0;
		eraser_addr = ERASE_ADDR_IS;
		// 璇诲彇4瀛楄妭灏忕鏁版嵁锛堢ず渚嬶細07 B2 01 00 -> 0x0001B207 = 111111锛?
        volatile uint32_t *p_flash = (volatile uint32_t*)eraser_addr;
        id = *p_flash;  // 鐩存帴璇诲彇32浣嶅皬绔暟鎹?
    SEGGER_RTT_printf(0, "Before Constructed URL\n");
//		const char *url_template = "http://47.108.145.137:3000/api/cw?x=%X-%02X-%s-%s-%X&p=1&mt=0";
//	const char *url_template = "http://gps.qianxingtu.cn/api/cw?x=%X-%02X-%s-%s-%X&p=1&mt=0";
		const char *url_template = "http://gps.qianxingtu.cn/api/cw?x=%X-%02X-%s-%s-%X-%s-%s-%X-%s-%s-%X-%s-%s-%X&p=1&mt=0&id=%d";
    // 鍙€夛細鏍规嵁鍗忚瑕佹眰鍐冲畾鏄惁浣跨敤 rsrp 鎴?rssi
//    int signal = cell_data.rsrp;  // 鍘熷鍊硷紝渚嬪 -119
    int signal = cell_data.rssi +110; // 宸茬粡鏄?rsrp + 110 鐨勬鍊?
		if(neighbor_cells[0].rsrp < 0)
		{
			neighbor_cells[0].rsrp+=110;
		}
		if(neighbor_cells[1].rsrp < 0)
		{
			neighbor_cells[1].rsrp+=110;
		}
			if(neighbor_cells[2].rsrp < 0)
		{
			neighbor_cells[2].rsrp+=110;
		}
    int len = snprintf(url, url_size,
        url_template,
        cell_data.mcc,
        cell_data.mnc,
        cell_data.tac,     // lac1
        cell_data.cell_id,
        signal,              // signal1
				neighbor_cells[0].lac,
				neighbor_cells[0].cell_id,
				neighbor_cells[0].rsrp,
				neighbor_cells[1].lac,
				neighbor_cells[1].cell_id,
				neighbor_cells[1].rsrp,
				neighbor_cells[2].lac,
				neighbor_cells[2].cell_id,
				neighbor_cells[2].rsrp,
				id
		
				
    );
		memset(neighbor_cells,0,sizeof(neighbor_cells));	
    if (len < 0 || len >= url_size) {
//        SEGGER_RTT_printf(0, "[ERROR] URL buffer overflow\n");
        return STATUS_FAILED;
    }

//    SEGGER_RTT_printf(0, "Constructed URL: %s\n", url);
    return STATUS_OK;
}

// 鏍规嵁瀹為檯閭诲尯鏁伴噺鍔ㄦ€佹瀯寤篣RL妯℃澘
//uint8_t Build_GPS_CW_Get_URL(char *url, size_t url_size) {
//    SEGGER_RTT_printf(0, "Before Constructed URL\n");
//    
//    // 鏍规嵁瀹為檯閭诲尯鏁伴噺閫夋嫨涓嶅悓鐨刄RL妯℃澘
//    const char *url_templates[] = {
//        "http://gps.qianxingtu.cn/api/cw?x=%X-%02X-%s-%s-%X&p=1&mt=0",          // 鏃犻偦鍖?
//        "http://gps.qianxingtu.cn/api/cw?x=%X-%02X-%s-%s-%X-%s-%s-%X&p=1&mt=0",  // 1涓偦鍖?
//        "http://gps.qianxingtu.cn/api/cw?x=%X-%02X-%s-%s-%X-%s-%s-%X-%s-%s-%X&p=1&mt=0"  // 2涓偦鍖?
//    };
//    
//    // 閫夋嫨鍚堥€傜殑妯℃澘锛堟渶澶氫娇鐢?涓偦鍖烘暟鎹級
//    int template_idx = (neighbor_count > 2) ? 2 : neighbor_count;
//    int signal = cell_data.rssi + 110; // 宸茶皟鏁寸殑淇″彿鍊?
//    
//    int len;
//    // 鏍规嵁閭诲尯鏁伴噺鐢熸垚涓嶅悓鐨刄RL
//    switch(template_idx) {
//        case 0: // 鏃犻偦鍖?
//            len = snprintf(url, url_size, url_templates[0],
//                cell_data.mcc, cell_data.mnc, cell_data.tac, cell_data.cell_id, signal);
//            break;
//        case 1: // 1涓偦鍖?
//            len = snprintf(url, url_size, url_templates[1],
//                cell_data.mcc, cell_data.mnc, cell_data.tac, cell_data.cell_id, signal,
//                neighbor_cells[0].lac, neighbor_cells[0].cell_id, neighbor_cells[0].rsrp);
//            break;
//        case 2: // 2涓偦鍖?
//            len = snprintf(url, url_size, url_templates[2],
//                cell_data.mcc, cell_data.mnc, cell_data.tac, cell_data.cell_id, signal,
//                neighbor_cells[0].lac, neighbor_cells[0].cell_id, neighbor_cells[0].rsrp,
//                neighbor_cells[1].lac, neighbor_cells[1].cell_id, neighbor_cells[1].rsrp);
//            break;
//        default:
//            len = -1;
//            break;
//    }
//    
//    if (len < 0 || len >= url_size) {
////        SEGGER_RTT_printf(0, "[ERROR] URL buffer overflow\n");
//        return STATUS_FAILED;
//    }

//    SEGGER_RTT_printf(0, "Constructed URL: %s\n", url);
//    return STATUS_OK;
//}

uint8_t Send_Http_Get_Request(const char *url) {
    Clear_RX_Buffer();
    // 鍔ㄦ€佽绠桿RL闀垮害
    char cmd[30];
    snprintf(cmd, sizeof(cmd), "AT+QHTTPURL=%d,80", (int)strlen(url)); // 淇敼杩欓噷
    AT_SendCmd(cmd);
    if (Wait_For_Easy_Response("CONNECT", HTTP_TIMEOUT_COUNT)) {
        SEGGER_RTT_printf(0, "HTTP connect OK\n");
        AT_SendCmd(url);
				if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT))
			 {
//					  SEGGER_RTT_printf(0, "url send OK\n");
						Clear_RX_Buffer();
						AT_SendCmd("AT+QHTTPGET=80");
					 if (Wait_For_Easy_Response("+QHTTPGET: 0,200", HTTP_TIMEOUT_COUNT))
					 {
									if (Read_Http_get_Response() == STATUS_OK) {
									SEGGER_RTT_printf(0, "HTTP response received\n");
									return STATUS_OK;
							} else {
//									SEGGER_RTT_printf(0, "Failed to read HTTP response\n");
									return STATUS_FAILED;
							}
					 } 
					 else
					 {
//							SEGGER_RTT_printf(0, "Failed to http get\n");
							return STATUS_FAILED;
					 }
			 }
		 }
		 else
		 {
//				SEGGER_RTT_printf(0, "Failed to send URL\n");
				return STATUS_FAILED;
		 }
		 return STATUS_OK;
		  
}
uint8_t get_cw_gps_data(void)
{
	  char cw_url[200] = {0};
//		SEGGER_RTT_printf(0, "before to build CW URL\n");
    // 鏋勯€?URL
    if (Build_GPS_CW_Get_URL(cw_url, sizeof(cw_url)) != STATUS_OK) {
//        SEGGER_RTT_printf(0, "Failed to build CW URL\n");
        return STATUS_FAILED;
    }
//			 SEGGER_RTT_printf(0, "Constructed URL: %s\n", cw_url);
    // 鍙戦€?HTTP GET 璇锋眰
    if (Send_Http_Get_Request(cw_url) == STATUS_OK) {
//        SEGGER_RTT_printf(0, "GPS CW data sent successfully\n");
				return STATUS_OK;
    } else {
//        SEGGER_RTT_printf(0, "Failed to send GPS CW data\n");
				return STATUS_FAILED;
    }
}

// 鍦ㄦ枃浠跺悎閫傜殑浣嶇疆娣诲姞瑙ｆ瀽鍑芥暟
int Parse_Location_Data(const char *json, GpsData_t* gps_data) {
    const char *lat_start = strstr(json, "\"lat\":");
    const char *lon_start = strstr(json, "\"lon\":");

    if (!lat_start || !lon_start) {
//        SEGGER_RTT_printf(0, "ERR: Missing location fields\n");
        return STATUS_FAILED;
    }

    // 淇1锛氭纭В鏋愬埌 latitude/longitude 瀛楁
    if (sscanf(lat_start + 6, "%lf", &gps_data->latitude) != 1) { // 浣跨敤 %f 瑙ｆ瀽 float
//        SEGGER_RTT_printf(0, "ERR: Latitude format error\n");
        return STATUS_FAILED;
    }

    if (sscanf(lon_start + 6, "%lf", &gps_data->longitude) != 1) {
//        SEGGER_RTT_printf(0, "ERR: Longitude format error\n");
        return STATUS_FAILED;
    }

    // 淇2锛氶獙璇佹纭殑瀛楁
    if (gps_data->latitude < -90.0f || gps_data->latitude > 90.0f || 
        gps_data->longitude < -180.0f || gps_data->longitude > 180.0f) {
//        SEGGER_RTT_printf(0, "ERR: Invalid coordinates (lat:%.6f, lon:%.6f)\n", 
//                        gps_data->latitude, gps_data->longitude);
        return STATUS_FAILED;
    }
		 if (gps_data->latitude == 0 || gps_data->longitude == 0) {
//        SEGGER_RTT_printf(0, "ERR: Invalid coordinates (lat:%.6f, lon:%.6f)\n", 
//                        gps_data->latitude, gps_data->longitude);
        return STATUS_FAILED;
    }

    // 淇3锛氳緭鍑烘纭殑瀛楁
    SEGGER_RTT_printf(0, "Parsed coordinates: Lat=%.6f, Lon=%.6f\n", 
                    gps_data->latitude, gps_data->longitude);
    return STATUS_OK;
}

uint8_t Read_Http_get_Response(void)
{
    AT_SendCmd("AT+QHTTPREAD=200");
    if (Wait_For_Response("+QHTTPREAD:", TIMEOUT_COUNT))
    {
        SEGGER_RTT_printf(0, "Response: %s\n", rx_buffer);
				if(Parse_Location_Data(rx_buffer,&Gps_Data) != STATUS_OK)
				{
					Clear_RX_Buffer();
					return STATUS_FAILED;
				}	
				Clear_RX_Buffer();
        return STATUS_OK;
    }
    else
    {
        Clear_RX_Buffer();
//        SEGGER_RTT_printf(0, "Failed to read response!\n");
        return STATUS_FAILED;
    }
}
uint8_t get_neighbourcell_data(void)
{
		Clear_RX_Buffer();
		AT_SendCmd("AT+QCELLINFO?");
		delay1ms(2000);
    if (Wait_For_Response("OK", HTTP_TIMEOUT_COUNT))
    {
        if(Parse_QCELLINFO_Response(rx_buffer) == STATUS_OK)
				{
					
				}
				else
				{
							Clear_RX_Buffer();
//							SEGGER_RTT_printf(0, "Failed to get neighbourcell data \n");
							return  STATUS_FAILED;
							
				}
			
    }
		else
		{
							Clear_RX_Buffer();
//							SEGGER_RTT_printf(0, "Failed to get data QCELLINFO\n");
							return  STATUS_FAILED;
		}
}
	
	





int Parse_QCELLINFO_Response(const char *response) {
    const char *p = response;
    neighbor_count = 0;
		memset(neighbor_cells, 0, sizeof(neighbor_cells));  // 娓呯┖閭诲尯鏁版嵁鏁扮粍
    SEGGER_RTT_printf(0, "Raw QCELLINFO: %s\n", response);

    while ((p = strstr(p, "+QCELLINFO:"))) {
        p += 11; // 璺宠繃 "+QCELLINFO:" 澶?

        // 浠呭鐞嗛偦鍖烘暟鎹?
        const char *cell_type = strstr(p, "\"neighbourcell\"");
        if (!cell_type) {
            p = strchr(p, '\n'); // 闈為偦鍖烘暟鎹紝璺宠繃褰撳墠琛?
            continue;
        }

        // 瀹氫綅鍒?LTE"鐨勪綅缃紙淇锛氬寘鍚紩鍙峰拰閫楀彿锛?
        const char *lte_pos = strstr(cell_type, "\"LTE\"");
        if (!lte_pos) {
            p = strchr(p, '\n');
            continue;
        }
        p = lte_pos + 6; // 绉诲姩鍒?LTE", 涔嬪悗锛堜緥濡傦細"LTE", -> 璺宠繃6瀛楃锛?LTE","锛?

        // 璋冩暣瀛楁浣嶇疆鍖归厤瀹為檯鏁版嵁
        char lac[8] = {0};
        char cell_id[16] = {0};
        int rsrp;

        // 鏍煎紡璇存槑锛氳烦杩嘙CC(460)銆丮NC(00)锛屾彁鍙朙AC銆丆ELLID锛岃烦杩嘐ARFCN(205)銆丳CI(28)銆佹湭鐭?-)銆?300锛屾彁鍙朢SRP(-103)
        int fields = sscanf(p, 
            "%*d,%*d,%7[^,],%15[^,],%*d,%*d,%*[^,],%*d,%d",  // 淇锛氫粠"LTE",鍚庣殑瀛楁寮€濮嬪尮閰?
            lac, cell_id, &rsrp);

        if (fields == 3) {
            // 楠岃瘉RSRP鑼冨洿锛?150~-40涓烘湁鏁堬級
            if (rsrp >= -150 && rsrp <= -40) {
                strncpy(neighbor_cells[neighbor_count].lac, lac, sizeof(neighbor_cells[neighbor_count].lac)-1);
                strncpy(neighbor_cells[neighbor_count].cell_id, cell_id, sizeof(neighbor_cells[neighbor_count].cell_id)-1);
                neighbor_cells[neighbor_count].rsrp = rsrp;
                
                SEGGER_RTT_printf(0, "Neighbor %d: LAC=%s, CELLID=%s, RSRP=%ddBm\n",
                                neighbor_count + 1, lac, cell_id, rsrp);
                
                if (neighbor_count < 4) neighbor_count++;
            }
        } else {
            // 鎵撳嵃鏇磋缁嗙殑澶辫触淇℃伅锛堟樉绀哄墠50瀛楃锛?
//            SEGGER_RTT_printf(0, "[WARN] Parse failed at: %.50s\n", p);
        }

        p = strchr(p, '\n'); // 绉诲姩鍒颁笅涓€琛?
    }

    return (neighbor_count > 0) ? STATUS_OK : STATUS_FAILED;
}

uint8_t ZellerWeek(uint16_t year, uint8_t month, uint8_t day) 
{
    if (month < 3) {
        month += 12;  // 1鏈?2鏈堣涓哄墠涓€骞寸殑13/14鏈?
        year--;
    }
    
    uint8_t m = month;
    uint16_t y = year % 100;  // 骞翠唤鍚庝袱浣?
    uint16_t c = year / 100;  // 涓栫邯鏁?
    
    // 钄″嫆鍏紡鏍稿績璁＄畻
    uint8_t week = (day + 13*(m+1)/5 + y + y/4 + c/4 - 2*c) % 7;
    
    // 澶勭悊璐熸暟锛?7淇濊瘉缁撴灉鍦?-6
    if (week > 6) week += 7; 
    return week % 7;
}

uint8_t Get_ICCID(uint8_t *iccid_buffer, uint8_t buffer_size)
{
    uint8_t retry;

    if ((iccid_buffer == NULL) || (buffer_size == 0)) {
        return STATUS_FAILED;
    }
    iccid_buffer[0] = '\0';

    for (retry = 0; retry < 5; retry++) {
        char *qccid_start;
        uint8_t out_idx = 0;

        Clear_RX_Buffer();
        SEGGER_RTT_printf(0, "[ICCID] try %d\r\n", retry + 1);
        AT_SendCmd("AT+QCCID");

        if (!Wait_For_Response("+QCCID:", TIMEOUT_COUNT)) {
            SEGGER_RTT_printf(0, "[ICCID] timeout, rx=%s\r\n", rx_buffer);
            delay1ms(500);
            continue;
        }

        qccid_start = strstr(rx_buffer, "+QCCID:");
        if (qccid_start == NULL) {
            delay1ms(500);
            continue;
        }

        qccid_start = strchr(qccid_start, ':');
        if (qccid_start == NULL) {
            delay1ms(500);
            continue;
        }
        qccid_start++;

        while ((*qccid_start != '\0') && (out_idx < (buffer_size - 1))) {
            if ((*qccid_start >= '0') && (*qccid_start <= '9')) {
                iccid_buffer[out_idx++] = (uint8_t)(*qccid_start);
            } else if ((out_idx > 0) && ((*qccid_start == '\r') || (*qccid_start == '\n'))) {
                break;
            }
            qccid_start++;
        }
        iccid_buffer[out_idx] = '\0';

        if (out_idx >= 18) {
            SEGGER_RTT_printf(0, "ICCID: %s\r\n", iccid_buffer);
            Clear_RX_Buffer();
            return STATUS_OK;
        }

        SEGGER_RTT_printf(0, "[ICCID] invalid len %d, rx=%s\r\n", out_idx, rx_buffer);
        delay1ms(500);
    }

    SEGGER_RTT_printf(0, "[ICCID] failed\r\n");
    Clear_RX_Buffer();
    return STATUS_FAILED;
}

/**
 * @brief  瑙ｆ瀽AT+CSQ鍝嶅簲锛堥€傞厤鏍囧噯鏍煎紡锛歕r\n+CSQ: 31,99\r\n\r\nOK\r\n锛?
 * @param  rssi: 杈撳嚭淇″彿寮哄害锛?~31=鏈夋晥锛?9=鏃犱俊鍙?鏈煡锛?
 * @param  ber: 杈撳嚭璇爜鐜囷紙閫氬父涓?9锛屾棤瀹為檯鎰忎箟锛?
 * @retval STATUS_OK-瑙ｆ瀽鎴愬姛锛孲TATUS_FAILED-瑙ｆ瀽澶辫触
 */
uint8_t Get_CSQ(uint8_t *rssi, uint8_t *ber)
{
    // 1. 鍙傛暟鍚堟硶鎬ф鏌?
    if (rssi == NULL || ber == NULL) {
        return STATUS_FAILED;
    }

    // 2. 娓呯┖鎺ユ敹缂撳啿鍖猴紝鍙戦€丄T+CSQ鎸囦护
    memset(rx_buffer, 0, sizeof(rx_buffer));
    AT_SendCmd("AT+CSQ");  // 澶嶇敤浣犲師鏈夌殑AT鎸囦护鍙戦€佸嚱鏁?

    // 3. 绛夊緟骞跺尮閰?+CSQ:"鍝嶅簲鍓嶇紑
    if (!Wait_For_Easy_Response("+CSQ:", TIMEOUT_SHORT)) {
        return STATUS_FAILED;
    }

    // 4. 瀹氫綅鍓嶇紑璧峰浣嶇疆锛岃烦杩囧啑浣欏瓧绗?
    char *p = strstr(rx_buffer, "+CSQ:");
    if (p == NULL) {
        return STATUS_FAILED;
    }
    p += 5;  // 璺宠繃"+CSQ: "锛?涓瓧绗︼級
    
    // 璺宠繃鎵€鏈夌┖鐧界锛圽r\n/绌烘牸锛夛紝瀹氫綅鍒版暟瀛楀紑澶?
    while (*p == '\r' || *p == '\n' || *p == ' ') {
        p++;
    }

    // 5. 瑙ｆ瀽淇″彿寮哄害锛圧SSI锛?
    *rssi = (uint8_t)atoi(p);  // 鑷姩蹇界暐鍚庣画闈炴暟瀛楀瓧绗?
		SEGGER_RTT_printf(0, "rssi: %d\n", *rssi);
    // 6. 瀹氫綅閫楀彿锛岃В鏋愯鐮佺巼锛圔ER锛?
    char *comma = strchr(p, ',');
    if (comma == NULL) {
        return STATUS_FAILED;
    }
    comma++;  // 璺宠繃閫楀彿
    while (*comma == '\r' || *comma == '\n' || *comma == ' ') {
        comma++;
    }
    *ber = (uint8_t)atoi(comma);
		SEGGER_RTT_printf(0, "ber: %d\n", *ber);

    return STATUS_OK;
}

uint8_t GNSS_Config_Beidou_GPS_WithCheck(void)
{
    // -------------------------- 姝ラ1锛氶厤缃寳鏂桸MEA绫诲瀷涓? --------------------------
    // 1.1 鍙戦€侀厤缃寚浠?
    AT_SendCmd("AT+QGPSCFG=\"beidounmeatype\",1");
    if (!Wait_For_Easy_Response("OK", TIMEOUT_SHORT)) {
        return STATUS_FAILED;
    }

    // 1.2 璇诲彇褰撳墠閰嶇疆鍊煎苟鏍￠獙
    memset(rx_buffer, 0, BUFFER_SIZE);
    AT_SendCmd("AT+QGPSCFG=\"beidounmeatype\"");
    if (Wait_For_Easy_Response("+QGPSCFG:", TIMEOUT_SHORT)) {
        char *p = strstr(rx_buffer, "+QGPSCFG: \"beidounmeatype\",");
        if (p == NULL || atoi(p + 24) != 1) {  // 24鏄墠缂€闀垮害锛屽彇鍚庨潰鐨勬暟鍊?
            return STATUS_FAILED;  // 璇诲彇鍊尖墵1锛岄厤缃湭鐢熸晥
        }
    } else {
        return STATUS_FAILED;  // 鏈敹鍒伴厤缃繑鍥?
    }

    // -------------------------- 姝ラ2锛氶厤缃瓽NSS绯荤粺涓? --------------------------
    // 2.1 鍙戦€侀厤缃寚浠?
    AT_SendCmd("AT+QGPSCFG=\"gnssconfig\",5");
    if (!Wait_For_Easy_Response("OK", TIMEOUT_SHORT)) {
        return STATUS_FAILED;
    }

    // 2.2 璇诲彇褰撳墠閰嶇疆鍊煎苟鏍￠獙
    memset(rx_buffer, 0, BUFFER_SIZE);
    AT_SendCmd("AT+QGPSCFG=\"gnssconfig\"");
    if (Wait_For_Easy_Response("+QGPSCFG:", TIMEOUT_SHORT)) {
        char *p = strstr(rx_buffer, "+QGPSCFG: \"gnssconfig\",");
        if (p == NULL || atoi(p + 20) != 5) {  // 20鏄墠缂€闀垮害锛屽彇鍚庨潰鐨勬暟鍊?
            return STATUS_FAILED;  // 璇诲彇鍊尖墵5锛岄厤缃湭鐢熸晥
        }
    } else {
        return STATUS_FAILED;  // 鏈敹鍒伴厤缃繑鍥?
    }
		SEGGER_RTT_printf(0, "GNSS_Config_Beidou_GPS_WithCheck success");

    // 鎵€鏈夐厤缃?鏍￠獙閮芥垚鍔?
    return STATUS_OK;
}

