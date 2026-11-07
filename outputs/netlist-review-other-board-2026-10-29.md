# 独立五路 CAN / ESP32 控制板网表审查

日期：2026-10-29。

输入：`D:/Download/Netlist_Schematic1_2026-10-29.enet`，92个元件，316051字节。SHA256：`F6770FF12770F771562559DC68CF36594BAF2F7560C768549F7C861B6046EE03`。

用户确认：VIN为24V；U12连接两路串联小灯，每路约100mA。本板独立审查，不套用之前WS2812板的10A负载和旧位号。

本次只做静态连接与器件资料审查，未修改原理图/PCB/固件，未操作硬件。

## 架构识别

- U7 STM32G474CBT6：三路片内FDCAN、两组SPI、与ESP32通信的USART1。
- U6/U8 MCP2515：分别通过SPI1/SPI2扩展第4、第5路经典CAN。
- U1～U5 TJA1051T/3：五路CAN收发器。
- U14 ESP32-C3-WROOM-02-N4：通过USB下载/通信，通过IO4/IO5与STM32通信。
- U22 LMR16006Y：VIN降压到5V_BUCK，D8/D9进行BUCK与USB电源二极管或合，U15 TLV1117LV33再稳压到3.3V。
- U9/U13 2N7002K：U12两路24V灯的低侧开关。

## 1. 确定漏线：两个灯开关的栅极没有连接

U9.1(G)和U13.1(G)的net字段均为空。U9源极接GND、漏极接R_GND；U13源极接GND、漏极接B_GND，U12公共正极接VIN。

这两路当前没有任何控制输入，不能由MCU正常控制；栅极悬空也可能导致不确定导通。应连接到预定GPIO/PWM，并各加栅源下拉，例如10kΩ～100kΩ；串联几十到几百欧栅极电阻可作为边沿/EMI调整手段。

每路100mA不意味着2N7002K一定不够用，但所选厂家手册只在4.5V和10V栅压下给出导通电阻保证，不能把阈值电压当作3.3V下充分导通的保证。若直接由STM32驱动，应验证3.3V下压降/温升，或选在2.5V/3.3V下明确保证RDS(on)的器件。

如灯串为裸LED，外部还必须有串联限流电阻或恒流电路；本板未见这两路的限流元件。若外接灯组件已有限流，这一点不构成缺失。不能仅靠MOSFET或LED串联数量控制100mA。

来源：[宏嘉诚2N7002K原厂手册，第2页](https://atta.szlcsc.com/upload/public/pdf/source/20250624/D2DB28AB912F465F819D626A88F1E3EB.pdf)。

## 2. 地连接不明确：GND与PGND没有显式低阻连接

逻辑电路、USB、CAN收发器及灯开关源极使用GND；降压器U22、输入负极、VIN接口负极使用PGND。

检查全部元件后，唯一同时出现这两种网络的器件是CN4：

- CN4.1负极=PGND；
- CN4.3附加脚=GND；
- CN4.4附加脚=PGND。

没有看到零欧、net-tie或其他明确共地连接。连接器不同引脚是否内部相通，尤其附加固定脚是否属于电气触点，不能仅凭网表认定。因此本次结论是“共地缺少明确证据”，而非声称已测得断路。

应核对CN4的实物/封装，并在原理图中明确设计GND与PGND的连接。如果没有板上低阻共地，USB拔掉后可能缺少正常电源回流，CAN和灯开关也失去正确参考。不能依赖USB地、CAN保护结构或连接器固定脚的偶然连接。

来源：[AMASS XT30PW-M产品与规格书](https://www.china-amass.net/xt30pw-m-product/)。

## 3. CAN的5V供电被或合二极管压低，存在欠压风险

反馈电阻R16=54.9kΩ、R34=10kΩ，U22典型参考0.765V，因此：

`5V_BUCK ≈ 0.765 × (1 + 54.9 / 10) = 4.96485V`。

实际供电路径为：

- VIN供电：`VIN → U22 → 5V_BUCK → D8 → 5V → 五个TJA1051 / U15`；
- USB供电：`USB VBUS → D9 → 5V → 五个TJA1051 / U15`。

D8/D9均为DSK110。TJA1051的推荐VCC范围为4.5～5.5V，因此不能把二极管前的约5V当作收发器脚上的5V。

仅按U22参考电压下限0.747V计算，二极管前约4.848V，留给D8、走线压降和纹波的总余量只有0.348V，尚未包括反馈电阻误差。网表器件参数给出的DSK110正向压降为850mV@1A，这不代表本板实际电流下必然掉850mV，但也不足以证明有稳定余量。

USB路径也要独立检查：例如VBUS为4.75V时，要保证CAN VCC不低于4.5V，D9和走线合计只能损失0.25V。USB线缆和端口电压变化还会进一步影响实际供电。

建议按两种供电方式分别建立最差情况预算，采用压降更小且可保证的或合方案，例如理想二极管/电源MUX，或合理调整BUCK反馈并校验所有上下限。仅提高BUCK输出不能解决USB单独供电时的同类问题。

来源：[TI LMR16006手册](https://www.ti.com/lit/ds/symlink/lmr16006.pdf)、[NXP TJA1051手册，第8页](https://www.nxp.com/docs/en/data-sheet/TJA1051.pdf)。

## 4. 0.6A降压的电流预算不足以直接覆盖所有峰值工况

U22=LMR16006YDDCR，额定输出600mA。它承担五个CAN收发器，以及经LDO供电的ESP32、STM32和两个MCP2515。两路24V灯从VIN取电，不计入这个5V降压的600mA负载。

可构造如下峰值负载场景：

- ESP32-C3模块手册中高功率802.11b发射工况电流约345mA；
- TJA1051显性发送时典型50mA、最大70mA，五路对应250mA/350mA；
- 前两项已合计约595mA/695mA，尚未加入STM32、两个MCP2515和其他损耗。

这些是指定工况相加的预算示例，并非断言实际一直超载。短时峰值能否由电容承受、平均电流是否允许，取决于无线发射持续时间和CAN活动情况；当前网表不能证明所有允许工况都满足。

建议按实际峰值和余量重新选降压，1A量级可作为重新核算的起点。U15标注1A不代表上游600mA自动获得1A能力。U15的SOT-223散热也需核对，例如输入4.6V、3.3V负载400mA时，LDO耗散约0.52W。

来源：[LMR16006手册](https://www.ti.com/lit/ds/symlink/lmr16006.pdf)、[ESP32-C3-WROOM-02手册，第23页](https://www.espressif.com/sites/default/files/documentation/esp32-c3-wroom-02_datasheet_en.pdf)、[TJA1051手册](https://www.nxp.com/docs/en/data-sheet/TJA1051.pdf)、[TLV1117LV手册](https://www.ti.com/lit/ds/symlink/tlv1117lv.pdf)。

## 5. 两个MCP2515的中断没有接到MCU

当前网络成员：

- INT1：U6.13和上拉R12；
- INT2：U8.13和上拉R14。

没有STM32引脚连接。因此不能使用硬件中断方式通知MCU接收/发送事件；若这是原计划，属于漏线。若明确使用SPI轮询，CAN仍可工作，但两个接收缓冲器对轮询延迟有要求，高负载下更容易溢出。建议将两根INT分别引到合适的MCU输入。

RESET1同时连接U6/U8的RESET和R13/R15两颗10kΩ上拉，没有MCU控制。共用RESET并非接错，两颗上拉等效5kΩ也并非功能错误。MCP2515支持SPI复位，硬件复位由MCU控制属于恢复能力建议，不应与INT漏接混为一谈。

SPI1_CS和SPI2_CS没有看到外部上拉，建议各加约10kΩ，保证MCU复位期间默认不选中。

MCP2515仅支持经典CAN 2.0B、最多8字节数据，不支持CAN FD。五路若要完全相同能力，必须以这两路的能力为约束。

来源：[Microchip MCP2515手册](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Family-Data-Sheet-DS20001801K.pdf)。

## 6. ESP32启动与复位建议补全

U14 EN网络只有模块2脚与R17=10kΩ上拉，没有电容或其他复位控制。建议在EN到GND加约1µF，配合10kΩ形成厂家推荐的上电延迟，必要时预留复位按键/测试点。实际参数仍按电源上升特性确认。

GPIO8/9已有上拉，GPIO9有BOOT按键，USB接GPIO18/19，基本方向合理。模块附近目前C24=4.7µF+C25=100nF，建议结合实际布局补足靠近模块的10µF级储能；同名电源网上其他电容不能自动证明高频去耦位置合适。

来源：[Espressif硬件设计指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/schematic-checklist.html)。

## 7. CAN终端、参考地和防护

- CAN1：R2=120Ω经SW1串接CAN1_H，可选择终端，接法正确。
- CAN2～5：R4/R6/R8/R10各120Ω固定跨接对应H/L，只有这些支路各自位于总线端点时才合适；建议按安装拓扑做可选。
- CN5十根针全为五对H/L，没有地针。并非任何CAN线都必须单独带地，但需要确认设备间已有可靠参考连接且共模在范围内；若为独立供电设备，建议预留参考地或采用隔离方案。
- 没有看到五路接口的CAN专用TVS；24V入口也未见保险、反接和浪涌保护，应结合外部线束与上游保护确定。

## 8. 已核对未发现明显连接错误的部分

- 三路片内FDCAN引脚：CAN1=PA11 RX/PA12 TX；CAN2=PB5 RX/PB6 TX；CAN3=PB3 RX/PA15 TX。
- 两路MCP2515的SPI SCK、MOSI/SI、MISO/SO及TXCAN/RXCAN方向正确。
- MCP2515以3.3V供电、16MHz晶体，不构成其手册规定的频率超限；不要因一些旧模块习惯使用8MHz就判错。SPI时钟应遵守手册10MHz上限。
- USART1：STM32 PA9(TX)接ESP IO5，PA10(RX)接ESP IO4；若ESP固件配置IO5=RX、IO4=TX，方向正确。它们不是模块默认UART0下载脚，需要GPIO映射。
- USB：CC1/CC2各5.1kΩ下拉，正反插D+/D−归并、USBLC6的通道连接及ESP GPIO19(D+)/GPIO18(D−)方向正确。
- D8/D9的二极管或合方向正确，可阻止正常直流情况下两路正电源直接互灌；主要问题是压降及电流预算，而非接反。
- 电阻/电容标注与可提取的器件描述未发现上一块板那类明显冲突。R20/R21/R22/R23仍是通用元件，最终BOM需补齐料号。

来源：[STM32G474手册](https://www.st.com/resource/en/datasheet/stm32g474cb.pdf)、[MCP2515手册](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Family-Data-Sheet-DS20001801K.pdf)、[ESP32-C3模块手册](https://www.espressif.com/sites/default/files/documentation/esp32-c3-wroom-02_datasheet_en.pdf)。

## 9. 次要但需要确认的项目

- X1/X2/X3标称负载电容均12pF，而三组外接电容各为12pF；两颗串联等效是6pF，再加引脚/PCB寄生，不能直接等同12pF负载。按晶体和振荡器实际要求计算与验证，不直接判定必然不起振。
- STM32的NRST未引出，建议预留调试复位能力。
- L2=47µH，网表额定800mA、饱和1A；正常600mA负载下不能仅凭此判为不足，但它的饱和电流低于降压器限流涉及的1.2A以上电流水平，短路/启动时保护配合需检查。U22为2.1MHz的Y版本，电感选择需按该版本重新核算，不能当成700kHz型号。
- 没有从网表获得铜皮、连接器附加脚实际结构、天线禁布区、温升和信号完整性证据，不能据此声明整板制造或运行验收通过。

建议优先顺序：补两路MOS栅极控制 → 明确共地 → 解决CAN供电压降与总电流预算 → 接MCP2515中断/补CS上拉 → 补ESP启动RC → 审核PCB和实测。
