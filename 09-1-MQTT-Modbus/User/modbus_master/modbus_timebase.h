/**
 * @file modbus_timebase.h
 * @brief Modbus 事务用时间基（微秒级）——基于 SysTick 的 1ms 主时基 + 子计数换算。
 * - 复用工程已有的 SysTick（如 bsp_systick）：
 * - 配置宏 MB_TIMEBASE_OWN_SYSTICK=0（默认即为0，不改即可）；
 * - 在你的 SysTick_Handler 中加一行 MB_Timebase_SysTickHook();
 * - 调用 MB_Timebase_Init()（不会改动 SysTick，仅清内部计数）；
 * 
 * - 若希望由本模块自行配置并接管 SysTick：
 * - 定义 MB_TIMEBASE_OWN_SYSTICK=1；
 * - 可选：定义 MB_TIMEBASE_WEAK_HANDLER=1，让本模块提供弱 SysTick_Handler（无你自己的处理逻辑时使用）。
 * - 回绕安全（32bit）。
 *
 * - 调用 MB_Timebase_Init() 完成 SysTick 配置（1ms）；
 * - 若工程已有 SysTick_Handler，请在其中调用 MB_Timebase_SysTickHook()；
 * - 若工程没有 SysTick_Handler，本模块提供的弱符号将自动生效。
 */
#ifndef __MODBUS_TIMEBASE_H__
#define __MODBUS_TIMEBASE_H__

#include <stdint.h>
/* 是否由本模块自行配置 SysTick（默认0=不配置，复用外部 SysTick） */
#ifndef MB_TIMEBASE_OWN_SYSTICK
#define MB_TIMEBASE_OWN_SYSTICK 0
#endif

/* 是否由本模块提供弱 SysTick_Handler（默认0=不提供，避免与工程冲突） */
#ifndef MB_TIMEBASE_WEAK_HANDLER
#define MB_TIMEBASE_WEAK_HANDLER 0
#endif


#ifdef __cplusplus
extern "C" {
#endif

void     MB_Timebase_Init(void);
uint32_t MB_TimeNowUs(void);

/* 在 SysTick_Handler 中调用，累加毫秒计数 */
void     MB_Timebase_SysTickHook(void);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_TIMEBASE_H__ */