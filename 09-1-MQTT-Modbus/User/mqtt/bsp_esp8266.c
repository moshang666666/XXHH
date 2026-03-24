#include "./mqtt/bsp_esp8266.h"
#include "./systick/bsp_systick.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

/* 封装联网+MQTT上云与一次采集上报循环 */
#include "./mqtt/bsp_esp8266_mqtt.h"           /* ESP8266_MQTT_TCP_CONNECT_AND_SUB / ESP8266_MQTT_PUB / ESP8266_MQTT_POLL_RECEIVE */
#include "./pwm/bsp_pwm.h"                     /* LED_PWM_GetDuty */
#include "./modbus_master/application_data_manager.h" /* 应用数据管理层 */
#include "./alarm/alarm.h"                     /* 报警系统 */

extern int led_value;                    /* 由业务层提供的LED状态 */
extern uint8_t mqtt_flag;                /* MQTT 连接标志（已有定义处）*/


static void ESP8266_GPIO_Config(void);
static void ESP8266_USART_Config(void);
static void ESP8266_USART_NVIC_Configuration(void);

struct STRUCT_USARTx_Fram strEsp8266_Fram_Record = {0};
struct STRUCT_USARTx_Fram strUSART_Fram_Record = {0};

/**
 * @brief ESP8266 串口格式化输出（内部使用，基于标准库 vsnprintf）
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
static void ESP8266_Printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0 || n >= (int)sizeof(buf))
        return;

    for (int i = 0; i < n; i++)
    {
        while (USART_GetFlagStatus(macESP8266_USARTx, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(macESP8266_USARTx, (uint8_t)buf[i]);
    }
    /* 等待最后一个字节发送完成 */
    while (USART_GetFlagStatus(macESP8266_USARTx, USART_FLAG_TC) == RESET)
        ;
}

/**
 * @brief  ESP8266初始化函数
 * @param  无
 * @retval 无
 */
void ESP8266_Init(void)
{
    ESP8266_GPIO_Config();

    ESP8266_USART_Config();

    macESP8266_RST_HIGH_LEVEL();

    macESP8266_CH_DISABLE();
}

/**
 * @brief 初始化ESP8266的GPIO引脚
 *
 */
static void ESP8266_GPIO_Config(void)
{
    /*定义一个GPIO_InitTypeDef类型的结构体*/
    GPIO_InitTypeDef GPIO_InitStructure;

    /* 配置 CH_PD 引脚*/
    macESP8266_CH_PD_AHBxClock_FUN(macESP8266_CH_PD_CLK, ENABLE);
    GPIO_InitStructure.GPIO_Pin = macESP8266_CH_PD_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(macESP8266_CH_PD_PORT, &GPIO_InitStructure);

    /* 配置 RST 引脚*/
    macESP8266_RST_AHBxClock_FUN(macESP8266_RST_CLK, ENABLE);
    GPIO_InitStructure.GPIO_Pin = macESP8266_RST_PIN;
    GPIO_Init(macESP8266_RST_PORT, &GPIO_InitStructure);
}

/**
 * @brief 初始化ESP8266用到的 USART
 *
 */
static void ESP8266_USART_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    macESP8266_USART_GPIO_AHBxClock_FUN(macESP8266_USART_GPIO_CLK, ENABLE);

    /* 使能 UART 时钟 */
    RCC_APB1PeriphClockCmd(macESP8266_USART_CLK, ENABLE);

    /* 连接 PXx 到 USARTx_Tx*/
    GPIO_PinAFConfig(macESP8266_USART_RX_PORT, macESP8266_USARTx_RX_SOURCE, macESP8266_USARTx_RX_AF);

    /*  连接 PXx 到 USARTx__Rx*/
    GPIO_PinAFConfig(macESP8266_USART_TX_PORT, macESP8266_USARTx_TX_SOURCE, macESP8266_USARTx_TX_AF);

    /* 配置Tx引脚为复用功能  */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;

    GPIO_InitStructure.GPIO_Pin = macESP8266_USART_TX_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(macESP8266_USART_TX_PORT, &GPIO_InitStructure);

    /* 配置Rx引脚为复用功能 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Pin = macESP8266_USART_RX_PIN;
    GPIO_Init(macESP8266_USART_RX_PORT, &GPIO_InitStructure);

    /* 配置串macESP8266_USARTx 模式 */
    USART_InitStructure.USART_BaudRate = macESP8266_USART_BAUD_RATE;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(macESP8266_USARTx, &USART_InitStructure);

    /* 中断配置 */
    USART_ITConfig(macESP8266_USARTx, USART_IT_RXNE, ENABLE); // 使能串口接收中断
    USART_ITConfig(macESP8266_USARTx, USART_IT_IDLE, ENABLE); // 使能串口总线空闲中断

    ESP8266_USART_NVIC_Configuration();

    USART_Cmd(macESP8266_USARTx, ENABLE);
}

/**
 * @brief 配置 ESP8266 USART 的 NVIC 中断
 *
 */
static void ESP8266_USART_NVIC_Configuration(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    /* Configure the NVIC Preemption Priority Bits */
    NVIC_PriorityGroupConfig(macNVIC_PriorityGroup_x);

    /* Enable the USART2 Interrupt */
    NVIC_InitStructure.NVIC_IRQChannel = macESP8266_USART_IRQ;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/**
 * @brief 重启ESP8266模块, 被 ESP8266_AT_Test 调用
 *
 */
void ESP8266_Rst(void)
{
#if 0
	 ESP8266_Cmd ( "AT+RST", "OK", "ready", 2500 );

#else
    macESP8266_RST_LOW_LEVEL();
    DelayMs(500);
    macESP8266_RST_HIGH_LEVEL();

#endif
}

/**
 * @brief 完成 ESP8266 初始化、上电、AT 测试、联网、以及 MQTT 连接与订阅
 *
 * @param ssid  WiFi 名称
 * @param pass  WiFi 密码
 */
void ESP8266_WiFiAndMQTT_Startup(const char *ssid, const char *pass)
{
    /* 初始化 ESP8266 GPIO/USART/中断 */
    ESP8266_Init();
    printf("-----------------------------------------\r\n");
    printf("[ESP8266]初始化完成。\r\n");
    macESP8266_CH_ENABLE();
    /* 复位模块,等待 Ready */
    ESP8266_Rst();
    printf("-----------------------------------------\r\n");
    DelayMs(800);

    /* 进行 AT 测试（最多三次提示失败信息，保持与原逻辑一致） */
    {
        int tries = 0;
        while (!ESP8266_AT_Test())
        {
            tries++;
            if (tries >= 3)
            {
                printf("[ESP8266][ERROR] AT 测试失败(多次),请检查接线/波特率/串口中断\r\n");
                break;
            }
            DelayMs(500);
        }
        if (tries < 3)
        {
            printf("[ESP8266] AT 测试通过。\r\n");
        }
    }
    printf("-----------------------------------------\r\n");

    /* 单连接、STA 模式、DHCP */
    while (!ESP8266_Enable_MultipleId(DISABLE))
        ;
    while (!ESP8266_Net_Mode_Choose(STA))
        ;
    while (!ESP8266_DHCP_CUR())
        ;

    /* 连接到指定 WiFi（保持原有重试直到成功的逻辑） */
    while (!ESP8266_JoinAP((char *)ssid, (char *)pass))
    { /* 重试直到成功,不打印中间状态 */
    }
    printf("[ESP8266] WiFi 连接参数: SSID=\"%s\" PASSWORD=\"%s\"\r\n", ssid, pass);
    printf("[ESP8266] WiFi 已连接。\r\n");
    printf("-----------------------------------------\r\n");

    /* 配置并连接 MQTT(OneNET),使用 TCP 直连 + MCU MQTT */
    printf("[ESP8266] 使用 TCP 直连 + MCU MQTT 模式。\r\n");
    if (!ESP8266_MQTT_TCP_CONNECT_AND_SUB())
    {
        printf("[ESP8266][ERROR] TCP 直连 + MQTT 握手失败,请检查 Broker 域名/端口与网络连通性\r\n");
        while (1)
        {
            DelayMs(1000);
        }
    }
    mqtt_flag = 1;
    printf("[ESP8266] 已使用 TCP 直连完成 MQTT 连接/订阅。\r\n");
    printf("-----------------------------------------\r\n");
}

/**
 * @brief 完成一次采集与 MQTT 上报，并在其后执行短轮询以处理下发
 *
 */
void ESP8266_MQTT_CycleOnce(void)
{
    /* 从应用数据层读取从站2的温湿度数据 */
    uint16_t temp_val = 0;
    uint16_t hum_val = 0;
    int temp_ret = app_data_get_value(APP_DATA_TEMP1, &temp_val);
    int hum_ret = app_data_get_value(APP_DATA_HUM1, &hum_val);

    if (temp_ret == 0 && hum_ret == 0)
    {
        /* 从应用数据层读取从站1的光敏ADC值 */
        uint16_t slave_light_adc = 0;
        if (app_data_get_value(APP_DATA_LIGHT_ADC, &slave_light_adc) != 0) {
            /* 若未采集到从站数据，使用默认值0（表示未知） */
            slave_light_adc = 0;
        }

        /* 从应用数据层读取从站3的MQ2烟雾浓度值(ppm) */
        uint16_t mq2_ppm = 0;
        if (app_data_get_value(APP_DATA_MQ2, &mq2_ppm) != 0) {
            /* 若未采集到MQ2数据，使用默认值0（表示未知） */
            mq2_ppm = 0;
        }

        /* 获取当前报警等级 */
        uint8_t alarm_level = (uint8_t)Alarm_GetCurrentLevel();

        /* 上报到 OneNET(物模型: Temp/Hum/Light/MQ2/Error) */
        if (!ESP8266_MQTT_PUB((uint8_t)temp_val, (uint8_t)hum_val, slave_light_adc, mq2_ppm, alarm_level))
        {
            printf("[ESP8266] 上报失败,稍后重试\r\n");
        }
        else
        {
            printf("> [ESP8266] 上报成功: Temp=%u,Hum=%u,Light=%u,MQ2=%u,Error=%u\r\n",
                   (unsigned)temp_val, (unsigned)hum_val,
                   (unsigned)slave_light_adc, (unsigned)mq2_ppm, (unsigned)alarm_level);
        }
    }
    else
    {
        printf("[Modbus] 从站2温湿度数据无效，等待采集\r\n");
    }

    /* 上报周期,拆分为短轮询以便随时处理下发 */
    for (int i = 0; i < 100; ++i) /* 100 * 100ms = 10s */
    {
        ESP8266_MQTT_POLL_RECEIVE();
        Alarm_Process();      /* 处理蜂鸣器周期性响/停 */
        Alarm_LED_Process();  /* 处理LED指示灯呼吸效果 */
        // Alarm_Fan_Process();  /* 处理温度联动风扇控制 */
        DelayMs(100);
    }
}

/**
 * @brief 获取ESP8266的DHCP状态
 *
 * @return true 状态获取成功
 * @return false 状态获取失败
 */
bool ESP8266_DHCP_CUR()
{
    char cCmd[40];

    sprintf(cCmd, "AT+CWDHCP=1,1");

    return ESP8266_Cmd(cCmd, "OK", NULL, 500);
}

/**
 * @brief 对ESP8266模块发送AT指令
 *
 * @param cmd 待发送的指令
 * @param reply1 期待的响应1, 为NULL表示不需响应
 * @param reply2 期待的响应2, 为NULL表示不需响应, 两者为或逻辑关系
 * @param waittime 等待响应的时间
 * @return true 指令发送成功
 * @return false 指令发送失败
 */
bool ESP8266_Cmd(char *cmd, char *reply1, char *reply2, u32 waittime)
{
    strEsp8266_Fram_Record.InfBit.FramLength = 0; // 从新开始接收新的数据包

    ESP8266_Printf("%s\r\n", cmd);

    if ((reply1 == 0) && (reply2 == 0)) // 不需要接收数据
        return true;

    DelayMs(waittime); // 延时

    strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength] = '\0';
    strEsp8266_Fram_Record.InfBit.FramLength = 0; // 清除接收标志
    strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;
    if ((reply1 != 0) && (reply2 != 0))
        return ((bool)strstr(strEsp8266_Fram_Record.Data_RX_BUF, reply1) ||
                (bool)strstr(strEsp8266_Fram_Record.Data_RX_BUF, reply2));

    else if (reply1 != 0)
        return ((bool)strstr(strEsp8266_Fram_Record.Data_RX_BUF, reply1));

    else
        return ((bool)strstr(strEsp8266_Fram_Record.Data_RX_BUF, reply2));
}

/**
 * @brief 对ESP8266模块进行AT测试
 *
 * @return true 测试成功
 * @return false 测试失败
 */
bool ESP8266_AT_Test(void)
{
    char count = 0;
    macESP8266_RST_HIGH_LEVEL();
    DelayMs(2000);
    while (count < 10)
    {
        if (ESP8266_Cmd("AT", "OK", NULL, 500))
        {
            return 1;
        }
        ESP8266_Rst();
        ++count;
    }
    return 0;
}

/**
 * @brief 选择ESP8266模块的工作模式
 *
 * @param enumMode 工作模式
 * @return true 选择成功
 * @return false 选择失败
 */
bool ESP8266_Net_Mode_Choose(ENUM_Net_ModeTypeDef enumMode)
{
    switch (enumMode)
    {
    case STA:
        return ESP8266_Cmd("AT+CWMODE=1", "OK", "no change", 2500);
    case AP:
        return ESP8266_Cmd("AT+CWMODE=2", "OK", "no change", 2500);
    case STA_AP:
        return ESP8266_Cmd("AT+CWMODE=3", "OK", "no change", 2500);
    default:
        return false;
    }
}

/**
 * @brief 连接ESP8266模块到外部WiFi
 *
 * @param pSSID WiFi名称字符串
 * @param pPassWord WiFi密码字符串
 * @return true 连接成功
 * @return false 连接失败
 */
bool ESP8266_JoinAP(char *pSSID, char *pPassWord)
{
    char cCmd[120];
    sprintf(cCmd, "AT+CWJAP=\"%s\",\"%s\"", pSSID, pPassWord);
    return ESP8266_Cmd(cCmd, "OK", NULL, 5000);
}

/**
 * @brief 创建ESP8266模块的WiFi热点
 *
 * @param pSSID WiFi名称字符串
 * @param pPassWord WiFi密码字符串
 * @param enunPsdMode WiFi加密方式代号字符串
 * @return true 创建成功
 * @return false 创建失败
 */
bool ESP8266_BuildAP(char *pSSID, char *pPassWord, ENUM_AP_PsdMode_TypeDef enunPsdMode)
{
    char cCmd[120];
    sprintf(cCmd, "AT+CWSAP=\"%s\",\"%s\",1,%d", pSSID, pPassWord, enunPsdMode);
    return ESP8266_Cmd(cCmd, "OK", 0, 1000);
}

/**
 * @brief ESP8266模块启动多连接
 *
 * @param enumEnUnvarnishTx 配置是否多连接
 * @return true  配置成功
 * @return false 配置失败
 */
bool ESP8266_Enable_MultipleId(FunctionalState enumEnUnvarnishTx)
{
    char cStr[20];
    sprintf(cStr, "AT+CIPMUX=%d", (enumEnUnvarnishTx ? 1 : 0));
    return ESP8266_Cmd(cStr, "OK", 0, 500);
}

/**
 * @brief 连接ESP8266模块到外部服务器
 *
 * @param enumE 网络协议
 * @param ip 服务器IP字符串
 * @param ComNum 服务器端口字符串
 * @param id 模块连接服务器的ID
 * @return true 连接成功
 * @return false 连接失败
 */
bool ESP8266_Link_Server(ENUM_NetPro_TypeDef enumE, char *ip, char *ComNum, ENUM_ID_NO_TypeDef id)
{
    char cStr[100] = {0}, cCmd[120];
    switch (enumE)
    {
    case enumTCP:
        sprintf(cStr, "\"%s\",\"%s\",%s", "TCP", ip, ComNum);
        break;
    case enumUDP:
        sprintf(cStr, "\"%s\",\"%s\",%s", "UDP", ip, ComNum);
        break;
    default:
        break;
    }
    if (id < 5)
        sprintf(cCmd, "AT+CIPSTART=%d,%s", id, cStr);
    else
        sprintf(cCmd, "AT+CIPSTART=%s", cStr);
    return ESP8266_Cmd(cCmd, "OK", "ALREAY CONNECT", 4000);
}

/**
 * @brief ESP8266模块开启或关闭服务器模式
 *
 * @param enumMode 开启/关闭
 * @param pPortNum 服务器端口号字符串
 * @param pTimeOver 服务器超时时间字符串,单位：秒
 * @return true 操作成功
 * @return false 操作失败
 */
bool ESP8266_StartOrShutServer(FunctionalState enumMode, char *pPortNum, char *pTimeOver)
{
    char cCmd1[120], cCmd2[120];
    if (enumMode)
    {
        sprintf(cCmd1, "AT+CIPSERVER=%d,%s", 1, pPortNum);

        sprintf(cCmd2, "AT+CIPSTO=%s", pTimeOver);

        return (ESP8266_Cmd(cCmd1, "OK", 0, 500) &&
                ESP8266_Cmd(cCmd2, "OK", 0, 500));
    }
    else
    {
        sprintf(cCmd1, "AT+CIPSERVER=%d,%s", 0, pPortNum);

        return ESP8266_Cmd(cCmd1, "OK", 0, 500);
    }
}

/**
 * @brief 获取 WF-ESP8266 的连接状态,适合单端口时使用
 *
 * @return uint8_t 2:获得ip | 3:建立连接 | 4:失去连接 | 0:获取状态失败
 */
uint8_t ESP8266_Get_LinkStatus(void)
{
    if (ESP8266_Cmd("AT+CIPSTATUS", "OK", 0, 500))
    {
        if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "STATUS:2\r\n"))
            return 2;

        else if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "STATUS:3\r\n"))
            return 3;

        else if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "STATUS:4\r\n"))
            return 4;
    }
    return 0;
}

/**
 * @brief 获取 ESP8266 的端口(Id)连接状态,较适合多端口时使用
 *
 * @return uint8_t 端口(Id)的连接状态,
 * - 低5位为有效位,分别对应Id0~4,某位若置1表该Id建立了连接,若被清0表该Id未建立连接
 */
uint8_t ESP8266_Get_IdLinkStatus(void)
{
    uint8_t ucIdLinkStatus = 0x00;
    if (ESP8266_Cmd("AT+CIPSTATUS", "OK", 0, 500))
    {
        if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "+CIPSTATUS:0,"))
            ucIdLinkStatus |= 0x01;
        else
            ucIdLinkStatus &= ~0x01;

        if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "+CIPSTATUS:1,"))
            ucIdLinkStatus |= 0x02;
        else
            ucIdLinkStatus &= ~0x02;

        if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "+CIPSTATUS:2,"))
            ucIdLinkStatus |= 0x04;
        else
            ucIdLinkStatus &= ~0x04;

        if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "+CIPSTATUS:3,"))
            ucIdLinkStatus |= 0x08;
        else
            ucIdLinkStatus &= ~0x08;

        if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "+CIPSTATUS:4,"))
            ucIdLinkStatus |= 0x10;
        else
            ucIdLinkStatus &= ~0x10;
    }
    return ucIdLinkStatus;
}

/**
 * @brief 获取 ESP8266 的 AP IP
 *
 * @param pApIp 存放 AP IP 的数组的首地址
 * @param ucArrayLength 存放 AP IP 的数组的长度
 * @return uint8_t 0:获取失败 | 1:获取成功
 */
uint8_t ESP8266_Inquire_ApIp(char *pApIp, uint8_t ucArrayLength)
{
    char uc;
    char *pCh;
    ESP8266_Cmd("AT+CIFSR", "OK", 0, 500);
    pCh = strstr(strEsp8266_Fram_Record.Data_RX_BUF, "APIP,\"");

    if (pCh)
        pCh += 6;
    else
        return 0;
    for (uc = 0; uc < ucArrayLength; uc++)
    {
        pApIp[uc] = *(pCh + uc);

        if (pApIp[uc] == '\"')
        {
            pApIp[uc] = '\0';
            break;
        }
    }
    return 1;
}

/**
 * @brief 配置 ESP8266 模块进入透传发送
 *
 * @return true 配置成功
 * @return false 配置失败
 */
bool ESP8266_UnvarnishSend(void)
{
    if (!ESP8266_Cmd("AT+CIPMODE=1", "OK", 0, 500))
        return false;
    return ESP8266_Cmd("AT+CIPSEND", "OK", ">", 500);
}

/**
 * @brief 配置WF-ESP8266模块退出透传模式
 *
 */
void ESP8266_ExitUnvarnishSend(void)
{
    DelayMs(1000);
    ESP8266_Printf("+++");
    DelayMs(500);
}

/**
 * @brief  直接向 ESP8266 串口发送原始字节流(不追加 \r\n,不做格式化)
 *         注意：调用者需确保对端(ESP8266)处于可接收数据的状态(如已收到 ">" 提示符)
 */
void ESP8266_SendBytesRaw(const uint8_t *buf, uint16_t len)
{
    if (!buf || len == 0)
        return;
    for (uint16_t i = 0; i < len; i++)
    {
        while (USART_GetFlagStatus(macESP8266_USARTx, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(macESP8266_USARTx, buf[i]);
    }
    while (USART_GetFlagStatus(macESP8266_USARTx, USART_FLAG_TC) == RESET)
        ;
}

/**
 * @brief 使用 AT+CIPSEND=Len 发送一段原始二进制数据
 * @param buf 待发送数据
 * @param len 数据长度
 * @return true 发送成功(收到了提示符并完成发送)/ false 发送失败
 */
bool ESP8266_CIPSEND_RAW(const uint8_t *buf, uint16_t len)
{
    char cmd[32];
    if (buf == NULL || len == 0)
        return false;

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)len);
    if (!ESP8266_Cmd(cmd, "> ", 0, 500))
        return false;

    ESP8266_SendBytesRaw(buf, len);

    // 略等一会儿等待 SEND OK 或远端响应进入 +IPD
    DelayMs(50);
    return true;
}

/**
 * @brief ESP8266模块发送字符串
 *
 * @param enumEnUnvarnishTx 声明是否已使能了透传模式
 * @param pStr 要发送的字符串
 * @param ulStrLength 要发送的字符串的字节数
 * @param ucId 发送字符串的ID
 * @return true 发送成功
 * @return false 发送失败
 */
bool ESP8266_SendString(FunctionalState enumEnUnvarnishTx, char *pStr, u32 ulStrLength, ENUM_ID_NO_TypeDef ucId)
{
    char cStr[20];
    bool bRet = false;
    if (enumEnUnvarnishTx)
    {
        ESP8266_Printf("%s", pStr);

        bRet = true;
    }
    else
    {
        if (ucId < 5)
            sprintf(cStr, "AT+CIPSEND=%d,%d", ucId, ulStrLength + 2);
        else
            sprintf(cStr, "AT+CIPSEND=%d", ulStrLength + 2);
        ESP8266_Cmd(cStr, "> ", 0, 100);
        bRet = ESP8266_Cmd(pStr, "SEND OK", 0, 500);
    }

    return bRet;
}

/**
 * @brief ESP8266模块接收字符串
 *
 * @param enumEnUnvarnishTx 声明是否已使能了透传模式
 * @return char* 接收到的字符串首地址
 */
char *ESP8266_ReceiveString(FunctionalState enumEnUnvarnishTx)
{
    char *pRecStr = 0;

    strEsp8266_Fram_Record.InfBit.FramLength = 0;
    strEsp8266_Fram_Record.InfBit.FramFinishFlag = 0;

    while (!strEsp8266_Fram_Record.InfBit.FramFinishFlag)
        ;
    strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength] = '\0';

    if (enumEnUnvarnishTx)
        pRecStr = strEsp8266_Fram_Record.Data_RX_BUF;

    else
    {
        if (strstr(strEsp8266_Fram_Record.Data_RX_BUF, "+IPD"))
            pRecStr = strEsp8266_Fram_Record.Data_RX_BUF;
    }

    return pRecStr;
}

/**
 * @brief USART 中断服务函数(当前为 USART3_IRQHandler)
 *
 * - 在 RXNE 产生时把收到的字节放入 strEsp8266_Fram_Record.Data_RX_BUF,推进 FramLength
 * - 在 IDLE 产生时认为一帧完成,置位 FramFinishFlag,并通过读 SR/DR 清中断
 */
void macESP8266_USART_INT_FUN(void)
{
    /* 接收寄存器非空：读出1字节并缓存 */
    if (USART_GetITStatus(macESP8266_USARTx, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = (uint8_t)USART_ReceiveData(macESP8266_USARTx);
        if (strEsp8266_Fram_Record.InfBit.FramLength < (RX_BUF_MAX_LEN - 1))
        {
            strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength++] = (char)ch;
        }
        else
        {
            /* 溢出保护：丢弃后续字节,保留已收数据 */
        }
    }

    /* 空闲中断：一帧结束,清空闲标志并置位完成标记 */
    if (USART_GetITStatus(macESP8266_USARTx, USART_IT_IDLE) != RESET)
    {
        volatile uint32_t tmp;
        /* 通过先读 SR 再读 DR 清除 IDLE 标志(F4 系列推荐方式) */
        tmp = macESP8266_USARTx->SR;
        (void)tmp;
        tmp = macESP8266_USARTx->DR;
        (void)tmp;

        /* 标记一帧完成并添加字符串结束符,便于后续 strstr 解析 */
        if (strEsp8266_Fram_Record.InfBit.FramLength < RX_BUF_MAX_LEN)
        {
            strEsp8266_Fram_Record.Data_RX_BUF[strEsp8266_Fram_Record.InfBit.FramLength] = '\0';
        }
        strEsp8266_Fram_Record.InfBit.FramFinishFlag = 1;
    }
}
