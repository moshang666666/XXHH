/*
 * tim4_tick_driver.c
 * 基于 TIM4 的 1ms 系统 tick - STM32F103C8T6, 标准外设库
 */

#include "tim4_tick_driver.h"
#include "stm32f10x.h"

/* 系统毫秒计数（在 TIM4 中断中 ++）*/
static volatile uint32_t s_ms_tick = 0;

static void prv_tim4_init(void)
{
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef nvic;

    /* 开启外设时钟: TIM4 在 APB1 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    /* 系统时钟 72MHz，APB1=36MHz，TIM 时钟=72MHz（因 APB1 预分频!=1 时倍频2倍）
     * 目标: 1ms 周期
     * 72MHz / (PSC+1) / (ARR+1) = 1000 Hz
     * 取 PSC=7199 -> 分频 7200 -> 72MHz/7200=10kHz -> ARR=9 -> 1kHz 中断（1ms）
     */
    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler     = 7199;            /* 72MHz / 7200 = 10kHz */
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    tb.TIM_Period        = 9;                /* 10kHz / (9+1) = 1kHz => 1ms */
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM4, &tb);

    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

    /* 配置NVIC */
    nvic.NVIC_IRQChannel = TIM4_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM4, ENABLE);
}

/* ----------提供计时功能函数--------- */
void TIM4_TickInit(void)
{
    s_ms_tick = 0;
    prv_tim4_init();
}

uint32_t TIM4_GetTick(void)
{
    return s_ms_tick;
}

uint32_t TIM4_GetElapsed(uint32_t start)
{
    return (uint32_t)(s_ms_tick - start);
}

/* ------------------------- 中断服务 ------------------------- */
void TIM4_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
        s_ms_tick++; /* 每 1ms +1 */
    }
}
