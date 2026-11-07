# CubeMX 基础配置修改与检查记录

日期：2026-11-06。范围：修改两份IOC的基础外设配置，灯效和Flash保存仅预留。

后续状态：此文记录IOC配置阶段的检查。此后已按用户指示生成正式工程、加入基础调试代码并完成Debug编译，详见《基础硬件调试工程_编译检查记录.md》；仍未烧录或实板验证。

## 1. 交付文件

- `Code/Fan Blade/Fan Blade.ioc`
- `Code/Center Controller/Center Controller.ioc`
- 原始IOC备份：`outputs/ioc配置备份_20261106/`

两份配置表的状态说明已同步更新。`Code/Test_EnergyMechanism`未修改，正式工程目录中未写入生成源码；本次生成结果仅用于核对配置，存放在`tmp/ioc-config-20261106/leaf/Fan Blade/`与`tmp/ioc-config-20261106/center/Center Controller/`。

## 2. 实际配置

| 项目 | 叶片控制板 | 中心控制板 |
| --- | --- | --- |
| MCU / 工程 | STM32G474CBTx，CMake，FW_G4 V1.6.3 | 同左 |
| HSE | 32 MHz晶体 | 按网表修正为16 MHz晶体 |
| PLL | M=2，N=20，R=2，Q=8 | M=2，N=40，R=2，Q=8 |
| SYSCLK / AHB / APB1 / APB2 | 160 MHz，Range 1 Boost，Flash latency 4 | 同左 |
| FDCAN时钟 / 速率 | PLLQ 40 MHz；FDCAN2经典CAN 1 Mbps | PLLQ 40 MHz；FDCAN1/2/3经典CAN 1 Mbps |
| CAN位时序 | Prescaler=2、SJW=4、Seg1=15、Seg2=4；采样点80% | 同左，三路一致 |
| ADC | ADC1/2/3同步时钟/4=40 MHz；12 bit；4/4/2扫描；47.5 cycles；TIM6 TRGO上升沿 | 未配置 |
| 基础定时器 | TIM6 PSC=159、ARR=99，10 kHz TRGO，无定时器IRQ | TIM7 PSC=159、ARR=999，1 kHz；TIM7_DAC_IRQn |
| 串口 | USART2：PB3 TX、PA15 RX；115200、8N1 | USART1：PA9 TX、PA10 RX；1000000、8N1 |
| SPI | 无 | SPI1/SPI2：Mode 0，8 bit，MSB，/32=5 MHz；软件NSS，无NSS Pulse，无SPI DMA |
| MCP2515控制 | 无 | PB0/PA8片选初始High；PB1/PB7复位初始High；PB11/12下降沿EXTI，共用EXTI15_10向量 |
| 灯相关 | TIM1四路、TIM15两路PWM引脚保留，Pulse=0；PA12 PWM_OE初始Low；移除原TIM1_CH1 DMA | PA3 B_G、PA4 R_G输出初始Low |
| Flash保存 | 未分配参数页、未改链接脚本、未增加保存逻辑 | 同左 |

注意：Flash latency 4是CPU运行所需的Flash访问等待周期，与参数掉电保存功能无关。

叶片ADC扫描顺序保持：ADC1 CH1/2/3/4，ADC2 CH17/13/3/4，ADC3 CH12/1。PB2、PB11仅保留ADC2模拟引脚，本次未纳入扫描，因此尚不能提供这两路电压采样值。IN9/IN10与PB0/PB1的环数映射仍须在后续检测代码的数据拆分层落实，本次不改测试工程。

CAN4经SPI2控制MCP2515，CAN5经SPI1控制MCP2515；CubeMX只负责SPI/GPIO外设，MCP2515的16 MHz振荡器及1 Mbps位时序须由后续驱动初始化。中心CAN1上电机与1号叶片共线，协议ID分配不在IOC中实现。

## 3. DMA与中断检查

| 板卡 | DMA通道 | 请求 | 模式 / 数据宽度 | DMA优先级 / IRQ抢占优先级 |
| --- | --- | --- | --- | --- |
| 叶片 | DMA1 Channel1 | ADC1 | Circular / Halfword | High / 2 |
| 叶片 | DMA1 Channel2 | ADC2 | Circular / Halfword | High / 2 |
| 叶片 | DMA1 Channel3 | ADC3 | Circular / Halfword | High / 2 |
| 叶片 | DMA1 Channel4 | USART2_RX | Circular / Byte | Low / 5 |
| 叶片 | DMA1 Channel5 | USART2_TX | Normal / Byte | Low / 5 |
| 中心 | DMA1 Channel1 | USART1_RX | Circular / Byte | High / 5 |
| 中心 | DMA1 Channel2 | USART1_TX | Normal / Byte | Medium / 5 |

所有DMA通道均独占，均为内存地址递增、外设地址不递增，请求宏存在于STM32G4 HAL中。两块独立MCU之间使用相同通道号不构成冲突。叶片DMA2 Channel1的原灯DMA及其中断已移除。

NVIC采用Priority Group 4，子优先级均为0。叶片ADC1_2/ADC3=2、FDCAN2_IT0=3、USART2=5；中心FDCAN1_IT0=1、TIM7_DAC=2、FDCAN2/3_IT0及EXTI15_10=3、USART1=5；两板SysTick=15。中心EXTI11和EXTI12共享中断向量，但线路不同，生成代码分别调用两路HAL处理函数，无EXTI线路复用冲突。

## 4. 验证证据与范围

1. **静态交叉检查通过**：读取本次`HardWare/*.enet`，按STM32G474 LQFP48物理脚号核对全部已配置实引脚，并对照本机ST MCU数据库检查复用功能。两板均无重复引脚占用，保留PA13/PA14 SWD；未启用会占用PB3的Trace。
2. **DMA与时序检查通过**：请求宏、通道唯一性、IRQ使能、ADC Rank顺序及PLL/CAN/SPI/TIM计算均通过。静态结果：`tmp/ioc-config-20261106/static-validation.json`。
3. **CubeMX原生检查通过**：CubeMX 6.14.1加载两份IOC后，分别返回`Clock Configuration Error: false`与`Pinout & Configuration Error: false`。扩展配置回读未改变所写外设参数。
4. **临时代码生成及回读通过**：使用本机已安装的FW_G4 V1.6.3成功生成两套初始化源码，核对了HSE、PLL、Flash等待周期、外设初始化顺序、DMA请求及HAL中断调用。中心CS/RESET先写High再切换输出；叶片OE先写Low再切换输出；未生成PWM启动调用。
5. 本次**没有编译、烧录或实板测试**。CAN滤波内容/通知/启动、ADC校准/DMA缓冲区/启动、MCP2515驱动、业务协议及失联停车逻辑仍属于后续固件工作。当前IOC通过配置检查不代表上述业务功能已完成。

原生验证日志：`tmp/ioc-config-20261106/mx-verify-local.log`。回读结果：`tmp/ioc-config-20261106/generated-validation.txt`。可复查脚本：同目录`verify_ioc.py`、`verify_generated.py`。

CubeMX日志仍有其通用FDCAN插件查询其他型号字段的警告，以及导入时自动尝试添加G4不存在的`VP_RIF_VS_RIF1`的提示；源IOC未配置这些资源。最终原生检查为无错误，生成源码未出现这些资源。首次隔离环境因固件库识别请求下载而中止，之后使用本机已安装固件库完成生成；未升级固件包。
