#ifndef __MODBUS_READ_H__
#define __MODBUS_READ_H__

#include <stdint.h>

/**
 * @brief 初始化Modbus主站读取模块（硬件 + RTU链路层 + 回调绑定）
 */
void Modbus_Read_Init(void);

/**
 * @brief 读取从站1光敏电阻ADC值（寄存器40001）
 */
void Modbus_ReadSlave1_Light(void);

/**
 * @brief 读取从站2温湿度数据（寄存器40011~40012）
 */
void Modbus_ReadSlave2_TempHum(void);

/**
 * @brief 读取从站3 MQ2烟雾浓度值（寄存器40021）
 */
void Modbus_ReadSlave3_MQ2(void);

#endif /* __MODBUS_READ_H__ */