/**
 * @file    bsp_usart.c
 * @version V1.0
 * @brief   重定向c库printf函数到usart端口
 */

#include "./usart/bsp_usart.h"

/* 串口接收命令的行缓冲（以回车/换行结束） */
#define USART_RX_BUF_SIZE 128
static volatile char s_rx_buf[USART_RX_BUF_SIZE];
static volatile uint16_t s_rx_idx = 0;       /* 正在写入的位置 */
static volatile uint8_t s_line_ready = 0;    /* 是否有完整一行可读 */
static volatile uint16_t s_line_len = 0;     /* 完整行长度 */

/**
 * @brief NVIC 配置
 * 
 */
static void NVIC_Configuration(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    /* 选择嵌套向量中断控制器 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    /* 配置USART为中断源 */
    NVIC_InitStructure.NVIC_IRQChannel = USART_IRQ;
    /* 抢占优先级设置为1 */
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    /* 子优先级设置为1 */
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    /* 使能中断 */
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    /* 初始化NVIC */
    NVIC_Init(&NVIC_InitStructure);
}

/**
 * @brief  USART GPIO 配置,工作模式配置。115200 8-N-1
 * @param  无
 * @retval 无
 */
void USARTx_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_AHB1PeriphClockCmd(USARTx_RX_GPIO_CLK | USARTx_TX_GPIO_CLK, ENABLE);

    /* 使能 USART 时钟 */
    USARTx_CLOCKCMD(USARTx_CLK, ENABLE);

    /* GPIO初始化 */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    /* 配置Tx引脚为复用功能  */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Pin = USARTx_TX_PIN;
    GPIO_Init(USARTx_TX_GPIO_PORT, &GPIO_InitStructure);

    /* 配置Rx引脚为复用功能 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Pin = USARTx_RX_PIN;
    GPIO_Init(USARTx_RX_GPIO_PORT, &GPIO_InitStructure);

    /* 连接 PXx 到 USARTx_Tx*/
    GPIO_PinAFConfig(USARTx_RX_GPIO_PORT, USARTx_RX_SOURCE, USARTx_RX_AF);

    /*  连接 PXx 到 USARTx__Rx*/
    GPIO_PinAFConfig(USARTx_TX_GPIO_PORT, USARTx_TX_SOURCE, USARTx_TX_AF);

    /* 波特率设置：DEBUG_USART_BAUDRATE */
    USART_InitStructure.USART_BaudRate = USARTx_BAUDRATE;
    /* 字长(数据位+校验位)：8 */
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    /* 停止位：1个停止位 */
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    /* 校验位选择：不使用校验 */
    USART_InitStructure.USART_Parity = USART_Parity_No;
    /* 硬件流控制：不使用硬件流 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    /* USART模式控制：同时使能接收和发送 */
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    /* 完成USART初始化配置 */
    USART_Init(USARTx, &USART_InitStructure);

    /* 使能串口 */
    USART_Cmd(USARTx, ENABLE);

    /* 使能串口接收中断 */
    USART_ITConfig(USARTx, USART_IT_RXNE, ENABLE);
    /* 配置并打开NVIC中断 */
    NVIC_Configuration();
}

/// 重定向c库函数printf到串口，重定向后可使用printf函数
int fputc(int ch, FILE *f)
{
    /* 发送一个字节数据到串口 */
    USART_SendData(USARTx, (uint8_t)ch);

    /* 等待发送完毕 */
    while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET)
        ;

    return (ch);
}

/// 重定向c库函数scanf到串口，重写向后可使用scanf、getchar等函数
int fgetc(FILE *f)
{
    while (USART_GetFlagStatus(USARTx, USART_FLAG_RXNE) == RESET)
        ;
    return (int)USART_ReceiveData(USARTx);
}

/**
 * @brief USART中断服务程序：接收字符，按行缓存。行结束符支持\r、\n、或\r\n。
 */
void USART_IRQHandler(void)
{
    if (USART_GetITStatus(USARTx, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = (uint8_t)USART_ReceiveData(USARTx);

        if (!s_line_ready)
        {
            if (ch == '\r' || ch == '\n')
            {
                if (s_rx_idx > 0)
                {
                    /* 完整一行就绪（去除结尾的回车换行） */
                    s_line_len = s_rx_idx;
                    s_line_ready = 1;
                }
                /* 准备下一行（若空行，则直接清空） */
                s_rx_idx = 0;
            }
            else
            {
                if (s_rx_idx < USART_RX_BUF_SIZE - 1)
                {
                    s_rx_buf[s_rx_idx++] = (char)ch;
                }
                else
                {
                    /* 溢出：丢弃本行，置错误并清空 */
                    s_rx_idx = 0;
                }
            }
        }
        /* 清除中断标志 */
        USART_ClearITPendingBit(USARTx, USART_IT_RXNE);
    }
}

/**
 * @brief  读取一行命令到用户缓冲区（由中断接收）。
 * @param  buf 用户缓冲区
 * @param  maxlen 缓冲区最大长度
 * @return >0: 拷贝的字节数；0: 无可用行
 */
int USART_GetLine(char *buf, int maxlen)
{
    if (!s_line_ready)
        return 0;

    int n = (s_line_len < (uint16_t)(maxlen - 1)) ? s_line_len : (maxlen - 1);
    for (int i = 0; i < n; ++i)
        buf[i] = s_rx_buf[i];
    buf[n] = '\0';

    /* 复位标志，准备接收下一行 */
    s_line_ready = 0;
    s_line_len = 0;
    return n;
}

/*********************************************END OF FILE**********************/
