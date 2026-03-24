/*
 * modbus_rtu_link.c
 * Modbus RTU 帧层实现：UART 字节流拼帧 + T3.5 超时 + CRC 校验
 */
#include "modbus_rtu_link.h"
#include "modbus_crc.h"
#include "tim4_tick_driver.h"   /* 提供 TIM4_GetTick/TIM4_GetElapsed 毫秒节拍 */
#include "modbus_slave_protocol.h" /* 协议层入口 */
#include <stdio.h>

/* 接收状态 */
typedef enum {
    RX_IDLE = 0,   /* 空闲：尚未接收 */
    RX_RCV         /* 接收中：累积字节，等待 T3.5 作为帧结束 */
} rx_state_t;

static volatile rx_state_t s_rx_state = RX_IDLE;        /* 当前接收状态 */
static uint8_t  s_rx_buf[MB_RTU_RX_BUF_SIZE];           /* 接收缓冲：地址+PDU+CRC */
static uint16_t s_rx_pos = 0;                           /* 已写入的字节数 */
static uint32_t s_last_rx_tick_ms = 0;                  /* 最近一次收到字节的 Tick(ms) */
static mb_rtu_frame_cb_t s_frame_cb = 0;                /* 完整帧回调：地址+PDU */

/* 复位接收 */
static inline void prv_rx_reset(void)
{
    s_rx_pos = 0;
    s_rx_state = RX_IDLE;
}

void MB_RTU_Init(void)
{
    prv_rx_reset();
    /* 注册协议层处理函数 */
    s_frame_cb = ModbusSlave_ProcessFrame;
}

void MB_RTU_RegisterFrameReady(mb_rtu_frame_cb_t cb)
{
    s_frame_cb = cb;
}

/* 下层串口在 RXNE 时调用此函数（usart2_rs485_driver.c 中已调用同名符号） */
void modbus_rtu_on_rx_byte(uint8_t byte)
{
    if (s_rx_state == RX_IDLE) {
        s_rx_pos = 0;
        s_rx_state = RX_RCV;
    }

    if (s_rx_pos < MB_RTU_RX_BUF_SIZE) {
        s_rx_buf[s_rx_pos++] = byte;  /* 缓冲写入 */
    } else {
        /* 溢出：简单丢弃后续字节，等待 T3.5 触发丢帧 */
    }

    /* 记录最近一次接收时间 */
    s_last_rx_tick_ms = TIM4_GetTick();
}

/**
 * @brief 判断是否超过 T3.5，若是则完成一帧并做 CRC 校验
 * 
 */
void MB_RTU_Process(void)
{
    if (s_rx_state == RX_RCV) {
    uint32_t elapsed = TIM4_GetElapsed(s_last_rx_tick_ms);
        if (elapsed >= MB_RTU_T35_MS) {
            /* 一帧结束：至少需要 地址(1)+功能(1)+CRC(2) */
            if (s_rx_pos >= 4u) {
                /* 校验：对“地址+PDU+CRC”整体做 CRC，正确应为 0 */
                uint16_t crc = Modbus_CRC16(s_rx_buf, s_rx_pos);
                if (crc == 0) {
                    if (s_frame_cb) {
                        /* 去掉尾部 2 字节 CRC，交付 地址+PDU */
                        s_frame_cb(s_rx_buf, (uint16_t)(s_rx_pos - 2u));
                    }
                }
                /* 错误帧直接丢弃 */
            }
            prv_rx_reset();
        }
    }
}
