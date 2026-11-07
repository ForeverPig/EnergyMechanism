# 叶片控制板 CubeMX 配置表

对应工程：`Code/Fan Blade/Fan Blade.ioc`。
硬件依据：`HardWare/叶片控制板.enet`；检测逻辑参考：`Code/Test_EnergyMechanism`及其`TEST.enet`。

状态（2026-11-06）：已按用户指示将基础配置落实到ioc，并通过CubeMX 6.14.1加载、时钟/引脚检查及临时目录代码生成回读。ADC采用基础调试的4/4/2序列；两路电压暂只预留模拟引脚；USART2 RX/TX DMA已启用。LED仅保留PWM资源、PWM_OE初始Low，不分配灯DMA；Flash保存暂未实现。未烧录或实测。详见同目录《CubeMX基础配置修改与检查记录.md》。ESP32不使用CubeMX。

## 1. 芯片与工程

| CubeMX位置/项目 | 配置 | 说明 |
| --- | --- | --- |
| MCU | STM32G474CBTx，LQFP48 | 实物STM32G474CBT6，128KB Flash |
| Project Name | Fan Blade | 保留用户指定目录 |
| Toolchain / IDE | CMake | 配合VS Code、STM32工具链与ST-LINK |
| Firmware Package | STM32Cube FW_G4 V1.6.3 | 沿用参考工程版本，不在本次升级 |
| Code Generator | Generate peripheral initialization as a pair of .c/.h files | 外设初始化分文件 |
| Keep User Code when re-generating | Enable | 业务代码仍放独立文件 |
| SYS Debug | Serial Wire | 使用PA13/PA14；释放PA15、PB3，禁止选Trace占PB3 |
| SYS Timebase Source | SysTick | 1ms HAL时基；ADC采样不依赖它 |
| Dead Battery Signals | Disable | 沿用当前工程选项 |
| RTOS | 本表暂按不启用设计 | 若后续使用RTOS，重新审查中断优先级约束 |
| IWDG/WWDG | 基础上电调试阶段不启用 | 正式固件是否启用及超时另行确定 |
| USB / USB_DEVICE | 不启用 | 新板PA11/PA12为灯输出和使能 |

ST-LINK线序按新板CN2：1=3.3V参考、2=SWDIO、3=SWCLK、4=GND。与测试板CN2线序不同。

## 2. RCC及Clock Configuration

CPU沿用测试工程160MHz；FDCAN另选PLLQ=40MHz，不直接沿用当前ioc显示的160MHz FDCAN时钟。

| 项目 | 填写/选择值 | 结果 |
| --- | --- | --- |
| HSE | Crystal/Ceramic Resonator | PF0/PF1；不是Bypass |
| HSE input frequency | 32MHz | 新板X1标称值 |
| LSE | Disable | 网表无LSE晶振 |
| PLL Source | HSE | |
| PLLM | /2 | PLL输入16MHz |
| PLLN | ×20 | VCO=320MHz |
| PLLR | /2 | SYSCLK=160MHz |
| PLLQ | /8，并使能Q输出 | 40MHz，给FDCAN |
| PLLP | /2 | 可保持参考值；ADC不用PLL P路径 |
| System Clock Mux | PLLCLK | 160MHz |
| AHB Prescaler | /1 | HCLK=160MHz |
| APB1 / APB2 Prescaler | 均/1 | PCLK1/PCLK2=160MHz；定时器亦160MHz |
| Power Regulator Voltage Scale | Range 1 Boost | 对应`PWR_REGULATOR_VOLTAGE_SCALE1_BOOST` |
| Flash latency | CubeMX校验自动生成；参考值4WS | 与已验证160MHz配置一致 |
| ADC12 / ADC345 Clock Mux | SYSCLK | Clock图可显示160MHz，ADC内部再同步/4 |
| FDCAN Clock Mux | PLLQ | HAL宏通常为`RCC_FDCANCLKSOURCE_PLL` |
| USART2 Clock Mux | PCLK1 | 160MHz |
| HSI48 / CRS | 当前功能不要求开启 | 不保留仅用于旧板USB的时钟依赖 |

生成后确认：`hadc*.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4`，实际ADC转换时钟为40MHz。不能仅凭Clock图中的160MHz就认为ADC正在以160MHz转换。

## 3. 引脚总表

| 引脚 | CubeMX功能 | 网表/用途 | GPIO配置 |
| --- | --- | --- | --- |
| PA0 | ADC1_IN1 | IN1，6环 | Analog、No pull |
| PA1 | ADC1_IN2 | IN2，7环 | Analog、No pull |
| PA2 | ADC1_IN3 | IN3，8环 | Analog、No pull |
| PA3 | ADC1_IN4 | IN4，9环 | Analog、No pull |
| PA4 | ADC2_IN17 | IN5，10环 | Analog、No pull |
| PA5 | ADC2_IN13 | IN6，5环 | Analog、No pull |
| PA6 | ADC2_IN3 | IN7，4环 | Analog、No pull |
| PA7 | ADC2_IN4 | IN8，3环 | Analog、No pull |
| PB0 | ADC3_IN12 | IN10，1环 | Analog、No pull |
| PB1 | ADC3_IN1 | IN9，2环 | Analog、No pull |
| PB2 | ADC2_IN12 | 5V检测 | Analog、No pull |
| PB11 | ADC2_IN14 | VIN检测 | Analog、No pull |
| PB12 | FDCAN2_RX | CAN_RX | AF、No pull |
| PB13 | FDCAN2_TX | CAN_TX | AF Push Pull、No pull、High speed |
| PB3 | USART2_TX | 本地调试TX | AF Push Pull、No pull、High speed |
| PA15 | USART2_RX | 本地调试RX | AF、Pull-up建议初值 |
| PA12 | GPIO_Output，PWM_OE | 灯输出使能 | Push Pull、No pull、Low speed、初始Low |
| PA13 | SYS_SWDIO | 下载调试 | 由SYS管理 |
| PA14 | SYS_SWCLK | 下载调试 | 由SYS管理 |
| PF0/PF1 | RCC_OSC_IN/OUT | 32MHz晶振 | 由RCC管理 |

本次已将Fan Blade.ioc中原先误设的PB4 USART2_RX改为PA15；PB4恢复未使用。已知未连接的空闲脚可设Analog/No pull；不要批量覆盖调试、BOOT0及晶振相关脚。

PA12拉低时，Q3关断，74AHCT541的/OE由R67上拉而关闭输出；PA12拉高才允许输出。保持上电Low，灯资源准备完成后由软件使能。

## 4. ADC1/2/3公共配置

| Parameter Settings字段 | 值 |
| --- | --- |
| Clock Prescaler | Synchronous clock mode divided by 4 |
| Resolution | 12 bits |
| Data Alignment | Right alignment |
| Scan Conversion Mode | Enable |
| Continuous Conversion Mode | Disable |
| Discontinuous Conversion Mode | Disable |
| External Trigger Conversion Source | TIM6 Trigger Out event / TIM6 TRGO |
| External Trigger Conversion Edge | Rising edge |
| End of Conversion Selection | End of sequence of conversion |
| DMA Continuous Requests | Enable |
| Overrun | Data overwritten |
| Low Power Auto Wait | Disable |
| Oversampling | Disable |
| Gain Compensation | 0 |
| Single/Differential | Single-ended，所有通道 |
| Offset | None |
| Multi-mode | Independent，不启用双/三ADC交错 |

### 4.1 首轮检测验证配置

先采用与已验证程序一致的`4/4/2`序列；PB2/PB11可保留模拟引脚，但不加入Regular Rank。

| ADC | Rank | Channel | 引脚 | 输入 | 环号 | Sampling Time |
| --- | --- | --- | --- | --- | --- | --- |
| ADC1 | 1 | IN1 | PA0 | IN1 | 6 | 47.5 cycles |
| ADC1 | 2 | IN2 | PA1 | IN2 | 7 | 47.5 cycles |
| ADC1 | 3 | IN3 | PA2 | IN3 | 8 | 47.5 cycles |
| ADC1 | 4 | IN4 | PA3 | IN4 | 9 | 47.5 cycles |
| ADC2 | 1 | IN17 | PA4 | IN5 | 10 | 47.5 cycles |
| ADC2 | 2 | IN13 | PA5 | IN6 | 5 | 47.5 cycles |
| ADC2 | 3 | IN3 | PA6 | IN7 | 4 | 47.5 cycles |
| ADC2 | 4 | IN4 | PA7 | IN8 | 3 | 47.5 cycles |
| ADC3 | 1 | IN12 | PB0 | IN10 | 1 | 47.5 cycles |
| ADC3 | 2 | IN1 | PB1 | IN9 | 2 | 47.5 cycles |

Number Of Conversion：ADC1=4、ADC2=4、ADC3=2。

### 4.2 加入电压显示后的建议配置

压力验证通过后，建议仅在ADC2末尾增加两个Rank，前四个压力通道顺序不动：

| ADC | Rank | Channel | 引脚 | 量 | Sampling Time |
| --- | --- | --- | --- | --- | --- |
| ADC2 | 5 | IN12 | PB2 | 5V分压，恢复系数标称2 | 247.5 cycles，建议初值 |
| ADC2 | 6 | IN14 | PB11 | VIN分压，恢复系数标称11 | 247.5 cycles，建议初值 |

此时Number Of Conversion变为ADC1=4、ADC2=6、ADC3=2。电压也随10kHz触发采样，在软件里滤波/降采样后低频显示；不必因此把压力检测降至20Hz。

按12bit每次转换12.5 cycles估算，ADC2一次扫描约`[4×(47.5+12.5)+2×(247.5+12.5)]/40MHz=19µs`，小于100µs触发周期。该估算不替代实测波形/噪声和丢块验证。

必须同时修改新程序的ADC2 DMA长度、每帧步长及数据拆分；不是只在CubeMX把4改成6。内部VREFINT暂不增加Rank；电压换算用实测参考电压校准，若以后增加VREFINT须重新更新此表。

## 5. TIM6采样触发

| 字段 | 值 |
| --- | --- |
| Clock Source | Internal Clock |
| Prescaler | 159 |
| Counter Mode | Up |
| Counter Period / ARR | 99 |
| Auto-reload preload | Disable |
| Master Output Trigger / TRGO | Update Event |
| Master/Slave Mode | Disable |
| TIM6 global interrupt | 采样触发不需要开启 |

`160MHz / (159+1) / (99+1) = 10kHz`。三路ADC先完成校准并启动DMA，再启动TIM6。不要在10kHz定时器中断中逐通道软件启动ADC。

## 6. DMA配置

以下通道分配是基于现有ADC配置的建议；DMAMUX使请求源与DMA通道不再固定绑定，但同一通道不能分给两个同时运行的外设。

| Request | DMA Channel | Direction | Mode | Peripheral/Memory Width | PInc/MInc | Priority |
| --- | --- | --- | --- | --- | --- | --- |
| ADC1 | DMA1 Channel1 | Peripheral to Memory | Circular | Half Word / Half Word | Disable / Enable | High |
| ADC2 | DMA1 Channel2 | Peripheral to Memory | Circular | Half Word / Half Word | Disable / Enable | High |
| ADC3 | DMA1 Channel3 | Peripheral to Memory | Circular | Half Word / Half Word | Disable / Enable | High |
| USART2_RX，可选 | DMA1 Channel4 | Peripheral to Memory | Circular | Byte / Byte | Disable / Enable | Low |
| USART2_TX，可选 | DMA1 Channel5 | Memory to Peripheral | Normal | Byte / Byte | Disable / Enable | Low |

所有请求：关闭DMAMUX同步和Request Generator。启用ADC DMA半满、全满、错误处理。USART2若第一阶段只用中断收发，可以不分配这两路DMA。

参考32帧环形缓冲：

| 阶段 | ADC1总长度 | ADC2总长度 | ADC3总长度 |
| --- | --- | --- | --- |
| 4/4/2检测 | 128个uint16_t | 128个uint16_t | 64个uint16_t |
| 4/6/2检测+电压 | 128个uint16_t | 192个uint16_t | 64个uint16_t |

DMA启动长度单位是采样元素数，不是字节。ADC3每帧步长始终为2；新板软件环号表按PA0～PA7/PB0/PB1顺序为`{6,7,8,9,10,5,4,3,1,2}`。

## 7. FDCAN2配置

与中心板采用同一组建议位时序：PLLQ=40MHz、Clock Divider=/1、1Mbps经典CAN。

| 字段 | 建议值 |
| --- | --- |
| Frame Format | Classic mode |
| Mode | Normal |
| Clock Divider | Divide by 1 |
| Auto Retransmission | Enable；应用层仍管理超时和过时待发数据 |
| Transmit Pause | Disable |
| Protocol Exception | Disable，经典CAN初值 |
| Nominal Prescaler | 2 |
| Nominal Sync Jump Width | 4 tq |
| Nominal Time Seg1 | 15 tq |
| Nominal Time Seg2 | 4 tq |
| 标称速率 | 40MHz / [2×(1+15+4)] = 1Mbps |
| 采样点 | (1+15)/20 = 80% |
| Standard Filters Number | 2，预留业务过滤条目，具体ID待协议确定 |
| Extended Filters Number | 0 |
| Tx FIFO/Queue Mode | Queue Operation，按ID仲裁待发优先级 |
| BRS | 发送帧中Disable，不发送CAN FD |

Data阶段时序在经典CAN下不参与传输；如生成器要求合法值，可填Prescaler=2、SJW=4、Seg1=15、Seg2=4。筛选器实际内容、全局拒收非匹配帧/远程帧、RX FIFO通知及启动由业务初始化完成，不能只配置CubeMX却未调用HAL启动。

本型号HAL的FDCAN消息RAM结构与部分H7教程不同，不照抄不存在的动态FIFO元素数量/偏移字段。

## 8. USART2本地调试

| 字段 | 建议初值 |
| --- | --- |
| Mode | Asynchronous，TX/RX |
| TX/RX | PB3 / PA15 |
| Baud Rate | 115200 |
| Word Length / Parity / Stop Bits | 8 Bits / None / 1 |
| Hardware Flow Control | None |
| Oversampling | 16 |
| FIFO Mode | 初期Disable |
| RX方式 | 中断或Circular DMA+IDLE，按是否需要持续日志选择 |

这是叶片本地调试口；与中心ESP32串口不是同一个接口，也不要求同一波特率。

## 9. 灯输出仅预留

| 板上接口 | STM32 | 定时器通道 | 已确认DMA请求情况 |
| --- | --- | --- | --- |
| DOUT1 | PA11 | TIM1_CH4 | TIM1_CH4请求可用 |
| DOUT2 | PA10 | TIM1_CH3 | TIM1_CH3请求可用 |
| DOUT3 | PA9 | TIM1_CH2 | TIM1_CH2请求可用 |
| DOUT4 | PA8 | TIM1_CH1 | TIM1_CH1请求可用，参考程序已使用 |
| DOUT5 | PB15 | TIM15_CH2 | 无独立TIM15_CH2 DMA请求 |
| DOUT6 | PB14 | TIM15_CH1 | TIM15_CH1请求可用 |

CubeMX可保留上述PWM引脚选择及Pulse=0，程序暂不启动通道，PWM_OE保持Low。未确定灯类型、颗数和并发方式前，不直接配置六路PWM DMA。

若临时复用已验证WS2812测试，仅在PA8/DOUT4上：TIM1 PSC=0、ARR=199、PWM mode1、高有效、Pulse=0、重复计数0；DMA2 Channel1，Request=TIM1_CH1，Memory to Peripheral、Normal、双Half Word、PInc禁用/MInc启用、Very High。这是单链测试配置，不是六路最终方案。

TIM15_CH2后续可评估TIM15_UP驱动CCR2或定时器DMA Burst等方案；须设计与CH1共用计时器的更新、预装载和启动时序，不能虚构`DMA_REQUEST_TIM15_CH2`。

## 10. NVIC建议初值

Priority Group=NVIC_PRIORITYGROUP_4；表中为抢占优先级，数值小者优先级高，Subpriority均0。

| Interrupt | Enable | Priority | 用途 |
| --- | --- | --- | --- |
| DMA1_Channel1/2/3 | Yes | 2 | ADC缓冲与错误 |
| ADC1_2 / ADC3 | Yes | 2 | ADC overrun等错误处理 |
| FDCAN2_IT0 | Yes | 3 | RX FIFO0与按需映射到Line0的错误通知 |
| FDCAN2_IT1 | 暂不启用 | — | 只有将事件映射到Line1时再启用 |
| USART2 | 使用该口时Yes | 5 | 收发/IDLE/错误 |
| DMA1_Channel4/5 | 启用串口DMA时Yes | 5 | 串口DMA |
| DMA2_Channel1 | 启用PA8单链测试时Yes | 1 | 灯DMA完成，沿用参考优先级 |
| SysTick | Yes | 15 | HAL时基 |
| TIM6_DAC | 不因ADC触发而开启 | — | TIM6 TRGO由硬件完成 |

ISR只搬运/入队、更新状态，不在其中解析整帧遥测或阻塞发送。最终优先级须随最坏执行时间和灯DMA方案复核。

## 11. 相对修改前ioc的检查项

| 项目 | 当前情况 | 目标 |
| --- | --- | --- |
| USART2_RX | PB4 | 改PA15 |
| USB | 测试板有USB CDC | 新板禁用USB及相关中间件 |
| FDCAN | 显示约3.333Mbps，时钟160MHz | 显式选择PLLQ40MHz并填1Mbps时序 |
| ADC采样时钟 | ioc不一定写出默认分频 | 生成后明确检查同步/4=40MHz |
| ADC环号 | 测试程序PB0=2环、PB1=1环 | 新板PB0=1环、PB1=2环 |
| 两路电压 | 已选引脚，未进入扫描序列 | 先检测4/4/2；加电压后4/6/2并改DMA拆分 |
| 灯DMA | 只有TIM1_CH1已有配置 | 保留单链测试能力，六路方案另定 |

运行还需要：ADC单端校准、三路DMA启动、TIM6启动、CAN过滤/通知/启动、串口接收启动及队列初始化。CubeMX生成不等于这些业务已运行。

依据：[STM32G474数据手册](https://www.st.com/resource/en/datasheet/stm32g474cb.pdf)、本地STM32CubeG4 V1.6.3的`stm32g4xx_hal_fdcan.h`/`stm32g4xx_hal_dma.h`与参考工程生成源码。表内时序计算是针对所列时钟的计算值，尚未经过新板实测。
