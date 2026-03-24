/**
 * @file    alarm.h
 * @author  Yukikaze
 * @version V1.0
 * @date    2025-11-28
 * @brief   报警系统头文件：根据传感器数据判断环境安全等级并控制蜂鸣器
 *
 * 报警等级定义：
 *   - ALARM_LEVEL_SAFE(0)：安全，蜂鸣器关闭
 *   - ALARM_LEVEL_WARNING(1)：警告，蜂鸣器响5秒停5秒
 *   - ALARM_LEVEL_CRITICAL(2)：严重警告，蜂鸣器常响
 */

#ifndef __ALARM_H
#define __ALARM_H

#include "stm32f4xx.h"
#include <stdint.h>

/* 报警等级枚举 */
typedef enum {
    ALARM_LEVEL_SAFE     = 0,  /* 安全：蜂鸣器关闭 */
    ALARM_LEVEL_WARNING  = 1,  /* 警告：蜂鸣器响5秒停5秒 */
    ALARM_LEVEL_CRITICAL = 2   /* 严重警告：蜂鸣器常响 */
} AlarmLevel_t;

/* 报警判断阈值（可根据实际需求调整） */
/* 安全条件：温度<27℃  20%<湿度<70%  光敏ADC>3000  ppm<300 */
#define ALARM_TEMP_SAFE_MAX         27   /* 安全温度上限（℃） */
#define ALARM_HUM_SAFE_MIN          20   /* 安全湿度下限（%） */
#define ALARM_HUM_SAFE_MAX          70   /* 安全湿度上限（%） */
#define ALARM_LIGHT_SAFE_MIN        3000 /* 安全光敏ADC下限 */
#define ALARM_MQ2_SAFE_MAX          300  /* 安全ppm上限 */

/* 警告条件：27℃<温度≤35℃  湿度<30%  2000<光敏ADC<3000  300≤ppm≤600 */
#define ALARM_TEMP_WARNING_MAX      35   /* 警告温度上限（℃） */
#define ALARM_LIGHT_WARNING_MIN     2000 /* 警告光敏ADC下限 */
#define ALARM_LIGHT_WARNING_MAX     3000 /* 警告光敏ADC上限 */
#define ALARM_MQ2_WARNING_MIN       300  /* 警告ppm下限 */
#define ALARM_MQ2_WARNING_MAX       600  /* 警告ppm上限 */

/* 严重警告条件：温度>35℃  湿度>70%  光敏ADC<2000  ppm>600 */
#define ALARM_LIGHT_CRITICAL_MAX    2000 /* 严重警告光敏ADC上限 */
#define ALARM_MQ2_CRITICAL_MIN      600  /* 严重警告ppm下限 */

/* LED 呼吸灯配置 */
/* LED1=红灯, LED2=绿灯, LED1+LED2=黄灯 */
#define LED_BREATH_STEP_WARNING     5    /* 警告模式呼吸灯亮度变化步进（快速） */
#define LED_BREATH_STEP_CRITICAL    5    /* 严重模式呼吸灯亮度变化步进（快速） */
#define LED_BREATH_MIN              5    /* 呼吸灯最低亮度 */
#define LED_BREATH_MAX              100  /* 呼吸灯最高亮度 */

/**
 * @brief  初始化报警系统（包括蜂鸣器GPIO）
 * @param  无
 * @retval 无
 */
void Alarm_Init(void);

/**
 * @brief  根据传感器数据判断报警等级
 * @param  temp      温度值（℃）
 * @param  hum       湿度值（%）
 * @param  light_adc 光敏电阻ADC值
 * @param  mq2_ppm   MQ2烟雾浓度（ppm）
 * @retval AlarmLevel_t 报警等级
 */
AlarmLevel_t Alarm_EvaluateLevel(uint8_t temp, uint8_t hum, uint16_t light_adc, uint16_t mq2_ppm);

/**
 * @brief  获取当前报警等级
 * @param  无
 * @retval AlarmLevel_t 当前报警等级
 */
AlarmLevel_t Alarm_GetCurrentLevel(void);

/**
 * @brief  设置报警等级并更新蜂鸣器状态
 * @param  level 报警等级
 * @retval 无
 */
void Alarm_SetLevel(AlarmLevel_t level);

/**
 * @brief  报警系统周期性处理函数（需在主循环中定期调用）
 * @note   用于实现警告模式下蜂鸣器的周期性响/停
 * @param  无
 * @retval 无
 */
void Alarm_Process(void);

/**
 * @brief  完整的报警检测与处理流程（读取传感器数据、判断等级、控制蜂鸣器）
 * @note   在主循环中调用此函数即可完成所有报警相关处理
 * @param  无
 * @retval 无
 */
void Alarm_CycleOnce(void);

/**
 * @brief  LED 指示灯处理函数（需在主循环中定期调用）
 * @note   根据报警等级控制 LED：
 *         - 安全(0)：绿灯(LED2)常亮100%
 *         - 警告(1)：黄灯(LED1+LED2)呼吸灯，中等速度
 *         - 严重(2)：红灯(LED1)呼吸灯，快速
 * @param  无
 * @retval 无
 */
void Alarm_LED_Process(void);

/**
 * @brief  风扇控制处理函数（根据温度报警等级控制风扇转速）
 * @note   控制逻辑：
 *         - 安全(0)：风扇停止
 *         - 警告(1)温度超限：风扇中速 (60%)
 *         - 严重(2)温度超限：风扇高速 (90%)
 * @param  无
 * @retval 无
 */
void Alarm_Fan_Process(void);

#endif /* __ALARM_H */
