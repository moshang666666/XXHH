/**
 * @file modbus_transaction.c
 * @brief Modbus主站事务层:请求构造、超时管理、重试机制实现
 * @author Yukikaze
 * @date 2025-11-26
 *
 * 功能定位:
 *   事务层位于应用层(modbus_read)之下,协议层(modbus_protocol)之上,
 *   负责单次Modbus事务的全生命周期管理:
 *
 * 核心职责:
 *   1. 请求构造: 组装ADU帧(地址+功能码+数据)并调用链路层发送
 *   2. 状态跟踪: 维护事务状态机(空闲->等待响应->完成/错误)
 *   3. 超时检测: 记录请求发送时间,轮询检查是否超时(默认1秒)
 *   4. 重试机制: 超时后自动重发请求,最多重试2次
 *   5. 响应匹配: 校验响应的从站地址和功能码是否与请求匹配
 *
 * 使用流程:
 *   1. MB_Trans_Init()初始化事务上下文
 *   2. MB_Trans_ReadHolding()等函数发起请求
 *   3. 主循环中调用MB_Trans_Poll()检测超时
 *   4. 链路层收到完整帧后调用MB_Trans_OnFrame()处理响应
 *   5. 检查MB_Trans_GetState()判断事务是否完成
 *
 * 超时计算:
 *   默认超时1000ms(1秒),对于9600bps最坏情况:
 *   - 请求帧8字节 ≈ 9.16ms
 *   - 从站处理时间 ≈ 10~100ms
 *   - 响应帧最大256字节 ≈ 293ms
 *   - 总计 < 400ms,1秒超时足够
 */

#include "modbus_transaction.h"
#include "modbus_rtu_link.h"

/**
 * @note 缩写对照:
 *   - ADU (Application Data Unit): 应用数据单元
 *     格式: [地址(1)] [PDU] (不含CRC,由链路层自动追加)
 *     本层构造ADU并交给链路层发送
 *
 *   - PDU (Protocol Data Unit): 协议数据单元
 *     格式: [功能码(1)] [数据(N)]
 *     例: [03][00][00][00][01] -> 功能码0x03,读起始地址0x0000,数量0x0001
 *
 *   - ctx: context(事务上下文结构体)
 *     包含请求/响应缓冲、状态机、重试计数等
 *
 *   - cfg: config(事务配置结构体)
 *     包含超时时间、最大重试次数等参数
 *
 *   - ex: exception(Modbus异常码)
 *     0x01~0x08,表示从站返回的错误类型
 *
 *   - t_start_us: 请求发送的起始时间戳(微秒)
 *     用于计算elapsed time并判断是否超时
 *
 *   - WAIT_RESP: MB_TR_WAIT_RESP状态
 *     表示请求已发送,正在等待从站响应
 */

/* 写大端16位：将 16 位数值按高字节在前写入 p[0..1] */
static inline void w16be(uint8_t *p, uint16_t v){ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)(v&0xFF); }

/**
 * @brief 初始化事务上下文:设置超时参数和复位状态
 * @author Yukikaze
 *
 * @param ctx 指向事务上下文结构体的指针(输出参数)
 * @param cfg 指向事务配置的指针,NULL则使用默认配置
 *
 * @note 默认配置:
 *       - respond_timeout_us = 1000000 (1秒)
 *       - max_retries = 2 (最多重试2次,加上首次共3次尝试)
 *
 * @note 初始状态:
 *       - state = MB_TR_IDLE (空闲)
 *       - retries = 0 (重试计数清零)
 *       - req_len = 0, rsp_len = 0 (请求/响应缓冲长度清零)
 *       - ex = MB_EX_NONE (无异常)
 *
 * @note 调用时机: 在发起首次事务前调用一次即可,后续事务会自动复用
 */
void MB_Trans_Init(mb_trans_ctx_t *ctx, const mb_trans_cfg_t *cfg)
{
	if (!ctx) return;  /* 参数检查:ctx不能为NULL */
	
	/* 配置参数初始化:
	 * 优先使用用户提供的cfg,否则使用默认值
	 * 默认超时1000ms: 足够容纳9600bps下最大帧传输时间(~400ms)
	 * 默认重试2次: 共3次尝试机会,平衡可靠性和响应速度
	 */
	if (cfg) {
		ctx->cfg = *cfg;  /* 使用用户配置 */
	} else {
		ctx->cfg.respond_timeout_us = 1000000;  /* 1秒超时 */
		ctx->cfg.max_retries = 2;               /* 最多重试2次 */
	}
	
	/* 状态机初始化 */
	ctx->state   = MB_TR_IDLE;     /* 空闲状态,可接受新请求 */
	ctx->retries = 0;              /* 重试计数器清零 */
	ctx->req_len = ctx->rsp_len = 0;  /* 缓冲区长度清零 */
	ctx->ex      = MB_EX_NONE;     /* 无异常码 */
}


/**
 * @brief 获取事务当前状态
 * @author Yukikaze
 *
 * @param ctx 事务上下文指针
 * @return mb_tr_state_t 状态码:
 *         - MB_TR_IDLE: 空闲,可接受新请求
 *         - MB_TR_WAIT_RESP: 等待从站响应
 *         - MB_TR_DONE: 事务完成
 *         - MB_TR_ERROR: 事务失败(超时/异常响应)
 *         - 返回MB_TR_ERROR如果ctx为NULL
 *
 * @note 使用场景: 主循环中检查事务是否完成,以便处理下一个请求
 */
mb_tr_state_t MB_Trans_GetState(const mb_trans_ctx_t *ctx){ 
	return ctx ? ctx->state : MB_TR_ERROR; 
}

/**
 * @brief 获取最近一次的Modbus异常码
 * @author Yukikaze
 *
 * @param ctx 事务上下文指针
 * @return mb_exception_t 异常码:
 *         - MB_EX_NONE (0x00): 无异常
 *         - MB_EX_ILLEGAL_FUNCTION (0x01): 非法功能码
 *         - MB_EX_ILLEGAL_DATA_ADDRESS (0x02): 非法寄存器地址
 *         - MB_EX_ILLEGAL_DATA_VALUE (0x03): 非法数据值
 *         - MB_EX_SLAVE_DEVICE_FAILURE (0x04): 从站设备故障
 *         - MB_EX_SLAVE_BUSY (0x06): 从站忙/超时
 *         - 返回MB_EX_SLAVE_DEVICE_FAILURE如果ctx为NULL
 *
 * @note 有意义时机: 仅在状态为MB_TR_ERROR或MB_TR_DONE时有效
 * @note 典型用法: 事务失败后调用此函数获取失败原因
 */
mb_exception_t MB_Trans_GetException(const mb_trans_ctx_t *ctx){ 
	return ctx ? ctx->ex : MB_EX_SLAVE_DEVICE_FAILURE; 
}


/**
 * @brief 处理链路层交付的响应帧:校验匹配性并解析PDU
 * @author Yukikaze
 *
 * @param ctx 事务上下文指针
 * @param adu 指向响应ADU的指针(地址+PDU,不含CRC)
 * @param aduLen ADU长度(字节数)
 *
 * @note 调用时机: 由RTU链路层在T3.5超时后,CRC校验通过时调用
 *
 * @note 响应匹配规则:
 *       1. 状态必须为MB_TR_WAIT_RESP(正在等待响应)
 *       2. 从站地址必须与请求一致
 *       3. 功能码必须匹配(忽略最高位,因异常响应会置1)
 *
 * @note 状态转换:
 *       - 解析成功 -> MB_TR_DONE (事务完成)
 *       - 解析失败/异常响应 -> MB_TR_ERROR (事务错误)
 */
void MB_Trans_OnFrame(mb_trans_ctx_t *ctx, const uint8_t *adu, uint16_t aduLen)
{
	/* 参数合法性检查 */
	if (!ctx || !adu || aduLen < 2) return;  /* 最小响应:地址+功能码 */
	
	/* 状态检查:只处理"等待响应"状态的帧 */
	if (ctx->state != MB_TR_WAIT_RESP) return;  /* 忽略未预期的响应 */

	/* 响应匹配性校验:
	 * 1. 从站地址匹配: adu[0] == ctx->req_adu[0]
	 * 2. 功能码匹配: (adu[1] & 0x7F) == ctx->req_adu[1]
	 *    使用& 0x7F屏蔽最高位,因为异常响应会将功能码最高位置1
	 *    例: 请求0x03 -> 正常响应0x03, 异常响应0x83
	 */
	if (adu[0] != ctx->req_adu[0]) return;      /* 从站地址不匹配:丢弃 */
	if ((adu[1] & 0x7F) != ctx->req_adu[1]) return;  /* 功能码不匹配:丢弃 */

	/* 保存响应ADU到上下文缓冲区
	 * 防止缓冲区溢出:如果响应帧超过缓冲大小,裁剪到缓冲上限
	 * 标准Modbus RTU最大帧256字节,缓冲区应足够大
	 */
	if (aduLen > sizeof(ctx->rsp_adu)) aduLen = sizeof(ctx->rsp_adu);
	for (uint16_t i=0; i<aduLen; i++) ctx->rsp_adu[i] = adu[i];
	ctx->rsp_len = aduLen;  /* 记录实际响应长度 */

	/* 调用协议层解析响应PDU:
	 * - 检查功能码是否为异常响应(最高位=1)
	 * - 验证PDU字段长度是否符合功能码规范
	 * - 提取异常码(如果是异常响应)
	 * 
	 * 返回值:
	 *   pr >= 0: PDU解析成功,数据有效
	 *   pr < 0: PDU格式错误或从站返回异常
	 */
	mb_exception_t ex = MB_EX_NONE;
	int pr = MB_Protocol_ParseResponse(ctx->rsp_adu, ctx->rsp_len, &ex);
	ctx->ex = ex;  /* 保存异常码(无论成功或失败) */
	
	if (pr >= 0) {
		/* 解析成功:正常响应,数据有效 */
		ctx->state = MB_TR_DONE;
	} else {
		/* 解析失败:异常响应或PDU格式错误 */
		ctx->state = MB_TR_ERROR;
	}
}

/**
 * @brief       周期轮询：检测超时并负责重发
 * 
 * @param ctx   事务上下文
 */
void MB_Trans_Poll(mb_trans_ctx_t *ctx)
{
	if (!ctx) return;
	if (ctx->state == MB_TR_WAIT_RESP) {
		uint32_t now = MB_TimeNowUs();
		uint32_t elapsed = (now - ctx->t_start_us);
		if (elapsed >= ctx->cfg.respond_timeout_us) {
			/* 超时：如未达到重试上限，重发 */
			if (ctx->retries < ctx->cfg.max_retries) {
				ctx->retries++;
				MB_RTU_Send(ctx->req_adu, ctx->req_len);
				ctx->t_start_us = MB_TimeNowUs();
			} else {
				ctx->ex = MB_EX_SLAVE_BUSY; /* 可替换为“自定义超时异常映射” */
				ctx->state = MB_TR_ERROR;
			}
		}
	}
}

/**
 * @brief 发送Modbus请求的内部通用函数:组装ADU并启动事务
 * @author Yukikaze
 *
 * @param ctx 事务上下文指针
 * @param slave 从站地址(1~247)
 * @param fc 功能码(0x01~0x10等)
 * @param pdu 指向PDU数据的指针(功能码后的字段:地址、数量等)
 * @param pduLen PDU长度(字节数,不含功能码本身)
 * @return int 返回码:
 *             0: 成功发送
 *            -1: ctx为NULL
 *            -2: 状态机不允许发送(前一事务未完成)
 *
 * @note 状态检查:
 *       只允许在IDLE/DONE/ERROR状态下发起新事务
 *       WAIT_RESP状态拒绝新请求(避免并发冲突)
 *
 * @note ADU组装:
 *       req_adu[] = [从站地址(1)] [功能码(1)] [PDU数据(N)]
 *       链路层会自动追加CRC16(2字节)
 *
 * @note 状态转换:
 *       发送成功后立即进入MB_TR_WAIT_RESP状态,等待从站响应
 */
static int prv_send(mb_trans_ctx_t *ctx, uint8_t slave, uint8_t fc, const uint8_t *pdu, uint16_t pduLen)
{
	if (!ctx) return -1;  /* 参数检查 */
	
	/* 状态检查:确保可以发起新事务
	 * 允许的状态: IDLE(初始), DONE(上次成功), ERROR(上次失败)
	 * 拒绝的状态: WAIT_RESP(正在等待响应,不能并发)
	 */
	if (ctx->state != MB_TR_IDLE && 
	    ctx->state != MB_TR_DONE && 
	    ctx->state != MB_TR_ERROR) {
		return -2;  /* 状态冲突:前一事务未完成 */
	}
	
	/* 复位事务状态 */
	ctx->state = MB_TR_IDLE;   /* 重置为空闲 */
	ctx->retries = 0;          /* 清零重试计数 */
	ctx->ex = MB_EX_NONE;      /* 清除旧异常码 */

	/* 组装请求ADU:
	 * ADU格式: [地址(1)] [功能码(1)] [PDU数据(pduLen)]
	 * 例: 读保持寄存器40001,数量1 -> [01][03][00][00][00][01]
	 */
	uint16_t len = 0;
	ctx->req_adu[len++] = slave;  /* 从站地址 */
	ctx->req_adu[len++] = fc;     /* 功能码 */
	for (uint16_t i=0; i<pduLen; i++) {
		ctx->req_adu[len++] = pdu[i];  /* 复制PDU数据 */
	}
	ctx->req_len = len;  /* 记录ADU总长度 */

	/* 通过链路层发送(链路层会自动追加CRC16) */
	MB_RTU_Send(ctx->req_adu, ctx->req_len);
	
	/* 记录发送时刻,用于超时检测 */
	ctx->t_start_us = MB_TimeNowUs();
	
	/* 进入等待响应状态 */
	ctx->state = MB_TR_WAIT_RESP;
	return 0;  /* 成功 */
}

/**
 * @brief 读保持寄存器(功能码0x03)
 * @author Yukikaze
 *
 * @param ctx 事务上下文指针
 * @param slave 从站地址(1~247)
 * @param addr 起始寄存器地址(0~65535,对应40001~49999的偏移)
 * @param qty 读取寄存器数量(1~125)
 * @return int 返回码:
 *             0: 请求成功发送
 *            -1: ctx为NULL
 *            -2: 状态机不允许发送(前一事务未完成)
 *
 * @note Modbus寄存器地址映射:
 *       逻辑地址40001 -> addr=0, 40002 -> addr=1, ...
 *       本函数使用偏移地址,调用者需自行转换
 *
 * @note 请求PDU格式: [起始地址H][起始地址L][数量H][数量L]
 * @example 读从站1的40001寄存器1个:
 *          MB_Trans_ReadHolding(ctx, 1, 0, 1)
 */
int MB_Trans_ReadHolding(mb_trans_ctx_t *ctx, uint8_t slave, uint16_t addr, uint16_t qty)
{
	uint8_t pdu[4];  /* PDU数据:起始地址(2)+数量(2) */
	w16be(&pdu[0], addr);  /* 大端序写入起始地址 */
	w16be(&pdu[2], qty);   /* 大端序写入寄存器数量 */
	return prv_send(ctx, slave, MB_FC_READ_HOLDING_REGS, pdu, 4);
}

/**
 * @brief 读输入寄存器(功能码0x04)
 * @author Yukikaze
 *
 * @param ctx 事务上下文指针
 * @param slave 从站地址(1~247)
 * @param addr 起始寄存器地址(0~65535,对应30001~39999的偏移)
 * @param qty 读取寄存器数量(1~125)
 * @return int 返回码(同MB_Trans_ReadHolding)
 *
 * @note 输入寄存器为只读类型,通常用于传感器数据采集
 * @note 与保持寄存器区别:输入寄存器不可写(功能码0x04 vs 0x03)
 */
int MB_Trans_ReadInput(mb_trans_ctx_t *ctx, uint8_t slave, uint16_t addr, uint16_t qty)
{
	uint8_t pdu[4];
	w16be(&pdu[0], addr);
	w16be(&pdu[2], qty);
	return prv_send(ctx, slave, MB_FC_READ_INPUT_REGS, pdu, 4);
}

/**
 * @brief 读线圈（0x01）
 * 
 * @param ctx 事务上下文
 * @param slave 从站地址
 * @param addr 起始线圈地址
 * @param qty 读取线圈数量
 * @return int 0 表示成功，负值表示失败
 */
int MB_Trans_ReadCoils(mb_trans_ctx_t *ctx, uint8_t slave, uint16_t addr, uint16_t qty)
{
	uint8_t pdu[5];
	w16be(&pdu[0], addr);
	w16be(&pdu[2], qty);
	return prv_send(ctx, slave, MB_FC_READ_COILS, pdu, 4);
}

/**
 * @brief 读离散输入（0x02）
 * 
 * @param ctx 事务上下文
 * @param slave 从站地址
 * @param addr 起始离散输入地址
 * @param qty 读取离散输入数量
 * @return int 0 表示成功，负值表示失败
 */
int MB_Trans_ReadDiscrete(mb_trans_ctx_t *ctx, uint8_t slave, uint16_t addr, uint16_t qty)
{
	uint8_t pdu[5];
	w16be(&pdu[0], addr);
	w16be(&pdu[2], qty);
	return prv_send(ctx, slave, MB_FC_READ_DISCRETE_INPUTS, pdu, 4);
}

/**
 * @brief 写单个保持寄存器(功能码0x06)
 * @author Yukikaze
 *
 * @param ctx 事务上下文指针
 * @param slave 从站地址(1~247)
 * @param addr 寄存器地址(0~65535)
 * @param value 寄存器值(16位无符号整数)
 * @return int 返回码(同MB_Trans_ReadHolding)
 *
 * @note 请求PDU格式: [地址H][地址L][值H][值L]
 * @note 响应格式与请求相同(回显确认)
 * @example 设置从站2的40011寄存器为100:
 *          MB_Trans_WriteSingleReg(ctx, 2, 10, 100)
 */
int MB_Trans_WriteSingleReg(mb_trans_ctx_t *ctx, uint8_t slave, uint16_t addr, uint16_t value)
{
	uint8_t pdu[4];  /* PDU数据:地址(2)+值(2) */
	w16be(&pdu[0], addr);   /* 大端序写入寄存器地址 */
	w16be(&pdu[2], value);  /* 大端序写入寄存器值 */
	return prv_send(ctx, slave, MB_FC_WRITE_SINGLE_REG, pdu, 4);
}

/**
 * @brief 写多个保持寄存器(功能码0x10)
 * @author Yukikaze
 *
 * @param ctx 事务上下文指针
 * @param slave 从站地址(1~247)
 * @param addr 起始寄存器地址(0~65535)
 * @param qty 写入寄存器数量(1~123)
 * @param values 指向寄存器值数组的指针(每个值16位)
 * @return int 返回码:
 *             0: 请求成功发送
 *            -1: values为NULL
 *            -2: PDU长度超限(qty过大)
 *            其他: 同MB_Trans_ReadHolding
 *
 * @note 请求PDU格式: [起始地址(2)][数量(2)][字节数(1)][数据(2×qty)]
 * @note 最大寄存器数量限制:
 *       PDU最大252字节 -> 字节数=qty×2 -> 最大qty=123
 *       实际Modbus标准限制qty≤123
 *
 * @note 字节数字段计算: byte_count = qty × 2 (每个寄存器2字节)
 * @example 写3个寄存器: values[]={100,200,300}
 *          MB_Trans_WriteMultipleRegs(ctx, 1, 0, 3, values)
 */
int MB_Trans_WriteMultipleRegs(mb_trans_ctx_t *ctx, uint8_t slave, uint16_t addr, uint16_t qty, const uint16_t *values)
{
	if (!values) return -1;  /* 参数检查:values数组不能为NULL */
	
	/* PDU长度计算和边界检查:
	 * PDU = 起始地址(2) + 数量(2) + 字节数(1) + 数据(qty×2)
	 * PDU最大252字节(Modbus RTU限制:ADU≤256,扣除地址/功能/CRC)
	 */
	uint16_t bytes = (uint16_t)(qty * 2);  /* 数据字节数 */
	uint16_t pduLen = (uint16_t)(5 + bytes);  /* PDU总长度 */
	if (pduLen > 252) return -2;  /* 超过最大PDU长度 */

	/* 构造PDU */
	uint8_t pdu[252];  /* 本地PDU缓冲区 */
	w16be(&pdu[0], addr);  /* 起始地址(大端) */
	w16be(&pdu[2], qty);   /* 寄存器数量(大端) */
	pdu[4] = (uint8_t)bytes;  /* 字节数 */
	
	/* 逐个写入寄存器值(大端序) */
	for (uint16_t i=0; i<qty; i++) {
		w16be(&pdu[5 + 2*i], values[i]);
	}
	
	return prv_send(ctx, slave, MB_FC_WRITE_MULTIPLE_REGS, pdu, pduLen);
}

