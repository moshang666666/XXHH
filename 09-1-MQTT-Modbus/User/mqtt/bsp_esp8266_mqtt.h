#ifndef __BSP_ESP8266_MQTT_H
#define __BSP_ESP8266_MQTT_H

#include <stdio.h>  
#include <string.h>  
#include <stdbool.h>
#include "./mqtt/bsp_esp8266.h"

// LED 开关状态（0=灭，1=亮），供上报使用
extern int led_value;
// MQTT 连接标志（1=已连接且完成订阅）
extern uint8_t mqtt_flag;

// 直连 TCP + MQTT 建链并订阅必要主题(post/reply, property/set)
bool ESP8266_MQTT_TCP_CONNECT_AND_SUB(void);

// 设备属性上报（post），捕获平台 post/reply ACK 并打印
bool ESP8266_MQTT_PUB(uint8_t temp_set, uint8_t humi_set, uint16_t slave_light, uint16_t mq2_ppm, uint8_t alarm_level);

// 常驻非阻塞轮询：处理 post/reply 与 property/set（控制 LED 并 set_reply）
void ESP8266_MQTT_POLL_RECEIVE(void);



#endif /* __BSP_ESP8266_MQTT_H */


