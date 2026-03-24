#ifndef __USART_H
#define __USART_H

#include "stm32f4xx.h"
#include <stdio.h>


//引脚定义
/*******************************************************/
#define USARTx                             USART1

/* 不同的串口挂载的总线不一样，时钟使能函数也不一样
 * 串口1/6                RCC_APB2PeriphClockCmd
 * 串口2/3/4/5/7          RCC_APB1PeriphClockCmd
 */
#define USARTx_CLK                         RCC_APB2Periph_USART1
#define USARTx_CLOCKCMD                    RCC_APB2PeriphClockCmd
#define USARTx_BAUDRATE                    115200  //串口波特率

#define USARTx_RX_GPIO_PORT                GPIOA
#define USARTx_RX_GPIO_CLK                 RCC_AHB1Periph_GPIOA
#define USARTx_RX_PIN                      GPIO_Pin_10
#define USARTx_RX_AF                       GPIO_AF_USART1
#define USARTx_RX_SOURCE                   GPIO_PinSource10

#define USARTx_TX_GPIO_PORT                GPIOA
#define USARTx_TX_GPIO_CLK                 RCC_AHB1Periph_GPIOA
#define USARTx_TX_PIN                      GPIO_Pin_9
#define USARTx_TX_AF                       GPIO_AF_USART1
#define USARTx_TX_SOURCE                   GPIO_PinSource9

#define USART_IRQHandler                  USART1_IRQHandler
#define USART_IRQ                 		  USART1_IRQn

/************************************************************/

void USARTx_Config(void);

/* 从中断接收的行缓冲中取一行（去除结尾的\r/\n），返回长度，0表示无可用命令 */
int USART_GetLine(char *buf, int maxlen);

#endif /* __USART_H */

