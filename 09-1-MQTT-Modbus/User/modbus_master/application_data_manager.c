/**
 * @file application_data_manager.c
 * @brief 应用数据管理层:业务变量与Modbus寄存器的映射、缓存和状态管理
 * @author Yukikaze
 * @date 2025-11-26
 *
 * 架构说明:
 *   本模块位于业务逻辑层与Modbus协议层之间,提供统一的数据访问接口:
 *   - 业务层: 通过逻辑ID(APP_DATA_LIGHT_ADC等)读写数据,无需关心底层寄存器地址
 *   - 协议层: 通过app_data_update_from_modbus()更新从站响应的寄存器值
 *
 * 核心功能:
 *   1. 静态映射表: 定义业务变量到(从站地址+寄存器地址)的映射关系
 *   2. 数据缓存: 保存最近一次从从站读取的寄存器值
 *   3. 有效性标志: 标记数据是否已成功采集(valid=1)或待采集(valid=0)
 *   4. 读写权限: 支持只读/可写标志(writable),可扩展为更细粒度的权限控制
 *
 * 数据流向:
 *   [从站设备] --Modbus RTU--> [modbus_read] --app_data_update_from_modbus()--> [本模块缓存]
 *   [业务逻辑] <--app_data_get_value()-- [本模块缓存]
 *
 * 扩展性:
 *   - 可添加时间戳字段记录最后更新时间
 *   - 可添加质量标志(Quality)表示数据可信度
 *   - 可添加变化检测(Changed)标志实现事件驱动
 *   - 可实现轮询策略(优先级、周期等)
 */

#include "application_data_manager.h"
#include <string.h> /* memset */

/* ============================= 从站地址分配 ============================= */
/* 从站1: STM32F103开发板 - 光敏传感器ADC采集 */
#define SLAVE_F103_1   1
/* 从站2: STM32F103开发板 - DHT11温湿度传感器 */
#define SLAVE_F103_2   2
/* 从站3: STM32F103开发板 - MQ2烟雾传感器 */
#define SLAVE_F103_3   3
/* 从站4: 西门子S7-200 Smart PLC(预留) */
#define SLAVE_PLC_S7   4

/* ============================= 静态映射表定义 ============================= */
/**
 * @brief 业务数据到Modbus寄存器的静态映射表
 * @author Yukikaze
 *
 * 表结构说明: { 逻辑ID, 从站地址, 寄存器地址, 可写标志, 初值, 有效标志 }
 *
 * 字段含义:
 *   - id: 业务逻辑层使用的枚举ID(APP_DATA_LIGHT_ADC等)
 *   - slave: Modbus从站地址(1~247)
 *   - reg_addr: 保持寄存器逻辑地址(40001~49999)
 *   - writable: 0=只读,1=可写(当前所有传感器数据为只读)
 *   - value: 寄存器值缓存(初值0xFFFF表示未采集)
 *   - valid: 数据有效标志(0=待采集,1=已采集)
 *
 * 注: APP_DATA_INVALID_VALUE = 0xFFFF,用于标识未初始化的数据
 */
static app_reg_map_t s_map[APP_DATA_COUNT] = {
	/* 从站1: 光敏电阻ADC值(0~4095,对应0~3.3V) */
	{ APP_DATA_LIGHT_ADC, SLAVE_F103_1, 40001, 0, APP_DATA_INVALID_VALUE, 0 }, 
	/* 从站2: 温度(整数,单位℃,范围0~50) */
	{ APP_DATA_TEMP1,     SLAVE_F103_2, 40011, 0, APP_DATA_INVALID_VALUE, 0 },
	/* 从站2: 湿度(整数,单位%,范围20~90) */
	{ APP_DATA_HUM1,      SLAVE_F103_2, 40012, 0, APP_DATA_INVALID_VALUE, 0 },
	/* 从站3: MQ2烟雾浓度 */
	{ APP_DATA_MQ2,       SLAVE_F103_3, 40021, 0, APP_DATA_INVALID_VALUE, 0 }, 
	/* 从站4: 备用温度通道(预留,暂未连接) */
	{ APP_DATA_TEMP2,     SLAVE_PLC_S7, 40031, 0, APP_DATA_INVALID_VALUE, 0 },
	/* 从站4: 备用湿度通道(预留,暂未连接) */
	{ APP_DATA_HUM2,      SLAVE_PLC_S7, 40032, 0, APP_DATA_INVALID_VALUE, 0 }
};

/**
 * @brief 根据从站地址和寄存器地址反查映射表索引
 * @author Yukikaze
 *
 * @param slave 从站地址(1~247)
 * @param reg_addr 保持寄存器逻辑地址(40001~49999)
 * @return int 映射表索引(0~APP_DATA_COUNT-1),未找到返回-1
 *
 * @note 用途: 当Modbus协议层解析响应帧后,通过(从站地址+寄存器地址)
 *       定位到映射表中的条目,以便更新对应的业务数据缓存
 *
 * @note 算法: 线性查找,时间复杂度O(n)
 *       如映射表较大(>50条),可改用哈希表或二分查找优化
 *
 * @example
 *       从站2返回寄存器40011的值 -> prv_find_by_slave_reg(2, 40011)
 *       -> 找到索引1(对应APP_DATA_TEMP1)
 */
static int prv_find_by_slave_reg(uint8_t slave, uint16_t reg_addr)
{
	/* 遍历映射表,匹配从站地址和寄存器地址 */
	for (size_t i = 0; i < APP_DATA_COUNT; ++i) {
		if (s_map[i].slave == slave && s_map[i].reg_addr == reg_addr) {
			return (int)i;  /* 找到匹配项,返回索引 */
		}
	}
	return -1; /* 遍历完成仍未找到:该寄存器未在映射表中配置 */
}

/**
 * @brief 根据逻辑 ID 获取映射条目指针（内部使用）
 * 
 * @param id 逻辑数据 ID
 * @return app_reg_map_t* 指向映射条目，越界返回 NULL
 */
static app_reg_map_t *prv_get_entry(app_data_id_t id)
{
	if (id < 0 || id >= APP_DATA_COUNT) return 0;
	return &s_map[id];
}

/**
 * @brief 初始化应用数据管理模块:复位所有数据为无效状态
 * @author Yukikaze
 *
 * @note 调用时机: 系统启动时,在Modbus通信初始化之前调用
 *
 * @note 操作内容:
 *       1. 将所有映射表条目的value字段设为0xFFFF(无效值)
 *       2. 将所有映射表条目的valid字段设为0(未采集标志)
 *
 * @note 作用: 确保在首次读取数据前,业务层能正确判断数据尚未采集,
 *       避免使用未初始化的随机值导致错误
 */
void app_data_init(void)
{
	/* 遍历映射表,将所有数据标记为"未采集"状态
	 * value = 0xFFFF: 魔数,表示无效值(不等于任何合法传感器读数)
	 * valid = 0: 标志位,表示该数据尚未从从站成功读取
	 *
	 * 初始化后的行为:
	 * - app_data_get_value()会返回-2(尚未采集)
	 * - MQTT上报会跳过这些数据或显示"等待采集"
	 */
	for (size_t i = 0; i < APP_DATA_COUNT; ++i) {
		s_map[i].value = APP_DATA_INVALID_VALUE;  /* 0xFFFF */
		s_map[i].valid = 0;                       /* 未采集 */
	}
}

/**
 * @brief 根据逻辑ID读取业务数据的当前缓存值
 * @author Yukikaze
 *
 * @param id 逻辑数据ID(APP_DATA_LIGHT_ADC等枚举值)
 * @param out_val 输出参数:接收寄存器值的指针,可为NULL(仅检查状态)
 * @return int 返回码:
 *             0: 成功读取,值已写入*out_val
 *            -1: ID越界(id < 0 或 id >= APP_DATA_COUNT)
 *            -2: 数据无效(尚未从从站成功采集)
 *
 * @note 使用场景:
 *       - MQTT上报前检查数据是否有效
 *       - 业务逻辑需要获取传感器最新值
 *       - 仅检查数据有效性而不读取值(out_val=NULL)
 *
 */
int app_data_get_value(app_data_id_t id, uint16_t *out_val)
{
	app_reg_map_t *e = prv_get_entry(id);  /* 获取映射表条目指针 */
	if (!e) return -1;               /* ID越界:无效的枚举值 */
	if (!e->valid) return -2;        /* 数据无效:尚未从Modbus从站采集 */
	if (out_val) *out_val = e->value;/* 复制缓存值到输出参数 */
	return 0;  /* 成功 */
}

/**
 * @brief 根据逻辑 ID 写入当前值
 * 
 * @param id 逻辑数据 ID
 * @param val 新值
 * @return int 0 成功；<0 失败（ID 越界 / 只读禁止写）
 */
int app_data_set_value(app_data_id_t id, uint16_t val)
{
	app_reg_map_t *e = prv_get_entry(id);
	if (!e) return -1;            /* 越界 */
	if (!e->writable) return -2;  /* 只读不允许写 */
	e->value = val;               /* 更新缓存 */
	e->valid = 1;                 /* 标记有效 */
	return 0;
}

/**
 * @brief Modbus协议层在收到从站响应后调用此函数更新缓存
 * @author Yukikaze
 *
 * @param slave 从站地址(1~247)
 * @param reg_addr 保持寄存器逻辑地址(40001~49999)
 * @param val 从从站读取的寄存器值(16位无符号整数)
 * @return int 返回码:
 *             0: 成功更新缓存
 *            -1: 未找到匹配的映射表项(该寄存器未配置)
 *
 * @note 调用时机: 由modbus_read.c中的prv_on_frame()回调函数调用,
 *       在RTU链路层完成CRC校验并交付完整响应帧后触发
 *
 * @note 更新流程:
 *       1. 通过(slave+reg_addr)在映射表中查找对应条目
 *       2. 将新值写入value字段
 *       3. 将valid标志置1,标记数据已采集且有效
 *
 * @note 线程安全: 当前实现未加锁,假设单线程调用(裸机环境)
 *       如需在RTOS中使用,需添加互斥锁保护s_map数组
 *
 * @example
 *       从站2返回寄存器40011=25 -> app_data_update_from_modbus(2, 40011, 25)
 *       -> s_map[1].value=25, s_map[1].valid=1
 */
int app_data_update_from_modbus(uint8_t slave, uint16_t reg_addr, uint16_t val)
{
	int idx = prv_find_by_slave_reg(slave, reg_addr);  /* 查找映射表索引 */
	if (idx < 0) return -1; /* 未找到:该寄存器不在映射表中(可能配置错误) */
	s_map[idx].value = val;  /* 更新缓存值 */
	s_map[idx].valid = 1;    /* 标记为有效(已成功采集) */
	return 0;  /* 成功 */
}

/**
 * @brief 返回映射条目指针（诊断/遍历用），不可修改内部字段
 * 
 * @param index 0..APP_DATA_COUNT-1
 * @return const app_reg_map_t* 或 NULL (越界)
 */
const app_reg_map_t *app_data_get_entry(size_t index)
{
	if (index >= APP_DATA_COUNT) return 0;
	return &s_map[index];
}

/**
 * @brief 检查某数据 ID 当前值是否有效
 * 
 * @param id 数据 ID
 * @return 1 有效；0 无效或越界
 */
int app_data_is_valid(app_data_id_t id)
{
	app_reg_map_t *e = prv_get_entry(id);
	if (!e) return 0;
	return e->valid ? 1 : 0;
}

/**
 * @brief 获取下一个待轮询的映射索引
 * 
 * @param cur 当前索引，-1 表示从头开始
 * @return int 下一个索引，末尾返回 -1
 */
int app_data_next_poll_index(int cur)
{
	/* 简单轮询：从当前下一个开始往后找第一个映射条目，若到末尾返回 -1 */
	int start = cur + 1;
	if (start < 0) start = 0;
	for (int i = start; i < (int)APP_DATA_COUNT; ++i) {
		/* 只读或可写都可以轮询；如需跳过可写条目可在此判断 writable==0 */
		return i; /* 返回找到的索引（当前策略：逐条都轮询） */
	}
	return -1; /* 末尾了 */
}


/**
 * @brief 处理读响应帧的批量更新（示例）：
 * 若一次读返回多个寄存器，可以在事务层解析后调用本函数做循环 app_data_update_from_modbus。
 * 这里给一个范式占位，留给后续在协议层完成。
 */
void app_data_bulk_update(uint8_t slave, const uint16_t *reg_list, const uint16_t *val_list, size_t count)
{
	if (!reg_list || !val_list) return;
	for (size_t i = 0; i < count; ++i) {
		(void)app_data_update_from_modbus(slave, reg_list[i], val_list[i]);
	}
}



