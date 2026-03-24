/**
 * @file    alarm.c
 * @author  Yukikaze
 * @version V1.0
 * @date    2025-11-28
 * @brief   报警系统实现：根据传感器数据判断环境安全等级并控制蜂鸣器
 *
 * 功能说明：
 *   1. 根据温度、湿度、光敏ADC、MQ2烟雾浓度判断报警等级
 *   2. 控制蜂鸣器输出：
 *      - 安全(0)：蜂鸣器关闭
 *      - 警告(1)：蜂鸣器响5秒停5秒
 *      - 严重警告(2)：蜂鸣器常响
 *
 * 报警判断条件：
 *   - 安全：温度<27℃ 且 30%<湿度<70% 且 光敏ADC>3000 且 ppm<300
 *   - 警告：27℃<温度≤35℃ 且 湿度<30% 且 2000<光敏ADC<3000 且 300≤ppm≤600
 *   - 严重警告：温度>35℃ 且 湿度>70% 且 光敏ADC<2000 且 ppm>600
 */

#include "./alarm/alarm.h"
#include "./beep/bsp_beep.h"
#include "./pwm/bsp_pwm.h"
#include "./motor/bsp_motor.h"
#include "./modbus_master/application_data_manager.h"
#include <stdio.h>

/* 当前报警等级 */
static AlarmLevel_t s_current_alarm_level = ALARM_LEVEL_SAFE;

/* 警告模式蜂鸣器状态（用于实现响1秒停1秒） */
static uint8_t s_beep_state = 0;           /* 0:关闭 1:响铃 */
static uint32_t s_beep_cycle_counter = 0;  /* 周期计数器（每次Alarm_CycleOnce调用+1） */

/* LED 呼吸灯状态 */
static uint8_t s_led_brightness = LED_BREATH_MIN;  /* 当前亮度 */
static int8_t s_led_direction = 1;                 /* 亮度变化方向：1=增加, -1=减少 */

/* 温度超限标志（用于风扇控制） */
static uint8_t s_temp_warning_exceeded = 0;   /* 温度超过警告阈值 */
static uint8_t s_temp_critical_exceeded = 0;  /* 温度超过严重阈值 */

/* 主循环周期估算：
 * - Alarm_CycleOnce() 每个大循环调用1次（约400ms）
 * - Alarm_Process() 在MQTT轮询中每100ms调用1次
 * - 1秒 ≈ 1000ms / 100ms = 10次调用
 */
#define BEEP_WARNING_CYCLE_COUNT  10  /* 警告模式下响/停的循环次数（约1秒） */

/**
 * @brief  初始化报警系统
 * @param  无
 * @retval 无
 */
void Alarm_Init(void)
{
    /* 初始化蜂鸣器GPIO */
    BEEP_GPIO_Config();
    
    /* 默认关闭蜂鸣器 */
    BEEP_OFF;
    
    /* 初始化 LED PWM（1kHz） */
    LED_PWM_Config(1000);
    
    /* 初始化电机驱动（风扇） */
    Motor_Init();
    Motor_Forward();  /* 设置为正转（吹风） */
    
    /* 初始化报警状态 */
    s_current_alarm_level = ALARM_LEVEL_SAFE;
    s_beep_state = 0;
    s_beep_cycle_counter = 0;
    s_temp_warning_exceeded = 0;
    s_temp_critical_exceeded = 0;
    
    /* 初始化 LED 状态（默认安全：绿灯常亮） */
    s_led_brightness = LED_BREATH_MIN;
    s_led_direction = 1;
    LED_PWM_SetDuty(1, 0);    /* LED1(红)关闭 */
    LED_PWM_SetDuty(2, 100);  /* LED2(绿)常亮 */
    LED_PWM_SetDuty(3, 0);    /* LED3 关闭 */
    
    printf("[ALARM] Alarm system initialized.\r\n");
}

/**
 * @brief  根据传感器数据判断报警等级
 * @param  temp      温度值（℃）
 * @param  hum       湿度值（%）
 * @param  light_adc 光敏电阻ADC值
 * @param  mq2_ppm   MQ2烟雾浓度（ppm）
 * @retval AlarmLevel_t 报警等级
 *
 * @note 判断条件（任一满足即触发）：
 *   - 严重警告(2)：温度>35℃ 或 湿度>70% 或 光敏ADC<2000 或 ppm>600
 *   - 警告(1)：27℃<温度≤35℃ 或 湿度<30% 或 2000≤光敏ADC≤3000 或 300≤ppm≤600
 *   - 安全(0)：不满足以上任何条件
 */
AlarmLevel_t Alarm_EvaluateLevel(uint8_t temp, uint8_t hum, uint16_t light_adc, uint16_t mq2_ppm)
{
    /* 重置温度超限标志 */
    s_temp_warning_exceeded = 0;
    s_temp_critical_exceeded = 0;
    
    /* 检查温度是否超过严重阈值 (>35℃) */
    if (temp > ALARM_TEMP_WARNING_MAX) {
        s_temp_critical_exceeded = 1;
    }
    /* 检查温度是否超过警告阈值 (>27℃ 且 ≤35℃) */
    else if (temp > ALARM_TEMP_SAFE_MAX) {
        s_temp_warning_exceeded = 1;
    }
    
    /* 严重警告条件（任一满足）：温度>35℃ 或 湿度>70% 或 光敏ADC<2000 或 ppm>600 */
    if (temp > ALARM_TEMP_WARNING_MAX ||
        hum > ALARM_HUM_SAFE_MAX ||
        light_adc < ALARM_LIGHT_CRITICAL_MAX ||
        mq2_ppm > ALARM_MQ2_CRITICAL_MIN) {
        return ALARM_LEVEL_CRITICAL;
    }
    
    /* 警告条件（任一满足）：27℃<温度≤35℃ 或 湿度<20% 或 2000≤光敏ADC≤3000 或 300≤ppm≤600 */
    if ((temp > ALARM_TEMP_SAFE_MAX && temp <= ALARM_TEMP_WARNING_MAX) ||
        hum < ALARM_HUM_SAFE_MIN ||
        (light_adc >= ALARM_LIGHT_WARNING_MIN && light_adc <= ALARM_LIGHT_WARNING_MAX) ||
        (mq2_ppm >= ALARM_MQ2_WARNING_MIN && mq2_ppm <= ALARM_MQ2_WARNING_MAX)) {
        return ALARM_LEVEL_WARNING;
    }
    
    /* 不满足以上条件，判定为安全 */
    return ALARM_LEVEL_SAFE;
}

/**
 * @brief  获取当前报警等级
 * @param  无
 * @retval AlarmLevel_t 当前报警等级
 */
AlarmLevel_t Alarm_GetCurrentLevel(void)
{
    return s_current_alarm_level;
}

/**
 * @brief  设置报警等级并更新蜂鸣器状态
 * @param  level 报警等级
 * @retval 无
 */
void Alarm_SetLevel(AlarmLevel_t level)
{
    /* 等级变化时打印日志 */
    if (s_current_alarm_level != level) {
        const char *level_str[] = {"SAFE", "WARNING", "CRITICAL"};
        printf("[ALARM] Level changed: %s -> %s\r\n", 
               level_str[s_current_alarm_level], 
               level_str[level]);
    }
    
    s_current_alarm_level = level;
    
    switch (level) {
        case ALARM_LEVEL_SAFE:
            /* 安全：关闭蜂鸣器 */
            BEEP_OFF;
            s_beep_state = 0;
            break;
            
        case ALARM_LEVEL_WARNING:
            /* 警告：初始化周期性响铃状态 */
            /* 实际的周期性控制在 Alarm_Process() 中处理 */
            /* 首次进入警告模式时启动蜂鸣器 */
            if (s_beep_state == 0 && s_beep_cycle_counter == 0) {
                s_beep_state = 1;
                BEEP_ON;
            }
            break;
            
        case ALARM_LEVEL_CRITICAL:
            /* 严重警告：蜂鸣器常响 */
            BEEP_ON;
            s_beep_state = 1;
            break;
            
        default:
            BEEP_OFF;
            s_beep_state = 0;
            break;
    }
}

/**
 * @brief  报警系统周期性处理函数（需在主循环中定期调用）
 * @note   用于实现警告模式下蜂鸣器的周期性响/停（响5秒停5秒）
 *         基于主循环调用次数计时，每次Alarm_CycleOnce调用约400ms
 * @param  无
 * @retval 无
 */
void Alarm_Process(void)
{
    /* 仅在警告模式下需要处理周期性响铃 */
    if (s_current_alarm_level != ALARM_LEVEL_WARNING) {
        s_beep_cycle_counter = 0;  /* 非警告模式时重置计数器 */
        return;
    }
    
    /* 递增循环计数器 */
    s_beep_cycle_counter++;
    
    /* 检查是否到达切换时间（约5秒 = 12次循环） */
    if (s_beep_cycle_counter >= BEEP_WARNING_CYCLE_COUNT) {
        s_beep_cycle_counter = 0;  /* 重置计数器 */
        
        if (s_beep_state == 1) {
            /* 当前响铃中 -> 切换到静音 */
            BEEP_OFF;
            s_beep_state = 0;
            printf("[ALARM] Beep OFF (warning cycle)\r\n");
        } else {
            /* 当前静音中 -> 切换到响铃 */
            BEEP_ON;
            s_beep_state = 1;
            printf("[ALARM] Beep ON (warning cycle)\r\n");
        }
    }
}

/**
 * @brief  完整的报警检测与处理流程
 * @note   在主循环中调用此函数即可完成所有报警相关处理：
 *         1. 从应用数据管理层读取传感器数据
 *         2. 判断报警等级
 *         3. 设置蜂鸣器状态
 *         4. 处理周期性响铃
 * @param  无
 * @retval 无
 */
void Alarm_CycleOnce(void)
{
    uint16_t temp = 0, hum = 0, light_adc = 0, mq2_ppm = 0;
    
    /* 从应用数据管理层读取传感器数据 */
    app_data_get_value(APP_DATA_TEMP1, &temp);
    app_data_get_value(APP_DATA_HUM1, &hum);
    app_data_get_value(APP_DATA_LIGHT_ADC, &light_adc);
    app_data_get_value(APP_DATA_MQ2, &mq2_ppm);
    
    /* 判断报警等级 */
    AlarmLevel_t level = Alarm_EvaluateLevel((uint8_t)temp, (uint8_t)hum, light_adc, mq2_ppm);
    
    /* 设置报警等级并更新蜂鸣器 */
    Alarm_SetLevel(level);
    
    /* 处理周期性响铃（警告模式） */
    Alarm_Process();
}

/**
 * @brief  LED 指示灯处理函数（需在主循环中定期调用）
 * @note   根据报警等级控制 LED：
 *         - 安全(0)：绿灯(LED2)常亮100%
 *         - 警告(1)：黄灯(LED1+LED2)呼吸灯，中等速度
 *         - 严重(2)：红灯(LED1)呼吸灯，快速
 * @param  无
 * @retval 无
 */
void Alarm_LED_Process(void)
{
    uint8_t step = 0;
    
    switch (s_current_alarm_level) {
        case ALARM_LEVEL_SAFE:
            /* 安全：绿灯(LED2)常亮100%，红灯关闭 */
            LED_PWM_SetDuty(1, 0);    /* LED1(红)关闭 */
            LED_PWM_SetDuty(2, 100);  /* LED2(绿)常亮100% */
            /* 重置呼吸灯状态 */
            s_led_brightness = LED_BREATH_MIN;
            s_led_direction = 1;
            break;
            
        case ALARM_LEVEL_WARNING:
            /* 警告：黄灯(LED1+LED2)呼吸灯，中等速度 */
            step = LED_BREATH_STEP_WARNING;
            
            /* 更新呼吸灯亮度 */
            if (s_led_direction > 0) {
                /* 亮度增加 */
                if (s_led_brightness + step >= LED_BREATH_MAX) {
                    s_led_brightness = LED_BREATH_MAX;
                    s_led_direction = -1;  /* 反向：开始减少 */
                } else {
                    s_led_brightness += step;
                }
            } else {
                /* 亮度减少 */
                if (s_led_brightness <= LED_BREATH_MIN + step) {
                    s_led_brightness = LED_BREATH_MIN;
                    s_led_direction = 1;   /* 反向：开始增加 */
                } else {
                    s_led_brightness -= step;
                }
            }
            
            /* 设置黄灯：LED1+LED2 同时亮 */
            LED_PWM_SetDuty(1, s_led_brightness);  /* LED1(红) */
            LED_PWM_SetDuty(2, s_led_brightness);  /* LED2(绿) */
            break;
            
        case ALARM_LEVEL_CRITICAL:
            /* 严重：红灯(LED1)呼吸灯，快速 */
            step = LED_BREATH_STEP_CRITICAL;
            
            /* 更新呼吸灯亮度 */
            if (s_led_direction > 0) {
                /* 亮度增加 */
                if (s_led_brightness + step >= LED_BREATH_MAX) {
                    s_led_brightness = LED_BREATH_MAX;
                    s_led_direction = -1;  /* 反向：开始减少 */
                } else {
                    s_led_brightness += step;
                }
            } else {
                /* 亮度减少 */
                if (s_led_brightness <= LED_BREATH_MIN + step) {
                    s_led_brightness = LED_BREATH_MIN;
                    s_led_direction = 1;   /* 反向：开始增加 */
                } else {
                    s_led_brightness -= step;
                }
            }
            
            /* 设置红灯：只有 LED1 亮 */
            LED_PWM_SetDuty(1, s_led_brightness);  /* LED1(红) */
            LED_PWM_SetDuty(2, 0);                 /* LED2(绿)关闭 */
            break;
            
        default:
            /* 默认关闭所有 LED */
            LED_PWM_SetDuty(1, 0);
            LED_PWM_SetDuty(2, 0);
            break;
    }
}

/**
 * @brief  风扇控制处理函数（根据温度报警等级控制风扇转速）
 * @note   控制逻辑：
 *         - 安全(0)：风扇停止
 *         - 警告(1)温度超限：风扇中速 (60%)
 *         - 严重(2)温度超限：风扇高速 (90%)
 *         注意：只有温度触发的报警才会启动风扇，其他传感器触发不启动
 * @param  无
 * @retval 无
 */
void Alarm_Fan_Process(void)
{
    static uint8_t s_last_fan_speed = 0;  /* 记录上次风扇速度，减少重复设置 */
    uint8_t new_speed = MOTOR_SPEED_STOP;
    
    /* 根据温度超限标志决定风扇速度 */
    if (s_temp_critical_exceeded) {
        /* 温度超过严重阈值 (>35℃)：高速运转 */
        new_speed = MOTOR_SPEED_HIGH;
    } else if (s_temp_warning_exceeded) {
        /* 温度超过警告阈值 (27℃~35℃)：中速运转 */
        new_speed = MOTOR_SPEED_MEDIUM;
    } else {
        /* 温度正常：停止风扇 */
        new_speed = MOTOR_SPEED_STOP;
    }
    
    /* 只在速度变化时更新，避免频繁设置 */
    if (new_speed != s_last_fan_speed) {
        Motor_SetSpeed(new_speed);
        s_last_fan_speed = new_speed;
        
        if (new_speed == MOTOR_SPEED_STOP) {
            printf("[FAN] Fan stopped (temp normal)\r\n");
        } else if (new_speed == MOTOR_SPEED_MEDIUM) {
            printf("[FAN] Fan medium speed (temp warning: >27C)\r\n");
        } else if (new_speed == MOTOR_SPEED_HIGH) {
            printf("[FAN] Fan high speed (temp critical: >35C)\r\n");
        }
    }
}