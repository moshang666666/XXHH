/*
 * application_data_manager.h
 * 应用数据管理层：Modbus 寄存器映射与 ADC 值管理
 */
#ifndef __APPLICATION_DATA_MANAGER_H__
#define __APPLICATION_DATA_MANAGER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Modbus 保持寄存器地址定义（PDU 地址，0 基） */
/* 寄存器索引 = Modbus 逻辑地址 - 40001 */
#define REG_SMOKE_PPM_ADDR  (20u)  /* 烟雾浓度值（ppm），对应 Modbus 逻辑地址 40021 */

/* 保持寄存器数量（需要支持到索引 20） */
#define HOLDING_REG_COUNT (21u)

/* 初始化应用数据层（启动 MQ2 传感器采集等） */
void AppData_Init(void);

/* 周期更新：从 MQ2 传感器读取 ppm 值并更新寄存器（可在主循环或定时调用） */
void AppData_UpdateMQ2(void);

/* 读保持寄存器（供协议层调用）：返回 0=成功，非 0=地址非法 */
int AppData_ReadHolding(uint16_t startAddr, uint16_t quantity, uint16_t *outBuf);

/* 写保持寄存器（供协议层调用）：返回 0=成功，非 0=地址非法或只读 */
int AppData_WriteHolding(uint16_t addr, uint16_t value);

/* 直接获取烟雾浓度值（供协议层调用，单位：ppm） */
uint16_t app_get_smoke_ppm(void);

#ifdef __cplusplus
}
#endif

#endif /* __APPLICATION_DATA_MANAGER_H__ */
