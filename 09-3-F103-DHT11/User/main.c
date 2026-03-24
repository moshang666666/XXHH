/**
 * @file    main.c
 * @version V1.0
 * @brief   Modbus RTU 从站2 - DHT11 温湿度传感器
 */

#include "stm32f10x.h"
#include "dwt_delay/core_delay.h"
#include "systick/bsp_SysTick.h"
#include "dht11/bsp_dht11.h"
#include "usart/bsp_usart.h"
#include "modbus_slave/usart2_rs485_driver.h"
#include "modbus_slave/tim4_tick_driver.h"
#include "modbus_slave/modbus_rtu_link.h"
#include "modbus_slave/application_data_manager.h"

// 软件延时
void Delay(__IO uint32_t nCount)
{
    for (; nCount != 0; nCount--)
        ;
}

/**
 * @brief  主函数 - Modbus RTU 从站（DHT11 温湿度传感器）
 * @param  无
 * @retval 无
 */
int main(void)
{
    uint32_t last_debug_tick = 0; /* 上次打印调试信息的时刻 */

    /* ===== 全局统一配置 NVIC 优先级分组 ===== */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); /* 2位抢占(0-3), 2位子优先级(0-3) */

    /* 初始化系统定时器（1us 中断，用于精确延时和毫秒计数） */
    SysTick_Init();

    /* 初始化 DWT 高精度延时（用于 DHT11 时序） */
    CPU_TS_TmrInit();

    /* 配置串口1（调试用，115200 8-N-1） */
    USART_Config();

    printf("\r\n========================================\r\n");
    printf("  Modbus RTU 从站 - DHT11 温湿度传感器\r\n");
    printf("  从站地址: 2\r\n");
    printf("  保持寄存器: 40011=温度(℃), 40012=湿度(%%RH)\r\n");
    printf("  通信参数: RS-485 9600bps 8E1\r\n");
    printf("  DHT11引脚: PB14 (单总线)\r\n");
    printf("  RS-485引脚: PB10(TX) PB11(RX) PB0(DE) PB1(RE)\r\n");
    printf("========================================\r\n\r\n");

    /* 初始化 1ms 系统节拍（TIM4，用于 Modbus 帧超时判断） */
    TIM4_TickInit();

    /* 初始化 RS-485 通信（USART3, PB10=TX, PB11=RX, PB0=DE, PB1=RE） */
    RS485_Init();

    /* 初始化 RTU 帧层（字节流拼帧 + CRC 校验） */
    MB_RTU_Init();

    /* 初始化应用数据层（初始化 DHT11，初始化寄存器表） */
    AppData_Init();

    printf("系统初始化完成，等待主站轮询...\r\n");
    printf("注意: DHT11 读取间隔 >=2 秒，数据仅使用整数部分\r\n\r\n");

    /* 做一次预读取并丢弃，避免 DHT11 上电后首次数据不稳定 */
    {
        DHT11_Data_TypeDef dht11_dummy;
        uint8_t dummy_result = DHT11_Read_TempAndHumidity(&dht11_dummy);
        if (dummy_result == SUCCESS) {
            printf("[预读取] DHT11 响应正常: Temp=%d℃, Humi=%d%%RH (数据已丢弃)\r\n", 
                   dht11_dummy.temp_int, dht11_dummy.humi_int);
        } else {
            printf("[预读取] DHT11 响应失败，请检查硬件连接\r\n");
        }
    }

    printf("开始读取 DHT11 数据...\r\n\r\n");

    /* 主循环 */
    while (1)
    {
        /* 周期处理帧层：判断 T3.5 超时并完成一帧 */
        MB_RTU_Process();

        /* 周期更新 DHT11 温湿度数据到寄存器（内部自动控制读取间隔 >=2 秒） */
        AppData_UpdateDHT11();

        /* 打印调试信息：每 5 秒打印一次当前温湿度值 */
        if (TIM4_GetElapsed(last_debug_tick) >= 5000u)
        {
            last_debug_tick = TIM4_GetTick();

            uint16_t temp = app_get_temperature();
            uint16_t humi = app_get_humidity();

            printf("[DHT11] 温度: %d℃, 湿度: %d%%RH\r\n", temp, humi);
        }
    }
}
/*********************************************END OF FILE**********************/
