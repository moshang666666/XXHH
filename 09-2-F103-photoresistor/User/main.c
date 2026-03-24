/**
  * @file    main.c
  * @version V1.0
  * @brief   Modbus RTU 从站 - 光敏电阻采集（从站地址1，寄存器40001）
  */
 
  
#include "stm32f10x.h"
#include "bsp_usart.h"
#include "photoresistor/bsp_photoresistor.h"
#include "modbus_slave/usart2_rs485_driver.h"
#include "modbus_slave/tim4_tick_driver.h"
#include "modbus_slave/modbus_rtu_link.h"
#include "modbus_slave/application_data_manager.h"

//ADC的转换值
extern __IO uint16_t ADC_ConvertedValue;


// 软件延时
void Delay(__IO uint32_t nCount)
{
  for(; nCount != 0; nCount--);
} 

/**
  * @brief  主函数
  * @param  无
  * @retval 无
  */
int main(void)
{	
	uint32_t last_update_tick = 0; /* 上次更新寄存器的时刻 */
	
	/* ===== 全局统一配置 NVIC 优先级分组 ===== */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); /* 2位抢占(0-3), 2位子优先级(0-3) */
	
	/* 配置串口1（调试用） */
	USART_Config();
	
    // 通常打印信息在串口1上
	printf("\r\n========================================\r\n");
	printf("  Modbus RTU 从站 - 光敏电阻采集\r\n");
	printf("  从站地址: 1\r\n");
	printf("  保持寄存器: 40001 (地址0)\r\n");
	printf("  通信参数: RS-485 9600bps 8E1\r\n");
	printf("========================================\r\n\r\n");
	
	/* 初始化 1ms 系统节拍（TIM4） */
	TIM4_TickInit();
	
	/* 初始化 RS-485 通信（USART2, PA2=TX, PA3=RX, PA4=DE/RE） */
	RS485_Init();
	
	/* 初始化 RTU 帧层（字节流拼帧 + CRC 校验） */
	MB_RTU_Init();
	
	/* 初始化应用数据层（启动 ADC 采集，初始化寄存器表） */
	AppData_Init();
	
	printf("系统初始化完成，等待主站轮询...\r\n\r\n");
	
	// /* ===== 测试 RS-485 (USART3) 硬件连通性 ===== */
	// printf("[测试] 通过USART3发送测试字节到RS-485...\r\n");
	
	// // 手动设置为发送模式（DE=高，RE=高）
	// GPIO_SetBits(GPIOB, GPIO_Pin_0);   // DE=高
	// GPIO_SetBits(GPIOB, GPIO_Pin_1);   // RE=高
	// Delay(100); // 短暂延时让MAX485稳定
	
	// // 直接向USART3发送测试数据
	// uint8_t test_data[] = {0x55, 0xAA, 0x01, 0x02, 0x03};
	// for(int i = 0; i < 5; i++) {
	// 	USART_SendData(USART3, test_data[i]);
	// 	while(USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
	// }
	// while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET); // 等待发送完成
	
	// // 切回接收模式（DE=低，RE=低）
	// GPIO_ResetBits(GPIOB, GPIO_Pin_0); // DE=低
	// GPIO_ResetBits(GPIOB, GPIO_Pin_1); // RE=低
	
	// printf("[测试] USART3发送完成，请查看RS-485工具是否收到: 55 AA 01 02 03\r\n\r\n");
	
	// Delay(0x3FFFFF); // 延时观察
	
	/* 主循环 */
	while (1)
	{	
		/* 周期处理帧层：判断 T3.5 超时并完成一帧 */
		MB_RTU_Process();
		
		/* 每 100ms 更新一次光敏 ADC 值到寄存器 */
		if (TIM4_GetElapsed(last_update_tick) >= 100u)
		{
			last_update_tick = TIM4_GetTick();
			AppData_UpdateLight();
            
			
			/* 调试：周期打印当前 ADC 值（可选，避免刷屏可注释掉） */
			// printf("[ADC] Light=%d\r\n", ADC_ConvertedValue);
		}
	}
}
/*********************************************END OF FILE**********************/
