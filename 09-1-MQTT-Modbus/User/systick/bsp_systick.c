/**
 * @file    bsp_systick.c
 * @version V1.0
 * @brief   systick
 */
#include "systick/bsp_systick.h"

static __IO uint32_t TimingDelay;

/**
 * @brief Systick 初始化
 * 
 */
void Systick_Init(void)
{
    /* SystemFrequency / 1000    1ms
     * SystemFrequency / 100000	 10us
     * SystemFrequency / 1000000 1us
     */
    if (SysTick_Config(SystemCoreClock / 1000))
    {
        /* Capture error */
        while (1)
            ;
    }
}

/**
 * @brief 延时 n 毫秒
 * @param nTime 以毫秒为单位的时间
 */
void Delay_1ms(__IO uint32_t nTime)
{
    TimingDelay = nTime;

    while (TimingDelay != 0)
        ;
}

/**
 * @brief Systick 中断服务程序
 * 
 */
void TimingDelay_Decrement(void)
{
    if (TimingDelay != 0x00)
    {
        TimingDelay--;
    }
}

/**
 * @brief 初始化 SysTick 为 1ms 周期,使用轮询方式延时
 * 
 */
void InitSysTick(void)
{
    SysTick->CTRL = 0;
    SysTick->LOAD = (SystemCoreClock / 1000) - 1; // 1ms 周期
    SysTick->VAL  = 0;                            // 清零当前计数
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}

/**
 * @brief 毫秒级阻塞延时
 * @param ms 延时的毫秒数
 */
void DelayMs(uint32_t ms)
{
    while (ms--)
    {
        // COUNTFLAG 在计数器回装时置位,读取 CTRL 会清除该标志
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0)
        {
            // 忙等
        }
    }
}
