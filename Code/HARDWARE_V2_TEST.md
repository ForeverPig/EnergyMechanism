# 中心板与 ESP32：UART V2 / BLE / 运行灯实测

日期：2026-11-07。工作区基于 `34b07f9` 后的 V2 实现；本记录对应已经烧录的最终固件，包含 Windows 快速重连修复。测试范围为用户授权的中心板/ESP32 烧录、UART V2、蓝牙连接和中心板运行灯；电机保持只读。

## 结果

| 项目 | 实测结果 | 证据与范围 |
| --- | --- | --- |
| 中心 STM32 写入 | 通过 | Flash 写入、读取校验、复位成功；最终热连接读取显示 Core is running |
| ESP32-C3 写入 | 通过 | bootloader、分区表、最终 Gateway app 均校验成功；应用广播与 GATT 可访问 |
| UART V2 | 206/206 查询成功 | 经 Windows BLE → ESP32 → 1 Mbps UART → STM32 往返；184 次 GET_STATE、22 次 GET_LEAVES |
| BLE | 通过 | Windows 扫描、完整服务发现、通知分片、3 次 HELLO 和 3 轮顺序 GATT/业务会话；含 2 次快速重连 |
| 数据完整性 | 通过 | 0 客户端请求重试、0 通知重组错误、0 非预期定向回复；每次全叶片查询均收到 5 条 LEAF 和 COMPLETED |
| UART 计数 | 测试中无新增 | uart_errors 2→2、uart_tx_drops 0→0；结尾 SWD 读取 RX overrun=0、parser errors=0 |
| PB4 / LED1 | 通过 | 24 次运行中读取均为输出，IDR/ODR 一致；观察到高低两态，15 次翻转间隔均为 500 ms；用户现场确认正常闪烁 |
| 电机限制 | 保持 | 测试命令仅 HELLO / GET_STATE / GET_LEAVES；中心 CAN TX 白名单仅叶片命令 0x500，没有电机驱动发送 |

本轮 200 次循环查询，加上前后状态和重连检查共计 206 次 UART 查询；3 次 HELLO 由网关本地返回，未计入 UART 查询数。请求到完整应答的端到端延迟：最小 65.527 ms、平均 213.291 ms、最大 422.925 ms，包含 Windows/BLE 通知分片和调度，不能当作 UART 线速延迟。

首末中心运行时间 871,024→945,561 ms，中心启动标识与 ESP 网关代次在本轮保持不变，三轮 client_session 分别为 10、12、14。这里验证的是顺序业务会话，**不是三个设备同时连接**，也不据此声称每次 Windows 关闭客户端都断开了物理 BLE 链路。

UART errors=2 在最终测试开始时已经存在，最终测试期间没有增长；不将累计计数表述为零，也不由本轮结果推断早先两次错误的具体原因。

## 设备与最终产物

- 中心 STM32：ST-Link `55FF6E065282505130441767`，目标 ID `0x469`，128 KiB Flash，探针读取电压 3.25 V。普通 Debug 固件，`CENTER_CAN_SELFTEST=OFF`。
- ESP32-C3 rev v0.4：USB `COM8`，出厂 MAC `90:DA:72:9E:2A:60`；BLE 地址 `90:DA:72:9E:2A:62`，广播名 `EnergyMechanism-2A60`。
- GATT 服务 `af510000-7b3d-4b28-9f02-6c6d454d0001`，INFO `454d42320200c2000303000002000000`，capability_revision=2。
- Windows 客户端使用隔离 Python 环境内的 Bleak 3.0.2；ESP-IDF 5.4 / NimBLE。

| 产物 | 大小 | SHA256 |
| --- | --- | --- |
| `Center Controller/build/Debug/Center_Controller.bin` | 29,400 B | `3009bfac8477bb846b2eafda4f26e7bfb397ec0eaf9e551ea0b2c82d5d9d1970` |
| `ESP32/build-gateway/energy_esp32_gateway.bin` | 554,032 B | `f5c1a91250d53d6471cc08054cb58b5ec86fe6a953b04589b0ececaec63ecfd4` |

中心 RAM 为 22,056 B。ESP 烧录器日志中的 app 写入长度为 555,008 B（写入对齐后的长度），上表是最终 bin 的实际文件大小。中心固件在本轮重连修复中未改动；ESP32 重新构建、写入和校验后进行了全部最终验证。

## 测试发现与处理

1. 首次 ESP 写入校验成功后，ROM 显示 `DOWNLOAD...waiting for download`，未进入网关应用。用户松开 BOOT 并完整断电重上电后恢复正常广播与 UART 握手。后续修复版写入后自动启动正常。
2. Windows 蓝牙无线电最初关闭，测试时已开启。扫描后按目标地址连接，核对 INFO 和全部五个特征值，再进行业务请求。
3. 首版连续 200 次查询成功，但快速重连的 HELLO 序号 1 被判为 REPLAY_EXPIRED。Windows GATT 客户端关闭后底层 BLE 连接可能继续存在，服务器仍保留旧请求高水位。修复为 CONTROL 通知退订时结束业务会话，客户端重新开启前显式先关闭该通知；网关为新业务会话分配新号，旧 UART 映射按旧会话隔离迟到结果。详见 [实现协议](PROTOCOL_V2.md)。
4. 增加主机回归验证同一 BLE handle 重建会话、序号从 1 重新开始、旧校准结果不串入新会话、其他端不受影响；7 个 C 程序和 8 项 Python 测试通过。最终固件先通过 16 次 UART 查询/3 轮会话的短测，再通过本记录的 206 次完整测试。

## 当前遥测与未覆盖范围

- 中心为 IDLE，未选择叶片。五个叶片均报告离线；查询返回五个正确槽位不代表叶片通信已经通过。
- C620 的 motor_known/motor_valid/motor_moving 均为 0，stopped=0；缺反馈继续按失联处理，没有绕过停转门禁。未进行真实电机反馈、驱动或制动测试。
- can_ready_mask=0x17：CAN1/2/3/5 就绪，**CAN4 未就绪**。本轮未做 CAN 排障或重复五路总线验收，不能用旧版 CAN 实测结果覆盖这个当前状态。
- PA3/PA4 外接灯输出均为低电平。灯效、用户参数 Flash 保存仍预留。
- 未实测三台控制端同时在线、微信/Android/iPhone、手机后台恢复、真实叶片击打、20 Hz 压力和重新校准。相应主机模拟测试不替代实板验收。
- 测试结束已取消通知并关闭测试客户端，没有驻留测试进程；Windows 可能继续维持底层 BLE 链路。中心和 ESP32 保持最终固件运行。

## 复现与本地证据

只读实测工具 [ble_v2_test.py](tools/ble_v2_test.py)，在安装 [BLE 依赖](tools/requirements-ble.txt) 后从仓库根目录运行；重复测试请更换输出目录：

```powershell
python Code/tools/ble_v2_test.py --address 90:DA:72:9E:2A:62 --output tmp/ble-v2-check --count 200 --reconnects 2
```

原始日志保留在仓库本地 `tmp/v2-hardware/`，受 Git 忽略规则保护：

- `center-flash.log`、`esp32-flash.log`、`esp32-flash-reconnect-fix.log`：下载与校验。
- `esp32-boot.log`、`ble-scan-after-powercycle.json`、`first-connection.json`：首次启动恢复与握手。
- `led-report.json`、`led-00.log`～`led-23.log`：PB4 运行中采样；现场实际发光由用户确认。
- `ble-uart-run-1/`：初版重连失败；`ble-uart-run-2/`：客户端直接写 CCCD 被 Bleak 拒绝，之后改用通知 API；保留失败记录。
- `ble-uart-run-3/report.json`：最终固件短测通过。
- `ble-uart-final/report.json`、`ble-uart-final/transactions.jsonl`：最终完整测试 PASS 和 209 条请求记录。
- `center-final-readback.log`：末次热连接读取，Core is running、UART RX/TX/解析计数与引脚状态。
