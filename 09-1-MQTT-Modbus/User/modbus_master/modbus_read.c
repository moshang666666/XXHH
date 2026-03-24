/**
 * @file modbus_read.c
 * @brief Modbus主站读取模块:负责构造并发送读请求,处理从站响应并更新应用数据缓存。
 * @author Yukikaze
 * @date 2025-11-26
 *
 * 功能说明:
 *   - 从站1(地址0x01): 读取光敏ADC值,保持寄存器40001,数量1
 *   - 从站2(地址0x02): 读取温湿度值,保持寄存器40011~40012,数量2
 *   - 自动计算CRC16校验码并通过RTU链路层发送
 *   - 解析从站响应帧,提取寄存器数据并更新到应用数据管理层
 *
 * Modbus RTU 帧格式:
 *   [从站地址(1)] [功能码(1)] [起始地址(2)] [寄存器数量(2)] [CRC16(2)]
 *   响应格式: [从站地址(1)] [功能码(1)] [字节数(1)] [数据(N*2)] [CRC16(2)]
 *
 * CRC16 参数:
 *   - 多项式: 0xA001 (反向多项式)
 *   - 初值: 0xFFFF
 *   - 字节序: 先发高字节,后发低字节
 */

#include "modbus_rtu_link.h"
#include "modbus_crc.h"
#include "rs485_driver.h"
#include "application_data_manager.h"  /* 应用数据管理层 */
#include "modbus_read.h"
#include <stdio.h>

/**
 * @brief 接收从站响应帧回调函数:解析响应数据并更新应用层缓存
 * @author Yukikaze
 *
 * @param adu 指向完整ADU帧的指针(地址+PDU,不含CRC)
 * @param len ADU帧长度(字节数)
 *
 * @note 由RTU链路层在T3.5超时后,CRC校验通过时调用
 * @note 本函数负责:
 *       1. 检查响应帧格式合法性(最小长度、功能码)
 *       2. 判断是否为异常响应(功能码最高位为1)
 *       3. 解析寄存器数据(大端序:高字节在前)
 *       4. 调用app_data_update_from_modbus()更新应用层缓存
 *       5. 打印调试信息(完整帧、寄存器值)
 */
static void prv_on_frame(const uint8_t *adu, uint16_t len)
{
    /* adu = 地址 + PDU (不含 CRC)
     * 最小响应帧: 地址(1) + 功能码(1) + 字节数(1) + 数据(2) = 5字节
     */
    if (!adu || len < 5) {
        /* 长度不足以承载最小响应:拒绝处理 */
        return;
    }
    uint8_t addr = adu[0];  /* 从站地址:0x01(从站1)或0x02(从站2) */
    uint8_t fc   = adu[1];  /* 功能码:0x03(读保持寄存器)或0x83(异常响应) */

    /* 检查异常响应:功能码最高位为1表示从站返回异常
     * 正常功能码0x03 -> 异常响应0x83 (0x03 | 0x80)
     * 异常码在adu[2]中,常见值:
     *   0x01: 非法功能码
     *   0x02: 非法数据地址
     *   0x03: 非法数据值
     *   0x04: 从站设备故障
     */
    if ((fc & 0x80) != 0) {
        printf("[RX] Slave%u Exception response: code=0x%02X\r\n", addr, adu[2]);
        return;
    }
    if (fc != 0x03) return;

    /* 响应格式：地址(1) + 功能(1) + 字节数(1) + 数据(2*N) */
    uint8_t bytecnt = adu[2];
    if (bytecnt < 2 || len < (uint16_t)(3 + bytecnt)) return;

    /* 打印完整接收帧（不含CRC）用于调试 */
    printf("[RX] Complete frame (%u bytes): ", len);
    for (uint16_t i = 0; i < len; i++) {
        printf("%02X ", adu[i]);
    }
    printf("\r\n");

    /* 处理从站1：光敏ADC（40001） */
    if (addr == 0x01) {
        if (bytecnt >= 2) {
            uint16_t reg0 = (uint16_t)(((uint16_t)adu[3] << 8) | adu[4]);
            app_data_update_from_modbus(0x01, 40001, reg0);
            printf("[RX] Slave1 Reg40001(Light ADC) = %u (0x%04X)\r\n", reg0, reg0);
        }
    }
    /* 处理从站2：温湿度（40011~40012） */
    else if (addr == 0x02) {
        if (bytecnt >= 4) {
            /* 第一个寄存器：温度（40011） */
            uint16_t temp = (uint16_t)(((uint16_t)adu[3] << 8) | adu[4]);
            app_data_update_from_modbus(0x02, 40011, temp);
            printf("[RX] Slave2 Reg40011(Temp) = %u (0x%04X)\r\n", temp, temp);
            
            /* 第二个寄存器：湿度（40012） */
            uint16_t hum = (uint16_t)(((uint16_t)adu[5] << 8) | adu[6]);
            app_data_update_from_modbus(0x02, 40012, hum);
            printf("[RX] Slave2 Reg40012(Hum) = %u (0x%04X)\r\n", hum, hum);
        }
    }
    /* 处理从站3：MQ2烟雾浓度（40021） */
    else if (addr == 0x03) {
        if (bytecnt >= 2) {
            /* MQ2烟雾浓度值(ppm,范围50~10000) */
            uint16_t mq2_ppm = (uint16_t)(((uint16_t)adu[3] << 8) | adu[4]);
            app_data_update_from_modbus(0x03, 40021, mq2_ppm);
            printf("[RX] Slave3 Reg40021(MQ2 ppm) = %u (0x%04X)\r\n", mq2_ppm, mq2_ppm);
        }
    }
}

/* 中间层转发：底层串口 RXNE 回调 -> MB_RTU_OnRxByte */
static void prv_rs485_rx(uint8_t b) { MB_RTU_OnRxByte(b); }
/* 底层串口 TXE 回调 -> MB_RTU_OnTxEmpty */
static void prv_rs485_txempty(void) { MB_RTU_OnTxEmpty(); }

/**
 * @brief 初始化硬件 + RTU 链路层 + 回调绑定
 * 
 */
void Modbus_Read_Init(void)
{
    /* 初始化硬件与链路层 */
    RS485_Init();
    /* 配置一个 T3.5 周期：9600 8E1 下约 3.5 字符 ≈ 4ms；4ms / 50us ≈ 80 tick */
    RS485_TimerInit(80);
    MB_RTU_LinkInit();
    MB_RTU_RegisterFrameReady(prv_on_frame);

    /* 绑定底层串口回调到 RTU 链路层 */
    RS485_RegisterCallbacks(prv_rs485_rx, prv_rs485_txempty);
}

/**
 * @brief 构造并发送从站1读取光敏ADC请求（01 03 00 00 00 01：读保持寄存器 40001 数量1）
 * 
 */
void Modbus_ReadSlave1_Light(void)
{
    uint8_t adu_no_crc[6]; /* 地址+PDU，不含 CRC */
    adu_no_crc[0] = 0x01;  /* 从站地址 */
    adu_no_crc[1] = 0x03;  /* 功能码：读保持寄存器 */
    adu_no_crc[2] = 0x00;  /* 起始地址高字节（0x0000） */
    adu_no_crc[3] = 0x00;  /* 起始地址低字节 */
    adu_no_crc[4] = 0x00;  /* 寄存器数量高字节（0x0001） */
    adu_no_crc[5] = 0x01;  /* 寄存器数量低字节 */

    /* 计算 CRC 方便调试打印（MB_RTU_Send 会自动重新计算并附加） */
    uint16_t crc = Modbus_CRC16(adu_no_crc, 6);
    uint8_t crc_hi = (uint8_t)((crc >> 8) & 0xFF);
    uint8_t crc_lo = (uint8_t)(crc & 0xFF);

    printf("[TX] Slave1 Light (addr=1 read holding 40001 qty=1):\r\n");
    printf("     01 03 00 00 00 01 %02X %02X\r\n", crc_hi, crc_lo);

    /* 通过 RTU 链路层发送（自动附加 CRC） */
    MB_RTU_Send(adu_no_crc, 6);
}

/**
 * @brief 构造并发送从站2读取温湿度请求（02 03 00 0A 00 02：读保持寄存器 40011~40012 数量2）
 * 
 */
void Modbus_ReadSlave2_TempHum(void)
{
    uint8_t adu_no_crc[6]; /* 地址+PDU，不含 CRC */
    adu_no_crc[0] = 0x02;  /* 从站地址2 */
    adu_no_crc[1] = 0x03;  /* 功能码：读保持寄存器 */
    adu_no_crc[2] = 0x00;  /* 起始地址高字节（0x000A，即40011） */
    adu_no_crc[3] = 0x0A;  /* 起始地址低字节 */
    adu_no_crc[4] = 0x00;  /* 寄存器数量高字节（0x0002） */
    adu_no_crc[5] = 0x02;  /* 寄存器数量低字节 */

    /* 计算 CRC 方便调试打印（MB_RTU_Send 会自动重新计算并附加） */
    uint16_t crc = Modbus_CRC16(adu_no_crc, 6);
    uint8_t crc_hi = (uint8_t)((crc >> 8) & 0xFF);
    uint8_t crc_lo = (uint8_t)(crc & 0xFF);

    printf("[TX] Slave2 TempHum (addr=2 read holding 40011~40012 qty=2):\r\n");
    printf("     02 03 00 0A 00 02 %02X %02X\r\n", crc_hi, crc_lo);

    /* 通过 RTU 链路层发送（自动附加 CRC） */
    MB_RTU_Send(adu_no_crc, 6);
}

/**
 * @brief 构造并发送从站3读取MQ2烟雾浓度请求（03 03 00 14 00 01：读保持寄存器 40021 数量1）
 * 
 * @note 从站3为MQ2烟雾传感器模块,寄存器40021存储烟雾浓度ppm值
 *       ppm范围: 50~10000,步长1
 */
void Modbus_ReadSlave3_MQ2(void)
{
    uint8_t adu_no_crc[6]; /* 地址+PDU，不含 CRC */
    adu_no_crc[0] = 0x03;  /* 从站地址3 */
    adu_no_crc[1] = 0x03;  /* 功能码：读保持寄存器 */
    adu_no_crc[2] = 0x00;  /* 起始地址高字节（0x0014，即40021） */
    adu_no_crc[3] = 0x14;  /* 起始地址低字节 */
    adu_no_crc[4] = 0x00;  /* 寄存器数量高字节（0x0001） */
    adu_no_crc[5] = 0x01;  /* 寄存器数量低字节 */

    /* 计算 CRC 方便调试打印（MB_RTU_Send 会自动重新计算并附加） */
    uint16_t crc = Modbus_CRC16(adu_no_crc, 6);
    uint8_t crc_hi = (uint8_t)((crc >> 8) & 0xFF);
    uint8_t crc_lo = (uint8_t)(crc & 0xFF);

    printf("[TX] Slave3 MQ2 (addr=3 read holding 40021 qty=1):\r\n");
    printf("     03 03 00 14 00 01 %02X %02X\r\n", crc_hi, crc_lo);

    /* 通过 RTU 链路层发送（自动附加 CRC） */
    MB_RTU_Send(adu_no_crc, 6);
}

