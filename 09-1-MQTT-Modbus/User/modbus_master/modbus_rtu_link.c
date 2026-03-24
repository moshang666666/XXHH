/**
 * @file modbus_rtu_link.c
 * @brief Modbus RTU 帧收发层实现：
 * - 从字节流构建完整 RTU 帧（地址+PDU+CRC），并做 CRC 校验与帧边界管理；
 * - 对异常帧(溢出/CRC错/超时不完整)计数并丢弃；
 * - 上层只接收校验通过的完整帧；
 * - 提供发送接口：输入 地址+PDU，内部自动追加 CRC 并驱动底层逐字节发送。
 */

#include "modbus_rtu_link.h"
#include "modbus_crc.h"
#include "rs485_driver.h"

/* 缩写说明：
 * - ADU: Application Data Unit，线上帧“地址(1)+PDU+CRC(2)”
 * - PDU: Protocol Data Unit，功能码+数据
 * - T3.5: 3.5 个字符间隔（RTU 帧边界）。在 9600 8E1 下，1 字符≈1.146ms，T3.5≈4.01ms≈80 个 50us tick
 * - RX: Receive（接收），TX: Transmit（发送）
 * - pos: position（当前位置索引），len: length（长度），idx: index（索引）
 */

/* ========================== 内部常量与状态 ========================== */
#define MB_SER_PDU_SIZE_MIN   4u      /* 最小：地址(1)+功能(1)+CRC(2) */
#define MB_SER_PDU_SIZE_MAX   256u    /* 含地址与CRC 的 ADU 最大值 */

typedef enum {
	RX_IDLE = 0,  // 空闲：尚未收到首字节
	RX_RCV,       // 接收中：累计字节到缓冲
	RX_ERROR      // 错误：缓冲溢出，等待 T3.5 丢弃本帧
} rx_state_t;

static volatile rx_state_t s_rx_state = RX_IDLE; // 接收状态机当前状态
static uint8_t  s_rx_buf[MB_RTU_RX_BUF_SIZE];    // 接收缓冲（地址+PDU+CRC）
static uint16_t s_rx_pos = 0;                    // 已写入的字节数（下一个写入位置）

static mb_rtu_rx_stats_t s_stats = {0};          // 统计信息（CRC 错误、溢出、完整帧数量等）
static mb_rtu_frame_cb_t s_frame_ready_cb = 0;   // 完整帧回调：交付“地址+PDU”（不含 CRC）

/* 发送缓冲与指针 */
static uint8_t  s_tx_buf[MB_SER_PDU_SIZE_MAX];   // 发送缓冲：地址+PDU+CRC
static uint16_t s_tx_len = 0;                    // 发送总长度
static uint16_t s_tx_idx = 0;                    // 已发送到的索引（下一个待发字节）

/* ========================== 工具函数 ========================== */


/**
 * @brief 复位接收状态机与计数
 * 
 */
static void prv_rx_reset(void)
{
	s_rx_pos   = 0;
	s_rx_state = RX_IDLE;
}

/**
 * @brief 在 T3.5 到期后尝试完成当前帧：长度与 CRC 校验通过则上报；否则统计错误
 *
 * 说明：对“地址+PDU+CRC”整体计算 CRC，若结果为 0 视为校验通过
 */
static void prv_try_finish_frame(void)
{
	// 条件：至少应有 地址(1)+功能(1)+CRC(2)
	if (s_rx_pos >= MB_SER_PDU_SIZE_MIN) {
		// 对当前缓存的整帧（含 CRC）做 CRC16；正确帧计算结果应为 0
		uint16_t crc = Modbus_CRC16(s_rx_buf, s_rx_pos);
		if (crc == 0) {
			// 去掉末尾 2 字节 CRC，回调上层交付 地址+PDU
			uint16_t payload_len = (uint16_t)(s_rx_pos - 2);
			if (s_frame_ready_cb) {
				s_frame_ready_cb(s_rx_buf, payload_len);
			}
			s_stats.frames_ok++;
		} else {
			s_stats.crc_error++;
		}
	} else {
		s_stats.incomplete++;
	}
	// 无论结果如何，重置接收状态机，等待下一帧
	prv_rx_reset();
}

/* ========================== 对外接口实现 ========================== */

/**
 * @brief RTU 链路层初始化：清空内部状态与缓冲
 * 
 */
void MB_RTU_LinkInit(void)
{
	prv_rx_reset();
}

/**
 * @brief 注册完整帧回调（校验通过后交付“地址+PDU”）
 * 
 * @param cb 回调指针，为 NULL 表示取消回调
 */
void MB_RTU_RegisterFrameReady(mb_rtu_frame_cb_t cb)
{
	s_frame_ready_cb = cb;
}

/**
 * @brief 串口接收中断回调（底层在 RXNE 时调用）：推进接收状态机
 * 
 * @param b 新收到的 1 个字节
 */
void MB_RTU_OnRxByte(uint8_t b)
{
	switch (s_rx_state) {
	case RX_IDLE:
		// 接收到首字节：进入接收状态，并写入缓冲
		s_rx_pos = 0;
		s_rx_buf[s_rx_pos++] = b;
		s_rx_state = RX_RCV;
		// 重启 T3.5（一次性定时），由底层驱动具体定时器
		RS485_TimerStart_T35();
		break;
	case RX_RCV:
		if (s_rx_pos < MB_RTU_RX_BUF_SIZE) {
			s_rx_buf[s_rx_pos++] = b; // 继续累加
		} else {
			s_rx_state = RX_ERROR;    // 溢出：后续丢弃到 T3.5 超时
		}
		RS485_TimerStart_T35();
		break;
	case RX_ERROR:
		// 溢出后继续读走字节但不存储，仅维持 T3.5 计时，等待丢弃
		RS485_TimerStart_T35();
		break;
	}
}

/**
 * @brief T3.5 到期事件（由底层定时器中断触发）：结束当前帧或丢弃
 * 
 */
void MB_RTU_OnT35Expired(void)
{
	// T3.5 到期：一帧结束或错误丢弃
	if (s_rx_state == RX_RCV) {
		// 正常结束，进行 CRC 校验与上报
		prv_try_finish_frame();
	} else if (s_rx_state == RX_ERROR) {
		// 发生过溢出：计数并复位
		s_stats.overflow++;
		prv_rx_reset();
	} else {
		// 空闲态的 T3.5：忽略
	}
}

/**
 * @brief 发送一帧 ADU 的“地址+PDU”（本函数自动计算并追加 CRC）
 * 
 * @param adu 指向“地址+PDU”的缓冲
 * @param len 上述缓冲长度（不含 CRC）
 */
void MB_RTU_Send(const uint8_t *adu, uint16_t len)
{
	if (!adu || len == 0) return;
	if (len + 2u > MB_SER_PDU_SIZE_MAX) return; // 边界保护

	// 复制 地址+PDU，计算 CRC 并以 Modbus RTU 标准字节序（低字节在前，高字节在后）附加
	for (uint16_t i = 0; i < len; i++) s_tx_buf[i] = adu[i];
	uint16_t crc = Modbus_CRC16(adu, len);
	s_tx_buf[len + 0] = (uint8_t)((crc >> 8) & 0xFF);// CRC High（实际先发）
	s_tx_buf[len + 1] = (uint8_t)(crc & 0xFF);       // CRC Low（后发）

	s_tx_len = (uint16_t)(len + 2u);
	s_tx_idx = 0;

	// 切换至发送方向，开启 TXE 中断，由中断函数逐字节写出
	RS485_SetDirectionTx();
	RS485_EnableIRQ(0, 1);
}


/**
 * @brief 串口发送缓冲空中断回调：逐字节发送，结尾时交由底层通过 TC 自动切回接收
 *
 * 说明：当最后一个字节已写入发送数据寄存器后，这里仅关闭 TXE 并开启 RX，
 * 由底层 rs485_driver 在 TXE 关闭的时刻开启 USART IT_TC；当真正发送完成
 *（移位寄存器空，SR.TC=1）时在 TC 中断里拉低 DE 切回接收，避免尾字节畸变。
 */
void MB_RTU_OnTxEmpty(void)
{
	if (s_tx_idx < s_tx_len) {
		(void)RS485_WriteByte(s_tx_buf[s_tx_idx++]); // 写入下一个字节
	} else {
		// 所有字节已写入：关闭 TXE，重新打开接收
		RS485_EnableIRQ(1, 0);
		// 不在此处直接切 DE；等待底层 TC 中断到来后再安全拉低 DE
	}
}


/**
 * @brief 获取接收统计数据
 * 
 * @param out 输出结构体指针
 */
void MB_RTU_GetRxStats(mb_rtu_rx_stats_t *out)
{
	if (!out) return;
	*out = s_stats;
}

