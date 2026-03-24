/*
 * modbus_slave_protocol.c
 * Modbus 从站协议层:支持 0x03/0x06,检查地址,调用寄存器层,生成应答(含CRC)
 */
#include "modbus_slave_protocol.h"
#include "modbus_crc.h"
#include "usart2_rs485_driver.h"
#include "stm32f10x.h"
#include <stdio.h>

/* 应用层寄存器访问接口（由 application_data_manager.c 提供实现） */
extern uint16_t app_get_light_value(void);
extern int app_read_holding(uint16_t startAddr, uint16_t quantity, uint16_t *outBuf);
extern int app_write_single_holding(uint16_t addr, uint16_t value);

/* ===================== 协议工具 ===================== */
// 将 2 字节大端数据转换为 uint16_t
// 输入: p 指向 2 字节数据（高字节在前）
static inline uint16_t rd_u16be(const uint8_t *p) 
{ 
    return (uint16_t)((p[0] << 8) | p[1]); 
}

// 将 uint16_t 转换为 2 字节大端数据
// 输出: p 指向 2 字节数据（高字节在前）
static inline void wr_u16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

/* 静态发送缓冲区 - 避免栈上buf在函数返回后失效 */
static uint8_t s_tx_buffer[256];

/* 发送应答 ADU（地址+PDU）并自动追加 CRC（低字节->高字节） */
static void prv_send_adu_with_crc(const uint8_t *adu, uint16_t len)
{
    if (len + 2u > sizeof(s_tx_buffer))
        return; /* 保护 */
    for (uint16_t i = 0; i < len; i++)
        s_tx_buffer[i] = adu[i];
    uint16_t crc = Modbus_CRC16(adu, len);
    s_tx_buffer[len + 0] = (uint8_t)((crc >> 8) & 0xFF); /* crc_hi 先发 */
    s_tx_buffer[len + 1] = (uint8_t)(crc & 0xFF);        /* crc_lo 后发 */

    /* 发送期间禁止RXNE中断 */
    USART_ITConfig(USART3, USART_IT_RXNE, DISABLE);
    RS485_SendBytes(s_tx_buffer, (uint16_t)(len + 2u));
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
}

/* 发送异常应答（地址、功能码|0x80、异常码），广播地址(0)不响应 */
static void prv_send_exception(uint8_t addr, uint8_t fc, uint8_t ex)
{
    if (addr == 0)
        return; /* 广播不应答 */
    uint8_t adu[3];
    adu[0] = addr;
    adu[1] = (uint8_t)(fc | 0x80);
    adu[2] = ex; /* 异常码 */
    prv_send_adu_with_crc(adu, 3);
}

/* 异常码 */
#define MB_EX_ILLEGAL_FUNCTION 0x01
#define MB_EX_ILLEGAL_DATA_ADDRESS 0x02
#define MB_EX_ILLEGAL_DATA_VALUE 0x03
#define MB_EX_SLAVE_DEVICE_FAILURE 0x04

/* ===================== 主处理入口 ===================== */
void ModbusSlave_ProcessFrame(uint8_t *frame, uint16_t len)
{
    if (!frame || len < 2)
        return;

    uint8_t addr = frame[0];
    uint8_t fc = frame[1];

    /* 地址检查：仅处理发给本从站地址或广播地址(0)（广播仅执行写不回包） */
    if (!(addr == MODBUS_SLAVE_ADDR || addr == 0))
    {
        return; /* 不是给我的 */
    }

    switch (fc)
    {
    case 0x03: /* 读保持寄存器：请求 PDU = 功能(1)+起始地址(2)+数量(2) */
        if (len != 6)
        {
            prv_send_exception(addr, fc, MB_EX_ILLEGAL_DATA_VALUE);
            break;
        }
        {
            uint16_t start = rd_u16be(&frame[2]);
            uint16_t qty = rd_u16be(&frame[4]);
            if (qty == 0 || qty > 125)
            {
                prv_send_exception(addr, fc, MB_EX_ILLEGAL_DATA_VALUE);
                break;
            }

            uint16_t regs[125];
            int rc = app_read_holding(start, qty, regs);
            if (rc != 0)
            {
                prv_send_exception(addr, fc, MB_EX_ILLEGAL_DATA_ADDRESS);
                break;
            }

            if (addr == 0)
            { /* 广播不应答 */
                break;
            }
            /* 直接使用静态缓冲区构建响应 */
            uint16_t idx = 0;
            s_tx_buffer[idx++] = addr;
            s_tx_buffer[idx++] = fc;
            s_tx_buffer[idx++] = (uint8_t)(qty * 2); /* 字节数 */
            for (uint16_t i = 0; i < qty; i++)
            {
                wr_u16be(&s_tx_buffer[idx], regs[i]);
                idx += 2;
            }

            /* 追加CRC并发送 - Modbus CRC: 先发crc_hi, 后发crc_lo */
            uint16_t crc = Modbus_CRC16(s_tx_buffer, idx);
            s_tx_buffer[idx++] = (uint8_t)((crc >> 8) & 0xFF); /* crc_hi 先发 */
            s_tx_buffer[idx++] = (uint8_t)(crc & 0xFF);        /* crc_lo 后发 */

            /* 在打印和发送之前就禁止USART3接收中断,避免s_tx_buffer被破坏 */
            USART_ITConfig(USART3, USART_IT_RXNE, DISABLE);

            /* 调试:打印完整发送帧 */
            printf("[TX] %d bytes: ", idx);
            for (uint16_t i = 0; i < idx; i++)
                printf("%02X ", s_tx_buffer[i]);
            printf("(CRC=%04X)\r\n", crc);

            RS485_SendBytes(s_tx_buffer, idx);
            USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
        }
        break;

    case 0x06: /* 写单个保持寄存器：请求 PDU = 功能(1)+寄存器地址(2)+值(2)；应答回显 */
        if (len != 6)
        {
            prv_send_exception(addr, fc, MB_EX_ILLEGAL_DATA_VALUE);
            break;
        }
        {
            uint16_t reg = rd_u16be(&frame[2]);
            uint16_t val = rd_u16be(&frame[4]);
            int rc = app_write_single_holding(reg, val);
            if (rc != 0)
            {
                prv_send_exception(addr, fc, MB_EX_ILLEGAL_DATA_ADDRESS);
                break;
            }

            if (addr == 0)
            { /* 广播写不应答 */
                break;
            }
            /* 直接使用静态缓冲区构建回显响应 */
            s_tx_buffer[0] = addr;
            s_tx_buffer[1] = fc;
            s_tx_buffer[2] = frame[2];
            s_tx_buffer[3] = frame[3];
            s_tx_buffer[4] = frame[4];
            s_tx_buffer[5] = frame[5];

            /* 追加CRC并发送 */
            uint16_t crc = Modbus_CRC16(s_tx_buffer, 6);
            s_tx_buffer[6] = (uint8_t)((crc >> 8) & 0xFF);  
            s_tx_buffer[7] = (uint8_t)(crc & 0xFF);        

            /* 发送期间禁止RXNE中断 */
            USART_ITConfig(USART3, USART_IT_RXNE, DISABLE);
            RS485_SendBytes(s_tx_buffer, 8);
            USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
        }
        break;

    default:
        prv_send_exception(addr, fc, MB_EX_ILLEGAL_FUNCTION);
        break;
    }
}
