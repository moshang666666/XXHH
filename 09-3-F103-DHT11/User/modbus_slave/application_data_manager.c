/*
 * application_data_manager.c
 * 应用数据管理层：Modbus 寄存器映射与 DHT11 温湿度数据管理
 */
#include "application_data_manager.h"
#include "stm32f10x.h" 
#include "./dht11/bsp_dht11.h"
#include "./systick/bsp_SysTick.h"

/* 外部函数声明（由 bsp_SysTick.c 提供） */
extern uint32_t SystemTick_GetMs(void);

/* DHT11 数据结构（用于存储读取的温湿度） */
static DHT11_Data_TypeDef s_dht11_data = {0};

/* 上次读取时间戳（单位：ms，用于控制读取间隔） */
static uint32_t s_last_read_time = 0;

/* 保持寄存器表：索引 10 对应 Modbus 逻辑地址 40011（温度），索引 11 对应 40012（湿度） */
static uint16_t s_holding_regs[HOLDING_REG_COUNT] = {0};

/**
 * @brief  初始化应用数据层
 * @note   初始化 DHT11 传感器，初始化寄存器表
 * @param  无
 * @retval 无
 */
void AppData_Init(void)
{
    /* 初始化 DHT11 模块（GPIO 配置） */
    DHT11_Init();
    
    /* 清空寄存器表 */
    for (uint16_t i = 0; i < HOLDING_REG_COUNT; i++) {
        s_holding_regs[i] = 0;
    }
    
    /* 清空 DHT11 数据结构 */
    s_dht11_data.humi_int = 0;
    s_dht11_data.humi_deci = 0;
    s_dht11_data.temp_int = 0;
    s_dht11_data.temp_deci = 0;
    s_dht11_data.check_sum = 0;
    
    /* 初始化时间戳为一个较大的负值，确保首次读取能立即执行 */
    s_last_read_time = (uint32_t)(-3000); /* 设置为 -3000ms，确保首次检查时 current_time - s_last_read_time >= 2000 */
}

/**
 * @brief  周期更新 DHT11 温湿度到保持寄存器
 * @note   从 DHT11 传感器读取温湿度数据（整数部分）并存入寄存器
 *         建议在主循环中调用，但内部会控制读取间隔 ≥2 秒
 *         读取失败时保持上次值不变
 * @param  无
 * @retval 无
 */
void AppData_UpdateDHT11(void)
{
    uint32_t current_time = SystemTick_GetMs();
    
    /* 控制读取间隔：至少 2000ms（2秒） */
    if (current_time - s_last_read_time < 2000) {
        return; /* 未到读取时间，直接返回 */
    }
    
    /* 尝试读取 DHT11 数据 */
    uint8_t result = DHT11_Read_TempAndHumidity(&s_dht11_data);
    if (result == SUCCESS) {
        /* 读取成功：更新寄存器（只使用整数部分） */
        s_holding_regs[REG_TEMP_ADDR] = (uint16_t)s_dht11_data.temp_int;
        s_holding_regs[REG_HUMI_ADDR] = (uint16_t)s_dht11_data.humi_int;
        
        /* 更新时间戳 */
        s_last_read_time = current_time;
        
        /* 调试信息：打印读取成功的数据 */
        extern int printf(const char *format, ...);
        printf("[DHT11 UPDATE] SUCCESS: Temp=%d℃, Humi=%d%%RH\r\n", 
               s_dht11_data.temp_int, s_dht11_data.humi_int);
    } else {
        /* 读取失败：保持上次值不变，不更新时间戳（下次循环会重试） */
        extern int printf(const char *format, ...);
        printf("[DHT11 UPDATE] FAILED: Read error, keeping previous values\r\n");
    }
}

/**
 * @brief            读保持寄存器
 * @param  startAddr 起始寄存器地址（0 基）
 * @param  quantity  读取数量
 * @param  outBuf    输出缓冲区（调用者保证足够大小）
 * @retval           0=成功；-1=地址非法或数量越界
 */
int AppData_ReadHolding(uint16_t startAddr, uint16_t quantity, uint16_t *outBuf)
{
    if (!outBuf) return -1;
    
    /* 地址与数量合法性检查 */
    if (startAddr >= HOLDING_REG_COUNT) return -1;
    if ((uint32_t)startAddr + quantity > HOLDING_REG_COUNT) return -1;
    
    /* 复制寄存器到输出缓冲 */
    for (uint16_t i = 0; i < quantity; i++) {
        outBuf[i] = s_holding_regs[startAddr + i];
    }
    return 0;
}

/**
 * @brief        写保持寄存器
 * @param  addr  寄存器地址（0 基）
 * @param  value 待写入值
 * @retval       0=成功；-1=地址非法或只读
 * @note         当前寄存器 10（温度）和 11（湿度）均为只读，拒绝写入
 */
int AppData_WriteHolding(uint16_t addr, uint16_t value)
{
    (void)value; /* 避免未使用警告 */
    
    /* 寄存器 10（温度）和 11（湿度）为只读，禁止写入 */
    if (addr == REG_TEMP_ADDR || addr == REG_HUMI_ADDR) {
        return -1; /* 拒绝写入只读寄存器 */
    }
    
    /* 地址越界检查 */
    if (addr >= HOLDING_REG_COUNT) return -1;
    
    /* 若后续支持可写寄存器，在此添加逻辑 */
    // s_holding_regs[addr] = value;
    // return 0;
    
    return -1; /* 当前所有寄存器均为只读 */
}

/**
 * @brief  直接获取温度值
 * @note   此函数为强符号，覆盖协议层的弱符号默认实现
 * @param  无
 * @retval 温度整数值（单位：℃，0~50）
 */
uint16_t app_get_temperature(void)
{
    return s_holding_regs[REG_TEMP_ADDR];
}

/**
 * @brief  直接获取湿度值
 * @note   此函数为强符号，覆盖协议层的弱符号默认实现
 * @param  无
 * @retval 湿度整数值（单位：%RH，20~90）
 */
uint16_t app_get_humidity(void)
{
    return s_holding_regs[REG_HUMI_ADDR];
}

/**
 * @brief            读保持寄存器（供 Modbus 协议层调用，强符号覆盖协议层弱实现）
 * @note             协议层调用 app_read_holding()
 * @param  startAddr 起始寄存器地址（0 基）
 * @param  quantity  读取数量
 * @param  outBuf    输出缓冲区（调用者保证足够大小）
 * @retval           0=成功；-1=地址非法或数量越界
 */
int app_read_holding(uint16_t startAddr, uint16_t quantity, uint16_t *outBuf)
{
    /* 直接调用内部实现 */
    return AppData_ReadHolding(startAddr, quantity, outBuf);
}

/**
 * @brief        写保持寄存器（供 Modbus 协议层调用，强符号覆盖协议层弱实现）
 * @note         协议层调用 app_write_single_holding()，此函数覆盖协议层的弱符号版本
 * @param  addr  寄存器地址（0 基）
 * @param  value 待写入值
 * @retval       0=成功；-1=地址非法或只读
 */
int app_write_single_holding(uint16_t addr, uint16_t value)
{
    /* 直接调用内部实现 */
    return AppData_WriteHolding(addr, value);
}
