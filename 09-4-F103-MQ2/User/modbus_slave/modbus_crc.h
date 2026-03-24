/*
 * modbus_crc.h
 * Modbus RTU CRC16 查表算法（多项式 0xA001，初值 0xFFFF）
 */
#ifndef __MODBUS_CRC_H__
#define __MODBUS_CRC_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 计算 CRC16：
 * 入参: buf 指向需要计算的数据（不包含 CRC 本身）；len 为数据长度。
 * 返回: 16 位 CRC 值（发送时按 低字节->高字节 的顺序发送）。
 */
uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_CRC_H__ */
