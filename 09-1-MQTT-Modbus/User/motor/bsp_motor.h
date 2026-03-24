/**
 * @file    bsp_motor.h
 * @author  Yukikaze
 * @version V1.0
 * @date    2025-11-29
 * @brief   直流电机（风扇）驱动
 *
 * 硬件连接说明：
 *   - 使用 TB6612FNG 或类似电机驱动模块
 *   - PWMA: 连接 TIM3_CH3 (PB0) - 控制电机转速
 *   - AIN1: 连接 PA4 - 控制电机方向
 *   - AIN2: 连接 PA5 - 控制电机方向
 *   - STBY: 连接 PA7 - 待机控制（高电平工作）
 *
 * 电机方向控制：
 *   - AIN1=0, AIN2=1: 正转
 *   - AIN1=1, AIN2=0: 反转
 *   - AIN1=AIN2: 停止
 */

#ifndef __BSP_MOTOR_H
#define __BSP_MOTOR_H

#include "stm32f4xx.h"

/*===========================================================================*/
/*                              定时器PWM配置                                 */
/*===========================================================================*/

/* 使用 TIM3 通道3 输出 PWM 控制电机转速 */
#define TIM_MOTOR                       TIM3
#define TIM_MOTOR_CLK                   RCC_APB1Periph_TIM3
#define TIM_MOTOR_RCC_CLK_ENABLE()      RCC_APB1PeriphClockCmd(TIM_MOTOR_CLK, ENABLE)

/* PWM 输出引脚: PB0 -> TIM3_CH3 (AF2) */
#define MOTOR_PWM_GPIO_PORT             GPIOB
#define MOTOR_PWM_GPIO_CLK              RCC_AHB1Periph_GPIOB
#define MOTOR_PWM_GPIO_PIN              GPIO_Pin_0
#define MOTOR_PWM_GPIO_PINSOURCE        GPIO_PinSource0
#define MOTOR_PWM_GPIO_AF               GPIO_AF_TIM3

/* TIM3 通道3 配置 */
#define TIM_MOTOR_CHANNEL               TIM_Channel_3
#define TIM_MOTOR_OCxInit               TIM_OC3Init
#define TIM_MOTOR_OCxPreloadConfig      TIM_OC3PreloadConfig
#define TIM_MOTOR_SetComparex           TIM_SetCompare3

/*===========================================================================*/
/*                           电机方向控制引脚配置                              */
/*===========================================================================*/

/* AIN1 引脚: PA4 */
#define MOTOR_AIN1_GPIO_PORT            GPIOA
#define MOTOR_AIN1_GPIO_CLK             RCC_AHB1Periph_GPIOA
#define MOTOR_AIN1_GPIO_PIN             GPIO_Pin_4

/* AIN2 引脚: PA5 */
#define MOTOR_AIN2_GPIO_PORT            GPIOA
#define MOTOR_AIN2_GPIO_CLK             RCC_AHB1Periph_GPIOA
#define MOTOR_AIN2_GPIO_PIN             GPIO_Pin_5

/* STBY 待机引脚: PA7 (高电平工作) */
#define MOTOR_STBY_GPIO_PORT            GPIOA
#define MOTOR_STBY_GPIO_CLK             RCC_AHB1Periph_GPIOA
#define MOTOR_STBY_GPIO_PIN             GPIO_Pin_7

/*===========================================================================*/
/*                              电机速度等级定义                               */
/*===========================================================================*/

/* PWM 周期值 (ARR+1，用于计算占空比) */
#define MOTOR_PWM_PERIOD                1000

/* 风扇速度等级 (占空比百分比) */
#define MOTOR_SPEED_STOP                0     /* 停止 */
#define MOTOR_SPEED_LOW                 30    /* 低速: 30% */
#define MOTOR_SPEED_MEDIUM              60    /* 中速: 60% */
#define MOTOR_SPEED_HIGH                90    /* 高速: 90% */

/*===========================================================================*/
/*                              函数声明                                      */
/*===========================================================================*/

/**
 * @brief  初始化电机驱动模块（PWM + 方向控制GPIO）
 * @param  无
 * @retval 无
 */
void Motor_Init(void);

/**
 * @brief  设置电机转速
 * @param  speed_percent 速度百分比 (0-100)
 * @retval 无
 */
void Motor_SetSpeed(uint8_t speed_percent);

/**
 * @brief  获取当前电机转速
 * @param  无
 * @retval 当前速度百分比 (0-100)
 */
uint8_t Motor_GetSpeed(void);

/**
 * @brief  设置电机正转（风扇吹风）
 * @param  无
 * @retval 无
 */
void Motor_Forward(void);

/**
 * @brief  设置电机反转
 * @param  无
 * @retval 无
 */
void Motor_Backward(void);

/**
 * @brief  停止电机
 * @param  无
 * @retval 无
 */
void Motor_Stop(void);

/**
 * @brief  使能电机驱动模块（退出待机模式）
 * @param  无
 * @retval 无
 */
void Motor_Enable(void);

/**
 * @brief  禁用电机驱动模块（进入待机模式）
 * @param  无
 * @retval 无
 */
void Motor_Disable(void);

#endif /* __BSP_MOTOR_H */


