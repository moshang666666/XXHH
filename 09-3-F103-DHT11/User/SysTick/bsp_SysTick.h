#ifndef __SYSTICK_H
#define __SYSTICK_H


#include "stm32f10x.h"


void SysTick_Init(void);
void Delay_us(__IO u32 nTime);         // 单位1us
uint32_t SystemTick_GetMs(void);      // 获取系统运行时间（毫秒）


#define Delay_ms(x) Delay_us(1000*x)	 //单位ms


#endif /* __SYSTICK_H */
