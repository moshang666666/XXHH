/**
 * @file modbus_protocol.h
 * @brief Modbus 主站协议解析层接口：负责 PDU(功能码+数据) 的解析与构造，调用应用数据管理。
 *
 * - 对 RTU 链路层交付的 ADU(地址+PDU) 做基础合法性检查。
 * - 解析功能码，调用对应的应用数据访问回调(读取/写入保持寄存器、线圈、离散输入、输入寄存器)。
 * - 将解析结果返回给事务层，事务层再决定是否继续下一步或报告上层。
 * - 维护异常码（非法功能、地址、值、设备故障等）。
 *
 * - 主站场景下，本模块主要做响应帧的解析；构造请求帧由事务层完成。
 * - 若本设备也支持从站角色，可在此增加应答 PDU 构造函数（当前聚焦主站，不实现从站应答）。
 */
#ifndef __MODBUS_PROTOCOL_H__
#define __MODBUS_PROTOCOL_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== 功能码枚举（主站常用） ========================== */
typedef enum {
	MB_FC_READ_COILS            = 0x01,
	MB_FC_READ_DISCRETE_INPUTS  = 0x02,
	MB_FC_READ_HOLDING_REGS     = 0x03,
	MB_FC_READ_INPUT_REGS       = 0x04,
	MB_FC_WRITE_SINGLE_COIL     = 0x05,
	MB_FC_WRITE_SINGLE_REG      = 0x06,
	MB_FC_WRITE_MULTIPLE_COILS  = 0x0F,
	MB_FC_WRITE_MULTIPLE_REGS   = 0x10,
	MB_FC_READWRITE_MULTIPLE_REGS = 0x17
} mb_function_code_t;

/* ========================== 异常码定义 ========================== */
typedef enum {
	MB_EX_NONE                 = 0x00, /* 正常 */
	MB_EX_ILLEGAL_FUNCTION     = 0x01, 
	MB_EX_ILLEGAL_DATA_ADDRESS = 0x02,
	MB_EX_ILLEGAL_DATA_VALUE   = 0x03,
	MB_EX_SLAVE_DEVICE_FAILURE = 0x04,
	MB_EX_ACKNOWLEDGE          = 0x05,
	MB_EX_SLAVE_BUSY           = 0x06,
	MB_EX_MEMORY_PARITY_ERROR  = 0x08
} mb_exception_t;

/* ========================== 寄存器访问方向枚举 ========================== */
typedef enum {
	MB_REG_READ = 0,  /* 读 */
	MB_REG_WRITE      /* 写 */
} mb_reg_mode_t;

/* ========================== 应用层数据访问回调 ========================== */
/* 应用层需实现以下函数，协议层通过它们操作真实数据：返回 0=成功，非0 转换为异常码。 */
typedef int (*mb_app_read_regs_t)(uint16_t startAddr, uint16_t quantity, uint16_t *outBuf);
typedef int (*mb_app_write_regs_t)(uint16_t startAddr, uint16_t quantity, const uint16_t *inBuf);
typedef int (*mb_app_read_bits_t)(uint16_t startAddr, uint16_t quantity, uint8_t *outPackedBits); /* 线圈/离散输入读，位打包 */
typedef int (*mb_app_write_bits_t)(uint16_t startAddr, uint16_t quantity, const uint8_t *inPackedBits);

typedef struct {
	mb_app_read_regs_t  readHolding;
	mb_app_write_regs_t writeHolding;
	mb_app_read_regs_t  readInput;      /* 输入寄存器只读 */
	mb_app_read_bits_t  readCoils;
	mb_app_write_bits_t writeCoils;
	mb_app_read_bits_t  readDiscrete;
} mb_protocol_app_if_t;

/* 注册应用层数据访问接口（系统启动时调用一次） */
void MB_Protocol_RegisterAppIF(const mb_protocol_app_if_t *appIf);

/* ========================== 解析入口 ========================== */
/* 解析 ADU：入参 adu 指向 地址+PDU (不含 CRC)，长度 aduLen。
 * 返回 0 表示正常处理；负值表示异常或解析错误。异常码若产生由 *pException 输出。
 * 对于读操作：需要上层准备好接收缓冲(事务层会提供)，本函数只解析与调用回调。
 */
int MB_Protocol_ParseResponse(const uint8_t *adu, uint16_t aduLen, mb_exception_t *pException);

/* 将低层错误码(应用层返回)映射为 Modbus 异常码 */
mb_exception_t MB_Protocol_MapError(int appErr);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_PROTOCOL_H__ */
