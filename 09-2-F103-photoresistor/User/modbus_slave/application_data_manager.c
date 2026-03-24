/*
 * application_data_manager.c
 * 应用数据管理层：Modbus 寄存器映射与 ADC 数据管理
 */
#include "application_data_manager.h"
#include "stm32f10x.h" 
#include "./photoresistor/bsp_photoresistor.h"

/* 外部 ADC 采集值（由 bsp_photoresistor.c 的中断服务更新） */
extern __IO uint16_t ADC_ConvertedValue;

/* 外部光敏电阻初始化函数（由 bsp_photoresistor.c 提供） */
extern void PhotoResistor_Init(void);

/* 保持寄存器表：索引 0 对应 Modbus 逻辑地址 40001 */
static uint16_t s_holding_regs[HOLDING_REG_COUNT] = {0};

/**
 * @brief  初始化应用数据层
 * @note   启动 ADC 连续采集（光敏电阻模块），初始化寄存器表
 * @param  无
 * @retval 无
 */
void AppData_Init(void)
{
    /* 初始化光敏电阻模块（GPIO + ADC + 中断） */
    PhotoResistor_Init();
    
    /* 清空寄存器表 */
    for (uint16_t i = 0; i < HOLDING_REG_COUNT; i++) {
        s_holding_regs[i] = 0;
    }
}

/**
 * @brief  周期更新光敏 ADC 值到保持寄存器
 * @note   从外部变量 ADC_ConvertedValue 读取最新 ADC 采样结果并存入寄存器
 *         在主循环中周期调用（如每 100ms）
 * @param  无
 * @retval 无
 */
void AppData_UpdateLight(void)
{
    /* ADC_ConvertedValue 由 ADC 中断服务自动更新（连续转换模式） */
    s_holding_regs[REG_LIGHT_ADDR] = (uint16_t)ADC_ConvertedValue;
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
 * @note         当前寄存器 0（光敏 ADC）为只读，拒绝写入；若需可写寄存器请修改此逻辑
 */
int AppData_WriteHolding(uint16_t addr, uint16_t value)
{
    (void)value; /* 避免未使用警告 */
    
    /* 寄存器 0（光敏 ADC）为只读，禁止写入 */
    if (addr == REG_LIGHT_ADDR) {
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
 * @brief  直接获取光敏 ADC 值
 * @note   此函数为强符号，覆盖协议层的弱符号默认实现
 * @param  无
 * @retval 12 位 ADC 值（0~4095）
 */
uint16_t app_get_light_value(void)
{
    return s_holding_regs[REG_LIGHT_ADDR];
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
