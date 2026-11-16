/******************************************************************************
* Copyright (C) 2017, Xiaohua Semiconductor Co.,Ltd All rights reserved.
*
* This software is owned and published by:
* Xiaohua Semiconductor Co.,Ltd ("XHSC").
*
* BY DOWNLOADING, INSTALLING OR USING THIS SOFTWARE, YOU AGREE TO BE BOUND
* BY ALL THE TERMS AND CONDITIONS OF THIS AGREEMENT.
*
* This software contains source code for use with XHSC
* components. This software is licensed by XHSC to be adapted only
* for use in systems utilizing XHSC components. XHSC shall not be
* responsible for misuse or illegal use of this software for devices not
* supported herein. XHSC is providing this software "AS IS" and will
* not be responsible for issues arising from incorrect user implementation
* of the software.
*
* Disclaimer:
* XHSC MAKES NO WARRANTY, EXPRESS OR IMPLIED, ARISING BY LAW OR OTHERWISE,
* REGARDING THE SOFTWARE (INCLUDING ANY ACOOMPANYING WRITTEN MATERIALS),
* ITS PERFORMANCE OR SUITABILITY FOR YOUR INTENDED USE, INCLUDING,
* WITHOUT LIMITATION, THE IMPLIED WARRANTY OF MERCHANTABILITY, THE IMPLIED
* WARRANTY OF FITNESS FOR A PARTICULAR PURPOSE OR USE, AND THE IMPLIED
* WARRANTY OF NONINFRINGEMENT.
* XHSC SHALL HAVE NO LIABILITY (WHETHER IN CONTRACT, WARRANTY, TORT,
* NEGLIGENCE OR OTHERWISE) FOR ANY DAMAGES WHATSOEVER (INCLUDING, WITHOUT
* LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION,
* LOSS OF BUSINESS INFORMATION, OR OTHER PECUNIARY LOSS) ARISING FROM USE OR
* INABILITY TO USE THE SOFTWARE, INCLUDING, WITHOUT LIMITATION, ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL OR CONSEQUENTIAL DAMAGES OR LOSS OF DATA,
* SAVINGS OR PROFITS,
* EVEN IF Disclaimer HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* YOU ASSUME ALL RESPONSIBILITIES FOR SELECTION OF THE SOFTWARE TO ACHIEVE YOUR
* INTENDED RESULTS, AND FOR THE INSTALLATION OF, USE OF, AND RESULTS OBTAINED
* FROM, THE SOFTWARE.
*
* This software may be replicated in part or whole for the licensed use,
* with the restriction that this Disclaimer and Copyright notice must be
* included with each copy of this software, whether used in part or whole,
* at all times.
*/
/******************************************************************************/
/** \file main.c
 **
 ** A detailed description is available at
 ** @link Sample Group Some description @endlink
 **
 **   - 2017-05-17  1.0  cj First version for Device Driver Library of Module.
 **
 ******************************************************************************/

/******************************************************************************
 * Include files
 ******************************************************************************/
#include "ddl.h"
#include "uart.h"
#include "bt.h"
#include "lpm.h"
#include "gpio.h"

#include "serial.h"
#include "SEGGER_RTT.h"
#include "hardware.h"

#include "rtc.h"
#include "flash.h"
#include <stdlib.h>
#include <string.h>
#include "network_strategy.h"

/******************************************************************************
 * Local pre-processor symbols/macros ('#define')                            
 ******************************************************************************/

/******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/

/******************************************************************************
 * Local type definitions ('typedef')                                         
 ******************************************************************************/

/******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/

/******************************************************************************
 * Local variable definitions ('static')                                      *
 ******************************************************************************/

/******************************************************************************
 * Local pre-processor symbols/macros ('#define')                             
 ******************************************************************************/

/*****************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/**
 ******************************************************************************
 ** \brief  Main function of project
 **
 ** \return uint32_t return value, if needed
 **
 ** This sample
 **
 ******************************************************************************/
#define POWER_ON_PORT       GpioPortB
#define POWER_ON_PIN        GpioPin11

#define STOP_CNT 18
#define RUN_CNT 6

 
 // 鍦ㄥ叏灞€鍙橀噺鍖哄煙娣诲姞
#define CACHE_SIZE 20  // 鏈€澶у瓨鍌?0鏉＄绾挎暟鎹?
#define DEBUG_MODE 1

// 鎿﹂櫎瀹屾垚鏍囪鍊硷紙鑷畾涔夛紝姣斿0xAA锛岄伩鍏嶅拰姝ｅ父鏁版嵁鍐茬獊锛?
#define FLASH_ERASE_FLAG_VAL      0xAA 
typedef struct {
    float latitude;
    float longitude;
    time_t timestamp;
		uint8_t move_status;
		float tem_backup;
		uint8_t location_type;
} GpsCacheItem;

typedef struct {
    GpsCacheItem items[CACHE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} GpsCache;

GpsCache g_gps_cache;
 
void FlashInit(void);
uint8_t flash_write_byte(uint32_t u32Addr,uint8_t u8Data,uint8_t len);
uint8_t flash_lock(void);
void sn_set(void);
void disable_key_irq(void);
void sport_dectet(void);
void key_dectet(void);
en_result_t BtTimerTest(void);
void BtTimerT_deinit(void);
uint8_t Cache_Get(GpsCacheItem *item);
uint8_t Cache_Put(const GpsCacheItem *item);
void Cache_Init(void);
void http_send_data(void);
void start_devices(void);
uint8_t Get_IMEI(char *imeiBuffer);
uint8_t Http_post_self_check(time_t use_timestamp);
uint8_t Send_Http_URL_self_check(void);
uint8_t Read_Http_Response_self_check(void);
void led_init(void);
uint8_t self_check(void);
void key_int1_init(void);
uint8_t Format_Http_Post_Data_self_check(GpsData_t *gps, char *buffer, size_t buffer_size, size_t *data_length, float batt, time_t timestamp) ;
uint8_t Set_Http_Content_Type(uint8_t u8Type);
uint8_t Parse_Server_Status(const char *response);
void key_int_deinit(void);
void led_start(void);
void led_stop(void);
void adc_io_init(void);

	 
 
stc_rtc_time_t  stcTime1;
extern uint8_t running_flag;
uint8_t get_gnss_flag = 0;
uint8_t loation_type = 0;  //瀹氫綅绫诲瀷 1-鍩虹珯 2-GNSS

uint8_t ext_loc_enable = 2;    // 瀹氫綅鍋忓ソ 2-鍩虹珯浼樺厛 1-GNSS浼樺厛   


uint16_t  still_cycle_backup = 10;
uint16_t  motion_cycle_backup = 10;
uint16_t  continuous_cycle_backup = 10;   //杩愬姩闂撮殧鐨勫浠芥暟鎹?

extern time_t timestamp_now;
extern GpsData_t   Gps_Data;

//flash read write
uint32_t eraser_addr = ERASE_ADDR_IS;
static volatile uint32_t u32FlashTestFlag   = 0;
uint8_t sn_data[6] = {0};

//sn flag
uint8_t get_sn_data = 0;
uint8_t sn_read_flag = 0;
uint8_t sn_set_flag = 0; //纭sn鏄惁鍐欏叆鎴愬姛
uint8_t sn_flash_set_ok = 0x33;
uint8_t activation_data = 0x44;
uint8_t activation_flag = 0x55;
uint8_t test_data = 0;
uint8_t wait_activation = 0;
//uint8_t key_activation = 0;
uint8_t activation_success = 0;

#define LED_PROT GpioPortA
#define LED_PIN GpioPin7

char g_imei[16] = {0}; // 瀛樺偍15浣岻MEI+缁撴潫绗?
GpsCacheItem gnss_data1 = {0};
GpsCacheItem cell_data1 = {0};

//LED鐘舵€?
uint8_t led_blink = 0;

// 鍦ㄥ叏灞€鍙橀噺鍖哄煙娣诲姞
#define MOTION_CYCLE_DEFAULT     60    // 榛樿杩愬姩鍛ㄦ湡60鍒嗛挓
#define STILL_CYCLE_DEFAULT      180   // 榛樿闈欐鍛ㄦ湡180鍒嗛挓
#define CONTINUOUS_CYCLE_DEFAULT 720   // 榛樿杩炵画闈欐鍛ㄦ湡720鍒嗛挓

//淇敼涓婁紶鏃堕棿
extern uint16_t loadInterval ;
uint8_t  loadInterval_cnt = 0;
uint16_t motion_cycle = MOTION_CYCLE_DEFAULT;
uint16_t still_cycle = STILL_CYCLE_DEFAULT;
uint16_t continuous_cycle = CONTINUOUS_CYCLE_DEFAULT;
uint8_t is_move = 0;  //妫€娴嬫槸鍚﹁繍鍔?
float gyroscope_data = 0;  //闄€铻轰华鏁版嵁

extern char post_data[300];
uint8_t  self_check_status = 0;
uint8_t frist_activation = 1;
uint8_t is_in_normal_state = 0;
uint8_t other_wakeup = 0;
uint8_t modem_psm_kept_on = 0;








// 瀹氫箟鐘舵€佹灇涓剧被鍨?
typedef enum {
    STATE_GET_ANGLE,
    STATE_CHECK_SIM,
		STATE_NET_STRATEGY,
    STATE_GET_GNSS,
    STATE_GET_ADC,
    STATE_SEND_DATA,
    STATE_SLEEP,
    STATE_DELAY
} State_Type;

#define DEBOUNCE_TICKS  2       // 20ms娑堟姈(鍋囪10ms妫€娴嬪懆鏈?
#define LONG_PRESS_TICKS 259   // 1绉掗暱鎸夊垽瀹?


typedef enum {
    KEY_DECTET_IDLE,
    KEY_DECTET_INT,
    KEY_DECTET_PRESS,
} KeyFlag_t;
KeyFlag_t key_detect_flag = KEY_DECTET_IDLE;

//typedef enum {
//    KEY_STATE_IDLE,
//    KEY_STATE_DEBOUNCE,
//    KEY_STATE_PRESSED,
//    KEY_STATE_HOLD
//} KeyState_t;
// 鎸夐敭鐘舵€佹灇涓?
typedef enum {
    KEY_STATE_IDLE,        // 绌洪棽鐘舵€?
    KEY_STATE_DEBOUNCE,    // 娑堟姈鐘舵€?
    KEY_STATE_PRESSED,     // 鎸変笅鐘舵€侊紙鏈‘璁わ級
    KEY_STATE_HOLD,        // 闀挎寜鐘舵€侊紙宸茬‘璁わ級
    KEY_STATE_RELEASE      // 閲婃斁鐘舵€侊紙鐭寜纭锛?
} KeyState_t;

volatile uint16_t key_ticks = 0;
volatile KeyState_t key_state = KEY_STATE_IDLE;
volatile boolean_t key_press_valid = FALSE;
volatile uint16_t key_timer_cnt = 0;

#define CONFIRM_TIMEOUT 909
#define  CONFIRM_PRESS_TIMES 3
static uint8_t  key_confirm_count = 0;  // 宸叉寜涓嬫鏁?
static uint16_t key_confirm_timer = 0;  // 10绉掕秴鏃惰鏃讹紙浠呴娆℃寜閿悗閫掑锛?
static boolean_t     key_confirm_timer_started = 0; // 瀹氭椂鍣ㄦ槸鍚﹀凡鍚姩锛堥娆℃寜閿悗缃綅锛?

// 鍦ㄥ叏灞€鍙橀噺鍖哄煙娣诲姞鐘舵€佸畾涔?
typedef enum {
    STATE_INIT,
    STATE_SN_SET,
    STATE_SELF_CHECK,
    STATE_WAIT_ACTIVATION,
    STATE_DONE
} SnState_t;
SnState_t sn_state = STATE_INIT;







// 瀹氫箟鐘舵€佹満缁撴瀯浣?
typedef struct {
    State_Type currentState;
    uint32_t delayCounter;
} StateMachine_Type;
// 鍒濆鍖栫姸鎬佹満
StateMachine_Type stateMachine = {STATE_CHECK_SIM, 0};



int32_t main(void)
{  
//		pwr_up();
	

		SystemClock_Init();
//		UartInit(115200u);     	
		SEGGER_RTT_printf(0,"Start!\r\n");
	
	//test
//		pwr_en(1);
//			delay1ms(1000);
//			pwr_up();
//	while(1)
//	{
//		
//	}	
//		gnss_work();
//		delay1ms(5000);
//		pwr_up();
//	while(1)
//	{}
//			delay1ms(5000);
//			hardware_deinit();
//			Rtc_Alarm_Int_Init();
	
	
		Rtc_Init_Frist();
		FlashInit();
		
		key_int1_init();
    led_init();
		BtTimerTest();
		SEGGER_RTT_printf(0,"tim0 init complete!\r\n");
		Cache_Init();	
		start_devices();	
//key interrupt reserved, do not deinit key here
//		key_int_deinit();
		adc_io_init();
	  delay1ms(3000);
		flash_lock();
		stateMachine.currentState = STATE_GET_ADC;		
    while(1)
	{
		 switch(stateMachine.currentState)
        {
            case STATE_GET_ANGLE:
						sport_dectet();
//            delay1ms(500);
              break;
            case STATE_CHECK_SIM:
            {
                Tele2StartupResult_t startup_result;

                Sysctrl_SetRCHTrim(SysctrlRchFreq22_12MHz);
                Sysctrl_ClkSourceEnable(SysctrlClkRCH, TRUE);
                Sysctrl_SysClkSwitch(SysctrlClkRCH);
                UartInit(115200u);
                pwr_en(1);
                delay1ms(1000);
                pwr_on();
                modem_psm_kept_on = 0;
                get_gnss_flag = 1;

                if (Wait_Module_AT_Ready(60) == STATUS_OK) {
                    startup_result = Try_Register_Tele2_First();
                } else {
                    SEGGER_RTT_printf(0,"module AT not ready\r\n");
                    stateMachine.currentState = STATE_SLEEP;
                    break;
                }

                delay1ms(3000);
                if (Set_Http_Content_Type(4) != STATUS_OK) {
                    SEGGER_RTT_printf(0,"Set ContentType failed, continue GNSS/cache flow\r\n");
                }

                Get_ICCID(g_iccid, sizeof(g_iccid));
                if (Get_CSQ(&g_rssi, &g_ber) == STATUS_OK) {
                    SEGGER_RTT_printf(0,"get rssi succees!\r\n");
                } else {
                    SEGGER_RTT_printf(0,"get rssi failed\r\n");
                }

                if (Check_SIM_Ready() == STATUS_OK) {
                    #if DEBUG_MODE
                    SEGGER_RTT_printf(0,"Check_SIM_Ready, APN configured\r\n");
                    #endif
                } else {
                    #if DEBUG_MODE
                    SEGGER_RTT_printf(0,"Check_SIM_Ready failed, continue GNSS/cache flow\r\n");
                    #endif
                }

                stateMachine.currentState = (startup_result == TELE2_STARTUP_RUSSIA)
                                          ? STATE_NET_STRATEGY
                                          : STATE_GET_GNSS;
                break;
            }
            case STATE_NET_STRATEGY:
            {
                // 杩愯澶ц剳锛屽畠涓€瀹氫細鍚愬嚭涓€涓渶浼樺厛鐨勭綉缁滅储寮曠粰浣?(鍝€曟槸鐩插彂鐨?
                uint8_t target_idx = Run_Network_Strategy();
                
                if (target_idx != 0xFF) {
                    active_op_idx = target_idx;
                    // 缃戠粶宸茬粡鍒囧ソ骞舵縺娲讳簡鏁版嵁鍩燂紝鐩存帴鍘绘嬁瀹氫綅鍙戞暟鎹?
                    stateMachine.currentState = STATE_GET_GNSS; 
                } 
                else {
                    SEGGER_RTT_printf(
                        0,
                        "[NET] Russia poll found no usable data network; continue GNSS/cache\r\n");
                    active_op_idx = 0xFF;
                    stateMachine.currentState = STATE_GET_GNSS;
                }
            }
            break;
          case STATE_GET_GNSS:
//鏆傛椂娴嬭瘯鍏抽棴

                 if(ext_loc_enable == 2) //鍩虹珯瀹氫綅
                {
                    // 鎵╁睍瀹氫綅妯″紡
                
                        // 鍩虹珯浼樺厛妯″紡
												 delay1ms(15000);
                        if(get_loca_data() == STATUS_OK)
                        {
														get_neighbourcell_data();
                            if(get_cw_gps_data() != STATUS_OK)
                            {
                                //鑾峰彇鏁版嵁涓嶆垚鍔燂紝鍒囨崲鍒癎NSS妯″紡
                                Get_Gnss_Message();
                            }
                        }
												else
												{
													       Get_Gnss_Message();
												}
                    

            }
                else //gnss瀹氫綅
                {
                    // 榛樿瀹氫綅妯″紡锛堝吋瀹瑰師鏈夐€昏緫锛?
                      if(Get_Gnss_Message() != STATUS_OK)
                        {
															delay1ms(15000);	
                              if(get_loca_data() == STATUS_OK)
                            {
																get_neighbourcell_data();
                                if(get_cw_gps_data() != STATUS_OK)
                                {
                                    //鑾峰彇鏁版嵁涓嶆垚鍔燂紝鍒囨崲鍒癎NSS妯″紡
                                }
                            }
                         }
                }
                stateMachine.currentState = STATE_SEND_DATA;
                break;

            case STATE_GET_ADC:
                #if DEBUG_MODE
								SEGGER_RTT_printf(0,"STATE_GET_ADC\r\n");
                #endif
								delay1ms(100);
								tem_test_en();
                adc_get_data();
                stateMachine.currentState = STATE_CHECK_SIM;
                break;

             case STATE_SEND_DATA:
								http_send_data();
								SEGGER_RTT_printf(0,"http_send_data\r\n");
								is_move = 0;
								led_stop();
                stateMachine.currentState = STATE_SLEEP;
                break;
			case STATE_SLEEP:
				if(get_gnss_flag)
				{
					SEGGER_RTT_printf(0, "power down modem\r\n");
					pwr_down();
					pwr_en(0);
					modem_psm_kept_on = 0;
					get_gnss_flag = 0;
				}
				//娴嬭瘯鏆傛椂鍏抽棴
//				Rtc_ReadDateTime(&stcTime1);
//				stcTime1.u8DayOfWeek = Rtc_CalWeek(&stcTime1.u8Day);
                #if 0
				SEGGER_RTT_printf(0, "Current time: %02x:%02x:%02x:%02x\n", stcTime1.u8Hour, stcTime1.u8Minute,stcTime1.u8Second,stcTime1.u8DayOfWeek);
                #endif
				SEGGER_RTT_printf(0,"before hardware_deinit");
				delay1ms(500);
				hardware_deinit();
				SEGGER_RTT_printf(0,"hardware_deinit");
				Rtc_Alarm_Int_Init();
//				SEGGER_RTT_printf(0,"Rtc_Alarm_Int_Init");	
				//鎸夐敭鍞ら啋鍚庯紝寮€鍚畾鏃跺櫒锛屽紑鍚疞ED
				if(other_wakeup)
				{
					key_int_deinit();
					led_init();
					BtTimerTest();
					led_blink = 1;
				}
        stateMachine.currentState = STATE_DELAY;
				stateMachine.delayCounter = 0;		
						
                break;
            case STATE_DELAY:
								adc_io_init();
                if(stateMachine.delayCounter >= 500) // 寤舵椂5绉? 鏆傛椂鏀规垚5s
                {
                    stateMachine.currentState = STATE_GET_ANGLE;
                }
                else
                {
                    delay1ms(1);
                    stateMachine.delayCounter++;
                }
                break;

            default:
                stateMachine.currentState = STATE_GET_ANGLE;
                break;
        }
    }
}
static uint8_t Flash_CheckEraseFlag(void)
{
    uint8_t u8Flag = 0;
    // 璇诲彇鏍囪鍦板潃鐨勫€?
    u8Flag = *(volatile uint8_t *)(eraser_addr+100);
    // 鑻ユ爣璁板€肩瓑浜庨璁惧€硷紝璇存槑宸叉摝闄ゅ苟鍐欏叆杩囨爣璁?
    return (u8Flag == FLASH_ERASE_FLAG_VAL) ? 1 : 0;
}

/**
 * @brief 鍐欏叆鎿﹂櫎瀹屾垚鏍囪锛團LASH涓紝閲嶅惎涓嶄涪澶憋級
 */
static void Flash_SetEraseFlag(void)
{		
		uint8_t u8FlagVal = FLASH_ERASE_FLAG_VAL;
    while(Ok != Flash_OpModeConfig(FlashWriteMode));
    Flash_Write8((eraser_addr+100), &u8FlagVal, 1);
}


 void FlashInit(void)
 {
	 
	  // STEP1锛欶LASH鍒濆鍖?
    while(Ok != Flash_Init(1, TRUE)){ while(1); }
		  // 妫€鏌LASH涓殑鎿﹂櫎鏍囪锛屽凡鎿﹂櫎鍒欑洿鎺ヨ繑鍥?
    if(Flash_CheckEraseFlag() == 1)
    {
        // 閲嶆柊瑙ｉ攣鐩爣鎵囧尯锛堥噸鍚悗閿佹帶鐘舵€佸彲鑳戒繚鐣欙紝闇€纭锛?
        Flash_LockSet(0x20000000);    //0xee00瀵瑰簲鎵囧尯
        return;
		}
    // STEP2锛氳В閿佺洰鏍囨墖鍖?
    if(Ok != Flash_LockSet(0x20000000)){ while(1); }

//    // STEP3锛氭摝闄ょ洰鏍囨墖鍖?
//    while(Ok != Flash_OpModeConfig(FlashSectorEraseMode));
//    if(Ok != Flash_SectorErase(eraser_addr)){ while(1); }

//    // STEP4锛氬啓鍏ユ摝闄ゅ畬鎴愭爣璁帮紙鍏抽敭锛氬啓鍏LASH锛岄噸鍚笉涓級
//    Flash_SetEraseFlag();

    return ;
		
      
 }
 uint8_t flash_write_byte(uint32_t u32Addr,uint8_t u8Data,uint8_t len)
 {
	 
	 
	
    ///< STEP-4 FLASH 瀛楄妭鍐欍€佹牎楠?   
    while(Ok != Flash_OpModeConfig(FlashWriteMode));
    if (Ok != Flash_Write8(u32Addr, &u8Data, 1))
    {
        while(1);
    }
		return STATUS_OK;
 }
 uint8_t flash_lock(void)
 {
				///< FLASH 璇绘ā寮?
		while(Ok != Flash_OpModeConfig(FlashReadMode));
		///< FLASH 鍏ㄩ儴鍔犻攣
		while(Ok != Flash_LockAll());
		return STATUS_OK;
 } 
 
 
 
 ///< PortA涓柇鏈嶅姟鍑芥暟
void PortB_IRQHandler(void)
{
    if(TRUE == Gpio_GetIrqStatus(POWER_ON_PORT, POWER_ON_PIN))
    {            
				#if 1
				SEGGER_RTT_printf(0, "ket int run!\n");
				SEGGER_RTT_printf(0, "activation_success =%d\n",activation_success);
				#endif
				activation_success ++;
				if(activation_success >= 100)
				{
					activation_success = 0;
				}
				if(is_in_normal_state)
				{
					
						other_wakeup = 1;
				}
        Gpio_ClearIrq(POWER_ON_PORT, POWER_ON_PIN);  
				//鐢ㄤ簬纭鏄惁澶勪簬婵€娲荤姸鎬侊紝浜伅涓€涓嬪悗杩涘叆鐫＄湢鐘舵€?
		
    }

} 
 

// void key_int_init(void)
// {
//		stc_gpio_cfg_t stcGpioCfg;
//    
//    ///< 鎵撳紑GPIO澶栬鏃堕挓闂ㄦ帶
//    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE);
//    
//    ///< 绔彛鏂瑰悜閰嶇疆->杈撳叆
//    stcGpioCfg.enDir = GpioDirIn;
//    ///< 绔彛椹卞姩鑳藉姏閰嶇疆->楂橀┍鍔ㄨ兘鍔?
//    stcGpioCfg.enDrv = GpioDrvL;
//    ///< 绔彛涓婁笅鎷夐厤缃?>鏃?
//    stcGpioCfg.enPu = GpioPuDisable;
//    stcGpioCfg.enPd = GpioPdDisable;
//    ///< 绔彛寮€婕忚緭鍑洪厤缃?>寮€婕忚緭鍑哄叧闂?
//    stcGpioCfg.enOD = GpioOdDisable;
//    ///< 绔彛杈撳叆/杈撳嚭鍊煎瘎瀛樺櫒鎬荤嚎鎺у埗妯″紡閰嶇疆->AHB
//    stcGpioCfg.enCtrlMode = GpioAHB;
//    ///< GPIO IO USER KEY鍒濆鍖?
//    Gpio_Init(POWER_ON_PORT, POWER_ON_PIN, &stcGpioCfg); 
//	 
//		 ///< 鎵撳紑骞堕厤缃甎SER KEY涓轰笅闄嶆部涓柇
//    Gpio_EnableIrq(POWER_ON_PORT, POWER_ON_PIN, GpioIrqFalling);
//    ///< 浣胯兘绔彛PORTA绯荤粺涓柇
//    EnableNvic(PORTB_IRQn, IrqLevel3, TRUE);
// }
  void key_int_deinit(void)
	{
		Gpio_DisableIrq(POWER_ON_PORT,POWER_ON_PIN,GpioIrqFalling);
		EnableNvic(PORTB_IRQn, IrqLevel3, FALSE);
	}
 
// void key_dectet(void)
// {
//		 boolean_t key_current = !Gpio_GetInputIO(POWER_ON_PORT, POWER_ON_PIN); // 鍋囪浣庣數骞虫湁鏁?
//    
//    switch(key_state) {
//        case KEY_STATE_IDLE:
//            if(key_current) {
//                key_state = KEY_STATE_DEBOUNCE;
////								SEGGER_RTT_printf(0,"123\n");
//                key_ticks = 0;
//            }
//            break;
//            
//        case KEY_STATE_DEBOUNCE:
//            if(++key_ticks >= DEBOUNCE_TICKS) {
//                key_state = key_current ? KEY_STATE_PRESSED : KEY_STATE_IDLE;
//								SEGGER_RTT_printf(0,"456\n");
//                key_ticks = 0;
//            }
//            break;
//            
//        case KEY_STATE_PRESSED:
//            if(!key_current) {
//                key_state = KEY_STATE_DEBOUNCE;
//                key_ticks = 0;
//            } else if(++key_ticks >= LONG_PRESS_TICKS) {
//                key_state = KEY_STATE_HOLD;
//                key_press_valid = TRUE;
//                key_activation = 1;
//                #if DEBUG_MODE
//                SEGGER_RTT_printf(0, "key press !");
//                #endif
//                
//            }
//            break;
//            
//        case KEY_STATE_HOLD:
//            if(!key_current) {
//                key_state = KEY_STATE_DEBOUNCE;
//                key_ticks = 0;
//            }
//            break;
//    }

// }

// 鏍稿績鎸夐敭妫€娴嬪嚱鏁帮紙瀵归綈浣犵殑缁撴瀯锛岃ˉ鍏呯煭鎸夌‘璁わ級
void key_dectet(void)
{
    // 璇诲彇鎸夐敭鐢靛钩锛氫綆鐢靛钩鏈夋晥锛堟寜涓嬫椂key_current=true锛?
    boolean_t key_current = !Gpio_GetInputIO(POWER_ON_PORT, POWER_ON_PIN);
    
    switch(key_state) {
        case KEY_STATE_IDLE:
            if(key_current) {
                key_state = KEY_STATE_DEBOUNCE;
                key_ticks = 0;
            }
            break;

        case KEY_STATE_DEBOUNCE:
            if(++key_ticks >= DEBOUNCE_TICKS) {
                key_state = key_current ? KEY_STATE_PRESSED : KEY_STATE_IDLE;
                key_ticks = 0;
            }
            break;

        case KEY_STATE_PRESSED:
            if(!key_current) {
                key_state = KEY_STATE_RELEASE;
                key_ticks = 0;
                key_press_valid = TRUE; // 鐭寜瑙﹀彂锛岀疆涓烘湁鏁?
            } else if(++key_ticks >= LONG_PRESS_TICKS) {
                key_state = KEY_STATE_HOLD;
                key_press_valid = FALSE; // 闀挎寜锛岀疆涓烘棤鏁?
            }
            break;

        case KEY_STATE_HOLD:
            if(!key_current) {
                key_state = KEY_STATE_DEBOUNCE;
                key_ticks = 0;
                // 淇鐐?锛氶暱鎸夐噴鏀撅紝閲嶇疆key_press_valid
                key_press_valid = FALSE;
            }
            break;

        case KEY_STATE_RELEASE:
            if(++key_ticks >= DEBOUNCE_TICKS) {
                key_state = KEY_STATE_IDLE;
                key_ticks = 0;
                // 淇鐐?锛氱煭鎸夐噴鏀炬秷鎶栧畬鎴愶紝閲嶇疆key_press_valid锛堟牳蹇冿紒锛?
//                key_press_valid = FALSE;
            }
            break;
    }
}
void Tim0_IRQHandler(void)
{
    static uint8_t i;
    
    //Timer0 妯″紡0 婧㈠嚭涓柇
    if(TRUE == Bt_GetIntFlag(TIM0, BtUevIrq))
    {
				 if(key_detect_flag == KEY_DECTET_PRESS) 
       {
            static uint8_t key_timer = 0;
            boolean_t key_current = !Gpio_GetInputIO(POWER_ON_PORT, POWER_ON_PIN); // 鍋囪浣庣數骞虫湁鏁?
						
            // 10ms鍛ㄦ湡澶勭悊鎸夐敭
            if(++key_timer >= 1) {
								key_timer = 0;
								key_dectet();
//							SEGGER_RTT_printf(0,"444\n");
            // 妫€娴嬬煭鎸夐噴鏀?
                if((key_state == KEY_STATE_DEBOUNCE) && !key_current && key_ticks >= DEBOUNCE_TICKS)
                {
									key_state = KEY_STATE_IDLE;
                }
            }

       }
        if(led_blink)
        {
            static uint8_t led_timer = 0;
            led_timer++;
            if(++led_timer >= 20)
            {
                Gpio_WriteOutputIO(LED_PROT,LED_PIN,!Gpio_ReadOutputIO(LED_PROT,LED_PIN));
                led_timer = 0;
            }
            
        }
				if(0)
				{
						 static uint8_t led_timer = 0;
            led_timer++;
            if(led_timer >= 50)
            {
//                SEGGER_RTT_printf(0,"789\n");
                led_timer = 0;
            }
				}
				 if(key_confirm_timer_started)
        {
            key_confirm_timer++; // 姣?0ms +1锛屼粠棣栨鎸夐敭鍚庡紑濮嬬畻
            
            // 10绉掕秴鏃讹細閲嶇疆鎵€鏈夋爣璁帮紝缁堟纭娴佺▼
            if(key_confirm_timer >= CONFIRM_TIMEOUT)
            {
                SEGGER_RTT_printf(0, "time out \n");
                key_confirm_count = 0;                // 閲嶇疆鎸夐敭璁℃暟
                key_confirm_timer = 0;                // 閲嶇疆璁℃椂
                key_confirm_timer_started = 0;    // 鍋滄瀹氭椂鍣?
                key_detect_flag = KEY_DECTET_IDLE;    // 鍏抽棴鎸夐敭妫€娴?
            }
        }
        
        
        Bt_ClearIntFlag(TIM0,BtUevIrq); //涓柇鏍囧織娓呴浂
    }
}
 
en_result_t BtTimerTest(void)
{
		
		  uint16_t                  u16ArrValue;
    uint16_t                  u16CntValue;
		uint16_t u16Period = 1000;
    stc_bt_mode0_cfg_t     stcBtBaseCfg;
	
    
    DDL_ZERO_STRUCT(stcBtBaseCfg);
    
    Sysctrl_SetPeripheralGate(SysctrlPeripheralBaseTim, TRUE); //Base Timer澶栬鏃堕挓浣胯兘
    
    stcBtBaseCfg.enWorkMode = BtWorkMode0;                  //瀹氭椂鍣ㄦā寮?
    stcBtBaseCfg.enCT       = BtTimer;                      //瀹氭椂鍣ㄥ姛鑳斤紝璁℃暟鏃堕挓涓哄唴閮≒CLK
    stcBtBaseCfg.enPRS      = BtPCLKDiv256;                 //PCLK/256
    stcBtBaseCfg.enCntMode  = Bt16bitArrMode;               //鑷姩閲嶈浇16浣嶈鏁板櫒/瀹氭椂鍣?
    stcBtBaseCfg.bEnTog     = FALSE;
    stcBtBaseCfg.bEnGate    = FALSE;
    stcBtBaseCfg.enGateP    = BtGatePositive;
    Bt_Mode0_Init(TIM0, &stcBtBaseCfg);                     //TIM0 鐨勬ā寮?鍔熻兘鍒濆鍖?
    
    u16ArrValue = 0x10000 - u16Period;
    Bt_M0_ARRSet(TIM0, u16ArrValue);                        //璁剧疆閲嶈浇鍊?ARR = 0x10000 - 鍛ㄦ湡),11.53ms涓€娆?
    
    u16CntValue = 0x10000 - u16Period;
    Bt_M0_Cnt16Set(TIM0, u16CntValue);                      //璁剧疆璁℃暟鍒濆€?
    
    Bt_ClearIntFlag(TIM0,BtUevIrq);                         //娓呬腑鏂爣蹇?  
    Bt_Mode0_EnableIrq(TIM0);                               //浣胯兘TIM0涓柇(妯″紡0鏃跺彧鏈変竴涓腑鏂?
    EnableNvic(TIM0_IRQn, IrqLevel3, TRUE);                 //TIM0涓柇浣胯兘
	
   Bt_M0_Run(TIM0);        //TIM0 杩愯銆?
    
   
    
    return TRUE;
}

void BtTimerT_deinit(void)
{
	 /* 瀹夊叏鍏抽棴娴佺▼ */
    Bt_M0_Stop(TIM0);
    Bt_Mode0_DisableIrq(TIM0);
    Bt_ClearIntFlag(TIM0,BtUevIrq); //涓柇鏍囧織娓呴浂
    
    // 娓呴櫎NVIC涓殑涓柇浣胯兘
      EnableNvic(TIM0_IRQn, IrqLevel3, FALSE);   
    
    // 绉婚櫎鐩存帴瀵勫瓨鍣ㄦ搷浣?
    // TIM2->CR0 = 0x0000;  // 鍘熼敊璇唬鐮?
    // TIM2->CR1 = 0x0000;  // 鍘熼敊璇唬鐮?
    
    // 鍙岄噸鏍￠獙鏃堕挓闂ㄦ帶
    if(Sysctrl_SetPeripheralGate(SysctrlPeripheralBaseTim, FALSE) != Ok) {
        #if DEBUG_MODE
        SEGGER_RTT_printf(0, "BT clock gate close failed");
        #endif
    }
    
    // 娣诲姞寤舵椂纭繚纭欢鐘舵€佺ǔ瀹?
    delay1ms(10);
}
	
	 
 
void sport_dectet(void)
{
    static uint16_t motion_counter = 0;    // 杩愬姩鍒嗛挓璁℃暟
    static uint16_t still_counter = 0;     // 闈欐鍒嗛挓璁℃暟
    static uint8_t consecutive_still = 0;  // 杩炵画闈欐娆℃暟
     SEGGER_RTT_printf(0,"Before get sport value\n");
    drv_sc7a20_i2c_init();
		     SEGGER_RTT_printf(0,"drv_sc7a20_i2c_init \n");
    sc7a20_init();
	SEGGER_RTT_printf(0,"sc7a20_init \n");
    SC7A20_Task();
		 SEGGER_RTT_printf(0,"After get sport value\n");

    // 鐘舵€佹娴嬶紙姣忔鍞ら啋闂撮殧10鍒嗛挓锛?
    if(running_flag) {
        motion_counter += 10;
//        still_counter = 0;
//        consecutive_still = 0;     // 杩愬姩鏃堕噸缃繛缁潤姝㈣鏁?
    } else {
        still_counter += 10;
//        motion_counter = 0;
    }

    // 鏍稿績瑙﹀彂鏉′欢锛氳繍鍔?闈欐鎬绘椂闂磋秴杩囪繍鍔ㄥ懆鏈?
    if(motion_counter > 0){
        if(((motion_counter + still_counter) >= motion_cycle)  || (consecutive_still >= 1)) {
                stateMachine.currentState = STATE_GET_ADC;
                motion_counter = still_counter = 0;
                consecutive_still = 0;
//							consecutive_still = 0;  // 瑙﹀彂鍚庨噸缃繛缁鏁?
                #if DEBUG_MODE
                SEGGER_RTT_printf(0,"[Trigger] Total time %dmin\n", motion_cycle);
                #endif
                return;
            }

    }
			
//濡傛灉if((motion_counter + still_counter) 浼氭竻鎺夋暟鎹?
    // 闈欐鐗规畩鍛ㄦ湡澶勭悊
    if(!running_flag) {
        if(consecutive_still >= 3) {
            // 杩炵画闈欐鍛ㄦ湡瑙﹀彂
            if(still_counter >= continuous_cycle) {
                stateMachine.currentState = STATE_GET_ADC;
                still_counter = 0;
								motion_counter = 0;
                #if DEBUG_MODE
                SEGGER_RTT_printf(0,"[Still] Continuous %dmin\n", continuous_cycle);
                #endif

            }
        } 
        else if(still_counter >= still_cycle) {
            // 鏅€氶潤姝㈣Е鍙?
            stateMachine.currentState = STATE_GET_ADC;
            still_counter = 0;
						motion_counter = 0;
            consecutive_still++;  // 鍙湁鏅€氶潤姝㈣Е鍙戞椂鎵嶇疮璁?
            #if DEBUG_MODE
            SEGGER_RTT_printf(0,"[Still] Regular %dmin\n", still_cycle);
            #endif
        }
    }

    // 榛樿鐘舵€佽缃?
    if(stateMachine.currentState != STATE_GET_ADC) {
        stateMachine.currentState = STATE_SLEEP;
        #if DEBUG_MODE
        SEGGER_RTT_printf(0,"[Sleep] No trigger condition met\n");
        #endif
    }
		#if DEBUG_MODE
    SEGGER_RTT_printf(0,"Motion:%d + Still:%d = %dmin\n", 
                    motion_counter, still_counter, motion_counter + still_counter);
		#endif
}



 // 鍦╩ain鍑芥暟鍒濆鍖栭儴鍒嗘坊鍔?
void Cache_Init(void)
{
    memset(&g_gps_cache, 0, sizeof(g_gps_cache));
}
// 淇敼缂撳瓨鍐欏叆鍑芥暟
// uint8_t Cache_Put(const GpsCacheItem *item)
// {
//     // 鎬绘槸鍐欏叆鏁版嵁
//     g_gps_cache.items[g_gps_cache.head] = *item;
//     g_gps_cache.head = (g_gps_cache.head + 1) % CACHE_SIZE;
    
//     if(g_gps_cache.count < CACHE_SIZE) {
//         g_gps_cache.count++;  // 鏈弧鏃跺鍔犺鏁?
//     } else {
//         // 缂撳瓨宸叉弧鏃剁Щ鍔ㄥ熬鎸囬拡锛堣鐩栨棫鏁版嵁锛?
//         g_gps_cache.tail = (g_gps_cache.tail + 1) % CACHE_SIZE;
//         #if DEBUG_MODE
//         SEGGER_RTT_printf(0, "Cache overflow, data overwritten\n");
//         #endif
//     }
    
//     return STATUS_OK;
// }
uint8_t Cache_Put(const GpsCacheItem *item)
{
    // 妫€鏌ユ槸鍚︿笌鏈€鏂扮紦瀛樼殑鏁版嵁閲嶅
    if (g_gps_cache.count > 0) {
        uint8_t latest_index = (g_gps_cache.head - 1 + CACHE_SIZE) % CACHE_SIZE;
        if (item->latitude == g_gps_cache.items[latest_index].latitude && 
            item->longitude == g_gps_cache.items[latest_index].longitude &&
            item->timestamp == g_gps_cache.items[latest_index].timestamp) {
            #if DEBUG_MODE
            SEGGER_RTT_printf(0, "Duplicate data, not added to cache\n");
            #endif
            return STATUS_OK; // 閲嶅鏁版嵁涓嶆坊鍔?
        }
    }
    
    // 鍐欏叆鏂版暟鎹?
    g_gps_cache.items[g_gps_cache.head] = *item;
    g_gps_cache.head = (g_gps_cache.head + 1) % CACHE_SIZE;
    
    if(g_gps_cache.count < CACHE_SIZE) {
        g_gps_cache.count++;
    } else {
        // 缂撳瓨宸叉弧鏃剁Щ鍔ㄥ熬鎸囬拡锛堣鐩栨棫鏁版嵁锛?
        g_gps_cache.tail = (g_gps_cache.tail + 1) % CACHE_SIZE;
        #if DEBUG_MODE
        SEGGER_RTT_printf(0, "Cache overflow, data overwritten\n");
        #endif
    }
    
    return STATUS_OK;
}
uint8_t Cache_Get(GpsCacheItem *item)
{
    if(g_gps_cache.count == 0) {
        return STATUS_FAILED; // 缂撳瓨涓虹┖
    }
    
    *item = g_gps_cache.items[g_gps_cache.tail];
    g_gps_cache.tail = (g_gps_cache.tail + 1) % CACHE_SIZE;
    g_gps_cache.count--;
    
    return STATUS_OK;
}
// void http_send_data(void)
// {
// 	  // 浼樺厛鍙戦€佸綋娆℃暟鎹?
//                 if (Http_post(ConvertToTimestamp()) != STATUS_OK)
//                 {
//                     // 淇濆瓨褰撳墠鏁版嵁鍒扮紦瀛橈紙浠呭綋鍧愭爣鏈夋晥鏃讹級
//                     if ((Gps_Data.latitude != 0.0f) && (Gps_Data.longitude != 0.0f)) {
//                         const GpsCacheItem currentItem = {
//                             .latitude = Gps_Data.latitude,
//                             .longitude = Gps_Data.longitude,
//                             .timestamp = timestamp_now,
// 														.move_status = is_move,
// 														.tem_backup = tem_res,
// 														.location_type = loation_type
//                         };
//                         Cache_Put(&currentItem);
//                         #if 1
//                         SEGGER_RTT_printf(0, "Saved current data to cache\n");
//                         #endif
//                     }
//                     stateMachine.currentState = STATE_SLEEP;
//                 }
//                 else
//                 {
//                     // 褰撴鍙戦€佹垚鍔熷悗浠呮竻闄ゅ畾浣嶆暟鎹?
//                     Gps_Data.latitude = 0.0f;
//                     Gps_Data.longitude = 0.0f;
//                     timestamp_now = 0;
                    
//                     // 澶勭悊缂撳瓨闃熷垪
//                     GpsCacheItem cachedItem;
//                     while (Cache_Get(&cachedItem) == STATUS_OK)
//                     {  
//                         // 鎭㈠缂撳瓨瀹氫綅鏁版嵁
//                         Gps_Data.latitude = cachedItem.latitude;
//                         Gps_Data.longitude = cachedItem.longitude;
//                         timestamp_now = cachedItem.timestamp;
// 												is_move = cachedItem.move_status;
// 												tem_res = cachedItem.tem_backup;
// 												loation_type = cachedItem.location_type;
                        
//                         if (Http_post(timestamp_now) != STATUS_OK)
//                         {
//                             Cache_Put(&cachedItem);
//                             break;
//                         }
// 												else
//                         {
//                             Gps_Data.latitude = 0.0f;
// 														Gps_Data.longitude = 0.0f;
// 														timestamp_now = 0;
//                         }
//                     }
//                     stateMachine.currentState = STATE_SLEEP;
//                 }
//                 Clear_RX_Buffer();
// }
void http_send_data(void)
{
    // 浼樺厛鍙戦€佸綋娆℃暟鎹?
    if (Http_post(ConvertToTimestamp()) != STATUS_OK)
    {
			
				if(frist_activation)
				{
					led_blink = 0;
					frist_activation = 0;
					Gpio_SetIO(LED_PROT,LED_PIN);
					BtTimerT_deinit();		
					while(1)
					{
						
					}
				}
        // 淇濆瓨褰撳墠鏁版嵁鍒扮紦瀛橈紙浠呭綋鍧愭爣鏈夋晥鏃讹級
        if ((Gps_Data.latitude != 0.0f) && (Gps_Data.longitude != 0.0f)) {
            const GpsCacheItem currentItem = {
                .latitude = Gps_Data.latitude,
                .longitude = Gps_Data.longitude,
                .timestamp = timestamp_now,
                .move_status = is_move,
                .tem_backup = tem_res,
                .location_type = loation_type
            };
            Cache_Put(&currentItem);
            #if 1
            SEGGER_RTT_printf(0, "Saved current data to cache\n");
            #endif
        }
				Gps_Data.latitude = 0.0f;
        Gps_Data.longitude = 0.0f;
        timestamp_now = 0;
        stateMachine.currentState = STATE_SLEEP;
    }
    else
    {
        // 褰撴鍙戦€佹垚鍔熷悗娓呴櫎瀹氫綅鏁版嵁
        Gps_Data.latitude = 0.0f;
        Gps_Data.longitude = 0.0f;
        timestamp_now = 0;
        
        // 澶勭悊缂撳瓨闃熷垪锛屼絾闄愬埗閲嶈瘯娆℃暟
        GpsCacheItem cachedItem;
        uint8_t retry_count = 0;
        const uint8_t MAX_RETRY_PER_ITEM = 3; // 姣忎釜缂撳瓨椤规渶澶氶噸璇?娆?
        
        while (Cache_Get(&cachedItem) == STATUS_OK)
        {
            // 鎭㈠缂撳瓨瀹氫綅鏁版嵁
            Gps_Data.latitude = cachedItem.latitude;
            Gps_Data.longitude = cachedItem.longitude;
            timestamp_now = cachedItem.timestamp;
            is_move = cachedItem.move_status;
            tem_res = cachedItem.tem_backup;
            loation_type = cachedItem.location_type;
            
            if (Http_post(timestamp_now) != STATUS_OK)
            {
                // 澧炲姞閲嶈瘯璁℃暟骞舵鏌ユ槸鍚﹁秴杩囨渶澶ч噸璇曟鏁?
                if (retry_count < MAX_RETRY_PER_ITEM) {
                    Cache_Put(&cachedItem); // 浠呭湪鏈秴杩囬噸璇曟鏁版椂閲嶆柊鏀惧叆缂撳瓨
                    Gps_Data.latitude = 0.0f;
                    Gps_Data.longitude = 0.0f;
                    timestamp_now = 0;
                    retry_count++;
                    #if DEBUG_MODE
                    // SEGGER_RTT_printf(0, "Retry sending cached data, count: %d\n", retry_count);
                    #endif
                } else {
                    #if DEBUG_MODE
                    // SEGGER_RTT_printf(0, "Max retry reached, data discarded\n");
                    #endif
                }
                break;
            }
            else
            {
                Gps_Data.latitude = 0.0f;
                Gps_Data.longitude = 0.0f;
                timestamp_now = 0;
                retry_count = 0; // 鎴愬姛鍚庨噸缃噸璇曡鏁?
            }
        }
        stateMachine.currentState = STATE_SLEEP;
    }
    Clear_RX_Buffer();
}
uint8_t Http_post_self_check(time_t use_timestamp) 
{
   
    size_t data_len = 0;
    if (Send_Http_URL_self_check() != STATUS_OK)
    {
        return STATUS_FAILED;
    }

    delay1ms(100);
    if (Format_Http_Post_Data_self_check(&Gps_Data, post_data, sizeof(post_data), &data_len, battery,use_timestamp) != STATUS_OK)
    {
				 SEGGER_RTT_printf(0, "tx failed");
        return STATUS_FAILED;
    }

//    SEGGER_RTT_printf(0, "post_data = %s\n", post_data);  // 淇鏍煎紡瀛楃涓?
//    SEGGER_RTT_printf(0, "data_len = %d\n", data_len);  // 鎵撳嵃鏁版嵁闀垮害

    if (Send_Http_Data(data_len) != STATUS_OK)
    {
        #if 1
        SEGGER_RTT_printf(0, "data_len= %d\n",data_len);

        #endif
        return STATUS_FAILED;
    }
    SEGGER_RTT_printf(0, "bk 1\n");
    delay1ms(100);
    if (Read_Http_Response_self_check() != STATUS_OK)
    {
        #if 1
        SEGGER_RTT_printf(0, "bk 4\n");
        #endif
        return STATUS_FAILED;
    }
		else
		{
			SEGGER_RTT_printf(0, "bk 66\n");
			return STATUS_OK;
		}

    
}

uint8_t Send_Http_URL_self_check(void)
{
		Clear_RX_Buffer();
//     AT_SendCmd("AT+QHTTPURL=44,30");
// 		if (Wait_For_Easy_Response("CONNECT", TIMEOUT_COUNT))
//     {
//         SEGGER_RTT_printf(0, "http connect!\n");
// //        AT_SendCmd("http://47.108.145.137:5055/receData/position?");
// 				// AT_SendCmd("http://182.92.135.10:5055/receData/position?");
//         AT_SendCmd("http://182.92.135.10:5055/receData/selfCheck");     
    char url[] = "http://182.92.135.10:5055/receData/selfCheck";
    char at_cmd[64];
    
    // 鍔ㄦ€佽绠桿RL闀垮害骞舵牸寮忓寲AT鎸囦护
    snprintf(at_cmd, sizeof(at_cmd), "AT+QHTTPURL=%d,30", (int)strlen(url));
    AT_SendCmd(at_cmd);
    
    if (Wait_For_Easy_Response("CONNECT", TIMEOUT_COUNT)) {
        SEGGER_RTT_printf(0, "http connect!\n");
        AT_SendCmd(url);      
        if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT))
        {
            #if 1
            SEGGER_RTT_printf(0, "connect ok!\n");
            #endif
            return STATUS_OK;
        }
        else
        {
            #if 1
            SEGGER_RTT_printf(0, "connect is failed!\n");
            #endif
            return STATUS_FAILED;
        }
    }
    else
    {
        #if DEBUG_MODE
        SEGGER_RTT_printf(0, "connect is failed!\n");
        #endif  
        return STATUS_FAILED;
    }
}

uint8_t Format_Http_Post_Data_self_check(GpsData_t *gps, char *buffer, size_t buffer_size, size_t *data_length, float batt, time_t timestamp) 
{
		#if DEBUG_MODE
    SEGGER_RTT_printf(0, "Entering Format_Http_Post_Data\n");
		#endif
		uint32_t id = 0;
		eraser_addr = ERASE_ADDR_IS;
		// 璇诲彇4瀛楄妭灏忕鏁版嵁锛堢ず渚嬶細07 B2 01 00 -> 0x0001B207 = 111111锛?
        volatile uint32_t *p_flash = (volatile uint32_t*)eraser_addr;
        id = *p_flash;  // 鐩存帴璇诲彇32浣嶅皬绔暟鎹?
    // const char *url_template = "id=%d&timestamp=%ld&lat=%.6f&lon=%.6f&speed=%.1f&altitude=%.1f&acceleration=%.1f&batt=%.2f&temperature=%.1f&type=%d";
		SEGGER_RTT_printf(0, "imei =%s\n",g_imei);
		const char *url_template = 
		"{"
		"\"id\":\"%lu\","             // 鏁板€艰浆瀛楃涓?
		"\"timestamp\":\"%ld\","      // 鏁板€艰浆瀛楃涓? 
		"\"imei\":\"%s\","
		"\"gnssLat\":\"%.6f\","       // 娴偣鏁拌浆瀛楃涓?
		"\"gnssLon\":\"%.6f\","
		"\"baseStationLat\":\"%.6f\","
		"\"baseStationLon\":\"%.6f\","
		"\"batt\":\"%.2f\","          // 淇濈暀涓や綅灏忔暟
		"\"temperature\":\"%.1f\","
		"\"gyroscope\":\"%.1f\","
		"\"rssi\":\"%d\","
		"\"iccid\":\"%s\""
		"}";
    // timestamp_now = ConvertToTimestamp();
    #if DEBUG_MODE
    SEGGER_RTT_printf(0, "After ConvertToTimestamp\n");
    #endif

    if (timestamp == STATUS_FAILED) {
        #if DEBUG_MODE
        SEGGER_RTT_printf(0, "[ERROR] Invalid timestamp\n");
        #endif
        return STATUS_FAILED;
    }
    #if DEBUG_MODE
    SEGGER_RTT_printf(0, "Before snprintf\n");
    #endif
		SEGGER_RTT_printf(0, "id_size = %d\n",sizeof(id));
    int len = snprintf(buffer, buffer_size, url_template,
						 id,	
             timestamp,
             g_imei,
             gnss_data1.latitude,
             gnss_data1.longitude,
             cell_data1.latitude,
             cell_data1.longitude,  // 浣跨敤缁撴瀯浣撲腑鐨勬柟鍚戝€?
             battery,         // 浠庡弬鏁拌幏鍙栫數姹犵數閲?
             tem_res,
             gyroscope_data,
						 g_rssi,
						 g_iccid);        // 绮惧害鍙傛暟鏆傛椂淇濈暀纭紪鐮?
    #if DEBUG_MODE
    SEGGER_RTT_printf(0, "After snprintf\n");
    #endif

    if (len < 0 || (size_t)len >= buffer_size) {
        #if DEBUG_MODE
        SEGGER_RTT_printf(0, "[ERROR] Buffer overflow (%d/%zu)\n", len, buffer_size);
        #endif
        return STATUS_FAILED;
    }
//		memset(&Gps_Data,0,sizeof(Gps_Data));
    *data_length = (size_t)len;
    #if DEBUG_MODE
    SEGGER_RTT_printf(0, "Exiting Format_Http_Post_Data\n");
    #endif
    return STATUS_OK;
}


// 淇敼鍚庣殑sn_set鍑芥暟
void start_devices(void)
{
//    char *time_str;
    eraser_addr = ERASE_ADDR_IS;
    if(*((volatile uint8_t*)eraser_addr+40) == activation_data) {
        sn_state = STATE_DONE;
    } else if(*((volatile uint8_t*)eraser_addr+30) == activation_flag) {
        sn_state = STATE_WAIT_ACTIVATION;
				key_int1_init();
//				self_check_hardware_deinit();
    }
    else if (*((volatile uint8_t*)eraser_addr+20) == sn_flash_set_ok)
    {
            sn_state = STATE_SELF_CHECK;
        /* code */
    }
    else if (*((volatile uint8_t*)eraser_addr+3) != 0xff)
    {
            sn_state = STATE_SELF_CHECK;
        /* code */
    }
    else
    {
        sn_state = STATE_SN_SET;
    }   
		SEGGER_RTT_printf(0,"sn_state =%d\r\n",sn_state);
		//test 
		
    while(1)
    {
        switch(sn_state)
        {
            case STATE_INIT:
              
                break;
            case STATE_SN_SET: {
								sn_set();
//								 uint32_t sn_value = 999994;
            
//								// 鏂板3锛氳浆鎹负灏忕妯″紡鐨?瀛楄妭瀛樺偍
//								uint8_t sn_bytes[4] = {
//										(uint8_t)(sn_value & 0xFF),         // 浣庝綅瀛楄妭
//										(uint8_t)((sn_value >> 8) & 0xFF),
//										(uint8_t)((sn_value >> 16) & 0xFF),
//										(uint8_t)((sn_value >> 24) & 0xFF)  // 楂樹綅瀛楄妭
//								};	
//								for(uint8_t i = 0; i < 4; i++)
//                {
//                    if(flash_write_byte(eraser_addr+i, sn_bytes[i],1) != Ok)
//                    {
//                        #if DEBUG_MODE
//                        AT_SendCmd("sn write error");
//                        #endif
//                        break;
//                    }
//                }
								sn_state = STATE_SELF_CHECK;
								SEGGER_RTT_printf(0,"sn set complete!\r\n");
                break;
            }
            case STATE_SELF_CHECK:
							{
                // 鎵ц鑷娴佺▼
                key_detect_flag = KEY_DECTET_PRESS;		
//								SEGGER_RTT_printf(0, "33\n");
								
								if(key_press_valid )
							{
								key_press_valid = FALSE;
								// 1. 棣栨鎸夐敭锛氬惎鍔ㄥ畾鏃跺櫒锛岃鏃朵粠杩欎竴鍒诲紑濮?
								if(!key_confirm_timer_started)
								{
										key_confirm_timer_started = 1; // 鍚姩10绉掕鏃?
										key_confirm_timer = 0;            // 棣栨鎸夐敭锛岃鏃舵竻闆讹紙璧风偣锛?
										SEGGER_RTT_printf(0, "time start 锛乗n");
								}

								// 2. 鎸夐敭璁℃暟+1
								key_confirm_count++;
								SEGGER_RTT_printf(0, "宸叉寜涓?d娆★紝杩橀渶%d娆★紙鍓╀綑鏃堕棿锛?d绉掞級\n", 
																	key_confirm_count, 
																	CONFIRM_PRESS_TIMES - key_confirm_count,
																	(CONFIRM_TIMEOUT - key_confirm_timer)/100); // 鍓╀綑绉掓暟

								// 3. 娓呴櫎鎸夐敭婵€娲绘爣璁帮紝绛夊緟涓嬩竴娆℃寜閿?
//								key_activation = 0;
                if(key_confirm_count >= 3) {
										  key_confirm_count = 0;                // 閲嶇疆鎸夐敭璁℃暟
											key_confirm_timer = 0;                // 閲嶇疆璁℃椂
											key_confirm_timer_started = 0;    // 鍋滄瀹氭椂鍣?
											key_detect_flag = KEY_DECTET_IDLE;    // 鍏抽棴鎸夐敭妫€娴?
										
                    #if 1
										SEGGER_RTT_printf(0, "before self check\n");
                    #endif 
										self_check();
										if(self_check_status == 1)
										{
											
												flash_write_byte(eraser_addr+30,activation_flag,1);
												pwr_down();
												pwr_en(0);
												sn_state = STATE_WAIT_ACTIVATION;	
												key_int1_init();
												activation_success = 0;
												self_check_hardware_deinit();
												goto_sleep_mode();
												Sysctrl_SetPeripheralGate(SysctrlPeripheralBaseTim, TRUE); //Base Timer澶栬鏃堕挓浣胯兘
												
										}
									 else
									 {
											 Gpio_SetIO(LED_PROT,LED_PIN);
											 sn_state =  STATE_INIT;
											 pwr_down();
											 pwr_en(0);
									 }
                } 
							}
                break;
							}
            case STATE_WAIT_ACTIVATION:
								{
                // ... 鍘熸湁婵€娲诲垽鏂唬鐮佷繚鎸佷笉鍙?...	
									key_detect_flag = KEY_DECTET_PRESS;	
									if(!key_confirm_timer_started) 
									{
											key_confirm_timer_started = 1; // 寮鸿璁╁悗鍙板紑濮嬫暟鏁?
											key_confirm_timer = 0; 
											SEGGER_RTT_printf(0, "Safety Timer Force Start!\n");
									}
									if(key_confirm_timer >= (CONFIRM_TIMEOUT-10))
										{
													key_confirm_count = 0;                // 閲嶇疆鎸夐敭璁℃暟
													key_confirm_timer = 0;                // 閲嶇疆璁℃椂
													key_confirm_timer_started = 0;    // 鍋滄瀹氭椂鍣?
													key_detect_flag = KEY_DECTET_IDLE;    // 鍏抽棴鎸夐敭妫€娴?
													self_check_hardware_deinit();
													goto_sleep_mode();
													Sysctrl_SetPeripheralGate(SysctrlPeripheralBaseTim, TRUE); //Base Timer澶栬鏃堕挓浣胯兘
										}
									if(key_press_valid )
									{
										key_press_valid = FALSE;
										// 1. 棣栨鎸夐敭锛氬惎鍔ㄥ畾鏃跺櫒锛岃鏃朵粠杩欎竴鍒诲紑濮?
										if(!key_confirm_timer_started)
										{
												key_confirm_timer_started = 1; // 鍚姩10绉掕鏃?
												key_confirm_timer = 0;            // 棣栨鎸夐敭锛岃鏃舵竻闆讹紙璧风偣锛?
												SEGGER_RTT_printf(0, "time start 锛乗n");
										}

										// 2. 鎸夐敭璁℃暟+1
										key_confirm_count++;
										SEGGER_RTT_printf(0, "宸叉寜涓?d娆★紝杩橀渶%d娆★紙鍓╀綑鏃堕棿锛?d绉掞級\n", 
																			key_confirm_count, 
																			CONFIRM_PRESS_TIMES - key_confirm_count,
																			(CONFIRM_TIMEOUT - key_confirm_timer)/100); // 鍓╀綑绉掓暟
										
										
									
											
											
										// 3. 娓呴櫎鎸夐敭婵€娲绘爣璁帮紝绛夊緟涓嬩竴娆℃寜閿?
		//								key_activation = 0;
										if(key_confirm_count >= 3) {
													key_confirm_count = 0;                // 閲嶇疆鎸夐敭璁℃暟
													key_confirm_timer = 0;                // 閲嶇疆璁℃椂
													key_confirm_timer_started = 0;    // 鍋滄瀹氭椂鍣?
													key_detect_flag = KEY_DECTET_IDLE;    // 鍏抽棴鎸夐敭妫€娴?
													sn_state = STATE_DONE;
													flash_write_byte(eraser_addr+40,activation_data,1);
												
										
										} 
									}	
									
//								static uint16_t start_cnt = 0;
//								SEGGER_RTT_printf(0, "88\n");
//								 if(activation_success >= 3)
//                {
//										
//                    sn_state = STATE_DONE;
//										flash_write_byte(eraser_addr+40,activation_data,1);
//                    activation_success = 0;
//                    start_cnt = 0;
//                }
//                if(start_cnt >= 50)  //5s
//                {
//           
//										start_cnt = 0;
//										activation_success = 0;
//										goto_sleep_mode();
//                    
//                }
//         
//                delay1ms(100);
//                start_cnt ++;
							
								break;
							}
            case STATE_DONE:
								led_init();
								Sysctrl_SetPeripheralGate(SysctrlPeripheralBaseTim, TRUE); //Base Timer澶栬鏃堕挓浣胯兘
								led_start();
                return; // 娴佺▼瀹屾垚閫€鍑?

            default:
						{
                sn_state = STATE_INIT;
                break;
						}
        }
        

    }
}



/******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/


void sn_set(void)
{
    char *time_str;
    while(1)
    {
        // ... 宸插瓨鍦ㄧ殑浠ｇ爜 ...

        if(Wait_For_Easy_Response("SN=",TIMEOUT_SHORT))
        {
            #if 1
            AT_SendCmd("sn get");
            #endif
            time_str = strstr(rx_buffer, "SN=") + 3; 
            
            // 淇敼1锛氱洿鎺ュ鍒跺師濮婣SCII鏁版嵁
            memcpy(sn_data, time_str, 6);
            get_sn_data = 1;

            // 鏂板2锛氳浆鎹SCII鍒版暣鏁?
            uint32_t sn_value = 0;
            for(uint8_t i=0; i<6; i++) {
                sn_value = sn_value * 10 + (sn_data[i] - '0');
            }
            
            // 鏂板3锛氳浆鎹负灏忕妯″紡鐨?瀛楄妭瀛樺偍
            uint8_t sn_bytes[4] = {
                (uint8_t)(sn_value & 0xFF),         // 浣庝綅瀛楄妭
                (uint8_t)((sn_value >> 8) & 0xFF),
                (uint8_t)((sn_value >> 16) & 0xFF),
                (uint8_t)((sn_value >> 24) & 0xFF)  // 楂樹綅瀛楄妭
            };

            if(get_sn_data)
            {
                eraser_addr = ERASE_ADDR_IS;
                Flash_SectorErase(eraser_addr);
                
                // 淇敼4锛氬啓鍏?瀛楄妭灏忕鏁版嵁
                for(uint8_t i = 0; i < 4; i++)
                {
                    if(flash_write_byte(eraser_addr+i, sn_bytes[i],1) != Ok)
                    {
                        #if DEBUG_MODE
                        AT_SendCmd("sn write error");
                        #endif
                        break;
                    }
                }
                
                // 淇敼5锛氶獙璇?瀛楄妭鏁版嵁
                uint8_t verify_pass = 1;
                for(uint8_t i=0; i<4; i++) {
                    if(*((volatile uint8_t*)(eraser_addr+i)) != sn_bytes[i]) {
                        verify_pass = 0;
                        break;
                    }
                }
								if(verify_pass == 1)
								{
									sn_state = STATE_SELF_CHECK;
									return;
								}
                // ... 鍚庣画瀛樺湪鐨勪唬鐮?...
            }
        }
				 AT_SendCmd("Wait write SN");
				delay1ms(200);
        
        // ... 宸插瓨鍦ㄧ殑浠ｇ爜 ...
    }
}
uint8_t self_check(void)
{   
    led_blink = 1;
		SystemClock_Init();
    UartInit(115200u);    
    sport_dectet();	
		SEGGER_RTT_printf(0,"sport_dectet power\r\n");
		pwr_en(1);
		delay1ms(1000);
		SEGGER_RTT_printf(0,"start power\r\n");
    pwr_up();
		SEGGER_RTT_printf(0,"pwr_up\r\n");
		delay1ms(5000);
		close_echo();
		if(Check_SIM_Ready() == STATUS_OK)
				{
					Get_ICCID(g_iccid,sizeof(g_iccid));   
					
                    // 淇敼鐜版湁璋冭瘯杈撳嚭璇彞
					if(Get_CSQ(&g_rssi,&g_ber)== STATUS_OK)
					{
						SEGGER_RTT_printf(0,"get rssi succees!\r\n");
					}
                    #if DEBUG_MODE
					SEGGER_RTT_printf(0,"Check_SIM_Ready\r\n");
                    #endif
				}
				else
			{
				#if DEBUG_MODE
				SEGGER_RTT_printf(0,"Check_SIM_Ready falied\r\n");
									#endif
			}
		Set_Http_Content_Type(4);
		

    Get_IMEI(g_imei);

    if(get_loca_data() == STATUS_OK)
    {
        if(get_cw_gps_data() != STATUS_OK)
        {
           cell_data1.latitude = 0;
           cell_data1.longitude = 0;
        }
        else
        {
            cell_data1.latitude = Gps_Data.latitude;
            cell_data1.longitude = Gps_Data.longitude;
        }
    Gps_Data.latitude = 0;
    Gps_Data.longitude = 0;
    }
    if(Get_Gnss_Message() != STATUS_OK)
    {
        gnss_data1.latitude = 0;
        gnss_data1.longitude = 0;
    }
    else
    {
        gnss_data1.latitude = Gps_Data.latitude;
        gnss_data1.longitude = Gps_Data.longitude;
//        gnss_data1.latitude = 31.35971;
//        gnss_data1.longitude = 121.53546;

    }
    Gps_Data.latitude = 0;
    Gps_Data.longitude = 0;
    tem_test_en();
    adc_get_data();

    if(Http_post_self_check(ConvertToTimestamp()) == STATUS_OK)
    {
			
        led_blink = 0;
				Gpio_WriteOutputIO(LED_PROT,LED_PIN,0);
				SEGGER_RTT_printf(0, "st ok\n");
    }
    else
    {
        led_blink = 0;
				Gpio_WriteOutputIO(LED_PROT,LED_PIN,1);
        //led甯镐寒
				SEGGER_RTT_printf(0, "st failed\n");

    }

    return STATUS_OK;


}

uint8_t Get_IMEI(char *imeiBuffer)
{
    // 鍙傛暟鏈夋晥鎬ф鏌?
    if(imeiBuffer == NULL) {
        return STATUS_FAILED;
    }
    AT_SendCmd("AT+CGSN=1");
    if(Wait_For_Easy_Response("+CGSN:", TIMEOUT_SHORT)) 
    {
        char *p = strstr(rx_buffer, "+CGSN:");
        if(p) 
        {
            p += 8; // 璺宠繃鍓嶇紑
            strncpy(imeiBuffer, p, 15);
            imeiBuffer[15] = '\0'; // 纭繚缁堟
            return STATUS_OK;
        }
    }
    return STATUS_FAILED;
}


// 淇敼鍝嶅簲澶勭悊鍑芥暟
uint8_t Read_Http_Response_self_check(void)
{
    Clear_RX_Buffer();
    AT_SendCmd("AT+QHTTPREAD=256"); // 澧炲姞璇诲彇闀垮害
    if (Wait_For_Response("READ:", HTTP_TIMEOUT_COUNT))
    {
        SEGGER_RTT_printf(0, "raw_data = %s\n",rx_buffer);
//				status  = rx_buffer[20]-0x30;
//				SEGGER_RTT_printf(0, "status: %d\n", status);
//        return status;
					 return(Parse_Server_Status(rx_buffer));
//					return  STATUS_FAILED;
    }
    
    Clear_RX_Buffer();
    return STATUS_OK;
}


uint8_t Parse_Server_Status(const char *response)
{
    // 澧炲姞绌烘寚閽堜繚鎶?
    const char *json_start = strchr(response, '{');
    if (json_start == NULL) {
        return STATUS_FAILED; // 鏈壘鍒癑SON閮ㄥ垎
    }
    
    // 2. 鏌ユ壘JSON閮ㄥ垎鐨勭粨鏉熶綅缃?
    const char *json_end = strchr(json_start, '}');
    if (json_end == NULL) {
        return STATUS_FAILED; // JSON鏍煎紡涓嶅畬鏁?
    }
    
    // 3. 鎻愬彇JSON瀛愬瓧绗︿覆
    size_t json_len = json_end - json_start + 1;
    char json_str[json_len + 1];
    strncpy(json_str, json_start, json_len);
    json_str[json_len] = '\0'; // 纭繚瀛楃涓茬粓姝?
    
    // 4. 鍦↗SON涓煡鎵?status"瀛楁
    const char *status_key = "\"status\":\"";
    char *status_start = strstr(json_str, status_key);
    if (status_start == NULL) {
        return STATUS_FAILED; // 鏈壘鍒皊tatus瀛楁
    }
    
    // 5. 瀹氫綅鐘舵€佸€艰捣濮嬩綅缃?
    status_start += strlen(status_key); // 绉诲姩鍒板€肩殑浣嶇疆
    
    // 6. 鏌ユ壘鐘舵€佸€肩粨鏉熶綅缃?
    char *status_end = strchr(status_start, '\"');
    if (status_end == NULL) {
        return STATUS_FAILED; // 鍊兼牸寮忛敊璇?
    }
    
    // 7. 鎻愬彇鐘舵€佹暟瀛楀瓧绗︿覆
    size_t status_len = status_end - status_start;
    char status_str[status_len + 1];
    strncpy(status_str, status_start, status_len);
    status_str[status_len] = '\0'; // 纭繚瀛楃涓茬粓姝?
    

    // 8. 浣跨敤鑷畾涔夊嚱鏁拌浆鎹负鏁存暟
    self_check_status =  atoi(status_str);
    SEGGER_RTT_printf(0, "status: %d\n", self_check_status);
    return STATUS_OK;
}

void led_init(void)
{
		stc_gpio_cfg_t stcGpioCfg;
    
    ///< 鎵撳紑GPIO澶栬鏃堕挓闂ㄦ帶
    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE); 
    
    ///< 绔彛鏂瑰悜閰嶇疆->杈撳嚭(鍏跺畠鍙傛暟涓庝互涓婏紙杈撳叆锛夐厤缃弬鏁颁竴鑷?
    stcGpioCfg.enDir = GpioDirOut;
    ///< 绔彛涓婁笅鎷夐厤缃?>涓嬫媺
    stcGpioCfg.enPu = GpioPuDisable;
    stcGpioCfg.enPd = GpioPdEnable;
    
    ///< LED鍏抽棴
    Gpio_ClrIO(LED_PROT, LED_PIN);
    
    ///< GPIO IO LED绔彛鍒濆鍖?
    Gpio_Init(LED_PROT, LED_PIN, &stcGpioCfg);
}

 void key_int1_init(void)
 {
		stc_gpio_cfg_t stcGpioCfg;
    
    ///< 鎵撳紑GPIO澶栬鏃堕挓闂ㄦ帶
    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE);
    
    ///< 绔彛鏂瑰悜閰嶇疆->杈撳叆
    stcGpioCfg.enDir = GpioDirIn;
    ///< 绔彛椹卞姩鑳藉姏閰嶇疆->楂橀┍鍔ㄨ兘鍔?
    stcGpioCfg.enDrv = GpioDrvL;
    ///< 绔彛涓婁笅鎷夐厤缃?>涓婃媺
    stcGpioCfg.enPu = GpioPuEnable;
    stcGpioCfg.enPd = GpioPdDisable;
    ///< 绔彛寮€婕忚緭鍑洪厤缃?>寮€婕忚緭鍑哄叧闂?
    stcGpioCfg.enOD = GpioOdDisable;
    ///< 绔彛杈撳叆/杈撳嚭鍊煎瘎瀛樺櫒鎬荤嚎鎺у埗妯″紡閰嶇疆->AHB
    stcGpioCfg.enCtrlMode = GpioAHB;
    ///< GPIO IO USER KEY鍒濆鍖?
    Gpio_Init(POWER_ON_PORT, POWER_ON_PIN, &stcGpioCfg); 
	 
		 ///< 鎵撳紑骞堕厤缃甎SER KEY涓轰笅闄嶆部涓柇
    Gpio_EnableIrq(POWER_ON_PORT, POWER_ON_PIN, GpioIrqFalling);
    ///< 浣胯兘绔彛PORTA绯荤粺涓柇
    EnableNvic(PORTB_IRQn, IrqLevel3, TRUE);
    #if 1
    SEGGER_RTT_printf(0, "ket int init!\n");
    #endif
    
 }

 uint8_t Set_Http_Content_Type(uint8_t u8Type)
{
    char cmd_buffer[64];

    snprintf(cmd_buffer, sizeof(cmd_buffer),
             "AT+QHTTPCFG=\"contenttype\",%d", u8Type);

    Clear_RX_Buffer();
    AT_SendCmd(cmd_buffer);

    if (Wait_For_Easy_Response("OK", TIMEOUT_COUNT)) {
        #if DEBUG_MODE
        SEGGER_RTT_printf(0, "ContentType set to %d OK\r\n", u8Type);
        #endif
        Clear_RX_Buffer();
        return STATUS_OK;
    }

    #if DEBUG_MODE
    SEGGER_RTT_printf(0, "Set ContentType failed, raw=%s\r\n", rx_buffer);
    #endif
    Clear_RX_Buffer();
    return STATUS_FAILED;
}void led_start(void)
{
	if(frist_activation)
	{
		led_blink = 1;
	}
}
void led_stop(void)
{
	if(frist_activation | other_wakeup)
	{
		led_blink = 0;
		frist_activation = 0;
		other_wakeup = 0;
		Gpio_ClrIO(LED_PROT,LED_PIN);
		is_in_normal_state = 1;
		BtTimerT_deinit();
		
	}
	led_blink = 0;
}
void adc_io_init(void)
{
		Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE);  
    Gpio_SetAnalogMode(GpioPortA, GpioPin2);        //PA00 (AIN0)
    Gpio_SetAnalogMode(GpioPortA, GpioPin6);        //PA06 (AIN6)
}
