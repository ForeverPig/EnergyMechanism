# 基础硬件调试协议 V1

状态：本次硬件调试暂定协议，已实现两块STM32及Python命令行工具；后续ESP32和界面可据调试结果修订。不是正式训练状态协议。不包含大小符启停、颜色/灯效、Flash保存、阈值写入、电压采样和电机驱动命令。

除C620反馈的原始帧为大端外，本协议所有多字节整数均为小端。CAN均使用11位标准ID、经典数据帧、8字节DLC。CAN1与C620共线，调试ID避开`0x200/0x1FF/0x201～0x208`。

## CAN：每条线上一块叶片

中心依物理端口编号1～5。五片共用固件和ID；叶片不保存编号，也不需要逐片编译。UID用于核对接线，不能在同一物理总线上接多片使用本协议的叶片。

| ID | 方向 | 字节0～7 |
| --- | --- | --- |
| `0x500` | 中心→叶片 | version=1、请求seq、opcode、value、0、0、0、0 |
| `0x501` | 叶片→中心 | version=1、请求seq、opcode、result、status、0、0、0 |
| `0x510` | 叶片→中心 | version=1、status、ADC错误u16、DMA丢块u16、启动秒数u16 |
| `0x512` | 叶片→中心 | version=1、part=0/1、UID的6字节；两片共12字节 |
| `0x513` | 叶片→中心 | 击打seq u8、ring u8、peak u16、样本时刻ms u32 |
| `0x520+ch` | 叶片→中心 | 快照seq u8、state u8、raw u16、baseline u16、delta i16 |
| `0x530+ch` | 叶片→中心 | 快照seq u8、ring u8、peak u16、样本时刻ms u32 |

`ch=0～9`，对应ADC扫描顺序，环号为`6,7,8,9,10,5,4,3,1,2`。UID字节按`UIDw0/UIDw1/UIDw2`的小端存储顺序发送，约每秒两片，中心只接受part0之后300 ms内到达的part1。

`status`低7位是校准状态：0进行中、1READY、2不稳定；bit7表示采样故障。每路`state`：0校准、1空闲、2候选、3等待释放、4锁定。raw/baseline/peak单位均为ADC计数。

命令opcode：2设置压力流，value=0停止/1开启；3重新校准，value必须为0。压力流租约1500 ms，中心对选中叶片每500 ms刷新开启命令，并向其余叶片发送关闭命令。没有观察请求时仍有心跳和击打事件。心跳200 ms，中心1000 ms未收到该片心跳则标离线。

result：0接受、1参数错误、2不可用/离线、3忙、4超时、5未实现。校准ACK表示重新启动采样/校准成功，不能当作基线已经READY。中心以相同CAN seq重试，间隔300 ms，最多3次；叶片对两秒内重复校准命令返回原结果，不再次复位检测器。

一次快照包含20帧。中心必须凑齐同一seq下所有ch的A/B片，所有B片的时间戳必须相同；100 ms超时、seq变化或时间不一致时丢弃未完成快照。叶片每50 ms冻结一次快照，发送未完成则换新一轮；不在发送过程中读取会变化的传感器数组。

CAN依赖链路层CRC，调试遥测不逐帧重发。计数/事件seq用于观察丢失；击打队列最多15条等待发送，不承诺断线历史回补。压力快照不是高速原始波形录制。

FDCAN采用40 MHz内核时钟、Prescaler=2、Seg1=15、Seg2=4，1 Mbps、80%采样点。MCP2515采用16 MHz振荡器、CNF1=00、CNF2=98、CNF3=01，1 Mbps、75%采样点；其SPI收发、过滤和RXRTR处理依据[Microchip MCP2515数据手册DS20001801K](https://www.microchip.com/content/dam/mchp/documents/APID/ProductDocuments/DataSheets/MCP2515-Family-Data-Sheet-DS20001801K.pdf)。双方位速率一致，采样点不必相同，实际接线时序仍需上板验证。

## UART二进制帧

叶片USART2：115200/8N1。中心USART1：1000000/8N1。无硬件流控，不依赖一次DMA接收/串口read对应一帧。

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 2 | `A5 5A` |
| 2 | 1 | version=1 |
| 3 | 1 | type |
| 4 | 2 | payload长度N，最大192 |
| 6 | 2 | 请求序号seq；异步遥测通常为0 |
| 8 | N | payload |
| 8+N | 2 | CRC16/CCITT-FALSE，小端保存；poly=1021、init=FFFF、无反射、xorout=0；覆盖偏移2起的6+N字节 |

接收器对非法版本/长度/CRC逐字节重同步；字节间隔超过100 ms丢弃残帧。UART使用512字节循环RX DMA及TC圈数记录、8槽TX DMA队列（可排队7帧）；主循环不等待串口发送，溢出有计数。软件调试时避免长时间阻塞主循环。

### 请求与应答

- `type=0x01`：payload固定3字节`[opcode,port,value]`。
- `opcode=1`：查询状态，port/value均为0。
- `opcode=2`：观察压力，value=1开启/0停止。中心port=1～5，直接连接叶片port=0。中心停止可用port=0,value=0。
- `opcode=3`：重新校准，value=0；中心port=1～5，直接连接叶片port=0。
- `type=0x02`：payload为`[opcode,port,result]`，seq对应请求。压力选择ACK表示中心已接受选择，实际数据还取决于叶片在线与流命令到达。
- 未实现命令明确返回5。状态查询返回`0x10`状态包，不再另外发送ACK。

### 遥测格式

**0x10 叶片状态，28字节：** `[role=1,status,can_ready,bus_off]`，随后6个u32：uptime_ms、adc_errors、dma_drops、uart_errors、uart_tx_drops、can_event_drops。

**0x10 中心状态，140字节：** 头20字节为`[role=2,selected_leaf,motor_write_enabled=0,can_ready_mask]`，随后4个u32：uptime_ms、uart_errors、uart_tx_drops、TIM7中断计数。之后5个端口记录，各24字节：`port u8, online u8, status u8, uid_valid u8, age_ms u16, adc_errors u16, dma_drops u16, can_rx_drops u16, uid[12]`。age_ms饱和到65535；在线与控制器ready是不同字段。

**0x11 CAN事件，12字节：** `port u8,id u16,dlc u8,data[8]`。中心转发击打；直接连接的叶片还通过此包报告心跳。port=0为本地叶片。参考上面的CAN表解释data。

**0x12 完整压力快照，106字节：** `port u8,snapshot_seq u8,sample_ms u32`；随后10个传感器记录，各10字节：`ring u8,state u8,raw u16,baseline u16,delta i16,peak u16`。

**0x14 C620只读反馈，14字节：** `motor_id u8,temp_C u8,angle_raw u16,rotor_rpm i16,current_raw i16,age_ms u32,online u8,motor_write_enabled=0 u8`。电流原值未经安培换算；转速是电机转子反馈，不是机关轴转速；角度原值范围通常0～8191。未收到过的电调不产生此包，收到后约500 ms汇报一次，超过1000 ms显示离线。

## 后续接口边界

BLE桥接必须透明保留UART帧边界/长度/CRC语义，不能假定一个BLE通知装得下一帧。多控制端裁决、正式状态同步、断线后的训练继续等需在正式协议层实现。本版中心串口只有一个请求来源，不实现多人控制租约。

叶片和中心均提供`ParameterStorage_Available()=false`及关闭灯输出的接口。后续加入阈值、Flash、电压或正式灯拓扑时，需要新增明确能力/命令和版本处理，不能悄悄改变本版字段含义。
