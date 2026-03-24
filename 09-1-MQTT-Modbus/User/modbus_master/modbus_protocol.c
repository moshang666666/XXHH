/**
 * @file modbus_protocol.c
 * @brief Modbus协议层:PDU格式验证、异常响应检测、应用错误码映射
 * @author Yukikaze
 * @date 2025-11-26
 *
 * 模块定位:
 *   位于RTU链路层(modbus_rtu_link)和事务层(modbus_transaction)之间,
 *   负责Modbus协议数据单元(PDU)的格式验证和初步解析。
 *
 * 核心功能:
 *   1. 异常响应检测: 识别功能码最高位为1的异常响应(fc | 0x80)
 *   2. PDU格式验证: 验证各功能码的响应长度和字段合法性
 *   3. 错误码映射: 将应用层错误码映射为标准Modbus异常码
 *   4. 应用接口: 支持可选的应用层回调接口(主站模式通常不使用)
 *
 * 支持的功能码:
 *   - 0x01: 读线圈(Read Coils)
 *   - 0x02: 读离散输入(Read Discrete Inputs)
 *   - 0x03: 读保持寄存器(Read Holding Registers)
 *   - 0x04: 读输入寄存器(Read Input Registers)
 *   - 0x05: 写单个线圈(Write Single Coil)
 *   - 0x06: 写单个寄存器(Write Single Register)
 *   - 0x0F: 写多个线圈(Write Multiple Coils)
 *   - 0x10: 写多个寄存器(Write Multiple Registers)
 *   - 0x17: 读写多个寄存器(Read/Write Multiple Registers)
 *
 * 数据流向:
 *   [RTU链路层] --ADU(地址+PDU)--> [本模块验证] --合法性判断--> [事务层处理]
 */

#include "modbus_protocol.h"

/* 缩写说明：
 * - ADU: 地址+PDU（主站这里接收的已去除 CRC）
 * - PDU: 功能码+数据
 * - app_if: 应用层接口，主站模式下通常可不使用
 */ 

/**
 * @brief 应用层回调接口指针(可选)
 * @author Yukikaze
 *
 * @note 用途: 从站模式下,用于回调应用层读写本地数据
 * @note 主站模式: 通常为NULL,不使用此接口
 * @note 线程安全: 静态变量,仅在初始化时设置,运行时只读
 *
 * @note 回调时机:
 *   - 从站收到读请求: 调用read_coils/read_regs等读取本地数据
 *   - 从站收到写请求: 调用write_coils/write_regs等写入本地数据
 *   - 主站收到响应: 不调用,由事务层直接解析
 */
static const mb_protocol_app_if_t *s_app_if = 0;

/**
 * @brief 注册应用层回调接口(从站模式使用)
 * @author Yukikaze
 *
 * @param appIf 指向应用层接口结构体的指针,NULL表示禁用回调
 *
 * @note 调用时机: 在Modbus初始化阶段调用,通常在main()开始处
 * @note 主站模式: 不需要调用此函数,保持s_app_if=NULL即可
 * @note 从站模式: 必须注册接口,否则无法响应主站的读写请求
 *
 * @note 接口结构体mb_protocol_app_if_t包含:
 *   - read_coils: 读线圈回调
 *   - read_discrete_inputs: 读离散输入回调
 *   - read_holding_regs: 读保持寄存器回调
 *   - read_input_regs: 读输入寄存器回调
 *   - write_single_coil: 写单个线圈回调
 *   - write_single_reg: 写单个寄存器回调
 *   - write_multiple_coils: 写多个线圈回调
 *   - write_multiple_regs: 写多个寄存器回调
 *
 * @example 从站模式示例:
 *   static const mb_protocol_app_if_t slave_if = {
 *       .read_holding_regs = my_read_holding_regs,
 *       .write_single_reg = my_write_single_reg,
 *       // ...
 *   };
 *   MB_Protocol_RegisterAppIF(&slave_if);
 */
void MB_Protocol_RegisterAppIF(const mb_protocol_app_if_t *appIf)
{
    s_app_if = appIf;  /* 保存接口指针(运行时只读,无需加锁) */
}

/**
 * @brief 将应用层错误码映射为 Modbus 异常码（主站/从站通用工具）
 *
 * @param appErr 应用内的错误码（0 表示成功）
 * @return Modbus 异常码
 */
mb_exception_t MB_Protocol_MapError(int appErr)
{
    if (appErr == 0)
        return MB_EX_NONE;
    switch (appErr)
    {
    case -1:
        return MB_EX_ILLEGAL_DATA_ADDRESS; /* 地址越界 */
    case -2:
        return MB_EX_ILLEGAL_DATA_VALUE; /* 数据非法 */
    case -3:
        return MB_EX_SLAVE_BUSY; /* 设备忙 */
    default:
        return MB_EX_SLAVE_DEVICE_FAILURE; /* 其他失败 */
    }
}

/**
 * @brief 大端序字节数组转16位整数(辅助工具)
 * @author Yukikaze
 *
 * @param p 指向2字节数组的指针
 * @return uint16_t 16位无符号整数
 *
 * @note 字节序: 大端(Big-Endian),高字节在前
 * @note Modbus协议: 所有16位数据均采用大端序传输
 *
 * @example
 *   uint8_t buf[] = {0x12, 0x34};
 *   uint16_t value = u16be(buf);  // value = 0x1234 (4660)
 *
 * @note 转换逻辑:
 *   p[0] = 高字节(MSB) -> 左移8位
 *   p[1] = 低字节(LSB) -> 保持原位
 *   结果 = (p[0] << 8) | p[1]
 */
static inline uint16_t u16be(const uint8_t *p) { 
    return (uint16_t)((p[0] << 8) | p[1]); 
}

/**
 * @brief 解析主站收到的Modbus响应ADU(地址+PDU,不含CRC)
 * @author Yukikaze
 *
 * @param adu 指向响应ADU字节数组的指针(链路层已去除CRC)
 * @param aduLen ADU总长度(字节数,包含地址和PDU)
 * @param pException 输出参数:异常码指针,可为NULL
 *                   - 正常响应: 写入MB_EX_NONE (0x00)
 *                   - 异常响应: 写入从站返回的异常码(0x01~0x08)
 *                   - 不支持功能码: 写入MB_EX_ILLEGAL_FUNCTION (0x01)
 *
 * @return int 解析结果:
 *             >= 0: PDU格式验证通过,数据有效
 *             -1: 参数错误(adu为NULL或长度不足)
 *             -2: 异常响应(从站返回错误)
 *             -3: PDU格式错误(字节数不匹配)
 *             -4: 不支持的功能码
 *
 * @note 调用流程:
 *   1. 链路层完成CRC校验后调用事务层MB_Trans_OnFrame()
 *   2. 事务层调用本函数验证PDU格式合法性
 *   3. 本函数返回>=0后,事务层解析具体数据字段
 *
 * @note PDU验证内容:
 *   - 异常响应检测: 功能码最高位是否为1
 *   - 长度校验: 根据功能码验证PDU长度是否符合协议规范
 *   - 字节数校验: 读响应中的字节数字段是否与后续数据长度匹配
 *
 * @note 功能码分类:
 *   - 读操作(0x01~0x04): 响应包含字节数字段+数据
 *   - 写操作(0x05,0x06): 响应回显请求的地址和值
 *   - 批量写(0x0F,0x10): 响应回显起始地址和数量
 *   - 读写操作(0x17): 响应包含读取的数据
 *
 * @example
 *   mb_exception_t ex;
 *   int ret = MB_Protocol_ParseResponse(adu, len, &ex);
 *   if (ret >= 0) {
 *       // PDU格式正确,继续解析数据
 *   } else if (ret == -2) {
 *       printf("从站返回异常码: 0x%02X\n", ex);
 *   }
 */
int MB_Protocol_ParseResponse(const uint8_t *adu, uint16_t aduLen, mb_exception_t *pException)
{
    /* 初始化输出参数:默认无异常 */
    if (pException)
        *pException = MB_EX_NONE;
    
    /* 参数合法性检查:最小ADU = 地址(1) + 功能码(1) */
    if (!adu || aduLen < 2)
        return -1;  /* 参数错误或长度不足 */

    /* 提取ADU各字段:
     * ADU格式: [从站地址(1)] [PDU...]
     * PDU格式: [功能码(1)] [数据(N)]
     */
    uint8_t addr = adu[0];        /* 从站地址(1~247) */
    uint8_t fc = adu[1];          /* 功能码(0x01~0x7F正常,0x81~0xFF异常) */
    const uint8_t *pdu = &adu[1]; /* PDU起始地址(从功能码开始) */
    uint16_t pduLen = (aduLen - 1); /* PDU长度(总长度减去地址字节) */

    (void)addr; /* 主站可根据需要校验响应地址是否匹配请求
                 * 当前实现:地址匹配由事务层MB_Trans_OnFrame()负责 */

    /* ==================== 异常响应检测 ==================== */
    /* Modbus异常响应规范:
     * - 功能码最高位置1: fc = 原功能码 | 0x80
     * - 例: 请求0x03 -> 异常响应0x83
     * - PDU格式: [异常功能码(1)] [异常码(1)]
     * 
     * 检测方法: fc & 0x80 != 0 表示异常响应
     */
    if (fc & 0x80)
    {
        /* 异常响应:提取异常码字节
         * adu[2] = 异常码(Exception Code)
         * 常见异常码:
         *   0x01 = 非法功能码
         *   0x02 = 非法数据地址
         *   0x03 = 非法数据值
         *   0x04 = 从站设备故障
         *   0x06 = 从站设备忙
         */
        if (pduLen >= 2)  /* 确保包含异常码字节 */
        {
            if (pException)
                *pException = (mb_exception_t)adu[2];  /* 返回异常码 */
        }
        return -2;  /* 异常帧标识 */
    }

    /* ==================== 应用层接口检查 ==================== */
    if (!s_app_if)
    {
        /* 主站模式(未注册应用接口):
         * 本层只做PDU基本格式检查,不解析具体数据内容
         * 数据解析由事务层或更上层的应用层负责
         * 
         * 策略: 异常响应已检测完毕,正常响应直接返回0
         *       让事务层根据功能码自行解析数据字段
         */
        return 0;  /* 格式检查通过,交由事务层处理 */
    }

    /* ==================== 功能码分支处理 ==================== */
    /* 从站模式(已注册应用接口):
     * 需要详细验证各功能码的PDU格式,并调用应用层回调
     * 主站模式下不会执行到这里(s_app_if为NULL已提前返回)
     */
    switch (fc)
    {
    /* ------------------ 读线圈/离散输入(0x01/0x02) ------------------ */
    case MB_FC_READ_COILS:           /* 0x01: 读线圈 */
    case MB_FC_READ_DISCRETE_INPUTS: /* 0x02: 读离散输入 */
    {
        /* 响应PDU格式: [功能码(1)] [字节数(1)] [位数据(N)]
         * 
         * 字段说明:
         *   - pdu[0] = 功能码(0x01或0x02)
         *   - pdu[1] = 字节数N(bit_count / 8 向上取整)
         *   - pdu[2..N+1] = 线圈/离散输入状态位
         * 
         * 位编码: 每字节8位,低位在前
         *   例: 读10个线圈 -> 字节数=2, pdu[2]低10位有效
         */
        if (pduLen < 2)  /* 最小长度:功能码+字节数 */
            return -3;   /* PDU格式错误 */
        
        uint8_t bytecnt = pdu[1];  /* 提取字节数字段 */
        
        /* 验证实际数据长度是否与字节数匹配 */
        if (pduLen < (uint16_t)(2 + bytecnt))
            return -3;  /* 数据长度不足 */
        
        /* 格式验证通过
         * 从站模式: 可在此调用应用层回调处理数据
         * 主站模式: 事务层会解析pdu[2..N+1]的位数据
         */
        return 0;
    }

    /* ------------------ 读保持/输入寄存器(0x03/0x04) ------------------ */
    case MB_FC_READ_HOLDING_REGS:  /* 0x03: 读保持寄存器 */
    case MB_FC_READ_INPUT_REGS:    /* 0x04: 读输入寄存器 */
    {
        /* 响应PDU格式: [功能码(1)] [字节数(1)] [寄存器数据(N)]
         * 
         * 字段说明:
         *   - pdu[0] = 功能码(0x03或0x04)
         *   - pdu[1] = 字节数N(寄存器数量 × 2)
         *   - pdu[2..N+1] = 寄存器值(大端序,每个寄存器2字节)
         * 
         * 寄存器编码: 16位大端序
         *   例: 读2个寄存器 -> 字节数=4
         *       pdu[2:3] = 第1个寄存器(高字节:低字节)
         *       pdu[4:5] = 第2个寄存器(高字节:低字节)
         */
        if (pduLen < 2)  /* 最小长度:功能码+字节数 */
            return -3;   /* PDU格式错误 */
        
        uint8_t bytecnt = pdu[1];  /* 提取字节数字段 */
        
        /* 寄存器数据长度校验:
         * 字节数必须是2的倍数(每个寄存器2字节)
         * 奇数字节数表示PDU格式错误
         */
        if ((bytecnt % 2) != 0)
            return -3;  /* 字节数不是2的倍数 */
        
        /* 验证实际数据长度是否与字节数匹配 */
        if (pduLen < (uint16_t)(2 + bytecnt))
            return -3;  /* 数据长度不足 */
        
        /* 格式验证通过
         * 主站模式: 事务层会提取pdu[2..N+1]解析为uint16_t数组
         * 例: modbus_read.c中的prv_on_frame()函数
         */
        return 0;
    }

    /* ------------------ 写单个线圈/寄存器(0x05/0x06) ------------------ */
    case MB_FC_WRITE_SINGLE_COIL:  /* 0x05: 写单个线圈 */
    case MB_FC_WRITE_SINGLE_REG:   /* 0x06: 写单个寄存器 */
    {
        /* 响应PDU格式: [功能码(1)] [地址(2)] [值(2)]
         * 
         * 响应策略: 回显请求内容(Echo Request)
         *   从站收到写请求后,成功执行则返回相同的地址和值
         *   主站通过比对响应与请求是否一致来确认写入成功
         * 
         * 字段说明:
         *   - pdu[0] = 功能码(0x05或0x06)
         *   - pdu[1:2] = 地址(大端序)
         *   - pdu[3:4] = 值(大端序)
         * 
         * 线圈值编码(0x05):
         *   - 0xFF00 = ON(线圈置位)
         *   - 0x0000 = OFF(线圈复位)
         * 
         * 寄存器值编码(0x06):
         *   - 16位无符号整数(0x0000~0xFFFF)
         */
        if (pduLen < 5)  /* PDU总长度 = 功能码(1) + 地址(2) + 值(2) */
            return -3;   /* PDU格式错误:长度不足 */
        
        /* 格式验证通过
         * 主站模式: 事务层会比对响应与请求的地址/值是否一致
         * 从站模式: 本层只验证格式,写入操作由应用层回调完成
         */
        return 0;
    }

    /* ------------------ 写多个线圈/寄存器(0x0F/0x10) ------------------ */
    case MB_FC_WRITE_MULTIPLE_COILS:  /* 0x0F(15): 写多个线圈 */
    case MB_FC_WRITE_MULTIPLE_REGS:   /* 0x10(16): 写多个寄存器 */
    {
        /* 响应PDU格式: [功能码(1)] [起始地址(2)] [数量(2)]
         * 
         * 响应策略: 回显起始地址和数量(不回显数据内容)
         *   从站收到批量写请求后,成功执行则返回起始地址和数量
         *   主站通过比对响应与请求的地址/数量是否一致来确认写入成功
         * 
         * 字段说明:
         *   - pdu[0] = 功能码(0x0F或0x10)
         *   - pdu[1:2] = 起始地址(大端序)
         *   - pdu[3:4] = 数量(大端序)
         *     * 0x0F: 写入的线圈数量(1~1968)
         *     * 0x10: 写入的寄存器数量(1~123)
         * 
         * 注: 响应不包含请求中的"字节数"和"数据"字段
         */
        if (pduLen < 5)  /* PDU总长度 = 功能码(1) + 地址(2) + 数量(2) */
            return -3;   /* PDU格式错误:长度不足 */
        
        /* 格式验证通过
         * 主站模式: 事务层会比对响应的地址/数量与请求是否一致
         */
        return 0;
    }

    /* ------------------ 读写多个寄存器(0x17) ------------------ */
    case MB_FC_READWRITE_MULTIPLE_REGS:  /* 0x17(23): 读写多个寄存器 */
    {
        /* 响应PDU格式: [功能码(1)] [字节数(1)] [读取的寄存器数据(N)]
         * 
         * 功能说明: 单次事务中同时读写不同地址的寄存器
         *   - 写操作先执行(写入指定地址的寄存器)
         *   - 读操作后执行(读取另一组寄存器)
         *   - 响应只包含读取的数据,不包含写入确认
         * 
         * 字段说明:
         *   - pdu[0] = 功能码(0x17)
         *   - pdu[1] = 字节数N(读取的寄存器数量 × 2)
         *   - pdu[2..N+1] = 读取的寄存器值(大端序)
         * 
         * 用途: 原子性读写操作,常用于工业控制场景
         */
        if (pduLen < 2)  /* 最小长度:功能码+字节数 */
            return -3;   /* PDU格式错误 */
        
        uint8_t bytecnt = pdu[1];  /* 提取字节数字段 */
        
        /* 验证实际数据长度是否与字节数匹配 */
        if (pduLen < (uint16_t)(2 + bytecnt))
            return -3;  /* 数据长度不足 */
        
        /* 格式验证通过 */
        return 0;
    }

    /* ------------------ 不支持的功能码 ------------------ */
    default:
        /* 功能码不在支持列表中
         * 
         * 处理策略:
         *   - 设置异常码为MB_EX_ILLEGAL_FUNCTION (0x01)
         *   - 返回-4表示不支持的功能码
         * 
         * 扩展方法:
         *   如需支持更多功能码(如0x07诊断,0x16掩码写等),
         *   在switch中添加对应case分支即可
         */
        if (pException)
            *pException = MB_EX_ILLEGAL_FUNCTION;  /* 非法功能码 */
        return -4;  /* 不支持的功能码 */
    }
}
