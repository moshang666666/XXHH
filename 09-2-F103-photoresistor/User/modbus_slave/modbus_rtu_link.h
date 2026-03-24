/*
 * modbus_rtu_link.h
 * Modbus RTU 帧层：UART 字节流拼帧 + T3.5 超时 + CRC 校验
 */
#ifndef __MODBUS_RTU_LINK_H__
#define __MODBUS_RTU_LINK_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 默认接收缓冲区与 T3.5 超时（ms）：9600bps 8E1 -> 1 字符约 1.146ms，T3.5≈4ms，取 5ms 稍留裕度 */
#ifndef MB_RTU_RX_BUF_SIZE
#define MB_RTU_RX_BUF_SIZE   (256u)
#endif
#ifndef MB_RTU_T35_MS
#define MB_RTU_T35_MS        (5u)
#endif

/* 完整帧回调（交付地址+PDU，不含CRC） */
typedef void (*mb_rtu_frame_cb_t)(const uint8_t *adu, uint16_t len);

/* 初始化与回调注册 */
void MB_RTU_Init(void);
void MB_RTU_RegisterFrameReady(mb_rtu_frame_cb_t cb);

/* 周期处理：在主循环中调用，用于判断 T3.5 超时并完成一帧 */
void MB_RTU_Process(void);

/* 串口接收时机：由下层 USART RXNE 中断调用（已在 usart2_rs485_driver.c 中调用） */
void modbus_rtu_on_rx_byte(uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_RTU_LINK_H__ */
