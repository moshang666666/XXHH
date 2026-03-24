/**
 * @file rs485_driver.c
 * @brief Modbus RTU 硬件抽象层(USART2 + RS-485)接口实现
 *
 *  - 本层仅封装硬件细节：GPIO、USART2、PB8(DE/RE) 方向控制与定时器。
 *  - 上层(帧层/协议层)通过注册回调获取字节与驱动发送，不直接访问寄存器。
 */

#include "rs485_driver.h"
#include "../systick/bsp_systick.h"  /* 使用 DelayMs，避免空转延时 */
#include "stm32f4xx.h"
/* 帧层 T3.5 到期回调（由帧层提供） */
extern void MB_RTU_OnT35Expired(void);

/* USART2: TX=PD5 AF7, RX=PD6 AF7; RS-485 DE/RE=PB8 输出，高=发送，低=接收 */
#define RS485_USART USART2
#define RS485_USART_CLK RCC_APB1Periph_USART2

#define RS485_TX_GPIO GPIOD
#define RS485_TX_PIN GPIO_Pin_5
#define RS485_TX_GPIO_CLK RCC_AHB1Periph_GPIOD
#define RS485_TX_AF GPIO_AF_USART2
#define RS485_TX_PINSRC GPIO_PinSource5

#define RS485_RX_GPIO GPIOD
#define RS485_RX_PIN GPIO_Pin_6
#define RS485_RX_GPIO_CLK RCC_AHB1Periph_GPIOD
#define RS485_RX_AF GPIO_AF_USART2
#define RS485_RX_PINSRC GPIO_PinSource6

#define RS485_DIR_GPIO GPIOB
#define RS485_DIR_PIN GPIO_Pin_8
#define RS485_DIR_GPIO_CLK RCC_AHB1Periph_GPIOB

/* 定时器：用于 50us tick 的 T3.5 与主站延时，这里使用 TIM6(基本定时器) */
#define RS485_TMR TIM6
#define RS485_TMR_CLK RCC_APB1Periph_TIM6
#define RS485_TMR_IRQ TIM6_DAC_IRQn

/* 目标串口参数：9600 8E1 */
#define RS485_BAUDRATE (9600)

/* ========================== 上层回调指针 ========================== */
RS485_RxCallback_t g_rs485RxCallback = 0;
RS485_TxEmptyCallback_t g_rs485TxEmptyCallback = 0;

/* 发送完成(整帧移出移位寄存器)后再切回接收：
 * - s_txe_enabled 跟踪 TXE 中断使能状态变化
 * - 当从“使能TXE”切换为“关闭TXE”时，认为已写入最后一个字节，随后开启 TC 中断等待真正发完
 */
static volatile uint8_t s_txe_enabled = 0;
static volatile uint8_t s_wait_tc_to_rx = 0;

void RS485_RegisterCallbacks(RS485_RxCallback_t rxCb,
                             RS485_TxEmptyCallback_t txEmptyCb)
{
    g_rs485RxCallback = rxCb;
    g_rs485TxEmptyCallback = txEmptyCb;
}

static void prv_GPIO_Config(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(RS485_TX_GPIO_CLK | RS485_RX_GPIO_CLK | RS485_DIR_GPIO_CLK, ENABLE);

    /* PD5 TX, PD6 RX 复用为 USART2 */
    GPIO_PinAFConfig(RS485_TX_GPIO, RS485_TX_PINSRC, RS485_TX_AF);
    GPIO_PinAFConfig(RS485_RX_GPIO, RS485_RX_PINSRC, RS485_RX_AF);

    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = RS485_TX_PIN;
    GPIO_Init(RS485_TX_GPIO, &gpio);
    gpio.GPIO_Pin = RS485_RX_PIN;
    GPIO_Init(RS485_RX_GPIO, &gpio);

    /* PB8 方向控制：推挽输出，初始接收(低) */
    gpio.GPIO_Mode = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = RS485_DIR_PIN;
    GPIO_Init(RS485_DIR_GPIO, &gpio);
    GPIO_ResetBits(RS485_DIR_GPIO, RS485_DIR_PIN); /* 低=接收 */
}

static void prv_USART_Config(void)
{
    USART_InitTypeDef us;
    NVIC_InitTypeDef nvic;

    RCC_APB1PeriphClockCmd(RS485_USART_CLK, ENABLE);

    /* 19200 8E1：8 数据位 + 偶校验 + 1 停止位
     * StdPeriph 中 8E1 常用设置：WordLength=USART_WordLength_9b, Parity=Even, StopBits=1
     * 因为有效数据位=8+1校验=9bit 框架。 */
    USART_StructInit(&us);
    us.USART_BaudRate = RS485_BAUDRATE;
    us.USART_WordLength = USART_WordLength_9b; /* 8 数据 + 1 校验位 */
    us.USART_StopBits = USART_StopBits_1;
    us.USART_Parity = USART_Parity_Even; /* 偶校验 */
    us.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    us.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(RS485_USART, &us);

    /* 使能中断：RXNE 与 TXE（TXE 初期可先关，由上层按需打开） */
    USART_ITConfig(RS485_USART, USART_IT_RXNE, ENABLE);
    USART_ITConfig(RS485_USART, USART_IT_TXE, DISABLE);

    /* NVIC 配置 */
    nvic.NVIC_IRQChannel = USART2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 5;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(RS485_USART, ENABLE);
}

static void prv_Timer_Config(uint16_t t35_50us)
{
    /* 使用 TIM6 产生 50us 基准：
     * 希望 1 tick = 50us，则计数频率 = 20kHz。
     * 预分频 = 84MHz / 20kHz = 4200 -> PSC = 4199。
     * 自动重装 ARR = t35_50us - 1。
     */
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef nvic;

    RCC_APB1PeriphClockCmd(RS485_TMR_CLK, ENABLE);

    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler = 4199; /* 84MHz / (4199+1) = 20kHz -> 50us */
    tb.TIM_Period = (t35_50us > 0 ? (t35_50us - 1) : 0);
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(RS485_TMR, &tb);

    TIM_ClearITPendingBit(RS485_TMR, TIM_IT_Update);
    TIM_ITConfig(RS485_TMR, TIM_IT_Update, DISABLE); /* 初始不使能，按需启动 */

    nvic.NVIC_IRQChannel = RS485_TMR_IRQ;
    nvic.NVIC_IRQChannelPreemptionPriority = 6;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}


void RS485_Init(void)
{
    prv_GPIO_Config();
    prv_USART_Config();
    /* 定时器的周期由上层通过 RS485_TimerInit 设置，这里仅完成 timer 基本配置骨架 */
}

void RS485_SetDirectionTx(void)
{
    /* 仅在“使能发送”前加入短延时，避免瞬时边沿造成畸形波形 */
    DelayMs(RS485_DIR_DELAY_MS);
    GPIO_SetBits(RS485_DIR_GPIO, RS485_DIR_PIN); /* 高=发送 */
}

void RS485_SetDirectionRx(void)
{
    /* 等待“发送完成”标志（DR与移位寄存器均空），再切回接收 */
    while ((RS485_USART->SR & USART_SR_TC) == 0) {
        /* 轮询等待 TC 置位；9600bps 下最多约1ms，避免提前释放 DE 导致尾字节畸变 */
    }
    /* 可选：极短延时用于驱动器稳态（若硬件良好可省略或保持为 0ms） */
    DelayMs(RS485_DIR_DELAY_MS);
    GPIO_ResetBits(RS485_DIR_GPIO, RS485_DIR_PIN); /* 低=接收 */
}

uint8_t RS485_ReadByte(uint8_t *pByte)
{
    if ((RS485_USART->SR & USART_SR_RXNE) != 0)
    {
        *pByte = (uint8_t)RS485_USART->DR; /* 读 DR 清 RXNE */
        return 1;
    }
    return 0;
}

uint8_t RS485_WriteByte(uint8_t byte)
{
    if ((RS485_USART->SR & USART_SR_TXE) != 0)
    {
        RS485_USART->DR = byte;
        return 1;
    }
    return 0;
}

void RS485_EnableIRQ(uint8_t rxEnable, uint8_t txEnable)
{
    USART_ITConfig(RS485_USART, USART_IT_RXNE, rxEnable ? ENABLE : DISABLE);
    /* 追踪 TXE 使能状态变化：当从 1->0 表示上层已无更多字节可发，开始等待 TC 再切回接收 */
    if (s_txe_enabled && !txEnable)
    {
        /* 开启 TC 中断，待真正“发送完成”后在中断里把 DE 拉低 */
        s_wait_tc_to_rx = 1;
        USART_ITConfig(RS485_USART, USART_IT_TC, ENABLE);
    }
    s_txe_enabled = txEnable ? 1 : 0;
    USART_ITConfig(RS485_USART, USART_IT_TXE, txEnable ? ENABLE : DISABLE);
}

void RS485_TimerInit(uint16_t t35_50us)
{
    prv_Timer_Config(t35_50us);
}

void RS485_TimerStart_T35(void)
{
    /* 启动一次性 T3.5 计时：清零计数，打开更新中断与计数器 */
    TIM_SetCounter(RS485_TMR, 0);
    TIM_ClearITPendingBit(RS485_TMR, TIM_IT_Update);
    TIM_ITConfig(RS485_TMR, TIM_IT_Update, ENABLE);
    TIM_Cmd(RS485_TMR, ENABLE);
}

void RS485_TimerStart_Us(uint32_t us)
{
    /* 将 us 转为 50us tick：tick = (us + 49)/50 */
    uint32_t ticks = (us + 49u) / 50u;
    if (ticks == 0)
        ticks = 1;
    TIM_SetAutoreload(RS485_TMR, (uint16_t)(ticks - 1));
    TIM_SetCounter(RS485_TMR, 0);
    TIM_ClearITPendingBit(RS485_TMR, TIM_IT_Update);
    TIM_ITConfig(RS485_TMR, TIM_IT_Update, ENABLE);
    TIM_Cmd(RS485_TMR, ENABLE);
}

void RS485_TimerStop(void)
{
    TIM_ITConfig(RS485_TMR, TIM_IT_Update, DISABLE);
    TIM_Cmd(RS485_TMR, DISABLE);
}

uint32_t RS485_GetErrorFlags(void)
{
    uint32_t sr = RS485_USART->SR;
    /* 关注 FE(帧错)/PE(奇偶)/ORE(过载)/NE(噪声) */
    return (sr & (USART_SR_FE | USART_SR_PE | USART_SR_ORE | USART_SR_NE));
}

/* ========================== 中断服务程序 ========================== */
uint8_t ch;

void USART2_IRQHandler(void)
{
    /* 发送完成中断：整帧已完全移出总线（含最后1位停止位） */
    if (USART_GetITStatus(RS485_USART, USART_IT_TC) != RESET)
    {
        USART_ClearITPendingBit(RS485_USART, USART_IT_TC);
        if (s_wait_tc_to_rx)
        {
            s_wait_tc_to_rx = 0;
            /* 在中断环境下避免调用带毫秒延时的接口，直接立即拉低 DE */
            GPIO_ResetBits(RS485_DIR_GPIO, RS485_DIR_PIN); /* 低=接收 */
            /* 关闭 TC 中断，防止重复进入 */
            USART_ITConfig(RS485_USART, USART_IT_TC, DISABLE);
        }
    }

    /* 接收中断 */
    if (USART_GetITStatus(RS485_USART, USART_IT_RXNE) != RESET)
    {
        ch = (uint8_t)USART_ReceiveData(RS485_USART);
        if (g_rs485RxCallback)
        {
            g_rs485RxCallback(ch);
        }
        USART_ClearITPendingBit(RS485_USART, USART_IT_RXNE);
    }

    /* 发送缓冲空中断 */
    if (USART_GetITStatus(RS485_USART, USART_IT_TXE) != RESET)
    {
        if (g_rs485TxEmptyCallback)
        {
            g_rs485TxEmptyCallback();
        }
        else
        {
            /* 若无回调，关闭 TXE 中断避免空转 */
            USART_ITConfig(RS485_USART, USART_IT_TXE, DISABLE);
        }
        USART_ClearITPendingBit(RS485_USART, USART_IT_TXE);
    }

    /* 错误中断：可按需扩展处理 */
}

void TIM6_DAC_IRQHandler(void)
{
    if (TIM_GetITStatus(RS485_TMR, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(RS485_TMR, TIM_IT_Update);
        /* 一次性超时：停止计时器，避免重复进入 */
        TIM_ITConfig(RS485_TMR, TIM_IT_Update, DISABLE);
        TIM_Cmd(RS485_TMR, DISABLE);

        /* 通知帧层：T3.5 到期，尝试完成当前接收帧 */
        MB_RTU_OnT35Expired();
    }
}
