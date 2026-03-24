/**
 * @file modbus_timebase.c
 * @brief 基于 SysTick 的微秒时间基实现，为事务层提供 MB_TimeNowUs()。
 *
 * - 使用 SysTick 作为 1ms 周期“主时基”（低中断开销），在中断中累加毫秒计数 s_ms；
 * - 通过读取 SysTick->VAL 得到当前毫秒内的子计数，并结合 SystemCoreClock 计算微秒；
 * - 最终提供单调递增的微秒时间戳（32bit，约 4294 秒回绕，事务层只做差值运算，回绕安全）。
 *
 * - 要求 SystemCoreClock 正确（由系统启动代码维护）；
 * - 若工程已有 SysTick_Handler，请在其中调用 MB_Timebase_SysTickHook()；
 * - 本文件同时提供一个 __weak 的 SysTick_Handler，若工程未自定义，将自动生效。
 */

#include "modbus_timebase.h"

#include "stm32f4xx.h"
#include "misc.h"

/* 1ms 计数器，由 SysTick 中断递增 */
static volatile uint32_t s_ms = 0;

void MB_Timebase_Init(void)
{
#if MB_TIMEBASE_OWN_SYSTICK
    /* 选择时钟源为 HCLK（内核时钟），以便使用 SystemCoreClock 直接计算 */
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK);

    /* 配置为 1ms 周期：RELOAD = SystemCoreClock/1000 - 1 */
    uint32_t reload = SystemCoreClock / 1000U;
    if (reload == 0)
        reload = 1; /* 防御性 */

    SysTick->LOAD = reload - 1U;
    SysTick->VAL  = 0U; /* 清当前值，避免第一次间隔异常 */
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
#else
    /* 复用外部已配置好的 SysTick（如 bsp_systick），这里只清内部计数 */
    s_ms = 0;
#endif
}

/* 供外部在 SysTick_Handler 中调用 */
void MB_Timebase_SysTickHook(void)
{
    s_ms++;
}

#if MB_TIMEBASE_WEAK_HANDLER
/* 若工程未自定义 SysTick_Handler，且启用本弱实现，则自动累加毫秒。
 * 注意：启用该弱实现后，原本在 SysTick_Handler 中的处理（如 TimingDelay_Decrement）将不会被调用。 */
__weak void SysTick_Handler(void)
{
    MB_Timebase_SysTickHook();
}
#endif

uint32_t MB_TimeNowUs(void)
{
    /* 通过“快照法”规避 SysTick 翻转竞态：
     *  - 读取两次毫秒计数 s_ms，期间读取一次 SysTick->VAL；
     *  - 若两次毫秒计数不相等，说明期间发生翻转，重新读取。 */
    uint32_t ms1, ms2, val;
    uint32_t reload = SysTick->LOAD + 1U; /* 周期内总 tick 数 */
    uint32_t ticks_per_us = SystemCoreClock / 1000000U; /* 每微秒的 tick 数 */
    if (ticks_per_us == 0)
        ticks_per_us = 1; /* 防御 */

    do {
        ms1 = s_ms;
        val = SysTick->VAL; /* 倒计数寄存器 */
        ms2 = s_ms;
    } while (ms1 != ms2);

    /* 本毫秒内已过的 tick 数：计数器倒数，故 elapsed = reload - val */
    uint32_t elapsed_ticks = (reload > val) ? (reload - val) : 0U;
    uint32_t sub_us = elapsed_ticks / ticks_per_us; /* 子毫秒微秒数 */

    return ms1 * 1000U + sub_us;
}
