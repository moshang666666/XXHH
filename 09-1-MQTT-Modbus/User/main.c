/**
 * @file    main.c
 * @version V1.0
 * @brief   主函数
 */

#include "stm32f4xx.h"
#include "./usart/bsp_usart.h"
#include "./pwm/bsp_pwm.h"
#include "./systick/bsp_systick.h"
#include "./mqtt/bsp_esp8266.h"
#include "./mqtt/bsp_esp8266_mqtt.h"
#include "./modbus_master/modbus_read.h"
#include "./alarm/alarm.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

// 定期轮询网络接收以实现随时下发即时响应
extern void ESP8266_MQTT_POLL_RECEIVE(void);

int main(void)
{
    USARTx_Config();
    LED_PWM_Config(1000); // 初始化 LED PWM
    InitSysTick();        // 初始化 SysTick 提供毫秒延时
    Modbus_Read_Init();   // 初始化Modbus主站
    Alarm_Init();         // 初始化报警系统

    // WiFi + MQTT 初始化
    //const char *kWifiSsid = "A6N107";      // 切换到其他: Ciallo~
    //const char *kWifiPass = "A6N107666#";  // 切换到其他: 0d000721
    const char *kWifiSsid = "Ciallo~";
    const char *kWifiPass = "0d000721";
    ESP8266_WiFiAndMQTT_Startup(kWifiSsid, kWifiPass);

    // 周期性采集并上报
    while (1)
    {   
        /* Modbus主站读取从站1光敏ADC数据 */
        Modbus_ReadSlave1_Light();
        DelayMs(100); // 等待从站响应
        
        /* Modbus主站读取从站2温湿度数据 */
        Modbus_ReadSlave2_TempHum();
        DelayMs(100); // 等待从站响应
        
        /* Modbus主站读取从站3 MQ2烟雾浓度数据 */
        Modbus_ReadSlave3_MQ2();
        DelayMs(100); // 等待从站响应

        /* 报警系统检测 */
        Alarm_CycleOnce();

        /* ESP8266单次采集 + 上报 + 轮询封装 */
        ESP8266_MQTT_CycleOnce();
    }
}


