/*
 * application_data_manager.c
 * 应用数据管理层：Modbus 寄存器映射与 MQ2 烟雾传感器数据管理
 * 
 * 从站地址：3
 * 寄存器映射：
 *   - 40021（索引 20）：烟雾浓度值（整数，单位：ppm）
 */
#include "application_data_manager.h"
#include "stm32f10x.h" 
#include "mq2/bsp_mq2.h"

/* 保持寄存器表：索引 20 对应 Modbus 逻辑地址 40021（烟雾浓度 ppm） */
static uint16_t s_holding_regs[HOLDING_REG_COUNT] = {0};

/**
 * @brief  初始化应用数据层
 * @note   初始化 MQ2 传感器（GPIO + ADC 配置），初始化寄存器表
 *         注意：MQ2 传感器首次上电后需要约 20 秒预热时间
 * @param  无
 * @retval 无
 */
void AppData_Init(void)
{
    /* 初始化 MQ2 烟雾检测模块（GPIO + ADC + 中断） */
    MQ2_Init();
    
    /* 清空寄存器表 */
    for (uint16_t i = 0; i < HOLDING_REG_COUNT; i++) {
        s_holding_regs[i] = 0;
    }
}

/**
 * @brief  更新 MQ2 烟雾浓度到保持寄存器
 * @note   从 MQ2 传感器读取 ADC 值，转换为 ppm（整数部分）并存入寄存器
 *         在主循环中调用
 * @param  无
 * @retval 无
 */
void AppData_UpdateMQ2(void)
{
    /* 获取 MQ2 烟雾浓度（ppm，整数部分） */
    uint16_t ppm = MQ2_GetPPM();
    
    /* 更新寄存器：索引 20 对应 Modbus 地址 40021 */
    s_holding_regs[REG_SMOKE_PPM_ADDR] = ppm;
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
 * @note         当前寄存器 20（烟雾浓度 ppm）为只读，拒绝写入
 */
int AppData_WriteHolding(uint16_t addr, uint16_t value)
{
    (void)value; /* 避免未使用警告 */
    
    /* 寄存器 20（烟雾浓度 ppm）为只读，禁止写入 */
    if (addr == REG_SMOKE_PPM_ADDR) {
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
 * @brief  直接获取烟雾浓度值
 * @note   此函数为强符号，覆盖协议层的弱符号默认实现
 * @param  无
 * @retval 烟雾浓度整数值（单位：ppm）
 */
uint16_t app_get_smoke_ppm(void)
{
    return s_holding_regs[REG_SMOKE_PPM_ADDR];
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
