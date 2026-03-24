/*
 * usart2_rs485_driver.c
 * RS-485 (USART3) 硬件抽象层实现 - STM32F103C8T6, 标准外设库
 *
 * 要点:
 * - USART3: 9600 8E1, 偶校验；PB10=TX, PB11=RX；PB0=DE, PB1=RE
 * - 接收: RXNE 中断 + 环形缓冲区；新字节回调 modbus_rtu_on_rx_byte()
 * - 发送: TXE 中断送字节；当上层数据发完后，开启 TC 中断，TC=1 后切回接收
 */

#include "usart2_rs485_driver.h"
#include "stm32f10x.h"

/* 由帧层提供的字节通知回调（从站） */
extern void modbus_rtu_on_rx_byte(uint8_t byte);

/* ------------------------- 硬件资源定义 ------------------------- */
#define RS485_USART                 USART3
#define RS485_USART_CLK             RCC_APB1Periph_USART3
#define RS485_USART_IRQn            USART3_IRQn

#define RS485_TX_GPIO               GPIOB
#define RS485_TX_PIN                GPIO_Pin_10
#define RS485_TX_GPIO_CLK           RCC_APB2Periph_GPIOB

#define RS485_RX_GPIO               GPIOB
#define RS485_RX_PIN                GPIO_Pin_11
#define RS485_RX_GPIO_CLK           RCC_APB2Periph_GPIOB

#define RS485_DE_GPIO               GPIOB
#define RS485_DE_PIN                GPIO_Pin_0
#define RS485_RE_GPIO               GPIOB
#define RS485_RE_PIN                GPIO_Pin_1
#define RS485_DIR_GPIO_CLK          RCC_APB2Periph_GPIOB

#define RS485_BAUDRATE              (9600u)

/* ------------------------- 内部状态与缓冲 ------------------------- */
static volatile uint8_t s_tx_busy = 0;              /* 正在发送标志 */
static volatile uint8_t s_wait_tc_to_rx = 0;        /* 关闭TXE后等待TC再切RX */
static uint8_t *s_tx_buf = 0;                       /* 当前发送缓冲地址 */
static volatile uint16_t s_tx_len = 0;              /* 剩余待发长度 */

/* 环形缓冲区（RX） */
static volatile uint16_t s_rx_head = 0;             /* 写入位置 */
static volatile uint16_t s_rx_tail = 0;             /* 读取位置 */
static uint8_t s_rx_ring[RS485_RX_RING_SIZE];       /* RX 环形缓冲 */

/* ------------------------- 辅助内部函数 ------------------------- */
/*
 * 微秒级延时：弱实现，默认使用粗略空转循环
 * 如需更精确的 SysTick/定时器延时，可在其他文件提供同名强实现覆盖
 */
#ifndef __weak
#define __weak __attribute__((weak))
#endif
__weak void RS485_DelayUs(uint32_t us)
{
    /* 近似：每次循环消耗若干周期；按经验对 72MHz 进行简化估算。
     * 可根据实测微调分母以获得更接近的延时。
     */
    uint32_t cycles = (SystemCoreClock / 3000000u) * us; /* 约 ~3 cycles/loop 的折中估算 */
    while (cycles--) {
        __NOP();
    }
}
static void prv_gpio_init(void)
{
    GPIO_InitTypeDef gpio;
    /* GPIO 时钟 (GPIOB) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RS485_TX_GPIO_CLK | RS485_RX_GPIO_CLK | RS485_DIR_GPIO_CLK, ENABLE);

    /* PB10 TX 推挽复用 */
    gpio.GPIO_Pin   = RS485_TX_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(RS485_TX_GPIO, &gpio);

    /* PB11 RX 浮空输入 */
    gpio.GPIO_Pin   = RS485_RX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(RS485_RX_GPIO, &gpio);

    /* PB0 DE 推挽输出，初始低电平（接收） */
    gpio.GPIO_Pin   = RS485_DE_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(RS485_DE_GPIO, &gpio);
    GPIO_ResetBits(RS485_DE_GPIO, RS485_DE_PIN);
    
    /* PB1 RE 推挽输出，初始低电平（允许接收） */
    gpio.GPIO_Pin   = RS485_RE_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(RS485_RE_GPIO, &gpio);
    GPIO_ResetBits(RS485_RE_GPIO, RS485_RE_PIN);
}

static void prv_usart_init(void)
{
    USART_InitTypeDef us;
    NVIC_InitTypeDef  nvic;

    /* USART3 时钟 */
    RCC_APB1PeriphClockCmd(RS485_USART_CLK, ENABLE);

    /* 9600 8E1: WordLength=9b + Parity=Even + StopBits=1 */
    USART_StructInit(&us);
    us.USART_BaudRate            = RS485_BAUDRATE;
    us.USART_WordLength          = USART_WordLength_9b;  /* 8 数据 + 1 校验位 */
    us.USART_StopBits            = USART_StopBits_1;
    us.USART_Parity              = USART_Parity_Even;    /* 偶校验 */
    us.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    us.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(RS485_USART, &us);

    /* 中断：RXNE 使能，TXE 初始关闭 */
    USART_ITConfig(RS485_USART, USART_IT_RXNE, ENABLE);
    USART_ITConfig(RS485_USART, USART_IT_TXE, DISABLE);

    /* NVIC 配置 (Group_2: 2位抢占, 2位子优先级)
     * 抢占=2(中), 子=0 - USART3 Modbus通信，优先级中等 */
    nvic.NVIC_IRQChannel = RS485_USART_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(RS485_USART, ENABLE);
}

static inline void prv_dir_tx(void)
{
    /* 在进入发送前插入极短延时 */
    RS485_DelayUs(RS485_DIR_PRE_TX_DELAY_US);
    GPIO_SetBits(RS485_DE_GPIO, RS485_DE_PIN);   /* DE=高，发送使能 */
    GPIO_SetBits(RS485_RE_GPIO, RS485_RE_PIN);   /* RE=高，禁止接收 */
}

static inline void prv_dir_rx(void)
{
    GPIO_ResetBits(RS485_DE_GPIO, RS485_DE_PIN); /* DE=低，禁止发送 */
    GPIO_ResetBits(RS485_RE_GPIO, RS485_RE_PIN); /* RE=低，允许接收 */
}

/* 在确认 TC=1 后切换到接收方向 */
static void prv_wait_tc_and_dir_rx(void)
{
    while ((RS485_USART->SR & USART_SR_TC) == 0) {
        /* 等待移位寄存器完全发送完毕（包括停止位） */
    }
    /* 在切回接收之前插入极短延时（可配置） */
    RS485_DelayUs(RS485_DIR_POST_TX_DELAY_US);
    prv_dir_rx();
}

/* 将字节写入 RX 环形缓冲，并调用帧层回调 */
static inline void prv_rx_ring_write(uint8_t b)
{
    uint16_t next = (uint16_t)((s_rx_head + 1u) & (RS485_RX_RING_SIZE - 1u));
    if (next != s_rx_tail) {
        s_rx_ring[s_rx_head] = b;
        s_rx_head = next;
    } else {
        /* 缓冲满则丢弃最旧一个，腾出空间 */
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) & (RS485_RX_RING_SIZE - 1u));
        s_rx_ring[s_rx_head] = b;
        s_rx_head = next;
    }

    /* 通知帧层 */
    modbus_rtu_on_rx_byte(b);
}

/* ------------------------- 对外 API 实现 ------------------------- */
void RS485_Init(void)
{
    s_tx_busy = 0;
    s_wait_tc_to_rx = 0;
    s_tx_buf = 0;
    s_tx_len = 0;
    s_rx_head = s_rx_tail = 0;

    prv_gpio_init();
    prv_usart_init();
}

void RS485_SetDirTx(void)
{
    /* 进入发送方向（内含可配置的预延时） */
    prv_dir_tx();
}

void RS485_SetDirRx(void)
{
    prv_wait_tc_and_dir_rx();
}

uint8_t RS485_IsBusy(void)
{
    return s_tx_busy;
}

void RS485_SendBytes(uint8_t *buf, uint16_t len)
{   
    /* 若仍在发送，简单阻塞等待（F103 资源有限；可改进为队列） */
    while (s_tx_busy) {
        /* wait */
    }

    s_tx_buf = buf;
    s_tx_len = len;
    s_tx_busy = 1;

    prv_dir_tx();

    /* 先写首字节，随后打开 TXE 由中断续发 */
    RS485_USART->DR = *s_tx_buf++;
    s_tx_len--;

    /* 使能 TXE 中断 */
    USART_ITConfig(RS485_USART, USART_IT_TXE, ENABLE);
    
    /* 阻塞等待发送完成（TC中断会切换方向并标记完成） */
    while (s_tx_busy) {
        /* wait */
    }
}

uint8_t RS485_GetByte(uint8_t *data)
{
    if (!data) return 0;
    if (s_rx_head == s_rx_tail) return 0; /* 空 */

    *data = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) & (RS485_RX_RING_SIZE - 1u));
    return 1;
}

/* ------------------------- 中断处理 ------------------------- */
void USART3_IRQHandler(void)
{
    /* 接收：RXNE */
    if (USART_GetITStatus(RS485_USART, USART_IT_RXNE) != RESET)
    {
        uint16_t rd = USART_ReceiveData(RS485_USART); /* 低 9 位有效（含奇偶）*/
        uint8_t  b  = (uint8_t)rd;                    /* 取低 8 位数据 */
        prv_rx_ring_write(b);
        USART_ClearITPendingBit(RS485_USART, USART_IT_RXNE);
    }

    /* 发送缓冲空：TXE */
    if (USART_GetITStatus(RS485_USART, USART_IT_TXE) != RESET)
    {
        if (s_tx_len > 0)
        {
            RS485_USART->DR = *s_tx_buf++;
            s_tx_len--;
        }
        else
        {
            /* 无更多数据可发：关闭 TXE，开启 TC，等真正发完再切换方向 */
            USART_ITConfig(RS485_USART, USART_IT_TXE, DISABLE);
            USART_ITConfig(RS485_USART, USART_IT_TC, ENABLE);
            s_wait_tc_to_rx = 1;
        }
        USART_ClearITPendingBit(RS485_USART, USART_IT_TXE);
    }

    /* 发送完成:TC */
    if (USART_GetITStatus(RS485_USART, USART_IT_TC) != RESET)
    {
        USART_ClearITPendingBit(RS485_USART, USART_IT_TC);
        if (s_wait_tc_to_rx)
        {
            s_wait_tc_to_rx = 0;
            /* 9600bps需要延时,确保MAX485完全发送完最后一字节 */
            uint32_t delay = (SystemCoreClock / 3000000u) * 1000; /* ~1ms延时 */
            while (delay--) { __NOP(); }
            /* 切回接收 */
            GPIO_ResetBits(RS485_DE_GPIO, RS485_DE_PIN);
            GPIO_ResetBits(RS485_RE_GPIO, RS485_RE_PIN);
            USART_ITConfig(RS485_USART, USART_IT_TC, DISABLE);
            s_tx_busy = 0;           /* 标记空闲 */
        }
    }
}
