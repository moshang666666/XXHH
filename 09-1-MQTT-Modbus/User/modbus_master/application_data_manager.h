/**
 * @file application_data_manager.h
 * @brief 应用数据模型与 Modbus 寄存器映射管理层（主站侧）。
 *
 * - 抽象“业务变量”与“远端从站寄存器地址”的对应关系；
 * - 提供统一读/写接口（上层不直接关心 Modbus 寄存器细节）；
 * - 接收 Modbus 事务/协议层回调，将新值写入内部缓存；
 * - 生成要轮询的寄存器请求（可按优先级/周期扩展）。
 *
 * - 所有寄存器使用保持寄存器地址空间（功能码 0x03 读 / 0x06 写单个 / 0x10 写多个）。
 * - 采用“逻辑数据 ID”枚举，屏蔽从站地址与寄存器基址细节；
 * - 提供线程/ISR 安全的最小保障：32 位 MCU 上单次读写 16/32-bit 是原子的；若需防止并发访问，可在外层加关中断或互斥。
 */

#ifndef __APPLICATION_DATA_MANAGER_H__
#define __APPLICATION_DATA_MANAGER_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" { 
#endif

/* ============================= 枚举：逻辑数据 ID ============================= */
/**
 * @brief 业务变量逻辑 ID（统一对外标识）
 */
typedef enum {
	APP_DATA_LIGHT_ADC = 0,    // F103_1 光敏电阻 ADC      寄存器 40001
	APP_DATA_TEMP1,            // F103_2 温度             寄存器 40011
	APP_DATA_HUM1,             // F103_2 湿度             寄存器 40012
	APP_DATA_MQ2,              // F103_3 MQ2烟雾传感      寄存器 40021
	APP_DATA_TEMP2,            // PLC S7-200 工业温度     寄存器 40031
	APP_DATA_HUM2,             // PLC S7-200 工业湿度     寄存器 40032
	APP_DATA_COUNT             // 数据项数量
} app_data_id_t;

/* ============================= 常量与宏 ============================= */
/** 默认无效值（初始化时填充，表示尚未采集到） */
#define APP_DATA_INVALID_VALUE   (0xFFFF)

/** 读出时若返回该值且应用逻辑需要区分“真实 0xFFFF”与“尚未采集”，可通过 app_data_is_valid 检查 */

/**
 * @brief 单个寄存器映射描述
 * - id 逻辑数据 ID
 * - slave 从站地址
 * - reg_addr Modbus 保持寄存器地址（40001 -> 0 基内部用 reg_addr = 0 or 40001? 直接存 40001 便于打印）
 * - writable 是否允许主站写入 (1=可写 0=只读)
 * - value 当前缓存值
 * - valid 值是否有效 (1=已采集,0=无效)
 */
typedef struct {
	app_data_id_t id;      // 逻辑数据 ID
	uint8_t      slave;    // 从站地址
	uint16_t     reg_addr; // Modbus 保持寄存器地址（40001 -> 0 基内部用 reg_addr = 0 or 40001? 直接存 40001 便于打印）
	uint8_t      writable; // 是否允许主站写入 (1=可写 0=只读)
	uint16_t     value;    // 当前缓存值
	uint8_t      valid;    // 值是否有效 (1=已采集,0=无效)
} app_reg_map_t;

/* ============================= API 声明 ============================= */

/**
 * @brief 初始化数据管理：装载静态映射表，所有值置为无效
 */
void app_data_init(void);

/**
 * @brief 根据逻辑 ID 读取当前值
 * 
 * @param id 逻辑数据 ID
 * @param out_val 输出地址（可为 NULL 表示只检查状态）
 * @return 0 成功；<0 失败（ID 越界 / 值无效）
 */
int app_data_get_value(app_data_id_t id, uint16_t *out_val);

/**
 * @brief 设置逻辑数据（仅当映射定义为可写）
 * 
 * @param id 逻辑数据 ID
 * @param val 要写入的值
 * @return 0 成功；<0 失败（越界 / 只读）
 */
int app_data_set_value(app_data_id_t id, uint16_t val);

/**
 * @brief 由 Modbus 事务层在收到从站寄存器新值时调用，刷新内部缓存
 * 
 * @param slave 从站地址
 * @param reg_addr 保持寄存器地址（40001 等自然值）
 * @param val 新值
 * @return 0 成功；<0 未找到匹配映射
 */
int app_data_update_from_modbus(uint8_t slave, uint16_t reg_addr, uint16_t val);

/**
 * @brief 返回映射条目指针（诊断/遍历用），不可修改内部字段
 * 
 * @param index 0..APP_DATA_COUNT-1
 * @return const app_reg_map_t* 或 NULL (越界)
 */
const app_reg_map_t *app_data_get_entry(size_t index);

/**
 * @brief 检查某数据 ID 当前值是否有效
 * 
 * @param id 数据 ID
 * @return 1 有效；0 无效或越界
 */
int app_data_is_valid(app_data_id_t id);

/**
 * @brief 生成下一次需要轮询的寄存器（可后续扩展为优先级/时间片）。
 * - 简单策略：循环遍历映射表，返回下一条（可读）。
 * 
 * @param cur 当前上一次使用的索引（初次调用传入 -1）
 * @return 下一个索引；若无则返回 -1
 */
int app_data_next_poll_index(int cur);

#ifdef __cplusplus
}
#endif

#endif /* __APPLICATION_DATA_MANAGER_H__ */
