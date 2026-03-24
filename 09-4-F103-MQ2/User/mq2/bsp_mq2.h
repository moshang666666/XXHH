#ifndef __BSP_MQ2_H
#define	__BSP_MQ2_H


#include "stm32f10x.h"

// ADC 编号选择
// 可以是 ADC1/2，如果使用ADC3，中断相关的要改成ADC3的
#define    ADC_APBxClock_FUN             RCC_APB2PeriphClockCmd
#define    ADCx                          ADC1
#define    ADC_CLK                       RCC_APB2Periph_ADC1

// ADC GPIO宏定义
// 注意：用作ADC采集的IO必须没有复用，否则采集电压会有影响
#define    ADC_GPIO_APBxClock_FUN        RCC_APB2PeriphClockCmd
#define    ADC_GPIO_CLK                  RCC_APB2Periph_GPIOA  
#define    ADC_PORT                      GPIOA
#define    ADC_PIN                       GPIO_Pin_4    
// ADC 通道宏定义
#define    ADC_CHANNEL                   ADC_Channel_4

// ADC 中断相关宏定义
#define    ADC_IRQ                       ADC1_2_IRQn
#define    ADC_IRQHandler                ADC1_2_IRQHandler

// DO 数字量GPIO宏定义
#define    MQ2_GPIO_APBxClock_FUN        RCC_APB2PeriphClockCmd
 #define    MQ2_GPIO_CLK                  RCC_APB2Periph_GPIOA
#define    MQ2_PORT                      GPIOA
#define    MQ2_PIN                       GPIO_Pin_5


#define RL                               10     /* 根据硬件原理图可知：RL = 10k */ 
#define R0                               20    /* MQ2在洁净空气中的阻值，官方数据手册没有给出，这是实验测试得出，想要准确请多次测试 */ 
#define VC                               5.0    /* MQ2供电电压,根据实际供电修改，默认接5V */
#define A                                43.03  /* y=ax^b 的 a */
#define B                                -1.66 /* y=ax^b 的 b */

void MQ2_Init(void);
float MQ2_Get_PPM(void);
uint16_t MQ2_GetPPM(void);  /* 返回整数 ppm 值，供 Modbus 寄存器使用 */

#endif /* __BSP_MQ2_H */

