# 中心控制板 CubeMX 配置表

对应工程：`Code/Center Controller/Center Controller.ioc`。
硬件依据：`HardWare/中心控制板.enet`。

状态（2026-11-06）：已按用户指示将基础配置落实到ioc，并通过CubeMX 6.14.1加载、时钟/引脚检查及临时目录代码生成回读。中心晶振按网表修正为16MHz，CPU从原170MHz改为160MHz；本表CAN、SPI、USART1 DMA、GPIO/EXTI与TIM7配置均已应用。灯效和Flash保存暂未实现。未烧录或实测。详见同目录《CubeMX基础配置修改与检查记录.md》。

## 1. 芯片与工程

| 项目 | 配置 |
| --- | --- |
| MCU | STM32G474CBTx，LQFP48，实物CBT6 |
| Project Name | Center Controller |
| Toolchain / IDE | CMake；VS Code+STM32工具链+ST-LINK |
| Firmware Package | STM32Cube FW_G4 V1.6.3 |
| 外设初始化 | Generate peripheral initialization as a pair of .c/.h files |
| 重生成保留用户代码 | Enable；业务代码放独立文件 |
| SYS Debug | Serial Wire，PA13/PA14；不启用占用PB3的Trace |
| Timebase | SysTick，1ms |
| USB / USB_DEVICE / UCPD | 本期不启用；Type-C数据实际接ESP32 |
| ADC | 中心无已确认电源ADC连接，本期不配置 |
| RTOS | 本表按不启用安排；后续选择RTOS再审查优先级 |
| IWDG/WWDG | 基础调试暂不启用；正式策略另定 |

中心CN2线序：1=3.3V参考、2=SWDIO、3=SWCLK、4=GND。ST-LINK烧录中心STM32，不通过板上Type-C给中心STM32直接烧录。

## 2. RCC及Clock Configuration建议

| 项目 | 值 | 结果/原因 |
| --- | --- | --- |
| HSE Mode | Crystal/Ceramic Resonator | PF0/PF1 |
| HSE Frequency | 16MHz | X1实际晶振；当前ioc的32MHz不符合网表 |
| PLL Source | HSE | |
| PLLM | /2 | 输入8MHz |
| PLLN | ×40 | VCO=320MHz |
| PLLR | /2 | SYSCLK=160MHz |
| PLLQ | /8，使能Q输出 | FDCAN kernel=40MHz |
| PLLP | /2 | 当前未用的输出，保持合法值 |
| SYSCLK Source | PLLCLK | 160MHz |
| AHB Prescaler | /1 | HCLK=160MHz |
| APB1/APB2 Prescaler | 均/1 | PCLK1/PCLK2及相关TIM=160MHz |
| Voltage Scaling | Range 1 Boost | |
| Flash latency | CubeMX校验自动生成，参考4WS | 不照搬170MHz参数 |
| FDCAN Clock Mux | PLLQ | 三个FDCAN实例共用该时钟 |
| USART1 Clock Mux | PCLK2 | 160MHz |
| SPI1 / SPI2源时钟 | PCLK2 / PCLK1 | 均160MHz |
| LSE、HSI48、CRS | 当前功能不要求启用 | ESP32 USB使用自身时钟 |

`16MHz/2×40/2=160MHz`；`16MHz/2×40/8=40MHz`。如果希望保留中心170MHz，需重新给出整套PLL、SPI和CAN位时序，不能把本表的分频数直接混用。

## 3. 引脚配置总表

| 引脚 | CubeMX功能/Label | 连接 | GPIO建议 |
| --- | --- | --- | --- |
| PA11 | FDCAN1_RX | CAN1_RX | AF、No pull |
| PA12 | FDCAN1_TX | CAN1_TX | AF Push Pull、High speed、No pull |
| PB5 | FDCAN2_RX | CAN2_RX | AF、No pull |
| PB6 | FDCAN2_TX | CAN2_TX | AF Push Pull、High speed、No pull |
| PB3 | FDCAN3_RX | CAN3_RX | AF、No pull；不可占为SWO |
| PA15 | FDCAN3_TX | CAN3_TX | AF Push Pull、High speed、No pull |
| PA5 | SPI1_SCK | CAN5的U8 SCK | AF Push Pull、High speed、No pull |
| PA6 | SPI1_MISO | U8 SO | AF、No pull |
| PA7 | SPI1_MOSI | U8 SI | AF Push Pull、High speed、No pull |
| PB0 | GPIO_Output，SPI1_CS | U8 /CS | Push Pull、初始High、No pull |
| PB1 | GPIO_Output，RESET1 | U8 /RESET | Push Pull、初始High、No pull |
| PB11 | GPIO_EXTI11，INT2 | U8 /INT | Falling edge、No pull，外部10k上拉 |
| PB13 | SPI2_SCK | CAN4的U6 SCK | AF Push Pull、High speed、No pull |
| PB14 | SPI2_MISO | U6 SO | AF、No pull |
| PB15 | SPI2_MOSI | U6 SI | AF Push Pull、High speed、No pull |
| PA8 | GPIO_Output，SPI2_CS | U6 /CS | Push Pull、初始High、No pull |
| PB7 | GPIO_Output，RESET2 | U6 /RESET | Push Pull、初始High、No pull |
| PB12 | GPIO_EXTI12，INT1 | U6 /INT | Falling edge、No pull，外部10k上拉 |
| PA9 | USART1_TX | ESP_UART1_RX→ESP GPIO5 | AF Push Pull、High speed、No pull |
| PA10 | USART1_RX | ESP_UART1_TX←ESP GPIO4 | AF、Pull-up建议初值 |
| PA3 | GPIO_Output，B_G | 蓝色辅助输出MOS栅极 | Push Pull、Low speed、No pull、初始Low |
| PA4 | GPIO_Output，R_G | 红色辅助输出MOS栅极 | Push Pull、Low speed、No pull、初始Low |
| PA13/PA14 | SYS_SWDIO/SWCLK | 下载口 | SYS自动管理 |
| PF0/PF1 | RCC_OSC_IN/OUT | 16MHz晶振 | RCC管理 |

GPIO输出先设置初值再切换输出模式，由生成代码顺序复核。RESET初始High表示不主动复位；业务初始化应发送满足MCP2515要求的复位脉冲/命令并等待芯片就绪。

## 4. FDCAN1/2/3

| 外设 | 实物总线 | 用途 |
| --- | --- | --- |
| FDCAN1 | CAN1 | C620电调与1号叶片共线 |
| FDCAN2 | CAN2 | 2号叶片 |
| FDCAN3 | CAN3 | 3号叶片 |

CAN4、CAN5是外部MCP2515，不能在CubeMX里找“FDCAN4/5”。CAN1受C620约束使用1Mbps；其余叶片总线本表建议统一1Mbps。

| Parameter Settings字段 | 三路建议配置 |
| --- | --- |
| Frame Format | Classic mode |
| Mode | Normal |
| Clock Divider | /1，三路填写一致 |
| Auto Retransmission | Enable；软件清理过时命令并处理bus-off |
| Transmit Pause | Disable |
| Protocol Exception | Disable |
| Nominal Prescaler | 2 |
| Nominal Sync Jump Width | 4 tq |
| Nominal Time Seg1 | 15 tq |
| Nominal Time Seg2 | 4 tq |
| Nominal Bit Rate | 1Mbps |
| Sample Point | 80% |
| Standard Filters Number | FDCAN1建议4；FDCAN2/3各建议2 |
| Extended Filters Number | 0 |
| Tx FIFO/Queue Mode | Queue Operation |

计算：`40MHz/[2×(1+15+4)]=1Mbps`。这些填写值为CubeMX字段值，不是寄存器的减一编码。

经典CAN不使用Data阶段；如工具要求，可同样填Data Prescaler=2、SJW=4、Seg1=15、Seg2=4；发送时BRS关闭。HAL的ClockDivider是三路共享设置，首个初始化实例设置它，不能每路指定不同内核分频。

过滤内容在协议中确定：FDCAN1至少区分C620反馈与叶片业务，按需要分FIFO；初期可统一FIFO0。叶片报文避开C620的`0x200`、`0x1FF`和`0x201～0x208`。C620实际ID须上电核实，不能把“默认值”直接认定为1。

要在代码中配置过滤器、全局过滤、通知路由并启动外设；仅开启NVIC不自动接收报文。发送队列与ID分配共同保障电机控制优先，诊断快照不得无限排队挤占CAN1。

## 5. SPI1/SPI2与MCP2515

### 5.1 CubeMX设置

| 字段 | SPI1 | SPI2 |
| --- | --- | --- |
| 对应芯片/总线 | U8 / CAN5 | U6 / CAN4 |
| Mode | Full-Duplex Master | Full-Duplex Master |
| Data Size | 8 bits | 8 bits |
| First Bit | MSB First | MSB First |
| Clock Polarity | Low | Low |
| Clock Phase | 1 Edge | 1 Edge |
| NSS | Software | Software |
| Baud Rate Prescaler | /32 | /32 |
| SCK结果 | 160MHz/32=5MHz | 160MHz/32=5MHz |
| TI Mode | Disable | Disable |
| CRC Calculation | Disable | Disable |
| NSS Pulse | Disable | Disable |
| DMA | 基础联调暂不分配 | 基础联调暂不分配 |

MCP2515支持SPI mode0/mode3，最高10MHz；本表选mode0、5MHz初值。当前ioc中的SPI1=42.5MHz、SPI2=85MHz都不可沿用。[Microchip数据手册](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Family-Data-Sheet-DS20001801K.pdf)

### 5.2 中断与应用初始化

| 事项 | 要求 |
| --- | --- |
| /CS | 每次完整指令期间保持Low，结束后High；不能逐字节拉高 |
| /INT | 低有效，PB11/PB12各自Falling EXTI，共享EXTI15_10 IRQ |
| IRQ处理 | 仅标记哪个MCP待处理；在可调度上下文中读取并处理状态 |
| 持续低电平 | 清完所有启用中断来源，检查INT电平；不能只等下一次下降沿 |
| SPI传输 | 初期采用有界短事务，不能在高优先级电机ISR里阻塞访问 |
| 芯片时钟 | U6/U8均独立16MHz晶振，不使用STM32 HSE数值代替 |
| CAN速率 | 驱动内设置16MHz对应的1Mbps位时序，非CubeMX设置项 |
| 工作模式 | 完成复位、配置、滤波/中断设置后切入Normal |

示例MCP位时序计算可选TQ=125ns，Sync=1、Prop=2、Phase1=3、Phase2=2，共8TQ得到1Mbps、采样点75%，SJW=1TQ；BRP寄存器字段为0。该例须在驱动中核对寄存器编码与实物波形，不把STM32的Prescaler=2直接写进MCP2515寄存器。不同控制器的采样点可以不同，但总线位速率必须相同，并通过整线通信验证。

## 6. USART1与ESP32链路

| 字段 | 建议初值 |
| --- | --- |
| Mode | Asynchronous，TX/RX |
| TX/RX | PA9 / PA10 |
| Baud Rate | 1000000，需ESP32同步配置 |
| Word Length / Parity / Stop Bits | 8 Bits / None / 1 |
| Hardware Flow Control | None，网表未接RTS/CTS |
| Oversampling | 16 |
| FIFO Mode | 初期Disable |
| RX | Circular DMA+IDLE事件，应用环形解析 |
| TX | Normal DMA，发送队列 |

1Mbps是本表建议，基于板内UART、五片遥测和多端转发留余量；不是已经冻结的协议参数。以160MHz时钟计算可以整数分频。不要把ESP_UART1_TX网络名称误解成STM32 TX：该网实际连STM32 PA10 RX。

| DMA Request | Channel | Direction | Mode | Width P/M | PInc/MInc | Priority |
| --- | --- | --- | --- | --- | --- | --- |
| USART1_RX | DMA1 Channel1 | Peripheral to Memory | Circular | Byte/Byte | Disable/Enable | High |
| USART1_TX | DMA1 Channel2 | Memory to Peripheral | Normal | Byte/Byte | Disable/Enable | Medium |

DMAMUX同步和Request Generator关闭。接收需要正确处理DMA半满/全满与IDLE，不因一次IDLE就重置整个环形缓冲。通信帧使用长度/校验/序号，不依赖一次DMA恰好收到一帧。

## 7. 电机控制周期定时器建议

用户界面不提供独立电机调试模式，但中心仍需内部周期速度闭环。建议新增TIM7产生1kHz调度事件：

| 字段 | 值 |
| --- | --- |
| Timer | TIM7，内部基本定时器，不占外部脚 |
| Prescaler | 159 |
| Counter Period | 999 |
| Counter Mode | Up |
| Auto-reload preload | Disable |
| Update Interrupt | Enable |
| Master Trigger | Reset，当前不触发其他外设 |

`160MHz/(159+1)/(999+1)=1000Hz`。此周期是控制软件建议值，不是C620反馈帧自动保证的调度。ISR只执行有界计算/发调度标记，不能在里面等SPI、UART或CAN发送完成。若后续选择其他调度方式，TIM7可替换，不改变机械速度公式。

## 8. NVIC建议初值

Priority Group=NVIC_PRIORITYGROUP_4，Subpriority均0。

| IRQ | Enable | 抢占优先级 | 说明 |
| --- | --- | --- | --- |
| FDCAN1_IT0 | Yes | 1 | C620反馈与叶片1事件，ISR快速入队 |
| TIM7_DAC | 采用TIM7调度时Yes | 2 | TIM7与DAC2/4共享IRQ；此处用于控制周期 |
| FDCAN2_IT0 | Yes | 3 | 叶片2 |
| FDCAN3_IT0 | Yes | 3 | 叶片3 |
| EXTI15_10 | Yes | 3 | 同时处理PB11 INT2和PB12 INT1 |
| USART1 | Yes | 5 | IDLE及串口错误 |
| DMA1_Channel1 | Yes | 5 | UART RX |
| DMA1_Channel2 | Yes | 5 | UART TX |
| FDCANx_IT1 | 默认不启用 | — | 仅在业务将事件分配Line1后启用 |
| SPI1/SPI2全局IRQ | 基础轮询事务暂不开 | — | 改IT/DMA驱动后按需开启 |
| SysTick | Yes | 15 | HAL时基 |

FDCAN RX与总线错误通知需显式映射到所启用的Line0；bus-off恢复、失联计时属于应用代码，不是勾选CubeMX就完成。

## 9. 不属于CubeMX的配置

| 事项 | 放在哪里 |
| --- | --- |
| MCP2515的CNF寄存器、报文过滤与中断位 | MCP驱动 |
| C620 ID、电流命令、PID、40/110皮带比和内部减速比 | 电机驱动/配置 |
| 小/大符状态机、计分、失联停机、连续重开 | 中心业务层 |
| CAN报文ID、UART帧、CRC、事件序号 | 协议层 |
| ESP32 BLE、USB串口、UART GPIO4/5 | ESP-IDF工程，不是CubeMX |
| 参数Flash预留区及可靠保存 | 链接脚本和存储模块，尚未确认是否纳入 |
| 终端电阻 | 实物；CAN1的R28经SW1接入，其余CAN2～5已有120Ω |

CAN1接电调和叶片后按实际总线两端配置终端，不因为有三个节点就保留三个120Ω终端。

## 10. 当前ioc修正清单

| 当前配置/缺项 | 配置表目标 |
| --- | --- |
| HSE=32MHz，网表为16MHz | HSE=16MHz；本表建议重配PLL到160MHz |
| FDCAN时钟170MHz、标称约3.542Mbps | PLLQ40MHz、1Mbps经典CAN |
| SPI1 /4、SPI2 /2 | 两路都/32，得到5MHz |
| MCP复位与INT未完整配置 | PB1/PB7输出、PB11/PB12下降沿EXTI |
| 红蓝辅助MOS脚未配置 | PA3/PA4输出，初始Low |
| ESP UART仅选引脚 | 补波特率、DMA、IDLE及错误处理 |
| 修改前无控制周期定时器 | 本次已新增TIM7 1kHz |

生成后先回读SystemClock_Config、GPIO初值、SPI时钟、三FDCAN位时序和NVIC，再做板上连通性验证。当前表尚不代表实物CAN、电机或蓝牙联调已通过。

依据：[STM32G474数据手册](https://www.st.com/resource/en/datasheet/stm32g474cb.pdf)、[MCP2515数据手册](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Family-Data-Sheet-DS20001801K.pdf)、[C620使用说明](https://cdn-hz.robomaster.com/robomasters/public/document/RoboMaster%20C620%20Brushless%20DC%20Motor%20Speed%20Controller%20V1.01.pdf)，以及本地STM32CubeG4 V1.6.3 HAL定义。
