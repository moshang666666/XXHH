#ifndef __PWM_H
#define	__PWM_H

#include "stm32f4xx.h"


/*
 * 使用 TIM5 的 CH1/CH2/CH3,通过 GPIOH 的 PH10/PH11/PH12 输出 PWM,
 * 分别对应 LED1/LED2/LED3。LED 为低电平点亮（active-low）。
 */

void LED_PWM_Config(uint32_t pwm_hz);						/* 初始化 PWM 频率（Hz） */
void LED_PWM_SetDuty(uint8_t ledIndex, uint8_t percent);	/* 设置占空比 0..100,ledIndex:1..3 */
uint8_t LED_PWM_GetDuty(uint8_t ledIndex);				/* 读取当前占空比 0..100 */

#endif /* __PWM_H */