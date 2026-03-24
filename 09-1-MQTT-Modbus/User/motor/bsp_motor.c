/**
 * @file    bsp_motor.c
 * @author  Yukikaze
 * @version V1.0
 * @date    2025-11-29
 * @brief   直流电机（风扇）驱动实现 - STM32F429 版本
 *
 * 功能说明：
 *   1. 使用 TIM3_CH3 (PB0) 输出 PWM 控制电机转速
 *   2. 使用 GPIO (PA4/PA5) 控制电机方向
 *   3. 使用 GPIO (PA7) 控制驱动模块待机
 *
 * F103 -> F429 主要移植改动：
 *   1. GPIO 时钟使用 RCC_AHB1PeriphClockCmd 而非 RCC_APB2PeriphClockCmd
 *   2. GPIO 初始化结构体增加 OType 和 PuPd 成员
 *   3. GPIO 复用功能配置使用 GPIO_PinAFConfig
 *   4. GPIO 模式使用 GPIO_Mode_AF/OUT 而非 GPIO_Mode_AF_PP/Out_PP
 */

#include "./motor/bsp_motor.h"
#include <stdio.h>

/* 当前电机速度 (0-100) */
static uint8_t s_motor_speed = 0;

/**
 * @brief  初始化电机 PWM 输出引脚 (PB0 -> TIM3_CH3)
 * @param  无
 * @retval 无
 */
static void Motor_PWM_GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    /* 使能 GPIO 时钟 (F429使用AHB1总线) */
    RCC_AHB1PeriphClockCmd(MOTOR_PWM_GPIO_CLK, ENABLE);
    
    /* 配置 GPIO 为复用功能 */
    GPIO_InitStructure.GPIO_Pin = MOTOR_PWM_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;           /* 复用功能 */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;         /* 推挽输出 */
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;       /* 无上下拉 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;     /* 高速 */
    GPIO_Init(MOTOR_PWM_GPIO_PORT, &GPIO_InitStructure);
    
    /* 配置引脚复用功能映射到 TIM3 */
    GPIO_PinAFConfig(MOTOR_PWM_GPIO_PORT, MOTOR_PWM_GPIO_PINSOURCE, MOTOR_PWM_GPIO_AF);
}

/**
 * @brief  初始化电机方向控制 GPIO (AIN1/AIN2/STBY)
 * @param  无
 * @retval 无
 */
static void Motor_Direction_GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    /* 使能 GPIO 时钟 (F429使用AHB1总线) */
    RCC_AHB1PeriphClockCmd(MOTOR_AIN1_GPIO_CLK, ENABLE);
    RCC_AHB1PeriphClockCmd(MOTOR_AIN2_GPIO_CLK, ENABLE);
    RCC_AHB1PeriphClockCmd(MOTOR_STBY_GPIO_CLK, ENABLE);
    
    /* 配置 AIN1 引脚 */
    GPIO_InitStructure.GPIO_Pin = MOTOR_AIN1_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;          /* 通用输出 */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;         /* 推挽输出 */
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;         /* 下拉 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(MOTOR_AIN1_GPIO_PORT, &GPIO_InitStructure);
    
    /* 配置 AIN2 引脚 */
    GPIO_InitStructure.GPIO_Pin = MOTOR_AIN2_GPIO_PIN;
    GPIO_Init(MOTOR_AIN2_GPIO_PORT, &GPIO_InitStructure);
    
    /* 配置 STBY 引脚 */
    GPIO_InitStructure.GPIO_Pin = MOTOR_STBY_GPIO_PIN;
    GPIO_Init(MOTOR_STBY_GPIO_PORT, &GPIO_InitStructure);
    
    /* 默认设置：停止状态 */
    GPIO_ResetBits(MOTOR_AIN1_GPIO_PORT, MOTOR_AIN1_GPIO_PIN);
    GPIO_ResetBits(MOTOR_AIN2_GPIO_PORT, MOTOR_AIN2_GPIO_PIN);
    
    /* STBY 拉高，使能驱动模块 */
    GPIO_SetBits(MOTOR_STBY_GPIO_PORT, MOTOR_STBY_GPIO_PIN);
}

/**
 * @brief  初始化电机 PWM 定时器 (TIM3)
 * @note   PWM 频率 = TIM_CLK / (PSC+1) / (ARR+1)
 *         F429: TIM3 挂在 APB1 (42MHz)，若 APB1 预分频!=1，则 TIM_CLK = APB1*2 = 84MHz
 *         设置: PSC=83, ARR=999 -> PWM频率 = 84MHz/84/1000 = 1kHz
 * @param  无
 * @retval 无
 */
static void Motor_PWM_TIM_Config(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;
    
    /* 使能 TIM3 时钟 */
    TIM_MOTOR_RCC_CLK_ENABLE();
    
    /* 配置定时器时基单元 */
    TIM_TimeBaseStructure.TIM_Prescaler = 84 - 1;                  /* 预分频: 84MHz/84 = 1MHz */
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;    /* 向上计数 */
    TIM_TimeBaseStructure.TIM_Period = MOTOR_PWM_PERIOD - 1;       /* 周期: 1000 (1kHz PWM) */
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;        /* 时钟分频 */
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;               /* 重复计数器 (高级定时器用) */
    TIM_TimeBaseInit(TIM_MOTOR, &TIM_TimeBaseStructure);
    
    /* 配置 PWM 输出比较单元 */
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;              /* PWM1模式 */
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;  /* 使能输出 */
    TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Disable;
    TIM_OCInitStructure.TIM_Pulse = 0;                             /* 初始占空比 0% */
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;      /* 高电平有效 */
    TIM_OCInitStructure.TIM_OCNPolarity = TIM_OCNPolarity_High;
    TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Reset;
    TIM_OCInitStructure.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    
    /* 初始化 TIM3 通道3 */
    TIM_MOTOR_OCxInit(TIM_MOTOR, &TIM_OCInitStructure);
    TIM_MOTOR_OCxPreloadConfig(TIM_MOTOR, TIM_OCPreload_Enable);
    
    /* 使能 ARR 预装载 */
    TIM_ARRPreloadConfig(TIM_MOTOR, ENABLE);
    
    /* 使能定时器 */
    TIM_Cmd(TIM_MOTOR, ENABLE);
}

/**
 * @brief  初始化电机驱动模块
 * @param  无
 * @retval 无
 */
void Motor_Init(void)
{
    /* 初始化方向控制 GPIO */
    Motor_Direction_GPIO_Config();
    
    /* 初始化 PWM 输出 GPIO */
    Motor_PWM_GPIO_Config();
    
    /* 初始化 PWM 定时器 */
    Motor_PWM_TIM_Config();
    
    /* 默认停止 */
    s_motor_speed = 0;
    Motor_Stop();
    
    printf("[MOTOR] Motor driver initialized.\r\n");
}

/**
 * @brief  设置电机转速
 * @param  speed_percent 速度百分比 (0-100)
 * @retval 无
 */
void Motor_SetSpeed(uint8_t speed_percent)
{
    uint32_t ccr_value;
    
    /* 限制速度范围 */
    if (speed_percent > 100) {
        speed_percent = 100;
    }
    
    s_motor_speed = speed_percent;
    
    /* 计算 CCR 值: CCR = (ARR+1) * percent / 100 */
    ccr_value = (MOTOR_PWM_PERIOD * speed_percent) / 100;
    
    /* 设置比较值 */
    TIM_MOTOR_SetComparex(TIM_MOTOR, ccr_value);
}

/**
 * @brief  获取当前电机转速
 * @param  无
 * @retval 当前速度百分比 (0-100)
 */
uint8_t Motor_GetSpeed(void)
{
    return s_motor_speed;
}

/**
 * @brief  设置电机正转（风扇吹风）
 * @note   AIN1=0, AIN2=1 -> 正转
 * @param  无
 * @retval 无
 */
void Motor_Forward(void)
{
    GPIO_ResetBits(MOTOR_AIN1_GPIO_PORT, MOTOR_AIN1_GPIO_PIN);
    GPIO_SetBits(MOTOR_AIN2_GPIO_PORT, MOTOR_AIN2_GPIO_PIN);
}

/**
 * @brief  设置电机反转
 * @note   AIN1=1, AIN2=0 -> 反转
 * @param  无
 * @retval 无
 */
void Motor_Backward(void)
{
    GPIO_SetBits(MOTOR_AIN1_GPIO_PORT, MOTOR_AIN1_GPIO_PIN);
    GPIO_ResetBits(MOTOR_AIN2_GPIO_PORT, MOTOR_AIN2_GPIO_PIN);
}

/**
 * @brief  停止电机
 * @note   AIN1=0, AIN2=0 -> 停止（刹车）
 * @param  无
 * @retval 无
 */
void Motor_Stop(void)
{
    GPIO_ResetBits(MOTOR_AIN1_GPIO_PORT, MOTOR_AIN1_GPIO_PIN);
    GPIO_ResetBits(MOTOR_AIN2_GPIO_PORT, MOTOR_AIN2_GPIO_PIN);
    Motor_SetSpeed(0);
}

/**
 * @brief  使能电机驱动模块（退出待机模式）
 * @param  无
 * @retval 无
 */
void Motor_Enable(void)
{
    GPIO_SetBits(MOTOR_STBY_GPIO_PORT, MOTOR_STBY_GPIO_PIN);
}

/**
 * @brief  禁用电机驱动模块（进入待机模式）
 * @param  无
 * @retval 无
 */
void Motor_Disable(void)
{
    GPIO_ResetBits(MOTOR_STBY_GPIO_PORT, MOTOR_STBY_GPIO_PIN);
}

/*********************************************END OF FILE**********************/
