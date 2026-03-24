# RS-485 引脚排查指南

## 当前配置（需要验证）
- USART2_TX: PA2
- USART2_RX: PA3  
- DE/RE控制: PA4 ← **这个最可疑！**

## 常见的RS-485引脚配置

### 配置1：USART2 + 邻近GPIO
```
PA2  → USART2_TX
PA3  → USART2_RX
PA1  → DE/RE (或PA0/PA4/PA5)
```

### 配置2：USART1 
```
PA9  → USART1_TX
PA10 → USART1_RX
PA8  → DE/RE (或PA11/PA12)
```

### 配置3：USART3
```
PB10 → USART3_TX
PB11 → USART3_RX
PB12 → DE/RE (或其他PB引脚)
```

## 快速测试步骤

### 测试1：检查是否根本没有RS-485
**可能性**：你的板子可能只有TTL串口，没有RS-485收发器

**验证方法**：
1. 用万用表测PA2（TX）对地电压
2. 如果是3.3V TTL电平 → **没有RS-485模块**
3. 如果测不到或很小 → 可能有RS-485

**解决方案**：
- 如果没有RS-485，需要外接MAX485模块
- 或者改用TTL转RS-485模块

### 测试2：DE/RE引脚错误
**现象**：从站发不出数据（一直处于接收模式）

**快速验证代码**：
在main.c的while循环中添加：
```c
// 强制设置DE=高（发送模式），看能否发送数据
GPIO_SetBits(GPIOA, GPIO_Pin_4);
printf("测试发送\r\n");
Delay(0xFFFF);
GPIO_ResetBits(GPIOA, GPIO_Pin_4);
```

### 测试3：尝试其他可能的DE引脚

常见候选：PA0, PA1, PA5, PA6, PA7

## 最简单的验证方法

**不用Modbus Poll，直接测试USART2能否收发**：

1. 短接PA2和PA3（TX和RX短接）
2. 运行测试代码，看能否自发自收
3. 如果可以 → USART2硬件正常，问题在RS-485部分
4. 如果不行 → USART2配置有问题

## 建议的调试代码

添加到main.c，替换现有的主循环：
```c
while(1)
{
    // 测试：USART2直接发送测试字节
    static uint8_t test = 0;
    USART_SendData(USART2, test++);
    while(USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    
    printf("[TEST] USART2 发送测试字节: %d\r\n", test);
    Delay(0x3FFFFF);
    
    // 用示波器或逻辑分析仪看PA2是否有波形
}
```
