# 中心 STM32 ↔ ESP32 ↔ BLE / USB：V2 基础调试实现版

版本：2.0 / capability_revision=3；日期：2026-11-07。本文冻结**本期已经实现**的字段和行为。完整产品需求仍见 [Draft 0.2](PROTOCOL_BLE_DRAFT.md)；本文件不代表大小符训练流程已经实现。

## 1. 本期范围与硬件边界

- 中心是设备状态的唯一裁决者；ESP32 负责 3 个 BLE 控制端及 1 个 USB 控制端的独立会话、定向应答、共享状态和压力观察租约。调试阶段开放连接，不配对、不绑定。
- 叶片继续运行 V1 固件；同一固件按中心 CAN1～5 的物理接口编号。CAN1 同时接收 C620 `0x201～0x208`。
- 电机只读。中心发送白名单仍只有 `0x500`，没有电机电流输出 API；运动控制大纲见 [motor_control_plan.h](Center%20Controller/App/Inc/motor_control_plan.h)。
- **没有电机反馈就是失联，绝不作为停转。** 首次尚无任何 C620 反馈时 `motor_known=0,motor_valid=0,stopped=0`。已经见过的电机均须在最近 1000 ms 内有反馈，且原始转子转速绝对值不超过 10 rpm、连续满足至少 500 ms，才置 `stopped=1`。这些是可调技术初值，不是已验收的机械停止参数。
- ENTER_DEBUG 只允许在已确认停转时进入 HIT_TEST；本期不能执行“先主动减速再进入”。运动中或失联返回 NOT_STOPPED。STOP/EXIT_DEBUG 也不发送制动电流；满足停转条件且无校准进行中时，仅清除观察并进入 IDLE。缺反馈时仍可查询状态/叶片、接收诊断击打和电机遥测。
- HIT_TEST 中一旦停转条件失效，回到 IDLE、撤销观察；已接受校准报告 FAILED。这不代表给电机发送了停车命令。
- 大小符、颜色/方向设置、连续训练、运动、比赛增益/成绩判定、可靠 CAN 击打、事件回放、阈值、电压、Flash 保存均未开放。START/PATCH_SETTINGS/EVENT_ACK/REPLAY_CURRENT 返回 UNSUPPORTED。不生成虚假的训练结果。
- 单片压力观察约 20 Hz。HIT 是 V1 的诊断事件，可能丢失、重复，不能用于权威比赛成绩；EVENT 通道里的 MOTOR 也是可降级遥测。
- 中心 V1 STATUS 查询保留，V1 STREAM/CALIBRATE 入口返回 UNSUPPORTED，防止绕过 V2 门禁。叶片直连串口 V1 工具没有改动。

## 2. GATT 与帧

服务：`af510000-7b3d-4b28-9f02-6c6d454d0001`。下面各 UUID 使用相同后缀。

| UUID 前段 | 名称 | 属性 |
| --- | --- | --- |
| af510001 | INFO | Read，16 字节 |
| af510002 | COMMAND | Write with response，每片至多 20 字节 |
| af510003 | CONTROL | Notify，定向结果、STATE、SESSION、LEAF |
| af510004 | EVENT | Notify，诊断 HIT / MOTOR |
| af510005 | PRESSURE | Notify，压力快照 |

广播名 `EnergyMechanism-XXXX`；身份以 SESSION 的 device_id 为准。先订阅 CONTROL、EVENT，再 HELLO；观察前还需订阅 PRESSURE。订阅通知本身不创建观察租约。每个 GATT 写成功仅表示这一分片被接收。

capability_revision=2 增加业务会话重建约定：**取消 CONTROL 通知订阅会结束该业务会话**，清除其请求缓存、序号高水位、分片、待发消息和观察 token，并分配新的 client_session；其他物理连接不受影响。已经转发到中心的操作仍可完成，但旧会话的迟到结果不会发给新会话。全局观察退出仍按现有租约流程处理。

Windows 关闭 GATT 客户端后可能保留底层 BLE 连接，因此新客户端不能只靠“关闭后重连”将序号从 1 开始。启动全新业务会话时应先确保 CONTROL 通知关闭，再启用 CONTROL/EVENT 并 HELLO。Bleak 工具先用临时空监听器 start_notify(CONTROL)，再 stop_notify(CONTROL)，随后安装正式监听器；使用平台通知 API，不直接写 CCCD。正常退出先取消 PRESSURE/EVENT，最后取消 CONTROL。同一电脑的多个进程若共享同一底层 BLE 连接，会相互替换业务会话；三端容量指三个独立 BLE 连接。

INFO：`magic[4]="EMB2", major u8=2, minor u8=0, max_frame u16=194, max_clients u8=3, flags u8, reserved u16=0, capability_revision u32=3`。flags bit0=中心 V2 链路在线，bit1=ESP 协议服务就绪，bit2=USB V2 会话可用。BLE 上限仍为 3，不计 USB。

完整 BLE 帧：

| 偏移 | 字段 |
| --- | --- |
| 0 | A5 5A |
| 2 | version u8=2 |
| 3 | type u8 |
| 4 | payload_len u16，0～184 |
| 6 | request_seq u16，请求及定向回复非零，异步消息为 0 |
| 8 | payload |
| 8+N | CRC16 u16，小端，覆盖偏移 2 起的 6+N 字节 |

全部多字节字段小端。CRC16/CCITT-FALSE：poly=0x1021、init=0xFFFF、refin/refout=false、xorout=0；`"123456789" → 0x29B1`。

分片：`transfer_id u16, index u8, count u8, data[1..16]`。count=1～13，index 从 0 起，非末片必须有 16 字节数据。完整帧最大 194 字节。按连接与特征值独立重组，整包最长 1000 ms；错序、缺片、越界、CRC 错误丢弃并计数。新的首片替换未完成旧包。同一特征值不能交织两帧；重试用新 transfer_id，保留原 request_seq 和原载荷。

UART：1 Mbps / 8N1。V2 请求及非零 seq 应答在上述载荷前增加 `gateway_epoch u64`；UART 载荷最大 192 字节。异步 seq=0 无此前缀。ESP 按路由表替换 seq，收回应答时验证 epoch、去掉前缀、恢复客户端 seq。

## 3. 身份、容量与完成语义

- device_id：ESP32 出厂 MAC 6 字节 + 6 个零；gateway_epoch：ESP32 启动时随机 64 位，UART 序号耗尽后换代。
- client_session：本次 ESP 启动内单调分配的 64 位号，与 gateway_epoch 一起标识业务会话。新物理连接或取消 CONTROL 订阅都会分配新号；复用 BLE handle 不会复用业务会话。
- center_boot：STM32 硬件 RNG 生成的 64 位启动标识；丢弃首个随机字并检查连续值及错误状态。使用现有 PLLQ=40 MHz，无新增 GPIO/DMA/IRQ，不写 Flash。随机源故障时 boot=0，V2 保持 NOT_READY，不能用 UID/uptime 冒充新代次。
- BLE request_seq 在一个业务会话中非零、递增且不复用。65535 后重建业务会话并重新 HELLO；重新连接的客户端也必须遵循上述订阅流程。ESP 的 UART seq 同样不复用；耗尽且映射清空后换 gateway_epoch、断开 BLE 要求重新握手，不重发旧动作。
- 同号同内容：重放缓存；同号不同内容：REQUEST_CONFLICT。缓存淘汰后的旧号返回 REPLAY_EXPIRED，不再次执行。缓存已满而不能存储拒绝结果时也推进高水位，后续同号返回 REPLAY_EXPIRED。
- 中心保留 8 个请求，每个最多 6 条应答；进行中校准和未发送应答不被淘汰。ESP 每连接保留 4 个请求结果、最多 2 个进行中请求；总 UART 映射 8 个，含 1 个内部维护槽。
- 中心保留最近 8 个退休 gateway_epoch，拒绝其重新握手。此为有界 UART 残帧隔离，不是防恶意重放的认证。
- UART 同号最多发送 3 次、间隔 500 ms。6 秒无终态给出 UNKNOWN，保留映射至 15 秒接收迟到终态；之后通过已有缓存查询。中心链路超过 1500 ms 无有效消息转为离线并通知控制端。中心重启清除旧启动的快照缓存，未决操作为 UNKNOWN，不能推断未执行。
- GET_STATE 以 STATE 收尾；HELLO 以 SESSION 收尾；GET_LEAVES 以全部 LEAF + RESULT 收尾。UART 缺任一叶片条目时不会提前转发成功收尾，会同号重试补齐。
- 校准的 ACCEPTED 表示中心开始下发；COMPLETED/READY 才是完成。CAN 校准同号最多 3 次，间隔 300 ms；V1 叶片短期去重。收到匹配 ACK 后，须再收到更新的心跳，状态 READY 才成功；UNSTABLE/采样故障为 FAILED，5 秒无明确终态为 UNKNOWN。中心启动后先等待 2100 ms，避开仍运行叶片的旧 V1 校准缓存。
- QUERY_REQUEST 只查当前业务会话缓存，不重新执行；校准进行中可返回 ACCEPTED，查询本身到此结束。缓存缺失返回 UNKNOWN/CACHE_GAP。不提供长期历史。

## 4. 请求

type=`0x01 REQUEST`，BLE 载荷：`opcode u8, expected_center_boot u64, expected_control_revision u32, args[]`，固定头 13 字节。

只读/HELLO 可将 expected 字段置零。修改命令必须匹配 center_boot；ENTER_DEBUG、EXIT_DEBUG、OBSERVE_ENTER、CALIBRATE 必须同时匹配 control_revision。STOP、续租、退出观察不检查旧 revision，但仍检查 boot 和观察 token。控制状态改变才推进 control_revision；普通遥测不推进它。没有额外操作死区，冲突修改由 revision 拒绝；每连接每秒最多 20 个新请求。

| opcode | 名称 | args | 完成/约束 |
| --- | --- | --- | --- |
| 0x01 | HELLO | client_kind u8、min_version u8、max_version u8 | 必须覆盖 2；client_kind 仅记录协议占位。UART 内部 HELLO 无 args，返回 STATE |
| 0x02 | GET_STATE | 无 | STATE |
| 0x03 | GET_LEAVES | slot u8，0 全部或 1～5 | LEAF + RESULT |
| 0x12 | STOP | 无 | 已停转时清除观察、进入 IDLE；不驱动电机 |
| 0x13 | ENTER_DEBUG | 无 | 已停转时进入 HIT_TEST，不自动观察 |
| 0x14 | EXIT_DEBUG | 无 | 已停转时进入 IDLE，不恢复运行 |
| 0x20 | OBSERVE_ENTER | slot u8=1～5 | HIT_TEST、停转、目标在线；切换全局选择，返回本连接 token |
| 0x21 | OBSERVE_RENEW | token u32 | 仅续本连接，不携带叶片号 |
| 0x22 | OBSERVE_LEAVE | token u32 | 退出本连接，最近一次退出 token 可重复退出 |
| 0x23 | CALIBRATE | slot u8=1～5 | HIT_TEST、停转、在线、无另一校准，先 ACCEPTED 后终态 |
| 0x32 | QUERY_REQUEST | original_seq u16 | 已缓存 RESULT/STATE/SESSION；RESULT 的 opcode 改为 QUERY，detail 填原 opcode，operation_id 保留 |

其他 opcode 返回 UNSUPPORTED。校准进行中，STOP/ENTER_DEBUG/EXIT_DEBUG 返回 BUSY；本期没有取消 V1 叶片校准命令。

观察租约：每个连接 3000 ms，建议每 1000 ms 续租。一个控制端成功选择新叶片后，全部现有观察者跟随该片，各自 token 不变。最后一个观察者退出/超时/断连后，ESP 发送全局退出；中心上游租约 1500 ms，ESP 约每 500 ms 续租。中心再以原 CAN V1 机制维护叶片 1500 ms 租约。所有超时都撤销压力转发，不停止叶片高速采样；蓝牙断开不触发电机动作。

## 5. 返回载荷布局

下面偏移均相对于 **BLE payload**（UART 定向消息先去掉 8 字节 epoch）。所有 reserved 字节必须填 0。

### RESULT，type=0x02，36 字节

| 偏移 | 字段 |
| --- | --- |
| 0 / 1 / 2 / 3 | opcode u8 / phase u8 / code u8 / detail u8 |
| 4 | center_boot u64 |
| 12 | control_revision u32 |
| 16 | attempt_id u32，本期固定 0 |
| 20 | operation_id u32，中心 UART 请求号；本地结果为 BLE 请求号，不能跨会话单独使用 |
| 24 | observer_token u32，仅 OBSERVE_ENTER 成功有效 |
| 28 | selection_epoch u32 |
| 32 / 33 | mode u8 / selected_slot u8 |
| 34～35 | reserved |

phase：0 ACCEPTED、1 COMPLETED、2 REJECTED、3 FAILED、4 UNKNOWN。detail：校准终态填叶片状态；QUERY 填原 opcode；其余为零。UART OBSERVE_ENTER 的 token 是全局 selection_epoch，ESP 替换成对应连接的 token。

code 按顺序从 0 到 16：OK、BAD_ARGUMENT、UNSUPPORTED、NOT_READY、WRONG_MODE、NOT_STOPPED、LEAF_OFFLINE、BUSY、RATE_LIMIT、STALE_STATE、ALREADY_RUNNING、TIMEOUT、LINK_DOWN、REQUEST_CONFLICT、REPLAY_EXPIRED、CACHE_GAP、FAULT_ACTIVE。

### SESSION，type=0x03，56 字节

| 偏移 | 字段 |
| --- | --- |
| 0 | device_id[12] |
| 12 / 20 / 28 | gateway_epoch u64 / client_session u64 / center_boot u64 |
| 36 / 40 | capabilities u32 / control_revision u32 |
| 44 / 45 | center_link_online u8 / max_clients u8=3 |
| 46 | max_ble_payload u16=184 |
| 48 / 49 / 50 / 51 | BLE 协议版本 2 / 中心协议版本 2（尚未获知为 0）/ 兼容叶片协议版本 1 / reserved |
| 52 | observer_lease_ms u32=3000 |

HELLO 的 SESSION 非零 seq、只给发起端；中心链路变化时用 seq=0 分别通知各连接。中心离线时 capabilities=0；在线为 `0x7F`：bit0 STATE、bit1 LEAVES、bit2 DEBUG、bit3 PRESSURE、bit4 CALIBRATE、bit5 MOTOR_READ、bit6 DIAGNOSTIC_HIT。未定义 bit 保留零。

### STATE，type=0x10，48 字节

| 偏移 | 字段 |
| --- | --- |
| 0 | center_boot u64 |
| 8 / 12 / 16 | control_revision u32 / state_revision u32 / center_time_ms u32 |
| 20 / 21 / 22 / 23 | mode u8 / selected_slot u8 / online_leaf_mask u8 / can_ready_mask u8 |
| 24 / 25 / 26 / 27 | stopped u8 / motor_valid_mask u8 / motor_moving_mask u8 / motor_known_mask u8 |
| 28 / 32 | selection_epoch u32 / capabilities u32 |
| 36 / 40 / 44 | uart_errors u32 / uart_tx_drops u32 / upstream_lease_remaining_ms u32 |

mode：0 IDLE、5 HIT_TEST。slot=0 表示未选择。叶片 mask bit0～4 对应 CAN1～5；电机 mask bit0～7 对应 C620 ID1～8。motor_known 是本次启动见过的电机集合；部分失联时 valid 与 known 不同。STATE 约每 500 ms 广播。state_revision 是状态发布序号；LEAF 批次引用最近发布值。

### LEAF，type=0x11，44 字节

| 偏移 | 字段 |
| --- | --- |
| 0 / 8 | center_boot u64 / state_revision u32 |
| 12 / 13 / 14 / 15 | slot u8 / online u8 / calibration_status u8 / uid_valid u8 |
| 16 | heartbeat_age_ms u32，未见过为 0xFFFFFFFF |
| 20 / 22 / 24 | adc_errors u16 / adc_drops u16 / can_rx_drops u32 |
| 28 | uid[12] |
| 40 / 42 / 43 | batch_id u16（UART 查询号）/ batch_count u8 / source_protocol u8=1 |

校准状态低 7 位：0 RUNNING、1 READY、2 UNSTABLE；bit7 采样故障。批次识别必须同时使用当前会话和查询 seq，不把 batch_id 作为跨启动唯一号。

### PRESSURE，type=0x12，125 字节

`boot u64 @0, selection_epoch u32 @8, slot u8 @12, snapshot_seq u32 @13, center_rx_ms u32 @17, leaf_sample_ms u32 @21, values[10] @25`。

每个 value 10 字节：`ring u8, state u8, raw u16, baseline u16, delta i16, peak u16`。使用旧 V1 完整 20 CAN 帧拼接结果。旧 V1 没有 leaf_boot，不声称具备可靠跨叶片重启的帧身份。

### HIT，type=0x13，28 字节；MOTOR，type=0x14，22 字节

HIT：`boot u64 @0, diagnostic_event_seq u32 @8, slot u8 @12, source_protocol u8=1 @13, leaf_seq u8 @14, ring u8 @15, peak u16 @16, leaf_sample_ms u32 @18, center_rx_ms u32 @22, reserved u16 @26`。诊断序号只支持发现显示缺口；不实现 ACK、回放、成绩判定。

MOTOR：`boot u64 @0, motor_id u8 @8, temperature u8 @9, angle_raw u16 @10, rotor_rpm i16 @12, current_raw i16 @14, age_ms u32 @16, online u8 @20, reserved u8 @21`。每 100 ms 轮询一个 ID，见过的 ID 最慢约 800 ms 上报一次；不能据此推断机关轴转速或电源电压。

## 6. 调度、实现入口与验证限制

- STM32 主循环每轮最多取 4 条 UART 帧、每个 CAN 端口最多取 8 帧；中心缓存应答每轮最多发送 4 条，压力只保留最新完整快照。UART DMA TX 环 8 槽（实际可排 7 条），满时保留尚未提交的控制回复，下轮重试。
- ESP UART TX 队列 8 条，用非阻塞 FIFO 写入；BLE 回调只做有界解析/排队。每连接 CONTROL 12 条、EVENT 有效容量 4 条；STATE 合并为最新，压力保留一条待发快照和一条正在分片的帧。CONTROL 队满断开该慢端；诊断 EVENT 溢出计数后丢弃。
- BLE 通知在 NimBLE 主机事件队列每 5 ms 调度，每端每通道至多一片。该位置与连接/断连回调串行，避免旧连接分片误送复用 handle；三端共享无线带宽，实际吞吐仍需实测。
- 中心 PB4/LED1 为高有效输出，每 500 ms 由主循环翻转，完整亮灭周期 1 秒。无新定时器和 DMA。PA3/PA4 外接灯输出保持关闭。
- 代码：`Common/*/comm_v2.*`、`Center Controller/App/*/center_service.*`、`ESP32/src/gateway_core.*`、`ESP32/src/gateway_main.c`。
- [Windows BLE 命令行](tools/ble_debug.py) 支持扫描、状态、叶片、debug/idle、watch/leave、cal、query。详情见 [ESP32 README](ESP32/README.md)。
- 主机测试覆盖 CRC/分片/超时、3 端路由、revision 冲突、停止门禁、租约、校准接受与终态、中心去重、V1 READY 重试 ACK、缺帧补齐、重启、handle 复用、缓存淘汰和序号耗尽。
- 2026-11-07 已完成中心/ESP32 烧录校验、Windows BLE 扫描与 3 轮顺序连接会话、206 次 UART V2 只读查询和 PB4 运行灯验证，见 [实板记录](HARDWARE_V2_TEST.md)。三控制端同时在线、微信端、20 Hz 压力与真实校准仍需联调；原有 CAN/UART V1 实测不能替代这些项目。

SDK 依据：[ESP-IDF 5.4 NimBLE](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32c3/api-reference/bluetooth/nimble/index.html)、[NimBLE GATT API](https://mynewt.apache.org/latest/network/ble_hs/ble_gatts.html)、[Bleak 客户端 API](https://bleak.readthedocs.io/en/latest/api/client.html)。本机实际编译使用已安装 ESP-IDF 5.4。

初版构建与主机验证见 [验证记录](VALIDATION_V2.md)；重连修复后的固件校验值与硬件结果见 [实板记录](HARDWARE_V2_TEST.md)。

## 7. USB V2 网关扩展（capability_revision=3）

Type-C 连接 ESP32-C3 原生 USB Serial/JTAG（GPIO18/19），由 ESP32 路由至中心 UART1。USB 只在 Gateway 固件中承载此协议；独立 UartTest 固件保留原有文本接口。应用运行期关闭 USB 文本日志；启动时 ROM/bootloader 文字由帧解析器跳过。

USB 使用上文完整 V2 帧头、CRC 和小端字段，**不使用 BLE 分片**。USB payload 最大 192 字节、帧最大 202 字节；所有 USB 消息 payload 最前方均为 `usb_nonce u64`。它与 ESP↔中心的 `gateway_epoch` 独立，ESP 不把 USB nonce 转发给中心。

| type | seq | payload | 用途 |
| --- | --- | --- | --- |
| 0x70 OPEN | 0 | nonce u64 | 建立 USB 业务会话，nonce 为客户端生成的非零随机 64 位值 |
| 0x71 OPEN_ACK | 0 | nonce u64 + INFO[16] | 确认 USB 会话；INFO 的 max_frame/max_clients 仍描述 BLE |
| 0x72 PING | 0 | nonce u64 | 客户端每 1000 ms 保活 |
| 0x73 PONG | 0 | nonce u64 | ESP 心跳应答 |
| 0x74 CLOSE | 0 | nonce u64 | 退出本 USB 会话，无应答 |
| 既有业务 type | 原业务 seq | nonce u64 + 原 BLE payload | 原业务字段、返回阶段、错误码保持一致 |

- 收到 OPEN_ACK 后发送正常 HELLO；USB 的 CONTROL/EVENT/PRESSURE 通道在会话建立时可用，仍须 OBSERVE_ENTER 成功才能转发压力。
- 重复当前 nonce 的 OPEN 只重发 ACK，不重置业务会话。新 nonce 替换旧 USB 会话并清空该端缓存、待发包和观察 token。最近 8 个退休 nonce 的 OPEN 被丢弃；此机制是有界残帧隔离，不是认证。
- 仅当前 nonce 的合法 PING 或业务请求刷新存活时间；3000 ms 未收到则退休该会话。USB 接线或 SOF 存在不代表上位机仍在运行。
- 关闭 USB、USB 队列溢出、心跳超时只影响本端；BLE 栈重置也只断开 BLE 端。任一端退出不触发电机操作。慢 USB 主机不会阻塞 UART/BLE。
- USB 复用原每端 4 条请求历史、2 个进行中请求和观察租约；全局 UART 映射仍为 8 槽（含 1 维护槽），容量不足明确 BUSY。BLE 三端与 USB 一端权限相同，中心按 boot/revision 仲裁。
- USB 输出采用有限队列和非阻塞部分写入；控制、事件和压力轮询公平发送。旧会话残留字节不能被新 nonce 接受。连续字节之间超过 100 ms 的半帧会被丢弃，随后同号请求重试。
- 桌面端支持同号同内容有限重试，不跨业务会话重放控制动作。重连只恢复状态查询，压力观察需再次明确开启。USB 打开/关闭不执行 DTR/RTS 下载复位序列。
- Windows/Linux 桌面程序见 [DesktopHost](DesktopHost/README.md)。中心固件的只读电机白名单及 UART 帧布局没有因 USB 扩展改变。
