/**
 * @file    bsp_pwm.c
 * @version V1.0
 * @brief   LED 调光 PWM 接口 (TIM5 PH10/PH11/PH12),低电平点亮
 */

#include "./pwm/bsp_pwm.h"

/* TIM5: CH1/CH2/CH3 -> PH10/PH11/PH12 (AF2)
 * LED 低电平点亮(active-low):配置 PWM1 + 低极性(OCPolarity=Low).
 * 在 PWM1 模式下,当 CNT < CCR 时输出为“有效电平”(这里设置为低电平,LED 亮).
 * 周期计数为 (ARR + 1),有效低电平占空比 = CCR / (ARR + 1).
 * 因此 CCR = (ARR + 1) * percent / 100 对应亮度百分比.
 */

static uint16_t s_arr = 0;              /* PWM 周期 (ARR) */
static uint8_t s_duty[3] = {0,0,0};     /* 记录 3 路占空比 0..100 */

/**
 * @brief 计算 TIM5 的实际时钟:APB1 预分频为 1 时,TIMCLK=PCLK1；不为 1 时,TIMCLK=PCLK1*2
 * 
 * @return uint32_t 
 */
static uint32_t prv_get_tim5_clk(void)
{
	uint32_t hclk = SystemCoreClock;                   /* HCLK */
	uint32_t ppre1 = (RCC->CFGR >> 10) & 0x7;         /* PPRE1 位[12:10] */
	uint32_t apb1_div = (ppre1 < 4) ? 1 : (1 << (ppre1 - 3)); /* 1,2,4,8,16 */
	uint32_t pclk1 = hclk / apb1_div;
	return (apb1_div == 1) ? pclk1 : (pclk1 * 2);
}

/**
 * @brief 初始化 LED PWM
 * 
 * @param pwm_hz 返回 PWM 频率(Hz)
 */
void LED_PWM_Config(uint32_t pwm_hz)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;

	// 开时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOH, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE);

	// 配置 GPIO为复用功能 TIM5 CH1/CH2/CH3
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOH, &GPIO_InitStructure);

	GPIO_PinAFConfig(GPIOH, GPIO_PinSource10, GPIO_AF_TIM5);
	GPIO_PinAFConfig(GPIOH, GPIO_PinSource11, GPIO_AF_TIM5);
	GPIO_PinAFConfig(GPIOH, GPIO_PinSource12, GPIO_AF_TIM5);

	// 计算定时器分频与周期:确保 ARR 落在 16bit 范围
	if (pwm_hz == 0) pwm_hz = 1000;
	uint32_t tim_clk = prv_get_tim5_clk();
	// 计算分频值与周期,prescaler = floor(tim_clk/(pwm_hz*65536)) + 1
	uint32_t prescaler = tim_clk / (pwm_hz * 65536);
	prescaler += 1;
	if (prescaler < 1) prescaler = 1;
	uint32_t cnt_clk = tim_clk / prescaler;
	uint32_t arr_calc = (cnt_clk / pwm_hz);
	if (arr_calc == 0) arr_calc = 1; // 避免 0 周期
	arr_calc -= 1; // 0..ARR 共 ARR+1 个计数
	if (arr_calc > 0xFFFF) arr_calc = 0xFFFF;
	s_arr = (uint16_t)arr_calc;

    // 配置定时器TIM5参数
	TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
	TIM_TimeBaseStructure.TIM_Prescaler = (uint16_t)(prescaler - 1); // 预分频
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数
	TIM_TimeBaseStructure.TIM_Period = s_arr;   // 自动重装载值
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 时钟分频
	TIM_TimeBaseInit(TIM5, &TIM_TimeBaseStructure); // 初始化定时器

	// 配置 PWM 模式:PWM1,低极性(active-low)
	TIM_OCStructInit(&TIM_OCInitStructure);
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1; // PWM1 模式
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; // 使能输出
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_Low; // 低极性
	TIM_OCInitStructure.TIM_Pulse = 0; // 初始 CCR=0,占空比 0%

    // 配置 3 路 PWM 输出
	TIM_OC1Init(TIM5, &TIM_OCInitStructure);
	TIM_OC1PreloadConfig(TIM5, TIM_OCPreload_Enable);
	TIM_OC2Init(TIM5, &TIM_OCInitStructure);
	TIM_OC2PreloadConfig(TIM5, TIM_OCPreload_Enable);
	TIM_OC3Init(TIM5, &TIM_OCInitStructure);
	TIM_OC3PreloadConfig(TIM5, TIM_OCPreload_Enable);

	TIM_ARRPreloadConfig(TIM5, ENABLE);
    // 复位比较寄存器,避免启动毛刺
	TIM_SetCompare1(TIM5, 0);
	TIM_SetCompare2(TIM5, 0);
	TIM_SetCompare3(TIM5, 0);
    // 立即更新预装载值
	TIM_GenerateEvent(TIM5, TIM_EventSource_Update);
	TIM_Cmd(TIM5, ENABLE);
}

/**
 * @brief 设置 LED 亮度百分比(0~100)
 * 
 * @param ledIndex LEDx x=1,2,3
 * @param percent 亮度百分比(0~100)
 */
void LED_PWM_SetDuty(uint8_t ledIndex, uint8_t percent)
{
	if (percent > 100) percent = 100;
	if (ledIndex < 1 || ledIndex > 3) return;

	s_duty[ledIndex - 1] = percent;
	/* CCR 对应低电平持续时间:使用 (ARR+1) 计算占空比 */
	uint32_t period_counts = (uint32_t)s_arr + 1;
	uint32_t ccr = (period_counts * percent) / 100;
	if (ccr > s_arr) ccr = s_arr; /* 100% 时取 ARR */

	switch (ledIndex)
	{
	case 1: TIM_SetCompare1(TIM5, (uint16_t)ccr); break;
	case 2: TIM_SetCompare2(TIM5, (uint16_t)ccr); break;
	case 3: TIM_SetCompare3(TIM5, (uint16_t)ccr); break;
	default: break;
	}
}

/**
 * @brief 从缓存获取 LED 亮度百分比(0~100)
 * 
 * @param ledIndex LEDx x=1,2,3
 * @return uint8_t 缓存的百分比值(便于外部显示与状态查询)
 */
uint8_t LED_PWM_GetDuty(uint8_t ledIndex)
{
	if (ledIndex < 1 || ledIndex > 3) return 0;
	return s_duty[ledIndex - 1];
}
