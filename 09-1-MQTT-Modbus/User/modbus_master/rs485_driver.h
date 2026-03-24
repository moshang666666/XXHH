/**
 * @file rs485_driver.h
 * @brief Modbus RTU 硬件抽象层(USART2 + RS-485)接口定义
 *
 * - MCU: STM32F429 
 * - 串口: USART2  波特率 9600，格式 8E1（8 数据位 + 偶校验 + 1 停止位）
 * - 引脚: PD5 = TX (AF7), PD6 = RX (AF7), PB8 = DE/RE (推挽输出, 高=发送, 低=接收)
 *
 * - 初始化 GPIO / USART / 定时器资源（只做硬件，不做协议逻辑）。
 * - 提供方向控制宏与函数，统一 RS-485 驱动风格。
 * - 在中断中回调上层注册的字节接收、发送缓冲空函数指针，不直接处理 Modbus 状态机。
 * - 提供 T3.5 等时间片定时启动/停止接口；时间单位采用“50us tick”与协议层保持一致。
 *
 */

#ifndef __RS485_DRIVER_H__
#define __RS485_DRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== 基础类型兼容处理 ========================== */
#include <stdint.h>
#include <stddef.h>

#ifndef BOOL
typedef unsigned char BOOL;
#endif
#ifndef TRUE
#define TRUE  (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

/* ========================== 对上层的回调类型 ========================== */
typedef void (*RS485_RxCallback_t)(uint8_t byte);          /* 收到 1 字节 */
typedef void (*RS485_TxEmptyCallback_t)(void);             /* TXE 发送缓冲空，可写下一个字节 */

extern RS485_RxCallback_t      g_rs485RxCallback;          /* 上层注册的接收回调 */
extern RS485_TxEmptyCallback_t g_rs485TxEmptyCallback;     /* 上层注册的发送空回调 */

void RS485_RegisterCallbacks(RS485_RxCallback_t rxCb,
							 RS485_TxEmptyCallback_t txEmptyCb);

/* ========================== 初始化与基本操作 ========================== */
/**
 * @brief 初始化 RS-485 驱动：GPIO, USART2, 定时器。
 *        - 配置 PD5/PD6 复用为 USART2
 *        - 配置 PB8 为推挽输出, 初始接收状态(低)
 *        - 配置 USART2: 9600, 8E1
 *        - 使能所需中断 (RXNE, TXE, 以及定时器中断)
 */
void RS485_Init(void);

/**
 * @brief 进入发送方向 (PB8 = 高)。通常在开始发送前调用。
 */
void RS485_SetDirectionTx(void);

/**
 * @brief 进入接收方向 (PB8 = 低)。发送完成后调用。
 */
void RS485_SetDirectionRx(void);

/* ========================== 方向切换延时配置 ========================== */
/**
 * @brief RS-485 在“使能发送/接收”之前插入的短延时（单位：毫秒）。
 *        在切换前加入小延时，避免收发器内部状态尚未稳定导致的边沿异常。按需调整 1~3ms。
 */
#ifndef RS485_DIR_DELAY_MS
#define RS485_DIR_DELAY_MS (1)
#endif


/**
 * @brief 读取 1 字节（若 RXNE 有数据）。
 * @param pByte 输出指针
 * @return 1=成功读到字节, 0=无数据
 */
uint8_t RS485_ReadByte(uint8_t *pByte);

/**
 * @brief 写 1 字节进发送数据寄存器（注意：需在 TXE=1 或调用回调时执行）。
 * @param byte 待发送字节
 * @return 1=写入成功, 0=忙/未准备好
 */
uint8_t RS485_WriteByte(uint8_t byte);

/**
 * @brief 控制 USART2 中断使能，作为更底层的开关；通常由帧层包装。
 * @param rxEnable 1=使能接收中断
 * @param txEnable 1=使能发送空中断
 */
void RS485_EnableIRQ(uint8_t rxEnable, uint8_t txEnable);

/* ========================== 定时器(T3.5 等)接口 ========================== */
/**
 * @brief 初始化 T3.5 定时所用的基础定时器，计数单位：50us tick。
 * @param t35_50us  T3.5 时间转换为 50us tick 的数值（由上层计算传入）
 */
void RS485_TimerInit(uint16_t t35_50us);

/**
 * @brief 启动 T3.5 定时（接收帧边界判定）。到期后在定时器 ISR 回调上层处理。
 */
void RS485_TimerStart_T35(void);

/**
 * @brief 启动一个自定义微秒定时（用于主站的转换延时或响应超时）。
 * @param us 需要延时的微秒数（内部转换为自动重装值）
 */
void RS485_TimerStart_Us(uint32_t us);

/**
 * @brief 停止所有正在运行的定时器。
 */
void RS485_TimerStop(void);

/* ========================== 内部/调试辅助 ========================== */
uint32_t RS485_GetErrorFlags(void);  /* 返回 USART SR 中的错误标志(帧错/奇偶/过载等) */

#ifdef __cplusplus
}
#endif

#endif /* __RS485_DRIVER_H__ */
