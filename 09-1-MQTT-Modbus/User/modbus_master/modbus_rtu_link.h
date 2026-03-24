/**
 * @file modbus_rtu_link.h
 * @brief Modbus RTU 帧收发层：从字节流构建完整帧，完成 CRC 校验、帧边界与异常管理。
 *
 * - 下接硬件抽象层(rs485_driver)：通过回调获取字节、驱动发送；
 * - 内部维护接收缓冲(默认 256B)、状态机与 T3.5 定时配合；
 * - 完整帧到达后调用上层注册的“帧就绪回调”，只输出校验通过的有效帧；
 * - 对异常帧（CRC 错误、长度越界、超时不完整）做统计且丢弃。
 */

#ifndef __MODBUS_RTU_LINK_H__
#define __MODBUS_RTU_LINK_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 默认缓冲长度，可根据总线最大 ADU(256) 调整 */
#ifndef MB_RTU_RX_BUF_SIZE
#define MB_RTU_RX_BUF_SIZE  (256u)
#endif

/* 事件/统计信息 */
typedef struct {
	uint32_t frames_ok;        /* 成功接收的完整帧数量 */
	uint32_t crc_error;        /* CRC 错误次数 */
	uint32_t overflow;         /* 缓冲溢出次数 */
	uint32_t incomplete;       /* 超时导致的不完整帧次数 */
} mb_rtu_rx_stats_t;

/* 上层回调：收到完整帧(地址+PDU，不含CRC) */
typedef void (*mb_rtu_frame_cb_t)(const uint8_t *adu, uint16_t len);

/* 初始化与回调注册 */
void MB_RTU_LinkInit(void);
void MB_RTU_RegisterFrameReady(mb_rtu_frame_cb_t cb);

/* 发送：入参为 地址+PDU，内部自动追加 CRC 并触发发送 */
void MB_RTU_Send(const uint8_t *adu, uint16_t len);

/* 定时器驱动接口（由硬件层/调度在 T3.5 到期时调用） */
void MB_RTU_OnT35Expired(void);

/* 获取统计信息 */
void MB_RTU_GetRxStats(mb_rtu_rx_stats_t *out);

/* 与硬件层的对接：注册到 rs485_driver 的回调会调用这两个入口（不要在应用层直接调用） */
void MB_RTU_OnRxByte(uint8_t b);   /* 串口收到 1 字节 */
void MB_RTU_OnTxEmpty(void);       /* 串口发送缓冲空，继续送下一个字节 */

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_RTU_LINK_H__ */
