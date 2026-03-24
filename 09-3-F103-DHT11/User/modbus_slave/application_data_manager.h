/*
 * application_data_manager.h
 * 应用数据管理层：Modbus 寄存器映射与 DHT11 温湿度数据管理
 */
#ifndef __APPLICATION_DATA_MANAGER_H__
#define __APPLICATION_DATA_MANAGER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Modbus 保持寄存器地址定义（PDU 地址，0 基） */
#define REG_TEMP_ADDR   (10u)  /* 温度值（整数），对应 Modbus 逻辑地址 40011 */
#define REG_HUMI_ADDR   (11u)  /* 湿度值（整数），对应 Modbus 逻辑地址 40012 */

/* 保持寄存器数量 */
#define HOLDING_REG_COUNT (12u)  /* 需要支持到索引 11 (对应 40012) */

/* 初始化应用数据层（初始化 DHT11 等） */
void AppData_Init(void);

/* 周期更新：从 DHT11 读取温湿度并更新寄存器（建议间隔 ≥2 秒） */
void AppData_UpdateDHT11(void);

/* 读保持寄存器（供协议层调用）：返回 0=成功，非 0=地址非法 */
int AppData_ReadHolding(uint16_t startAddr, uint16_t quantity, uint16_t *outBuf);

/* 写保持寄存器（供协议层调用）：返回 0=成功，非 0=地址非法或只读 */
int AppData_WriteHolding(uint16_t addr, uint16_t value);

/* 直接获取温度值（供协议层调用） */
uint16_t app_get_temperature(void);

/* 直接获取湿度值（供协议层调用） */
uint16_t app_get_humidity(void);

#ifdef __cplusplus
}
#endif

#endif /* __APPLICATION_DATA_MANAGER_H__ */
