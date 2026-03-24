/**
 * @file modbus_transaction.h
 * @brief Modbus 主站事务层：构造请求、发送、等待响应，支持超时与重试。
 */

#ifndef __MODBUS_TRANSACTION_H__
#define __MODBUS_TRANSACTION_H__

#include <stdint.h>
#include "modbus_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 事务配置 */
typedef struct {
	uint32_t respond_timeout_us;   /* 响应超时，例如 500000us */
	uint8_t  max_retries;          /* 最大重试次数，例如 2 */
} mb_trans_cfg_t;

/* 事务状态 */
typedef enum {
	MB_TR_IDLE = 0,
	MB_TR_WAIT_RESP,
	MB_TR_DONE,
	MB_TR_ERROR
} mb_tr_state_t;

/* 事务上下文（单通道简化实现） */
typedef struct {
	mb_trans_cfg_t cfg;
	mb_tr_state_t  state;
	uint8_t retries;
	uint32_t t_start_us;

	/* 请求 ADU（地址+PDU，不含CRC）缓存与长度 */
	uint8_t  req_adu[256];
	uint16_t req_len;

	/* 响应 ADU（地址+PDU，不含CRC）缓存与长度 */
	uint8_t  rsp_adu[256];
	uint16_t rsp_len;

	mb_exception_t ex;
} mb_trans_ctx_t;

/* 初始化事务层 */
void MB_Trans_Init(mb_trans_ctx_t *ctx, const mb_trans_cfg_t *cfg);

/* 轮询驱动（放在主循环或定时器中调用） */
void MB_Trans_Poll(mb_trans_ctx_t *ctx);

/* 供 RTU 链路层回调：收到响应帧后由上层转给事务层匹配（地址/功能码应与请求一致） */
void MB_Trans_OnFrame(mb_trans_ctx_t *ctx, const uint8_t *adu, uint16_t aduLen);

/* 同步 API：发起常用请求（内部构造 PDU） */
int MB_Trans_ReadHolding(mb_trans_ctx_t *ctx, uint8_t slave, uint16_t addr, uint16_t qty);
int MB_Trans_ReadInput(mb_trans_ctx_t *ctx,   uint8_t slave, uint16_t addr, uint16_t qty);
int MB_Trans_ReadCoils(mb_trans_ctx_t *ctx,   uint8_t slave, uint16_t addr, uint16_t qty);
int MB_Trans_ReadDiscrete(mb_trans_ctx_t *ctx,uint8_t slave, uint16_t addr, uint16_t qty);
int MB_Trans_WriteSingleReg(mb_trans_ctx_t *ctx, uint8_t slave, uint16_t addr, uint16_t value);
int MB_Trans_WriteMultipleRegs(mb_trans_ctx_t *ctx, uint8_t slave, uint16_t addr, uint16_t qty, const uint16_t *values);

/* 获取当前状态 */
mb_tr_state_t MB_Trans_GetState(const mb_trans_ctx_t *ctx);
mb_exception_t MB_Trans_GetException(const mb_trans_ctx_t *ctx);

/* 时间获取抽象：需由应用实现，返回当前系统时间(微秒) */
uint32_t MB_TimeNowUs(void);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_TRANSACTION_H__ */
