/*
 * modbus_slave_protocol.h
 * Modbus 从站协议层：解析 ADU(地址+PDU，不含CRC)，调用应用寄存器层，生成应答
 */
#ifndef __MODBUS_SLAVE_PROTOCOL_H__
#define __MODBUS_SLAVE_PROTOCOL_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MODBUS_SLAVE_ADDR
#define MODBUS_SLAVE_ADDR  (3u)     /* 本从站地址：3 (MQ2 烟雾传感器) */
#endif

/* 处理一帧 ADU（地址+PDU，不含CRC）。若需要应答，将在内部直接通过 RS-485 发送 */
void ModbusSlave_ProcessFrame(uint8_t *frame, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_SLAVE_PROTOCOL_H__ */
