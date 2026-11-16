#include "hardware.h"
#include "adc.h"
#include "ddl.h"
#include "gpio.h"
#include "SEGGER_RTT.h"
#include <stdlib.h>
#include "i2c.h"
#include  "math.h"
#include "uart.h"
#include "bgr.h"

#define M_PI 3.1415926535898
#define SC7A20TR_FILTERING_TICK			8			/* 平锟斤拷锟剿诧拷锟斤拷锟斤拷 */
#define SC7A20TR_Angular_Difference		5			/* 锟角度诧拷值 */
float battery = 0;
float tem_res = 0;

static uint16_t u16ScanResult[8];
extern float gyroscope_data;
extern uint8_t is_move;

#define SC7A20TR_SDA_PORT	2
#define SC7A20TR_SDA_PIN	5
#define SC7A20TR_SCL_PORT	2
#define SC7A20TR_SCL_PIN	6
uint8_t running_flag = 0;



 ///< ADC中断服务函数
void Adc_IRQHandler(void)
{    
//    if(TRUE == Adc_GetIrqStatus(AdcMskIrqJqr))
//    {
//        Adc_ClrIrqStatus(AdcMskIrqJqr);
//        
//   
//    }
}


uint8_t adc_init(void)
{
	
	  stc_adc_cfg_t              stcAdcCfg;

    DDL_ZERO_STRUCT(stcAdcCfg);
	
		Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE);
    
    Gpio_SetAnalogMode(GpioPortA, GpioPin2);        //PA02 (AIN2) battery
    Gpio_SetAnalogMode(GpioPortA, GpioPin6);        //PA06 (AIN6)  tem
	

    Sysctrl_SetPeripheralGate(SysctrlPeripheralAdcBgr, TRUE); 
    Bgr_BgrEnable();        ///< 开启BGR
		delay1ms(10);
    ///< ADC 初始化配置
    stcAdcCfg.enAdcMode         = AdcScanMode;              ///<采样模式-扫描
    stcAdcCfg.enAdcClkDiv       = AdcMskClkDiv1;            ///<采样分频-1
    stcAdcCfg.enAdcSampCycleSel = AdcMskSampCycle12Clk;      ///<采样周期数-12
    stcAdcCfg.enAdcRefVolSel    = AdcMskRefVolSelAVDD;      ///<参考电压选择-VCC
    stcAdcCfg.enAdcOpBuf        = AdcMskBufDisable;         ///<OP BUF配置-关
    stcAdcCfg.enInRef           = AdcMskInRefDisable;       ///<内部参考电压使能-关
    stcAdcCfg.enAdcAlign        = AdcAlignRight;               ///<转换结果对齐方式-右
    Adc_Init(&stcAdcCfg);
		
		 stc_adc_jqr_cfg_t          stcAdcJqrCfg;
    
    DDL_ZERO_STRUCT(stcAdcJqrCfg);
    
    ///< 插队扫描模式功能及通道配置
    ///< 注意：扫描模式下，当配置转换次数为n时，转换通道的配置范围必须为[JQRCH(0)MUX,JQRCH(n-1)MUX]
    stcAdcJqrCfg.bJqrDmaTrig = FALSE;
    stcAdcJqrCfg.u8JqrCnt    = 2;
    Adc_JqrModeCfg(&stcAdcJqrCfg);

    Adc_CfgJqrChannel(AdcJQRCH0MUX, AdcExInputCH2);
    Adc_CfgJqrChannel(AdcJQRCH1MUX, AdcExInputCH6);
    
//    ///< ADC 中断使能
//    Adc_EnableIrq();
//    EnableNvic(ADC_IRQn, IrqLevel3, TRUE);
    
    ///< 启动插队扫描采样
//    Adc_JQR_Start();
	
		
		
		return Ok;
			

}
void adc_get_data(void)
{
	
			uint16_t battary_adc_value = 0;
			uint16_t tem_adc_value = 0;
			uint32_t u32Tick = 0;
			uint32_t u32Timeout = 0xffff;
			adc_init();
			SEGGER_RTT_printf(0,"adc_init\r\n");
			delay1ms(100);





			
    // 步骤3：启动插队扫描转换
		Adc_JQR_Start();
		SEGGER_RTT_printf(0,"Adc_JQR_Start\r\n");

    // 步骤4：轮询等待转换完成（带超时）
    while (FALSE == Adc_GetIrqStatus(AdcMskIrqJqr))
    {
        if (u32Timeout > 0)
        {
            delay10us(1);  // 延时10us（驱动中已有的延时函数）
            u32Tick++;
            if (u32Tick >= u32Timeout)
            {
                Adc_JQR_Stop();  // 超时停止转换

						}
					}
			}
				SEGGER_RTT_printf(0,"Adc_GetIrqStatus\r\n");
    // 步骤5：读取转换结果
        battary_adc_value   = Adc_GetJqrResult(AdcJQRCH0MUX);
        tem_adc_value   = Adc_GetJqrResult(AdcJQRCH1MUX);
				battery = ((float)battary_adc_value/4096)*3.3*3;
				tem_res = ((float)tem_adc_value/4096)*3.3/(3.3-(float)tem_adc_value/4096*3.3)*10.0;
				SEGGER_RTT_printf(0,"tem_res is %d,battery is %d\r\n",battary_adc_value,tem_adc_value);
				SEGGER_RTT_printf(0,"tem_res is %f,battery is %f\r\n",tem_res,battery);  
				Adc_ClrIrqStatus(AdcMskIrqJqr);
				Adc_JQR_Stop();        
        close_adc();
        delay1ms(1000);
}
void close_adc(void)
{
	Bgr_BgrDisable();
	Adc_Disable();
	  if (Ok != Sysctrl_SetPeripheralGate(SysctrlPeripheralAdcBgr, FALSE))  //ADCBGR 锟斤拷锟斤拷时锟斤拷使锟斤拷
    {
        SEGGER_RTT_printf(0,"adc closed error");;
    }
//		SEGGER_RTT_printf(0,"ClkPeripheralAdcBgr is %d,\r\n",Clk_GetPeripheralGate(ClkPeripheralAdcBgr));
	
}
static void App_Gnss_Pwrup_Init(void)
{
    stc_gpio_cfg_t stcGpioCfg;
    
    ///< 打开GPIO外设时钟门控
    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE); 
    
    ///< 端口方向配置->输出(其它参数与以上（输入）配置参数一致)
    stcGpioCfg.enDir = GpioDirOut;
    ///< 端口上下拉配置->下拉
    stcGpioCfg.enPu = GpioPuDisable;
    stcGpioCfg.enPd = GpioPdEnable;
    
    ///< LED关闭
    Gpio_ClrIO(GNSS_PWRUP_PORT, GNSS_PWRUP_PIN);
    
    ///< GPIO IO LED端口初始化
    Gpio_Init(GNSS_PWRUP_PORT, GNSS_PWRUP_PIN, &stcGpioCfg);
    

}

static void App_Gnss_On_Init(void)
{
    stc_gpio_cfg_t stcGpioCfg;
    
    ///< 打开GPIO外设时钟门控
    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE); 
    
    ///< 端口方向配置->输出(其它参数与以上（输入）配置参数一致)
    stcGpioCfg.enDir = GpioDirOut;
    ///< 端口上下拉配置->下拉
    stcGpioCfg.enPu = GpioPuDisable;
    stcGpioCfg.enPd = GpioPdEnable;
    
    ///< LED关闭
    Gpio_ClrIO(GNSS_PWREN_PORT, GNSS_PWREN_PIN);
    
    ///< GPIO IO LED端口初始化
    Gpio_Init(GNSS_PWREN_PORT, GNSS_PWREN_PIN, &stcGpioCfg);
    

}
void pwr_en(uint8_t state)
{
		App_Gnss_On_Init();
		if(state == TRUE)
		{
			Gpio_SetIO(GNSS_PWREN_PORT,GNSS_PWREN_PIN);
		}
		else
		{
			Gpio_ClrIO(GNSS_PWREN_PORT, GNSS_PWREN_PIN);
		}
}




void pwr_on(void)
{
		App_Gnss_Pwrup_Init();
    Gpio_SetIO(GNSS_PWRUP_PORT,GNSS_PWRUP_PIN);
		delay1ms(100);
		Gpio_ClrIO(GNSS_PWRUP_PORT, GNSS_PWRUP_PIN);
		delay1ms(2500);
		Gpio_SetIO(GNSS_PWRUP_PORT,GNSS_PWRUP_PIN);
		/* Start UART probing immediately after releasing PWRKEY. */
}

void pwr_down(void)
{
		App_Gnss_Pwrup_Init();
    Gpio_ClrIO(GNSS_PWRUP_PORT, GNSS_PWRUP_PIN);
		delay1ms(3000);
		Gpio_SetIO(GNSS_PWRUP_PORT,GNSS_PWRUP_PIN);
		delay1ms(30000);
}

void pwr_up(void)
{
		pwr_on();
}
void gnss_work(void)
{

//		delay1ms(2500);
}
static void App_Tem_EN_Init(void)
{
    stc_gpio_cfg_t stcGpioCfg;
    
    ///< 打开GPIO外设时钟门控
    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE); 
    
    ///< 端口方向配置->输出(其它参数与以上（输入）配置参数一致)
    stcGpioCfg.enDir = GpioDirOut;
    ///< 端口上下拉配置->下拉
    stcGpioCfg.enPu = GpioPuDisable;
    stcGpioCfg.enPd = GpioPdEnable;
    
    ///< LED关闭
    Gpio_ClrIO(TEM_EN_PORT, TEM_EN_PIN);
    
    ///< GPIO IO LED端口初始化
    Gpio_Init(TEM_EN_PORT, TEM_EN_PIN, &stcGpioCfg);
    

}
void tem_test_en(void)
{
		App_Tem_EN_Init();
    Gpio_SetIO(TEM_EN_PORT,TEM_EN_PIN);
}

/* Private user code ---------------------------------------------------------*/


/******************************************************************************/
/*********************************I2C START************************************/
/******************************************************************************/




//------------------------------------------------------------------------------
// Copyright , 2017-2021 锟芥迹锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟狡硷拷锟斤拷锟睫癸拷司
// Filename    : drv_sc7a20.c
// Auther      : zhaoning
// Version     :
// Date : 2022-1-13
// Description : 锟斤拷准IIC协锟介传锟斤拷锟斤拷锟斤拷时为MSB锟斤拷式锟斤拷锟斤拷锟斤拷位锟斤拷前锟斤拷位锟节后，碉拷锟斤拷些锟斤拷锟斤拷为LSB锟斤拷式锟斤拷
//          
//          
// History     :
//     
//    1. Time         :  2022-1-13
//       Modificator  : zhaoning
//       Modification : Created
//    2.
// Others :
//------------------------------------------------------------------------------

// Includes ---------------------------------------------------------------------

//------------------------------------------------------------------------------
// Copyright , 2017-2021 锟芥迹锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟狡硷拷锟斤拷锟睫癸拷司
// Filename    : drv_sc7a20.c
// Auther      : zhaoning
// Version     :
// Date : 2022-1-13
// Description : 锟斤拷准IIC协锟介传锟斤拷锟斤拷锟斤拷时为MSB锟斤拷式锟斤拷锟斤拷锟斤拷位锟斤拷前锟斤拷位锟节后，碉拷锟斤拷些锟斤拷锟斤拷为LSB锟斤拷式锟斤拷
//          
//          
// History     :
//     
//    1. Time         :  2022-1-13
//       Modificator  : zhaoning
//       Modification : Created
//    2.
// Others :
//------------------------------------------------------------------------------

// Includes ---------------------------------------------------------------------

#include <math.h>
#include "i2c.h"  // HC32L110 I2C 头锟侥硷拷

// Private defines / typedefs ---------------------------------------------------

#define PI 3.1415926535898
#define DRV_SC7A20_I2C_NUM 0  // 锟斤拷锟斤拷使锟矫碉拷0锟斤拷 I2C 锟斤拷锟斤拷
#define DRV_SC7A20_I2C_SLAVE_ADDR 0x32  // 锟斤拷锟斤拷 SC7A20 锟斤拷 I2C 锟斤拷址为 0x1E

// Private variables ------------------------------------------------------------

// Public variables -------------------------------------------------------------

// Private functions prototypes -------------------------------------------------

// Functions --------------------------------------------------------------------

void delay(uint16_t u16cnt)
{
    uint16_t i;
    for(i = 0; i < u16cnt; i++)
    {
        __NOP();
    }
}



/**
 ******************************************************************************
 ** \brief  主机接收函数
 **
 ** \param u8Addr从机内存地址，pu8Data读数据存放缓存，u32Len读数据长度
 **
 ** \retval 读数据是否成功
 **
 ******************************************************************************/
//en_result_t I2C_MasterReadData(M0P_I2C_TypeDef* I2CX,uint8_t *pu8Data,uint32_t u32Len)
//{
//    en_result_t enRet = Error;
//    uint8_t u8i=0,u8State;
//    
//    I2C_SetFunc(I2CX,I2cStart_En);
//    
//    while(1)
//    {
//        while(0 == I2C_GetIrq(I2CX))
//        {;}
//        u8State = I2C_GetState(I2CX);
//        switch(u8State)
//        {
//            case 0x08:                                    //已发送起始条件，将发送SLA+R
//                I2C_ClearFunc(I2CX,I2cStart_En);
//                I2C_WriteByte(I2CX,SC7A20TR_WRITE_ADDRESS);//发送SLA+W
//                break;
//            case 0x18:                                    //已发送SLA+W,并接收到ACK
//                I2C_WriteByte(I2CX,0);                    //发送内存地址
//                break;
//            case 0x28:                                    //已发送数据，接收到ACK
//                I2C_SetFunc(I2CX,I2cStart_En);
//                break;
//            case 0x10:                                    //已发送重复起始条件
//                I2C_ClearFunc(I2CX,I2cStart_En);
//                I2C_WriteByte(I2CX,SC7A20TR_READ_ADDRESS);//读命令发送
//                break;
//            case 0x40:                                    //已发送SLA+R，并接收到ACK
//                if(u32Len>1)
//                {
//                    I2C_SetFunc(I2CX,I2cAck_En);
//                }
//                break;
//            case 0x50:                                    //已接收数据字节，并已返回ACK信号
//                pu8Data[u8i++] = I2C_ReadByte(I2CX);
//                if(u8i==u32Len-1)
//                {
//                    I2C_ClearFunc(I2CX,I2cAck_En);        //读数据时，倒数第二个字节ACK关闭
//                }
//                break;
//            case 0x58:                                    //已接收到最后一个数据，NACK已返回
//                pu8Data[u8i++] = I2C_ReadByte(I2CX);
//                I2C_SetFunc(I2CX,I2cStop_En);             //发送停止条件
//                break;    
//            case 0x38:                                    //在发送地址或数据时，仲裁丢失
//                I2C_SetFunc(I2CX,I2cStart_En);            //当总线空闲时发起起始条件
//                break;
//            case 0x48:                                    //发送SLA+R后，收到一个NACK
//                I2C_SetFunc(I2CX,I2cStop_En);
//                I2C_SetFunc(I2CX,I2cStart_En);
//                break;
//            default:                                      //其他错误状态，重新发送起始条件
//                I2C_SetFunc(I2CX,I2cStart_En);            //其他错误状态，重新发送起始条件
//                break;
//        }
//        I2C_ClearIrq(I2CX);                               //清除中断状态标志位
//        if(u8i==u32Len)                                   //数据全部读取完成，跳出while循环
//        {
//            break;
//        }
//    }
//    enRet = Ok;
//    return enRet;
//}

/**
 * @brief  SC7A20 专用复合读取函数
 * @param  u8RegAddr: 寄存器起始地址 (读多字节请务必 |0x80)
 * @param  pu8Data: 接收缓冲区
 * @param  u32Len: 读取长度
 */
en_result_t SC7A20_I2C_MasterRead(M0P_I2C_TypeDef* I2CX, uint8_t u8RegAddr, uint8_t *pu8Data, uint32_t u32Len)
{
    en_result_t enRet = Error;
    uint8_t u8i = 0, u8State;
    
    I2C_SetFunc(I2CX, I2cStart_En);
    
    while(1)
    {
        while(0 == I2C_GetIrq(I2CX));
        u8State = I2C_GetState(I2CX);
        switch(u8State)
        {
            case 0x08: // 已发送起始条件
                I2C_ClearFunc(I2CX, I2cStart_En);
                I2C_WriteByte(I2CX, SC7A20TR_WRITE_ADDRESS); // 发送从机写地址
                break;
            case 0x18: // 已接收到 SLA+W 的 ACK
                I2C_WriteByte(I2CX, u8RegAddr); // 【关键修正】：发送传入的寄存器地址
                break;
            case 0x28: // 已发送数据地址，接收到 ACK
                I2C_SetFunc(I2CX, I2cStart_En); // 发送重复起始 (Repeated Start)
                break;
            case 0x10: // 已发送重复起始
                I2C_ClearFunc(I2CX, I2cStart_En);
                I2C_WriteByte(I2CX, SC7A20TR_READ_ADDRESS); // 发送从机读地址
                break;
            case 0x40: // SLA+R 已发送，收到 ACK
                if(u32Len > 1) I2C_SetFunc(I2CX, I2cAck_En);
                break;
            case 0x50: // 读取中
                pu8Data[u8i++] = I2C_ReadByte(I2CX);
                if(u8i == u32Len - 1) I2C_ClearFunc(I2CX, I2cAck_En);
                break;
            case 0x58: // 读取完成
                pu8Data[u8i++] = I2C_ReadByte(I2CX);
                I2C_SetFunc(I2CX, I2cStop_En);
                break;
            case 0x38: case 0x48:
                I2C_SetFunc(I2CX, I2cStart_En);
                break;
        }
        I2C_ClearIrq(I2CX);
        if(u8i == u32Len) break;
    }
    return Ok;
}

/**
 ******************************************************************************
 ** \brief  主机发送函数
 **
 ** \param u8Addr从机内存地址，pu8Data写数据，u32Len写数据长度
 **
 ** \retval 写数据是否成功
 **
 ******************************************************************************/
en_result_t I2C_MasterWriteData(M0P_I2C_TypeDef* I2CX,uint8_t *pu8Data,uint32_t u32Len)
{
    en_result_t enRet = Error;
    uint8_t u8i=0,u8State;
    I2C_SetFunc(I2CX,I2cStart_En);
    while(1)
    {
        while(0 == I2C_GetIrq(I2CX))
        {;}
        u8State = I2C_GetState(I2CX);
        switch(u8State)
        {
            case 0x08:                                 ///已发送起始条件
                I2C_ClearFunc(I2CX,I2cStart_En);
                I2C_WriteByte(I2CX,SC7A20TR_WRITE_ADDRESS);  ///从设备地址发送
                break;
            case 0x18:                                 ///已发送SLA+W，并接收到ACK
            case 0x28:                                 ///上一次发送数据后接收到ACK
                I2C_WriteByte(I2CX,pu8Data[u8i++]);
                break;
            case 0x20:                                 ///上一次发送SLA+W后，收到NACK
            case 0x38:                                 ///上一次在SLA+读或写时丢失仲裁
                I2C_SetFunc(I2CX,I2cStart_En);         ///当I2C总线空闲时发送起始条件
                break;
            case 0x30:                                 ///已发送I2Cx_DATA中的数据，收到NACK，将传输一个STOP条件
                I2C_SetFunc(I2CX,I2cStop_En);          ///发送停止条件
                break;
            default:
                break;
        }            
        if(u8i>u32Len)
        {
            I2C_SetFunc(I2CX,I2cStop_En);              ///此顺序不能调换，出停止条件
            I2C_ClearIrq(I2CX);
            break;
        }
        I2C_ClearIrq(I2CX);                            ///清除中断状态标志位
    }
    enRet = Ok;
    return enRet;
}
/**
  * Function    : drv_sc7a20_i2c_init
  * Description : 锟斤拷始锟斤拷使锟矫的碉拷0锟斤拷 i2c 锟斤拷锟斤拷
  * Input       : 
  *               
  * Output      : 
  * Return      : 0 锟缴癸拷锟斤拷-1 失锟斤拷
  * Auther      : zhaoning
  * Others      : 
  **/
int drv_sc7a20_i2c_init(void)
{
		stc_gpio_cfg_t stcGpioCfg;
    
    DDL_ZERO_STRUCT(stcGpioCfg);
	
		stc_i2c_cfg_t stcI2cCfg;
    
    DDL_ZERO_STRUCT(stcI2cCfg);                            ///< 初始化结构体变量的值为0
    
    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio,TRUE);   //开启GPIO时钟门控 
    
    stcGpioCfg.enDir = GpioDirOut;                           ///< 端口方向配置->输出    
    stcGpioCfg.enOD = GpioOdEnable;                          ///< 开漏输出
    stcGpioCfg.enPu = GpioPuEnable;                          ///< 端口上拉配置->使能
    stcGpioCfg.enPd = GpioPdDisable;                         ///< 端口下拉配置->禁止
    stcGpioCfg.bOutputVal = TRUE;
    
    Gpio_Init(GpioPortB,GpioPin6,&stcGpioCfg);               ///< 端口初始化
    Gpio_Init(GpioPortB,GpioPin7,&stcGpioCfg);
    
    Gpio_SetAfMode(GpioPortB,GpioPin6,GpioAf1);              ///< 配置PB06为SCL
    Gpio_SetAfMode(GpioPortB,GpioPin7,GpioAf1);              ///< 配置PB07为SDA
	

    
    Sysctrl_SetPeripheralGate(SysctrlPeripheralI2c0,TRUE); ///< 开启I2C0时钟门控
    
    stcI2cCfg.u32Pclk = Sysctrl_GetPClkFreq();             ///< 获取PCLK时钟
    stcI2cCfg.u32Baud = 100000;                           ///< 100KHz
    stcI2cCfg.enMode = I2cMasterMode;                      ///< 主机模式
    stcI2cCfg.u8SlaveAddr = SC7A20TR_WRITE_ADDRESS;                          ///< 从地址，主模式无效
    stcI2cCfg.bGc = FALSE;                                 ///< 广播地址应答使能关闭
    I2C_Init(M0P_I2C0,&stcI2cCfg);                         ///< 模块初始化
	
		
//    stc_i2c_config_t stcI2cCfg;
////    stc_clk_config_t stcCfg;
//	
////    DDL_ZERO_STRUCT(stcCfg);
//    DDL_ZERO_STRUCT(stcI2cCfg);
//    //时锟接诧拷锟斤拷锟斤拷锟斤拷
////    stcCfg.enClkSrc = ClkXTH;
////    stcCfg.enHClkDiv = ClkDiv1;
////    stcCfg.enPClkDiv = ClkDiv1;
////    Clk_Init(&stcCfg);

//    Clk_SetPeripheralGate(ClkPeripheralI2c,TRUE);
//    
//    stcI2cCfg.enFunc = I2cBaud_En;
//    stcI2cCfg.u8Tm = 0x04;//100K锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 4M/8/U8TM+1
//    stcI2cCfg.pfnI2cCb = NULL;
//    stcI2cCfg.bTouchNvic = FALSE;
//    if(stcI2cCfg.bTouchNvic == TRUE)
//	{
//		EnableNvic(I2C_IRQn,3,TRUE);
//	}
//    I2C_DeInit();
//    
//    I2C_Init(&stcI2cCfg);
//    
//    Clk_SetPeripheralGate(ClkPeripheralGpio,TRUE);
//    Gpio_SetIO(2, 5, TRUE);
//    Gpio_SetIO(2, 6, TRUE);    
//    Gpio_InitIOExt(2,5,GpioDirOut,FALSE,FALSE,TRUE,FALSE);   
//    Gpio_InitIOExt(2,6,GpioDirOut,FALSE,FALSE,TRUE,FALSE);
//    
//    Gpio_SetFunc_I2CDAT_P25(); 
//    Gpio_SetFunc_I2CCLK_P26();
//    
//    I2C_SetFunc(I2cHlm_En);
//    I2C_SetFunc(I2cMode_En);

    return 0;
}

/**
  * Function    : drv_sc7a20_i2c_read
  * Description : 锟斤拷sc7a20锟侥寄达拷锟斤拷锟斤拷锟斤拷
  * Input       : addr   锟侥达拷锟斤拷锟斤拷址
  *               data   锟斤拷取锟侥寄达拷锟斤拷值
  * Output      : 
  * Return      : 
  * Auther      : zhaoning
  * Others      : 
  **/
//int drv_sc7a20_i2c_read(unsigned char *addr, unsigned char *data)
//{
//    uint8_t u8SendData[1] = { *addr };
//    uint8_t u8RecvData[1] = { 0 };
//	
//		//< 启动I2C数据传输
//    if (I2C_MasterWriteData(M0P_I2C0,u8SendData, 1) != Ok)
//    {
//        return -1;
//    }
//    if (I2C_MasterReadData(M0P_I2C0,u8RecvData, 1) != Ok)
//    {
//        return -1;
//    }
////		 SEGGER_RTT_printf(0, "u8RecvData = %d",*u8RecvData);
//    *data = u8RecvData[0];
//		
//    return 0;
//}

int drv_sc7a20_i2c_read(unsigned char *addr, unsigned char *data)
{
    uint8_t u8RecvData[1] = { 0 };
	
    // 直接调用修正后的复合读取函数，传入 *addr
    if (SC7A20_I2C_MasterRead(M0P_I2C0, *addr, u8RecvData, 1) != Ok)
    {
        return -1;
    }

    *data = u8RecvData[0];
    return 0;
}

/**
  * Function    : drv_sc7a20_i2c_write
  * Description : 写sc7a20锟侥寄达拷锟斤拷
  * Input       : addr   锟侥达拷锟斤拷锟斤拷址
  *               data   写锟斤拷拇锟斤拷锟街?
  * Output      : 
  * Return      : 
  * Auther      : zhaoning
  * Others      : 
  **/
int drv_sc7a20_i2c_write(unsigned char *addr, unsigned char *data)
{
    uint8_t u8SendData[2] = { *addr, *data };
    if (I2C_MasterWriteData(M0P_I2C0,u8SendData, 2) != Ok)
    {
        return -1;
    }
    return 0;
}

/**
  * Function    : drv_sc7a20_get_acc
  * Description : 锟斤拷取锟斤拷锟斤拷锟斤拷俣锟斤拷锟街碉拷锟斤拷锟斤拷锟?2bit
  * Input       : pXa    X锟斤拷锟斤拷俣却娲拷锟街革拷锟?
  *               pYa    Y锟斤拷锟斤拷俣却娲拷锟街革拷锟?
  *               pZa    Z锟斤拷锟斤拷俣却娲拷锟街革拷锟?
  * Output      : 
  * Return      : 
  * Auther      : zhaoning
  * Others      : 
  **/
uint8_t drv_sc7a20_get_acc(int16_t* pXa, int16_t* pYa, int16_t* pZa)
{
    uint8_t buff[6] = {0};
    uint8_t i = 0;
    unsigned char RegAddr = 0;

    for(i = 0; i < 6; i++)
    {
        RegAddr = 0X28 + i;
        if (drv_sc7a20_i2c_read(&RegAddr, &buff[i]) != 0)
        {
						SEGGER_RTT_printf(0,"i2c read error at reg 0x%x\r\n", RegAddr);
            return FALSE;
        }
				for(volatile int d=0; d<200; d++); // 简单延时
//				SEGGER_RTT_printf(0,"buff =%d\r\n",buff[i]); 
    }
//		SEGGER_RTT_printf(0, "Raw Buff [0-5]: %02X %02X | %02X %02X | %02X %02X\n", 
//                      buff[0], buff[1], buff[2], buff[3], buff[4], buff[5]);

    // X锟斤拷
    *pXa = buff[1];
    *pXa <<= 8;
    *pXa |= buff[0];
    *pXa >>= 4; // 取12bit锟斤拷锟斤拷

    // Y锟斤拷
    *pYa = buff[3];
    *pYa <<= 8;
    *pYa |= buff[2];
    *pYa >>= 4; // 取12bit锟斤拷锟斤拷

    // Z锟斤拷
    *pZa = buff[5];
    *pZa <<= 8;
    *pZa |= buff[4];
    *pZa >>= 4; // 取12bit锟斤拷锟斤拷

    return TRUE;
}

/**
  * Function    : drv_sc7a20_get_pitch
  * Description : 锟斤拷取锟斤拷锟斤拷锟斤拷
  * Input       : x    X锟斤拷锟斤拷俣锟?
  *               y    Y锟斤拷锟斤拷俣锟?
  *               z    Z锟斤拷锟斤拷俣锟?
  * Output      : 
  * Return      : 
  * Auther      : zhaoning
  * Others      : 
  **/
float drv_sc7a20_get_pitch(int16_t x, int16_t y, int16_t z)
{
    float pitch = 0;

    pitch = atan2((float)(0 - y), z) * 180 / PI; // 转锟斤拷为锟斤拷锟斤拷

    return pitch;
}

/**
  * Function    : drv_sc7a20_get_roll
  * Description : 锟斤拷取锟斤拷锟斤拷锟?
  * Input       : x    X锟斤拷锟斤拷俣锟?
  *               y    Y锟斤拷锟斤拷俣锟?
  *               z    Z锟斤拷锟斤拷俣锟?
  * Output      : 
  * Return      : 
  * Auther      : zhaoning
  * Others      : 
  **/
float drv_sc7a20_get_roll(int16_t x, int16_t y, int16_t z)
{
    float roll = 0;

    roll = atan2((float)(x), z) * 180 / PI; // 转锟斤拷为锟斤拷锟斤拷

    return roll;
}

/**
  * Function    : drv_sc7a20_get_angle
  * Description : 锟斤拷取Z锟斤拷锟斤拷牵锟斤拷锟斤拷锟剿斤拷锟侥角讹拷
  * Input       : pAngleZ    锟角度存储锟斤拷指锟斤拷
  *               
  * Output      : 
  * Return      : 
  * Auther      : zhaoning
  * Others      : 
  **/
uint8_t drv_sc7a20_get_angle(float* pAngleZ)
{
    double Angle = 0.0;
    int16_t Xa, Ya, Za;

    // 锟斤拷取锟斤拷锟劫讹拷锟斤拷锟斤拷
    if (drv_sc7a20_get_acc(&Xa, &Ya, &Za) == FALSE) 
		{
				SEGGER_RTT_printf(0,"get error\r\n"); 	
        return FALSE;
		}
		
//		SEGGER_RTT_printf(0,"Xa =%d,Ya=%d,Za=%d\r\n",Xa,Ya,Za); 
    Angle = atan(sqrt(Xa * Xa + Ya * Ya) / Za) * 180 / PI; 

    *pAngleZ = Angle;

    return TRUE;
}

/**
  * Function    : drv_sc7a20_get_angle
  * Description : 锟斤拷取Z锟斤拷锟斤拷牵锟斤拷锟斤拷锟剿斤拷锟侥角讹拷
  * Input       : pAngleZ    锟角度存储锟斤拷指锟斤拷
  *               
  * Output      : 
  * Return      : 
  * Auther      : zhaoning
  * Others      : 
  **/
//uint8_t drv_sc7a20_get_acce(float* acce)
//{
//    double all_acce = 0.0;
//    int16_t Xa, Ya, Za;
//		float xa_acce,ya_acce,za_acce;

//    // 锟斤拷取锟斤拷锟劫讹拷锟斤拷锟斤拷
//    if (drv_sc7a20_get_acc(&Xa, &Ya, &Za) == FALSE) 
//		{
//				SEGGER_RTT_printf(0,"get error\r\n"); 	
//        return FALSE;
//		}
//		xa_acce = (float)Xa/2048*2;
//		ya_acce = (float)Ya/2048*2;
//		za_acce = (float)Za/2048*2;
//		all_acce = sqrt(xa_acce*xa_acce+ya_acce*ya_acce+za_acce*za_acce);
//		
////		SEGGER_RTT_printf(0,"Xa =%d,Ya=%d,Za=%d\r\n",Xa,Ya,Za);  

//    *acce = all_acce;

//    return TRUE;
//}

uint8_t drv_sc7a20_get_acce_xyz(AcceData_t* acce)
{
    int16_t Xa, Ya, Za;

    // 获取原始数据
    if (drv_sc7a20_get_acc(&Xa, &Ya, &Za) == FALSE) 
    {
        SEGGER_RTT_printf(0,"get error\r\n");   
        return FALSE;
    }
    
    // 转换为g值，保留各轴独立数据
    acce->x = (float)Xa / 2048.0f * 2.0f;
    acce->y = (float)Ya / 2048.0f * 2.0f;
    acce->z = (float)Za / 2048.0f * 2.0f;
    return TRUE;
}




void SC7A20_Task(void)
{
    #define SAMPLE_INTERVAL  200    // 200ms采样间隔
    #define SAMPLE_TIMES     50     // 10秒总采样次数
//    #define VARIANCE_THRESH  0.008  // 综合方差阈值（需要根据新算法重新标定）
		 #define VARIANCE_THRESH  0.015  // 综合方差阈值（需要根据新算法重新标定）

    AcceData_t samples[SAMPLE_TIMES] = {0};
    float sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
    float mean_x = 0.0, mean_y = 0.0, mean_z = 0.0;
    float var_x = 0.0, var_y = 0.0, var_z = 0.0;
    float total_variance = 0.0;
		
		static uint8_t PRECISION_FACTOR = 100;
    uint8_t  SC7A20_REG_CTRL_REG1 = 0x20;
    uint8_t  CTRL_REG1_data = 0x00;
    uint8_t  SC7A20_REG_CTRL_REG2 = 0x23;
    uint8_t  CTRL_REG2_data = 0x00;
    uint8_t  SC7A20_REG_CTRL_REG3 = 0x24;
    uint8_t  CTRL_REG3_data = 0x00;

    // 1. 采集50次样本
    for(uint8_t i = 0; i < SAMPLE_TIMES; i++)
    {
        drv_sc7a20_get_acce_xyz(&samples[i]);
        // SEGGER_RTT_printf(0,"samples[%d]: X=%.2f, Y=%.2f, Z=%.2f\n", i, samples[i].x, samples[i].y, samples[i].z);
        
        sum_x += samples[i].x;
        sum_y += samples[i].y;
        sum_z += samples[i].z;
        delay1ms(SAMPLE_INTERVAL);
    }
        
    // 2. 计算各轴均值
    mean_x = sum_x / SAMPLE_TIMES;
    mean_y = sum_y / SAMPLE_TIMES;
    mean_z = sum_z / SAMPLE_TIMES;

    // 3. 计算各轴方差
    for(uint8_t i = 0; i < SAMPLE_TIMES; i++) {
        float diff_x = samples[i].x - mean_x;
        float diff_y = samples[i].y - mean_y;
        float diff_z = samples[i].z - mean_z;
        
        var_x += diff_x * diff_x;
        var_y += diff_y * diff_y;
        var_z += diff_z * diff_z;
    }
    
    var_x /= SAMPLE_TIMES;
    var_y /= SAMPLE_TIMES;
    var_z /= SAMPLE_TIMES;

    // 4. 合成总震动能量 (三轴方差之和，完美消除重力方向影响)
    total_variance = var_x + var_y + var_z;
		
		SEGGER_RTT_printf(0, "Var_X: %.5f, Var_Y: %.5f, Var_Z: %.5f, Total: %.5f\n", 
                      var_x*PRECISION_FACTOR, var_y*PRECISION_FACTOR, var_z*PRECISION_FACTOR, total_variance*PRECISION_FACTOR);

    // 5. 判断阈值
    if(total_variance > VARIANCE_THRESH) {
        running_flag = 1;
        is_move = 1;
        SEGGER_RTT_printf(0,"Vibration detected! Total Variance: %.5f\n", total_variance);
    } else {
        running_flag = 0;
//      is_move = 0;
        SEGGER_RTT_printf(0,"Static state. Total Variance: %.5f\n", total_variance);
    }
        
    // 关闭传感器... (保留你原有的逻辑)
    if (drv_sc7a20_i2c_write(&SC7A20_REG_CTRL_REG1, &CTRL_REG1_data)!= Ok) {}
    if (drv_sc7a20_i2c_write(&SC7A20_REG_CTRL_REG2, &CTRL_REG2_data)!= Ok) {}
    if (drv_sc7a20_i2c_write(&SC7A20_REG_CTRL_REG3, &CTRL_REG3_data)!= Ok) {}
}





//void SC7A20_Task(void)
//{
//    // 淇敼閲囨牱鍙傛暟
//    #define SAMPLE_INTERVAL  200    // 200ms閲囨牱闂撮殧
//    #define SAMPLE_TIMES     50     // 10绉掓�婚噰鏍锋鏁?(10000ms/200ms)
//    #define VARIANCE_THRESH  0.006    // 鏂瑰樊闃堝�硷紙鏍规嵁瀹炴祴璋冩暣锛?

//    float samples[SAMPLE_TIMES] = {0};
//    float sum = 0.0,mean = 0.0, variance = 0.0;
//    uint8_t  SC7A20_REG_CTRL_REG1 = 0x20;
//    uint8_t  CTRL_REG1_data = 0x00;
//		uint8_t  SC7A20_REG_CTRL_REG2 = 0x23;
//		uint8_t  CTRL_REG2_data = 0x00;
//		uint8_t  SC7A20_REG_CTRL_REG3 = 0x24;
//		uint8_t  CTRL_REG3_data = 0x00;
//		

//    // 閲囬泦50娆℃牱鏈紙鍏?0绉掞級
//    for(uint8_t i = 0; i < SAMPLE_TIMES; i++)
//    {
//        drv_sc7a20_get_acce(&samples[i]);
//				SEGGER_RTT_printf(0,"samples[%d] = : %.2f\n", i,samples[i]);
//				sum += samples[i];  // 实时累加
//        delay1ms(SAMPLE_INTERVAL);
//    }
//		
//		SEGGER_RTT_printf(0,"sum: %.2f\n", sum);
//    mean = sum/SAMPLE_TIMES;
//		gyroscope_data  = mean;
//    // 璁＄畻鏂瑰樊
//		for(uint8_t i = 0; i < SAMPLE_TIMES; i++) {
//				float diff = samples[i] - mean;
//				variance += diff * diff;  // 避免使用powf节省计算资源
//		}
//    variance /= SAMPLE_TIMES;
//    if(variance > VARIANCE_THRESH) {
//        running_flag = 1;
//        is_move = 1;
//        SEGGER_RTT_printf(0,"Vibration detected! Variance: %.2f\n", variance);
//    } else {
//        running_flag = 0;
////        is_move = 0;
//        SEGGER_RTT_printf(0,"Static state. Variance: %.2f\n", variance);
//    }
//		
//    // 鍏抽棴浼犳劅鍣?
//    if (drv_sc7a20_i2c_write(&SC7A20_REG_CTRL_REG1, &CTRL_REG1_data)!= Ok) {
////        SEGGER_RTT_printf(0,"Sensor shutdown error"); 
//    }
//		 if (drv_sc7a20_i2c_write(&SC7A20_REG_CTRL_REG2, &CTRL_REG2_data)!= Ok) {  // 100Hz, 锟斤拷锟斤拷模式
//    }
//		  if (drv_sc7a20_i2c_write(&SC7A20_REG_CTRL_REG3, &CTRL_REG3_data)!= Ok) {  // 100Hz, 锟斤拷锟斤拷模式
//    }
//}

uint8_t sc7a20_init(void) {
    // 1. 锟斤拷证锟借备ID
	uint8_t who_am_i  = 0x20;
	uint8_t SC7A20_REG_WHO_AM_I = 0x0f;
	uint8_t SC7A20_REG_CTRL_REG1  = 0x20;
	uint8_t SC7A20_REG_CTRL_REG2  = 0x21;
	uint8_t SC7A20_REG_CTRL_REG4  = 0x23;
	uint8_t data = 0x57;
	uint8_t hpf_disable = 0x00;	
//	uint8_t filte_data =  0x0f;
	uint8_t SC7A20_RANGE_2G = 0;
    if (drv_sc7a20_i2c_read(&SC7A20_REG_WHO_AM_I, &who_am_i)  != Ok) {
        return FALSE;  // 通锟斤拷失锟杰伙拷ID锟斤拷匹锟斤拷
    }
		SEGGER_RTT_printf(0,"SC7A20_REG_WHO_AM_I: %d\n", SC7A20_REG_WHO_AM_I);
//		SEGGER_RTT_printf(0,"who_am_i =%d\r\n",who_am_i); 

    // 2. 锟斤拷锟斤拷 CTRL_REG1: 锟斤拷锟筋传锟斤拷锟斤拷 + 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟?
    // ODR=100Hz (0x50), 锟酵癸拷锟斤拷模式锟截憋拷
    if (drv_sc7a20_i2c_write(&SC7A20_REG_CTRL_REG1, &data)!= Ok) {  // 100Hz, 锟斤拷锟斤拷模式
        return FALSE;
    }
		 if (drv_sc7a20_i2c_write(&SC7A20_REG_CTRL_REG2, &hpf_disable)!= Ok) {  // 100Hz, 锟斤拷锟斤拷模式
        return FALSE;
    }

    // 3. 锟斤拷锟斤拷 CTRL_REG4: 锟斤拷锟斤拷选锟斤拷默锟较★拷2g锟斤拷
    if (drv_sc7a20_i2c_write(&SC7A20_REG_CTRL_REG4, &SC7A20_RANGE_2G)!= Ok) {
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief  兼容旧库的GPIO扩展初始化函数
 * @param  u8Port: GPIO端口号(0-3)
 * @param  u8Pin: GPIO引脚号(0-15)
 * @param  enDir: GPIO方向(输入/输出)
 * @param  bPullUp: 上拉电阻使能
 * @param  bPullDown: 下拉电阻使能
 * @param  bSchmidt: 施密特触发使能(新库无直接配置)
 * @param  bOpenDrain: 开漏输出使能
 * @retval 操作结果
 */
static en_result_t Gpio_InitIOExt(uint8_t u8Port, uint32_t u8Pin, en_gpio_dir_t enDir, 
                           boolean_t bPullUp, boolean_t bPullDown, 
                           boolean_t bSchmidt, boolean_t bOpenDrain)
{
    // 端口映射
    en_gpio_port_t enNewPort;
    switch(u8Port)
    {
        case 0: enNewPort = GpioPortA; break;
        case 1: enNewPort = GpioPortB; break;
        case 2: enNewPort = GpioPortC; break;
        case 3: enNewPort = GpioPortD; break;
        default: return Error;
    }
    
    // 引脚映射
    en_gpio_pin_t enNewPin = (en_gpio_pin_t)u8Pin;
    if(u8Pin > 15) return Error;
    
    // 配置GPIO参数
    stc_gpio_cfg_t stcGpioCfg;
    DDL_ZERO_STRUCT(stcGpioCfg);
    
    // 设置方向
    stcGpioCfg.enDir = enDir;
    
    // 设置输出值（默认低电平）
    stcGpioCfg.bOutputVal = FALSE;
    
    // 设置上拉
    stcGpioCfg.enPu = bPullUp ? GpioPuEnable : GpioPuDisable;
    
    // 设置下拉
    stcGpioCfg.enPd = bPullDown ? GpioPdEnable : GpioPdDisable;
    
    // 设置开漏
    stcGpioCfg.enOD = bOpenDrain ? GpioOdEnable : GpioOdDisable;
    
    // 设置驱动能力（默认低驱动）
    stcGpioCfg.enDrv = GpioDrvL;
    
    // 设置控制模式（默认快速IO）
    stcGpioCfg.enCtrlMode = GpioFastIO;
    
    // 注意：新库中没有直接的施密特触发配置选项
    // 如果需要此功能，请查阅HC32L130芯片手册了解具体实现方式
    
    // 调用新库的GPIO初始化函数
    return Gpio_Init(enNewPort, enNewPin, &stcGpioCfg);
}
/**
 * @brief  I2C去初始化函数（替代旧库的I2C_DeInit）
 */
static void I2C_DeInit(void)
{
    // 禁用I2C模块
    I2C_ClearFunc(M0P_I2C0, I2cModule_En);
    
    // 禁用高速模式
    I2C_ClearFunc(M0P_I2C0, I2cHlm_En);
}

 void key_int_init(void)
 {
		stc_gpio_cfg_t stcGpioCfg;
    
    ///< 打开GPIO外设时钟门控
    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE);
    
    ///< 端口方向配置->输入
    stcGpioCfg.enDir = GpioDirIn;
    ///< 端口驱动能力配置->高驱动能力
    stcGpioCfg.enDrv = GpioDrvL;
    ///< 端口上下拉配置->无
    stcGpioCfg.enPu = GpioPuDisable;
    stcGpioCfg.enPd = GpioPdDisable;
    ///< 端口开漏输出配置->开漏输出关闭
    stcGpioCfg.enOD = GpioOdDisable;
    ///< 端口输入/输出值寄存器总线控制模式配置->AHB
    stcGpioCfg.enCtrlMode = GpioAHB;
    ///< GPIO IO USER KEY初始化
    Gpio_Init(GpioPortB, GpioPin11, &stcGpioCfg); 
	 
		 ///< 打开并配置USER KEY为下降沿中断
    Gpio_EnableIrq(GpioPortB, GpioPin11, GpioIrqFalling);
    ///< 使能端口PORTA系统中断
    EnableNvic(PORTB_IRQn, IrqLevel3, TRUE);
    #if 1
    SEGGER_RTT_printf(0, "ket int init!\n");
    #endif
    
 }

void hardware_deinit(void)
{
     
//    Sysctrl_SetPeripheralGate(SysctrlPeripheralBaseTim,FALSE);//??0/2????????
//    Sysctrl_SetPeripheralGate(SysctrlPeripheralUart0,FALSE);
////		 Clk_SetPeripheralGate(ClkPeripheralRtc,FALSE);
//		M0P_BGR->CR_f.BGR_EN = 0x0u;
//	//I2C IO DEINIT
//		I2C_DeInit();
//		Sysctrl_SetPeripheralGate(SysctrlPeripheralI2c0,FALSE);
//		Gpio_InitIOExt(GpioPortA,GpioPin9,GpioDirIn,FALSE,FALSE,FALSE,FALSE);   
//    Gpio_InitIOExt(GpioPortA,GpioPin10,GpioDirIn,FALSE,FALSE,FALSE,FALSE); 
//		Gpio_InitIOExt(GpioPortB,GpioPin6,GpioDirIn,FALSE,FALSE,FALSE,FALSE);   
//    Gpio_InitIOExt(GpioPortB,GpioPin7,GpioDirIn,FALSE,FALSE,FALSE,FALSE);
//	//tem_en
//		Gpio_InitIOExt(TEM_EN_PORT,TEM_EN_PIN,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
//	//main ri
////	 Gpio_InitIOExt(3,2,GpioDirIn,FALSE,FALSE,FALSE,FALSE);
//	//key
//		Gpio_InitIOExt(GpioPortB,GpioPin11,GpioDirIn,TRUE,FALSE,FALSE,FALSE);
//	//adc 7
//	 Gpio_InitIOExt(GpioPortA,GpioPin6,GpioDirIn,TRUE,FALSE,FALSE,FALSE);
//	//led
//	 Gpio_InitIOExt(GpioPortA,GpioPin7,GpioDirIn,FALSE,FALSE,FALSE,FALSE);
//	 //adc in0
//	 Gpio_InitIOExt(GpioPortA,GpioPin2,GpioDirIn,TRUE,FALSE,FALSE,FALSE);
//	 //swd
//		Gpio_InitIOExt(GpioPortA,GpioPin13,GpioDirIn,FALSE,FALSE,FALSE,FALSE);
//		Gpio_InitIOExt(GpioPortA,GpioPin14,GpioDirIn,FALSE,FALSE,FALSE,FALSE);
//		//GNSS power on
//		Gpio_InitIOExt(GpioPortA,GpioPin4,GpioDirIn,FALSE,FALSE,FALSE,FALSE);
//		//sc7a20h int
//		Gpio_InitIOExt(GpioPortB,GpioPin5,GpioDirIn,FALSE,FALSE,FALSE,FALSE);
	
	
		M0P_BGR->CR_f.BGR_EN = 0x0u;
		delay1ms(20);
		Sysctrl_SetFunc(SysctrlSWDUseIOEn, TRUE);
		//batt adc  io
		Gpio_InitIOExt(GpioPortA,GpioPin2,GpioDirIn,TRUE,FALSE,FALSE,FALSE);   
		//gnss power on 
		Gpio_InitIOExt(GpioPortA,GpioPin4,GpioDirIn,FALSE,TRUE,FALSE,FALSE); 
			// tem_en
		Gpio_InitIOExt(GpioPortA,GpioPin5,GpioDirIn,FALSE,TRUE,FALSE,FALSE); 
		// tem adc
		Gpio_InitIOExt(GpioPortA,GpioPin6,GpioDirIn,TRUE,FALSE,FALSE,FALSE); 
			//led
		Gpio_InitIOExt(GpioPortA,GpioPin7,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
	//unuseed
		Gpio_InitIOExt(GpioPortB,GpioPin0,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		//unuseed
		Gpio_InitIOExt(GpioPortB,GpioPin1,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		//pwoer up  不初始化这个
//		Gpio_InitIOExt(GpioPortB,GpioPin11,GpioDirIn,TRUE,FALSE,FALSE,FALSE);
			//unuseed
		Gpio_InitIOExt(GpioPortA,GpioPin8,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
	
	
	//uart io deinit, keep TX idle high so modem RXD will not float/wake sleep
		Sysctrl_SetPeripheralGate(SysctrlPeripheralUart0,FALSE);
		Gpio_SetIO(GpioPortA,GpioPin9);
		Gpio_InitIOExt(GpioPortA,GpioPin9,GpioDirOut,FALSE,FALSE,FALSE,FALSE);   
		Gpio_SetIO(GpioPortA,GpioPin9);
    Gpio_InitIOExt(GpioPortA,GpioPin10,GpioDirIn,TRUE,FALSE,FALSE,FALSE); 
    Sysctrl_SetPeripheralGate(SysctrlPeripheralBaseTim, FALSE);//??0/2????????
					//unuseed
		Gpio_InitIOExt(GpioPortA,GpioPin11,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
					//unuseed
		Gpio_InitIOExt(GpioPortA,GpioPin12,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		//swd
		Gpio_InitIOExt(GpioPortA,GpioPin13,GpioDirIn,TRUE,FALSE,FALSE,FALSE);
		//swc
		Gpio_InitIOExt(GpioPortA,GpioPin14,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
  	//unuseed
		Gpio_InitIOExt(GpioPortA,GpioPin15,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		//unuseed
		Gpio_InitIOExt(GpioPortB,GpioPin3,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		//unuseed
		Gpio_InitIOExt(GpioPortB,GpioPin4,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
			//sc7a20h int
		Gpio_InitIOExt(GpioPortB,GpioPin5,GpioDirIn,FALSE,FALSE,FALSE,FALSE);
	
	//I2C IO DEINIT
		I2C_DeInit();
		Sysctrl_SetPeripheralGate(SysctrlPeripheralI2c0,FALSE);
		Gpio_InitIOExt(GpioPortB,GpioPin6,GpioDirIn,TRUE,FALSE,FALSE,FALSE);   
    Gpio_InitIOExt(GpioPortB,GpioPin7,GpioDirIn,TRUE,FALSE,FALSE,FALSE);
		//BOOT
		Gpio_InitIOExt(GpioPortB,GpioPin5,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		
		key_int_init();
		
		
		
	
	
}
void self_check_hardware_deinit(void)
{

		M0P_BGR->CR_f.BGR_EN = 0x0u;
		delay1ms(20);
		Sysctrl_SetFunc(SysctrlSWDUseIOEn, TRUE);
		//batt adc  io
		Gpio_InitIOExt(GpioPortA,GpioPin2,GpioDirIn,TRUE,FALSE,FALSE,FALSE);   
		//gnss power on 
		Gpio_InitIOExt(GpioPortA,GpioPin4,GpioDirIn,FALSE,TRUE,FALSE,FALSE); 
			// tem_en
		Gpio_InitIOExt(GpioPortA,GpioPin5,GpioDirIn,FALSE,TRUE,FALSE,FALSE); 
		// tem adc
		Gpio_InitIOExt(GpioPortA,GpioPin6,GpioDirIn,TRUE,FALSE,FALSE,FALSE); 
			//led
		Gpio_InitIOExt(GpioPortA,GpioPin7,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
	//unuseed
		Gpio_InitIOExt(GpioPortB,GpioPin0,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		//unuseed
		Gpio_InitIOExt(GpioPortB,GpioPin1,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
			//unuseed
		Gpio_InitIOExt(GpioPortA,GpioPin8,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
	
	
	//uart io deinit
		Sysctrl_SetPeripheralGate(SysctrlPeripheralUart0,FALSE);
		Gpio_InitIOExt(GpioPortA,GpioPin9,GpioDirIn,TRUE,FALSE,FALSE,FALSE);   
    Gpio_InitIOExt(GpioPortA,GpioPin10,GpioDirIn,TRUE,FALSE,FALSE,FALSE); 
    Sysctrl_SetPeripheralGate(SysctrlPeripheralBaseTim, FALSE);//??0/2????????
					//unuseed
		Gpio_InitIOExt(GpioPortA,GpioPin11,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
					//unuseed
		Gpio_InitIOExt(GpioPortA,GpioPin12,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		//swd
		Gpio_InitIOExt(GpioPortA,GpioPin13,GpioDirIn,TRUE,FALSE,FALSE,FALSE);
		//swc
		Gpio_InitIOExt(GpioPortA,GpioPin14,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
  	//unuseed
		Gpio_InitIOExt(GpioPortA,GpioPin15,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		//unuseed
		Gpio_InitIOExt(GpioPortB,GpioPin3,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
		//gnss power ctl
		Gpio_InitIOExt(GpioPortB,GpioPin4,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
			//sc7a20h int
		Gpio_InitIOExt(GpioPortB,GpioPin5,GpioDirIn,FALSE,FALSE,FALSE,FALSE);
	
	//I2C IO DEINIT
		I2C_DeInit();
		Sysctrl_SetPeripheralGate(SysctrlPeripheralI2c0,FALSE);
		Gpio_InitIOExt(GpioPortB,GpioPin6,GpioDirIn,TRUE,FALSE,FALSE,FALSE);   
    Gpio_InitIOExt(GpioPortB,GpioPin7,GpioDirIn,TRUE,FALSE,FALSE,FALSE);
		//BOOT
		Gpio_InitIOExt(GpioPortB,GpioPin5,GpioDirIn,FALSE,TRUE,FALSE,FALSE);
}

