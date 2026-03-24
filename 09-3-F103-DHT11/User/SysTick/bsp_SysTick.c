/**
 ******************************************************************************
 * @file    bsp_SysTick.c
 * @version V1.0
 * @brief   SysTick 系统滴答时钟10us中断函数库,中断时间可自由配置，
 *          常用的有 1us 10us 1ms 中断。
 */

#include "./systick/bsp_SysTick.h"

static __IO u32 TimingDelay;

/* 全局毫秒计数器（用于 DHT11 更新间隔控制） */
static volatile uint32_t s_system_ms = 0;
 
/**
  * @brief  启动系统滴答定时器 SysTick
  * @param  无
  * @retval 无
  */
void SysTick_Init(void)
{
	/* SystemFrequency / 1000    1ms中断一次
	 * SystemFrequency / 100000	 10us中断一次
	 * SystemFrequency / 1000000 1us中断一次
	 */
//	if (SysTick_Config(SystemFrequency / 100000))	// ST3.0.0库版本
	if (SysTick_Config(SystemCoreClock / 1000000))	// ST3.5.0库版本
	{ 
		/* Capture error */ 
		while (1);
	}
		// 关闭滴答定时器  
	SysTick->CTRL &= ~ SysTick_CTRL_ENABLE_Msk;
}

/**
  * @brief   us延时程序,10us为一个单位
  * @param  
  *		@arg nTime: Delay_us( 1 ) 则实现的延时为 1 * 10us = 10us
  * @retval  无
  */
void Delay_us(__IO u32 nTime)
{ 
	TimingDelay = nTime;	

	// 使能滴答定时器  
	SysTick->CTRL |=  SysTick_CTRL_ENABLE_Msk;

	while(TimingDelay != 0);
}

/**
  * @brief  获取节拍程序
  * @param  无
  * @retval 无
  * @attention  在 SysTick 中断函数 SysTick_Handler()调用
  */
void TimingDelay_Decrement(void)
{
	if (TimingDelay != 0x00)
	{ 
		TimingDelay--;
	}
}

/**
  * @brief  获取系统运行时间（毫秒）
  * @param  无
  * @retval 系统运行毫秒数
  * @note   此函数供 application_data_manager.c 调用，用于控制 DHT11 读取间隔
  */
uint32_t SystemTick_GetMs(void)
{
	return s_system_ms;
}

/**
  * @brief  SysTick 中断服务函数（每 1us 触发一次）
  * @note   在 bsp_SysTick.c 中已配置为 1us 中断
  */
void SysTick_Handler(void)
{
	static uint32_t us_counter = 0;
	
	/* 用于 SysTick 延时 */
	TimingDelay_Decrement();
	
	/* 累计 1000us = 1ms */
	us_counter++;
	if (us_counter >= 1000)
	{
		us_counter = 0;
		s_system_ms++;
	}
}
/*********************************************END OF FILE*********************/
