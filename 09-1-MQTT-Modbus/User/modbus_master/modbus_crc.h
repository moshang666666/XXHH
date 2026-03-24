/**
 * @file modbus_crc.h
 * @brief Modbus RTU CRC16 计算模块头文件（硬件抽象层上层的通用算法，仅做数据校验）。
 *
 * - 提供标准 Modbus RTU CRC16 (多项式 0xA001，初值 0xFFFF) 计算接口；
 * - 不依赖具体硬件；不使用查表也可通过循环算法，但本实现采用查表方式提高效率；
 * - 供 RTU 帧收发层(modbus_rtu_link) 调用，在帧边界确定后进行完整性校验。
 *
 * - uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len)
 *     入参：buf 指向需要计算的数据（不包含 CRC 本身）；len 为数据长度。
 *     返回：16 位 CRC 值（低字节在前，高字节在后用于发送）。
 *
 * - RTU 帧格式中 CRC 放在最后两字节，发送顺序：低字节 -> 高字节。
 * - 验证时：对“地址 + PDU + CRC”整体计算结果应为 0（即累加包含 CRC 后结果 == 0）。
 */
#ifndef __MODBUS_CRC_H__
#define __MODBUS_CRC_H__

#include <stdint.h>                                                                                 

#ifdef __cplusplus
extern "C" {
#endif

uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_CRC_H__ */
