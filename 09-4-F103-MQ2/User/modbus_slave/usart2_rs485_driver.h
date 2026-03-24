/*
 * usart2_rs485_driver.h
 * RS-485 (USART3) 硬件抽象层 - STM32F103C8T6, 标准外设库
 *
 * 资源/参数:
 * - 串口: USART3 9600bps, 8E1, 偶校验
 * - 引脚: PB10 = TX, PB11 = RX, PB0 = DE, PB1 = RE (DE高=发送, RE低=接收)
 * - 收: 中断 + 环形缓冲区；每个新字节回调 modbus_rtu_on_rx_byte()
 * - 发: 方向置位为 TX -> 逐字节 TXE 中断送数 -> 最后启用 TC 等待移位寄存器清空 -> 切回 RX
 */

#ifndef __USART2_RS485_DRIVER_H__
#define __USART2_RS485_DRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* 环形缓冲区大小，可按需调整（2 的幂更高效） */
#ifndef RS485_RX_RING_SIZE
#define RS485_RX_RING_SIZE (256u)
#endif

/* 要求环形缓冲区大小为 2 的幂，便于位与取模 */
#if ((RS485_RX_RING_SIZE & (RS485_RX_RING_SIZE - 1u)) != 0)
#error "RS485_RX_RING_SIZE must be a power of two"
#endif

/*
 * 方向切换边沿的微小延时（单位：微秒）
 * - RS485_DIR_PRE_TX_DELAY_US: 在拉高 DE 进入发送之前的延时
 * - RS485_DIR_POST_TX_DELAY_US: 在 TC=1 后，拉低 DE 切回接收之前的延时
 * 注：9600bps下1字节≈1.15ms，需要足够延时确保MAX485完全发送完毕
 */
#ifndef RS485_DIR_PRE_TX_DELAY_US
#define RS485_DIR_PRE_TX_DELAY_US   (50u)
#endif
#ifndef RS485_DIR_POST_TX_DELAY_US
#define RS485_DIR_POST_TX_DELAY_US  (500u)  /* 增加到500us，确保最后一个字节完全发送 */
#endif

/* 初始化 USART3 与 RS-485 方向控制 GPIO (PB10/PB11/PB0/PB1) */
void RS485_Init(void);

/* 发送一段数据（阻塞式启动，余下由中断驱动；内部自动控制方向并在 TC 后切回 RX） */
void RS485_SendBytes(uint8_t *buf, uint16_t len);

/* 从环形缓冲区取 1 字节；返回 1=取到，0=无数据 */
uint8_t RS485_GetByte(uint8_t *data);

/* 手动切换方向（若调用 RS485_SetDirRx，会等待 TC=1 再拉低） */
void RS485_SetDirTx(void);
void RS485_SetDirRx(void);

/* 发送忙状态（可选查询） */
uint8_t RS485_IsBusy(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART2_RS485_DRIVER_H__ */
