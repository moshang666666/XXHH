/*
 * tim4_tick_driver.h
 * 1ms 系统节拍定时 - 基于 TIM4 - STM32F103C8T6, 标准外设库
 *
 * 用途：供 Modbus RTU 帧层进行帧间超时判断（T3.5 等上层可换算到毫秒判断）
 */
#ifndef __TIM4_TICK_DRIVER_H__
#define __TIM4_TICK_DRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 初始化 TIM4 产生 1ms 周期中断，并开始计数 */
void TIM4_TickInit(void);

/* 读取毫秒 tick 计数（开机以来的 ms）*/
uint32_t TIM4_GetTick(void);

/* 返回自 start 起经过的毫秒数（自动处理 32 位溢出）*/
uint32_t TIM4_GetElapsed(uint32_t start);

#ifdef __cplusplus
}
#endif

#endif /* __TIM4_TICK_DRIVER_H__ */
