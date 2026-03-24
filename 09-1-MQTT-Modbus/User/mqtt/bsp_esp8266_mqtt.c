#include "./mqtt/bsp_esp8266_mqtt.h"
#include "./mqtt/bsp_esp8266.h"
#include "./systick/bsp_systick.h"
#include "./pwm/bsp_pwm.h"  // 控制实际 LED 亮/灭
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// OneNET 接入参数
#define ONENET_PRODUCT_ID "uCq21dfshX"
#define ONENET_DEVICE_NAME "Test1"
#define ONENET_TOKEN "version=2018-10-31&res=products%2FuCq21dfshX%2Fdevices%2FTest1&et=1856188586&method=md5&sign=4OVLB4PaNhWs8uOeXo9v%2BQ%3D%3D"

// Broker 与端口
#define ONENET_BROKER "uCq21dfshX.mqtts.acc.cmcconenet.cn"
#define ONENET_PORT 1883

int led_value = 0;     // led开关值
uint8_t mqtt_flag = 0; // mqtt连接标志

// 发送原始二进制的辅助函数
extern bool ESP8266_CIPSEND_RAW(const uint8_t *buf, uint16_t len);

// 访问串口接收缓冲
extern struct STRUCT_USARTx_Fram strEsp8266_Fram_Record;

/*
 * 为降低栈占用,将 MQTT 构包过程中使用的局部数组改为全局静态数组
 * 不可在中断中并行读写。
 */
// 最终要发送的 MQTT 帧拼装缓冲(CONNECT/SUBSCRIBE/PUBLISH 均复用)
static uint8_t g_mqtt_frame_buffer[768];

// 临时可变头部缓冲(如协议名/级别/flags/keepalive,或 PUBLISH 的主题长度等)
static uint8_t g_mqtt_var_header_buffer[128];

// 临时负载缓冲(如 ClientId/Username/Password 或 SUBSCRIBE 的主题+QoS)
static uint8_t g_mqtt_payload_buffer[384];

/**
 * @brief  将无符号整数按 MQTT 可变字节整数(Variable Byte Integer)规则编码到缓冲区
 *
 * @param buf 输出缓冲区指针,写入后的字节序列即为剩余长度字段的编码结果
 * @param x   需要编码的无符号整数(典型用于 MQTT “剩余长度”字段)
 * @return uint32_t 实际写入的字节数(1~4 字节)。若超过 4 次循环会被截断。
 */
static uint32_t mqtt_varint_enc(uint8_t *buf, uint32_t x)
{
    uint32_t n = 0;
    do
    {
        uint8_t byte = x % 128;
        x /= 128;
        if (x > 0)
            byte |= 0x80;
        buf[n++] = byte;
    } while (x > 0 && n < 4);
    return n;
}

/**
 * @brief   按 MQTT 协议写入字符串: 先写入 2 字节长度,再写入字符串本体
 *
 * @param p 写入起始位置指针(调用前由调用者保证空间充足)
 * @param s 以 '\0' 结尾的 C 字符串(不写入末尾的 '\0')
 * @return uint8_t* 写入完成后的下一个可写入位置(p+2+strlen(s))
 */
static uint8_t *mqtt_write_str(uint8_t *p, const char *s)
{
    uint16_t len = (uint16_t)strlen(s);
    *p++ = (uint8_t)(len >> 8);
    *p++ = (uint8_t)(len & 0xFF);
    memcpy(p, s, len);
    return p + len;
}

/**
 * @brief           组装一帧 MQTT CONNECT 报文
 *
 * @param out       输出缓冲区首地址
 * @param cap       输出缓冲区容量(字节数)
 * @param clientId  ClientId(必填)
 * @param username  用户名(可为空,为空则不写入 Username 字段)
 * @param password  密码(可为空,为空则不写入 Password 字段)
 * @param keepalive 客户端与服务器之间允许的最大时间间隔(单位: 秒)
 * @return int      生成的报文总长度(>0)；若空间不足或参数非法返回 -1
 */
static int build_mqtt_connect(uint8_t *out, size_t cap,
                              const char *clientId, const char *username, const char *password,
                              uint16_t keepalive)
{
    uint8_t *p = out; // 指向输出缓冲区的当前写入位置
    uint8_t *q = g_mqtt_var_header_buffer;  // 使用全局缓冲存储可变头部内容

    q = mqtt_write_str(q, "MQTT"); // 写入协议名 "MQTT"
    *q++ = 4;                      // 写入协议级别 4
    uint8_t flags = 0;  // 初始化连接标志位
    flags |= 0x02; // 设置 Clean Session 标志位,表示清除会话
    
    // 提供了用户名,则设置用户名标志位
    if (username && *username)
        flags |= 0x80;
    // 提供了密码,则设置密码标志位
    if (password && *password)
        flags |= 0x40;
    *q++ = flags; // 写入连接标志位
    *q++ = (uint8_t)(keepalive >> 8); // 写入 KeepAlive 高字节
    *q++ = (uint8_t)(keepalive & 0xFF); // 写入 KeepAlive 低字节
    uint32_t vh_len = (uint32_t)(q - g_mqtt_var_header_buffer); // 计算可变头部长度

    // 负载: ClientId、Username、Password
    uint8_t *r = g_mqtt_payload_buffer;     // 使用全局缓冲存储负载内容
    r = mqtt_write_str(r, clientId); // 写入 ClientId
    if (flags & 0x80) // 如果设置了用户名标志位,写入用户名
        r = mqtt_write_str(r, username);
    if (flags & 0x40) // 如果设置了密码标志位,写入密码
        r = mqtt_write_str(r, password);
    uint32_t pl_len = (uint32_t)(r - g_mqtt_payload_buffer); // 计算负载长度
    
    uint32_t rem_len = vh_len + pl_len; // 剩余长度 = 可变头部长度 + 负载长度
    uint8_t rem[4];
    uint32_t rem_n = mqtt_varint_enc(rem, rem_len); // 编码剩余长度为 MQTT 可变字节整数

    // 检查缓冲区是否足够容纳整个报文
    if (1 + rem_n + rem_len > cap)
        return -1;

    *p++ = 0x10; // 写入固定头部的报文类型 CONNECT (0x10)
    memcpy(p, rem, rem_n); // 写入剩余长度字段
    p += rem_n;
    memcpy(p, g_mqtt_var_header_buffer, vh_len); // 写入可变头部内容
    p += vh_len;
    memcpy(p, g_mqtt_payload_buffer, pl_len); // 写入负载内容
    p += pl_len;
    return (int)(p - out); // 返回生成的报文总长度
}

/**
 * @brief       组装一帧 MQTT SUBSCRIBE 报文
 *
 * @param out   输出缓冲区首地址
 * @param cap   输出缓冲区容量(字节数)
 * @param pktId 报文标识符(Packet Identifier),建议自增且非 0
 * @param topic 要订阅的主题字符串
 * @return int  生成的报文总长度(>0)；若空间不足或参数非法返回 -1
 */
static int build_mqtt_subscribe(uint8_t *out, size_t cap, uint16_t pktId, const char *topic)
{
    uint8_t *p = out; // 指向输出缓冲区的当前写入位置
    uint8_t vh[2] = {(uint8_t)(pktId >> 8), (uint8_t)(pktId & 0xFF)}; // 构造报文标识符(Packet Identifier),占 2 字节

    // 复用全局负载缓冲存放“主题+QoS”
    uint8_t *r = g_mqtt_payload_buffer; // 使用全局缓冲存储负载内容
    r = mqtt_write_str(r, topic); // 写入主题字符串
    *r++ = 0x00; // 写入 QoS 0(服务质量等级 0)

    uint32_t vh_len = 2; // 可变头部长度固定为 2 字节(Packet Identifier)
    uint32_t pl_len = (uint32_t)(r - g_mqtt_payload_buffer); // 计算负载长度
    uint32_t rem_len = vh_len + pl_len; // 剩余长度 = 可变头部长度 + 负载长度

    uint8_t rem[4];
    uint32_t rem_n = mqtt_varint_enc(rem, rem_len); // 编码剩余长度为 MQTT 可变字节整数

    // 检查缓冲区是否足够容纳整个报文
    if (1 + rem_n + rem_len > cap)
        return -1;

    *p++ = 0x82; // 写入固定头部的报文类型 SUBSCRIBE (0x82)
    memcpy(p, rem, rem_n); // 写入剩余长度字段
    p += rem_n;
    memcpy(p, vh, vh_len); // 写入可变头部内容(Packet Identifier)
    p += vh_len;
    memcpy(p, g_mqtt_payload_buffer, pl_len); // 写入负载内容(主题+QoS)
    p += pl_len;

    return (int)(p - out); // 返回生成的报文总长度
}

/**
 * @brief  组装一帧 MQTT PUBLISH 报文
 *
 * @param out     输出缓冲区首地址
 * @param cap     输出缓冲区容量(字节数)
 * @param topic   主题字符串
 * @param payload 文本载荷(以 '\0' 结尾,按 strlen 计算长度)
 * @return int 生成的报文总长度(>0)；若空间不足或参数非法返回 -1
 */
static int build_mqtt_publish(uint8_t *out, size_t cap, const char *topic, const char *payload)
{
    uint8_t *p = out;
    // 复用全局可变头部缓冲存放“主题长度+主题名”
    uint8_t *q = g_mqtt_var_header_buffer;
    q = mqtt_write_str(q, topic);
    uint32_t vh_len = (uint32_t)(q - g_mqtt_var_header_buffer);
    uint32_t pl_len = (uint32_t)strlen(payload);
    uint32_t rem_len = vh_len + pl_len;
    uint8_t rem[4];
    uint32_t rem_n = mqtt_varint_enc(rem, rem_len);

    if (1 + rem_n + rem_len > cap)
        return -1;

    *p++ = 0x30; // PUBLISH QoS0
    memcpy(p, rem, rem_n);
    p += rem_n;
    memcpy(p, g_mqtt_var_header_buffer, vh_len);
    p += vh_len;
    memcpy(p, payload, pl_len);
    p += pl_len;
    return (int)(p - out);
}

/**
 * @brief  解析 CONNACK 报文,获取连接返回码
 *
 * @param buf CONNACK 报文字节流
 * @param len 报文长度(字节)
 * @return int 返回码 rc(0 表示成功)；负数表示解析错误或报文不匹配
 */
static int parse_mqtt_connack(const uint8_t *buf, uint16_t len)
{
    if (len < 4)
        return -1;
    if (buf[0] != 0x20)
        return -2; // not CONNACK
    // buf[1] 剩余长度通常 0x02
    if (len < 4)
        return -3;
    uint8_t rc = buf[3];
    return rc; // 0 = success
}

/**
 * @brief  在 ESP8266 的串口接收缓存中查找 +IPD 数据段,提取其有效数据指针与长度
 *
 * @param payload 输出: 指向 +IPD 数据起始处的指针(不含 "+IPD,<len>:" 头部)
 * @param payload_len 输出: 数据长度(字节)
 * @return int 0 表示成功；负数表示未找到或格式错误
 */
static int extract_ipd_payload(uint8_t **payload, uint16_t *payload_len)
{
    // 查找 "+IPD," 格式: +IPD,<len>:<data>
    char *p = strstr(strEsp8266_Fram_Record.Data_RX_BUF, "+IPD,");
    if (!p)
        return -1;
    p += 5; // 跳过 +IPD,
    int len = 0;
    while (*p >= '0' && *p <= '9')
    {
        len = len * 10 + (*p - '0');
        p++;
    }
    if (*p != ':')
        return -2;
    p++; // 到达数据起始
    *payload = (uint8_t *)p;
    *payload_len = (uint16_t)len;
    return 0;
}

/**
 * @brief  从一帧完整 MQTT 报文字节流中提取 PUBLISH 的负载指针与长度(QoS0)
 *
 * @param mq     输入: MQTT 报文首地址
 * @param mq_len 输入: MQTT 报文总长度(字节)
 * @param pl     输出: 指向载荷起始位置的指针
 * @param pl_len 输出: 载荷长度(字节)
 * @return int 0 表示成功；负数表示非 PUBLISH 或报文格式/长度非法
 */
static int extract_mqtt_publish_payload(const uint8_t *mq, uint16_t mq_len, const uint8_t **pl, uint16_t *pl_len)
{
    if (mq_len < 5)
        return -1;
    uint8_t hdr = mq[0];
    if ((hdr >> 4) != 0x03)
        return -2; // 非 PUBLISH
    // 解析剩余长度
    uint32_t mul = 1;
    uint32_t rem = 0;
    uint32_t idx = 1;
    uint8_t byte;
    do
    {
        if (idx >= mq_len)
            return -3;
        byte = mq[idx++];
        rem += (byte & 0x7F) * mul;
        mul *= 128;
        if (mul > (128 * 128 * 128 * 128))
            return -4;
    } while (byte & 0x80);
    uint32_t after_var = idx; // 记录“可变头部开始处”位置,用于计算负载长度
    if (idx + rem > mq_len)
        return -5;
    // 主题
    if (idx + 2 > mq_len)
        return -6;
    uint16_t topic_len = ((uint16_t)mq[idx] << 8) | mq[idx + 1];
    idx += 2;
    if (idx + topic_len > mq_len)
        return -7;
    idx += topic_len;
    // QoS0,无 packet id
    if (idx > mq_len)
        return -8;
    *pl = mq + idx;
    // 负载长度 = 剩余长度 rem - (可变头部已消耗字节数)
    // 对于 QoS0,可变头部消耗为 (2 + topic_len)
    uint32_t vh_consumed = idx - after_var;
    if (rem < vh_consumed)
        return -9;
    *pl_len = (uint16_t)(rem - vh_consumed);
    if (idx + *pl_len > mq_len)
        return -9;
    return 0;
}

/**
 * @brief  通过 TCP 直连 OneNET Broker,发送 CONNECT 并等待 CONNACK,然后订阅 Topic
 *
 * @return true 连接和订阅成功
 * @return false 失败(TCP 连接、CONNECT 发送、或 CONNACK/订阅异常)
 */
static bool mqtt_tcp_connect_and_subscribe(void)
{
    // TCP 连接
    char port_str[8];
    sprintf(port_str, "%d", ONENET_PORT);
    if (!ESP8266_Link_Server(enumTCP, (char *)ONENET_BROKER, port_str, (ENUM_ID_NO_TypeDef)5))
    {
        // 关键日志: TCP 连接失败
        printf("[MQTT][ERROR] TCP 连接失败\r\n");
        return false;
    }

    // 发送 CONNECT
    int n = build_mqtt_connect(g_mqtt_frame_buffer, sizeof(g_mqtt_frame_buffer), ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_TOKEN, 120);
    if (n <= 0 || !ESP8266_CIPSEND_RAW(g_mqtt_frame_buffer, (uint16_t)n))
    {
        // 关键日志: CONNECT 发送失败
        printf("[MQTT][ERROR] CONNECT 发送失败\r\n");
        return false;
    }

    // 等待 CONNACK
    uint32_t wait = 0;
    int conn_ok = 0;
    while (wait < 2000)
    {
        DelayMs(100);
        wait += 100;
        uint8_t *ipd = NULL;
        uint16_t ipd_len = 0;
        if (extract_ipd_payload(&ipd, &ipd_len) == 0)
        {
            if (ipd_len >= 4 && ipd[0] == 0x20)
            {
                int rc = parse_mqtt_connack(ipd, ipd_len);
                if (rc == 0)
                {
                    conn_ok = 1;
                    break;
                }
                else
                {
                    printf("[MQTT] CONNACK rc=%d\r\n", rc);
                    return false;
                }
            }
        }
    }
    if (!conn_ok)
    {
        printf("[MQTT][ERROR] 未收到 CONNACK\r\n");
        return false;
    }

    // 订阅 post/reply 主题(用于接收平台对上报的应答, 携带 code/msg)
    char topic_reply[160];
    sprintf(topic_reply, "$sys/%s/%s/thing/property/post/reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    n = build_mqtt_subscribe(g_mqtt_frame_buffer, sizeof(g_mqtt_frame_buffer), 1, topic_reply);
    if (n <= 0 || !ESP8266_CIPSEND_RAW(g_mqtt_frame_buffer, (uint16_t)n))
    {
        printf("[MQTT] 订阅 post/reply 失败\r\n");
        return false;
    }

    // 等待 SUBACK 以确认订阅成功
    {
        uint32_t swait = 0; int sub_ok = 0;
        while (swait < 1000)
        {
            DelayMs(100); swait += 100;
            uint8_t *ipd = NULL; uint16_t ipd_len = 0;
            if (extract_ipd_payload(&ipd, &ipd_len) == 0)
            {
                if (ipd_len >= 5 && ipd[0] == 0x90) // SUBACK
                {
                    // ipd[2..3] packet id, ipd[4] return code
                    uint8_t rc = ipd[ipd_len-1];
                    printf("[MQTT] SUBACK(post/reply) rc=%d\r\n", rc);
                    sub_ok = 1; break;
                }
            }
        }
        if (!sub_ok) { /* 静默,订阅失败则交由上层整体处理,减少打印不必要的日志 */ }
    }

    // 订阅属性设置主题: $sys/{pid}/{dev}/thing/property/set
    {
        char topic_set[160];
        sprintf(topic_set, "$sys/%s/%s/thing/property/set", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
        int n2 = build_mqtt_subscribe(g_mqtt_frame_buffer, sizeof(g_mqtt_frame_buffer), 2, topic_set);
        if (n2 <= 0 || !ESP8266_CIPSEND_RAW(g_mqtt_frame_buffer, (uint16_t)n2))
        {
            printf("[MQTT] 订阅 property/set 失败\r\n");
            return false;
        }
        // 等待 SUBACK
        uint32_t swait2 = 0; int sub_ok2 = 0;
        while (swait2 < 1000)
        {
            DelayMs(100); swait2 += 100;
            uint8_t *ipd = NULL; uint16_t ipd_len = 0;
            if (extract_ipd_payload(&ipd, &ipd_len) == 0)
            {
                if (ipd_len >= 5 && ipd[0] == 0x90)
                {
                    uint8_t rc = ipd[ipd_len-1];
                    printf("[MQTT] SUBACK(property/set) rc=%d\r\n", rc);
                    sub_ok2 = 1; break;
                }
            }
        }
    if (!sub_ok2) { /* 静默,订阅失败则交由上层整体处理,减少打印不必要的日志 */ }
    }
    return true;
}

/**
 * @brief  外部 API: 使用 TCP 直连并执行最小订阅(post)
 *
 * @return true 执行成功
 * @return false 执行失败
 */
bool ESP8266_MQTT_TCP_CONNECT_AND_SUB(void)
{
    return mqtt_tcp_connect_and_subscribe();
}

/**
 * @brief 设备属性上报(post)——“发送即返回”,不阻塞等待 ACK
 * - 构造 OneNET 物模型属性上报 JSON: Temp/Hum/Light/MQ2
 * - Temp/Hum 可以测出浮点值, 但物模型里定义的是整数, 所以只能上报整数部分
 * - Light 为光敏电阻 ADC 原始值
 * - MQ2 为烟雾浓度值(ppm),范围50~10000
 * - 注意:光敏电阻 ADC 值 ≠ 光强 lux 值,需按实际传感器特性换算
 * - 傻逼厂家没有提供换算公式,闹麻了,调个阈值凑合用吧
 * 
 * - PUBLISH 到 $sys/{product}/{device}/thing/property/post
 * - 不在此函数内等待 post/reply；所有 ACK 与下行 set 由 ESP8266_MQTT_POLL_RECEIVE() 处理
 * 
 * - 笑死我了,原本MQ2阈值为50-10000,结果实验室环境太好了,测出低值了
 * - 下次拿到厕所测试(
 * - 现在的范围为1-10000
 * 
 * @param temp_set    温度整数（从站2寄存器40011）
 * @param humi_set    湿度整数（从站2寄存器40012）
 * @param slave_light 从站1光敏电阻ADC值（寄存器40001）
 * @param mq2_ppm     从站3 MQ2烟雾浓度值(ppm,寄存器40021）
 * @param alarm_level 报警等级（0=安全运行,1=警报,2=严重警报）
 * @return true       报文已成功构造并通过 ESP8266 发送
 * @return false      发送失败或构造失败(缓冲不足等)
 */
bool ESP8266_MQTT_PUB(uint8_t temp_set, uint8_t humi_set, uint16_t slave_light, uint16_t mq2_ppm, uint8_t alarm_level)
{
    // 构造明文 JSON
    char topic_post[160];
    char payload[256];
    sprintf(topic_post, "$sys/%s/%s/thing/property/post", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    uint16_t light = slave_light; // 上报从机光敏电阻的 ADC 值
    snprintf(payload, sizeof(payload),
             "{\"id\":\"123\",\"params\":{"
             "\"Temp\":{\"value\":%d},"
             "\"Hum\":{\"value\":%d},"
             "\"Light\":{\"value\":%u},"
             "\"MQ2\":{\"value\":%u},"
             "\"Error\":{\"value\":%u}"
             "}}",
             temp_set, humi_set, (unsigned)light, (unsigned)mq2_ppm, (unsigned)alarm_level);
    // 仅负责"发送",不阻塞等待 ACK；ACK 与下行 set 均由 ESP8266_MQTT_POLL_RECEIVE() 处理
    int n = build_mqtt_publish(g_mqtt_frame_buffer, sizeof(g_mqtt_frame_buffer), topic_post, payload);
    if (n <= 0) {
        printf("[MQTT][ERROR] 构造PUBLISH报文失败（缓冲不足或参数非法）\r\n");
        return false;
    }
    // 打印日志(仅针对上报 PUBLISH)
    printf("[MQTT] AT+CIPSEND=%u\r\n", (unsigned)n);
    if (!ESP8266_CIPSEND_RAW(g_mqtt_frame_buffer, (uint16_t)n)) {
        printf("[MQTT][ERROR] 发送PUBLISH报文失败（ESP8266未响应或超时）\r\n");
        return false;
    }

    return true;
}

/**
 * @brief 处理网络下行 MQTT PUBLISH(post/reply 与 property/set)
 * - 检查并解析串口接收缓冲中的 +IPD 数据(格式 +IPD,len:<data>)
 * 
 * - 若载荷是 MQTT PUBLISH: 
 *  - 主题 $sys/.../thing/property/post/reply: 打印平台 ACK 文本
 *  - 主题 $sys/.../thing/property/set:        解析 JSON,控制硬件 LED
 *  - 向 $sys/.../thing/property/set_reply     发布应答: {"id":"..","code":0,"msg":"success"}
 * 
 * - 解析参数时,Brightness 优先生效,若存在则作为亮度百分比并同步影响 led_value
 * - 'いいご身分ですわね' 还真是高高在上呢
 */
void ESP8266_MQTT_POLL_RECEIVE(void)
{
    // 从串口缓冲中提取一帧 +IPD 数据载荷(格式: +IPD,<len>:<data>)
    uint8_t *ipd = NULL; uint16_t ipd_len = 0;
    if (extract_ipd_payload(&ipd, &ipd_len) != 0)
        return; // 无新帧或格式不匹配

    // MQTT 最小 PUBLISH 帧一般不少于 5 字节(固定头 1B + 剩余长度 1~4B + 主题长度至少 2B)
    if (ipd_len < 5)
        return;

    // 仅处理 PUBLISH 报文: 固定头高 4 bit 为 0x3
    if ((ipd[0] >> 4) != 0x03)
        return;

    // 解析“剩余长度”字段(MQTT 可变字节整数 Variable Byte Integer)
    uint32_t mul = 1, rem = 0; uint32_t idx = 1; uint8_t byte;
    do {
        if (idx >= ipd_len) return;
        byte = ipd[idx++];
        rem += (byte & 0x7F) * mul; mul *= 128;
    } while (byte & 0x80);
    uint32_t after_var = idx; // 记录“可变头部开始处”位置
    if (idx + 2 > ipd_len) return;
    uint16_t tlen = ((uint16_t)ipd[idx] << 8) | ipd[idx + 1];
    idx += 2;
    if (idx + tlen > ipd_len) return; // 主题长度非法

    // 将主题复制到本地缓冲并确保结尾 0 终止,防止越界
    char topic[192];
    uint16_t copy = tlen < sizeof(topic) - 1 ? tlen : (sizeof(topic) - 1);
    memcpy(topic, &ipd[idx], copy); topic[copy] = '\0';
    idx += tlen; // QoS0 情况下无 Packet Identifier,idx 此时指向负载起始

    // 计算负载长度: rem(剩余长度)减去可变头部已消耗的字节数
    uint32_t vh_consumed = idx - after_var; // 此时 idx 指向负载起始
    if (rem < vh_consumed) return;          // 防溢出/畸形帧
    uint16_t pl_len = (uint16_t)(rem - vh_consumed);
    if (idx + pl_len > ipd_len) return;     // 越界保护

    // 组装常用的系统主题字符串以便快速比对
    char topic_reply[160];                  // 上报应答: post/reply
    char topic_set[160];                    // 属性设置: property/set
    char topic_set_reply[160];              // 属性设置应答: set_reply
    sprintf(topic_reply, "$sys/%s/%s/thing/property/post/reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    sprintf(topic_set, "$sys/%s/%s/thing/property/set", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    sprintf(topic_set_reply, "$sys/%s/%s/thing/property/set_reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);

    // 处理 post/reply: 打印 ACK 文本并“消费”该帧,避免重复处理
    if (strcmp(topic, topic_reply) == 0)
    {
        char ack[256];
        uint16_t alen = pl_len < sizeof(ack) - 1 ? pl_len : (sizeof(ack) - 1);
        memcpy(ack, &ipd[idx], alen); ack[alen] = '\0';
        printf("[OneNET][ACK] %s\r\n", ack);
        printf("\r\n", ack);
        // 消费本帧,清空缓冲标记,防止下次再次解析到同一帧
        strEsp8266_Fram_Record.InfBit.FramLength = 0;
        strEsp8266_Fram_Record.Data_RX_BUF[0] = '\0';
        strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        return;
    }

    // 处理 property/set: 解析 JSON,优先 Brightness,其次 Led,并回复 set_reply
    if (strcmp(topic, topic_set) == 0)
    {
        // 将负载复制为以 0 结尾的字符串,便于 strstr/strchr 等 C 字符串处理
        char msg[256];
        uint16_t mlen = pl_len < sizeof(msg) - 1 ? pl_len : (sizeof(msg) - 1);
        memcpy(msg, &ipd[idx], mlen); msg[mlen] = '\0';

        // 提取 request id(兼容 id 或 request_id,字符串或数字)
        char req_id[48] = {0};
        const char *keys[2] = {"\"id\"", "\"request_id\""};
        const char *found = NULL;
        for (int ki = 0; ki < 2 && !found; ++ki)
        {
            const char *pkey = strstr(msg, keys[ki]);
            if (pkey) found = pkey;
        }
        if (found)
        {
            const char *pcolon = strchr(found, ':');
            if (pcolon)
            {
                const char *p = pcolon + 1;
                while (*p == ' ' || *p == '\t') p++; // 跳过空白
                if (*p == '"')                       // 字符串 id
                {
                    const char *pend = strchr(++p, '"');
                    if (pend && (size_t)(pend - p) < sizeof(req_id))
                    {
                        memcpy(req_id, p, (size_t)(pend - p));
                        req_id[pend - p] = '\0';
                    }
                }
                else                                  // 数字 id: 复制到逗号或右花括号
                {
                    const char *pend = p;
                    while (*pend && *pend != ',' && *pend != '}' && (size_t)(pend - p) < sizeof(req_id) - 1) pend++;
                    size_t n = (size_t)(pend - p);
                    memcpy(req_id, p, n);
                    req_id[n] = '\0';
                }
            }
        }

        // 解析 params.Brightness 与 params.Led(两种格式: 直接值或 {"value":x})
        int new_led = -1;       // Led: true/false/1/0 -> 1/0,-1 表示未出现
        int has_brightness = 0; // Brightness 是否存在
        int brightness_val = -1;// Brightness 的 0..100 数值

        // 解析 Brightness, 支持 "Brightness":NN 或 {"Brightness":{"value":NN}}
        do {
            char *pp = strstr(msg, "\"params\""); // 尽量从 params 子树开始查找
            if (!pp) { pp = msg; }
            char *pb = strstr(pp, "\"Brightness\"");
            if (!pb) break;
            char *pcolon = strchr(pb, ':');
            if (!pcolon) break;
            char *p = pcolon + 1;
            while (*p==' '||*p=='\t') p++;
            if (*p=='{') { // 嵌套 {"value":NN}
                char *pv = strstr(p, "\"value\"");
                if (pv) { p = strchr(pv, ':'); if (p) p++; }
                while (*p==' '||*p=='\t') p++;
            }
            if (*p=='"') p++; // 跳过可能的引号(容错: 值被引号包裹)
            long v = 0; int any=0;
            while (*p>='0'&&*p<='9') { v = v*10 + (*p-'0'); p++; any=1; }
            if (any) {
                if (v<0) v=0; if (v>100) v=100;
                brightness_val = (int)v;
                has_brightness = 1;
            }
        } while (0);

        // 解析 Led(支持 "Led":true/false/0/1 或 {"Led":{"value":...}})
        do {
            char *pp = strstr(msg, "\"params\"");
            if (!pp) { pp = msg; }
            char *pld = strstr(pp, "\"Led\"");
            if (!pld) break;
            char *pcolon2 = strchr(pld, ':');
            if (!pcolon2) break;
            char *p = pcolon2 + 1;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '{')
            {
                char *pv = strstr(p, "\"value\"");
                if (pv) { p = strchr(pv, ':'); if (p) p++; }
            }
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "true", 4) == 0) new_led = 1;
            else if (strncmp(p, "false", 5) == 0) new_led = 0;
            else if (*p == '1') new_led = 1;
            else if (*p == '0') new_led = 0;
        } while (0);

        // 应用优先级: Brightness 优先生效,若存在则作为亮度百分比并同步影响 led_value
        if (has_brightness)
        {
            int b = brightness_val;
            if (b < 0) b = 0; if (b > 100) b = 100;
            LED_PWM_SetDuty(1, (uint8_t)b);
            LED_PWM_SetDuty(2, (uint8_t)b);
            LED_PWM_SetDuty(3, (uint8_t)b);
            led_value = (b > 0) ? 1 : 0; // 若亮度>0 则视为点亮
            printf("[SET] Brightness=%d\r\n", b);

            // 回复 set_reply: code=0 表示处理成功
            char reply_payload[128];
            const char *rid = (req_id[0] ? req_id : "1");
            snprintf(reply_payload, sizeof(reply_payload),
                     "{\"id\":\"%s\",\"code\":0,\"msg\":\"success\"}", rid);
            int rn = build_mqtt_publish(g_mqtt_frame_buffer, sizeof(g_mqtt_frame_buffer), topic_set_reply, reply_payload);
            if (rn > 0) { ESP8266_CIPSEND_RAW(g_mqtt_frame_buffer, (uint16_t)rn); }
            // 消费本帧,清空缓冲
            strEsp8266_Fram_Record.InfBit.FramLength = 0;
            strEsp8266_Fram_Record.Data_RX_BUF[0] = '\0';
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        }
        else if (new_led != -1) // 仅 Led 生效
        {
            led_value = new_led;
            LED_PWM_SetDuty(1, new_led ? 100 : 0);
            LED_PWM_SetDuty(2, new_led ? 100 : 0);
            LED_PWM_SetDuty(3, new_led ? 100 : 0);
            // 仅提示 LED 控制结果
            printf("[SET] Led=%s\r\n", new_led ? "true" : "false");

            // 回复 set_reply: code=0 表示处理成功
            char reply_payload[128];
            const char *rid = (req_id[0] ? req_id : "1");
            snprintf(reply_payload, sizeof(reply_payload),
                     "{\"id\":\"%s\",\"code\":0,\"msg\":\"success\"}", rid);
            int rn = build_mqtt_publish(g_mqtt_frame_buffer, sizeof(g_mqtt_frame_buffer), topic_set_reply, reply_payload);
            if (rn > 0) { ESP8266_CIPSEND_RAW(g_mqtt_frame_buffer, (uint16_t)rn); }
            // 消费本帧
            strEsp8266_Fram_Record.InfBit.FramLength = 0;
            strEsp8266_Fram_Record.Data_RX_BUF[0] = '\0';
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        }
        else // 参数无效: 回复错误并消费帧
        {
            char reply_payload[160];
            snprintf(reply_payload, sizeof(reply_payload),
                     "{\"id\":\"%s\",\"code\":-1,\"msg\":\"invalid params\"}",
                     (req_id[0] ? req_id : "1"));
            int rn = build_mqtt_publish(g_mqtt_frame_buffer, sizeof(g_mqtt_frame_buffer), topic_set_reply, reply_payload);
            if (rn > 0) { ESP8266_CIPSEND_RAW(g_mqtt_frame_buffer, (uint16_t)rn); }
            // 消费本帧
            strEsp8266_Fram_Record.InfBit.FramLength = 0;
            strEsp8266_Fram_Record.Data_RX_BUF[0] = '\0';
            strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
        }
    }
}
