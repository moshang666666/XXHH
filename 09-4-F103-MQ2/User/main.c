/**
 * @file    main.c
 * @version V1.0
 * @brief   MQ2 烟雾传感器 Modbus RTU 从站（从站地址：3）
 *          寄存器映射：40021 - 烟雾浓度值（整数，单位：ppm）
 */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x.h"
#include "usart/bsp_usart.h"
#include "mq2/bsp_mq2.h"
#include "./modbus_slave/application_data_manager.h"
#include "./modbus_slave/modbus_rtu_link.h"
#include "./modbus_slave/usart2_rs485_driver.h"
#include "./modbus_slave/tim4_tick_driver.h"

extern __IO uint16_t ADC_ConvertedValue;

static void Delay(__IO uint32_t nCount) // 简单的延时函数
{
    for (; nCount != 0; nCount--)
        ;
}

/**
 * @brief  主函数
 * @param  无
 * @retval 无
 */
int main(void)
{
    /*初始化USART 配置模式为 115200 8-N-1（用于调试打印） */
    USART_Config();

    /* Modbus RTU 从站初始化 */
    RS485_Init();     /* RS-485 驱动初始化（USART3 @ 9600bps, 8E1） */
    TIM4_TickInit();  /* 定时器初始化（1ms 周期，用于 Modbus 帧超时判断） */
    AppData_Init();   /* 应用数据层初始化（MQ2 传感器初始化） */
    MB_RTU_Init();    /* Modbus RTU 链路层初始化 */

    printf("\r\n ----MQ2 烟雾传感器 Modbus RTU 从站----\r\n");
    printf("\r\n ----从站地址：3，寄存器：40021（烟雾浓度 ppm）----\r\n");
    printf("\r\n ----检测浓度范围：300~10000ppm----\r\n");
    printf("\r\n ----传感器预热中，请等待约 20 秒...----\r\n");

    /* 预热延时（MQ2 需要约 20 秒预热时间） */
    Delay(0xFFFFFF); /* 简单延时，实际应用可优化 */

    printf("\r\n ----传感器预热完成，开始工作----\r\n");

    while (1)
    {
        /* Modbus RTU 帧处理（接收、解析、响应） */
        MB_RTU_Process();

        /* 更新 MQ2 烟雾浓度到 Modbus 寄存器 */
        AppData_UpdateMQ2();
    }
}

/*********************************************END OF FILE**********************/
