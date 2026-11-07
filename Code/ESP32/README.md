# ESP32 BLE / USB 网关与 UART 测试

默认构建为 V2 网关，支持 3 个 BLE 控制端加 1 个 USB 控制端，BLE 为调试阶段开放连接。协议与完整字段见 [V2 实现版](../PROTOCOL_V2.md)。早期 BLE 验证见 [实板记录](../HARDWARE_V2_TEST.md)，桌面客户端与 USB 扩展验证见 [桌面验收记录](../DesktopHost/VALIDATION.md)。真实三 BLE 端同时在线尚未实测。

目标：中心板 U14，ESP32-C3-WROOM-02-N4（4 MB），ESP-IDF 5.4 / NimBLE，不使用 PlatformIO。

| 信号 | ESP32 | 中心 STM32/接口 |
| --- | --- | --- |
| UART1 TX | GPIO4 | PA10 / USART1 RX |
| UART1 RX | GPIO5 | PA9 / USART1 TX |
| USB D− / D+ | GPIO18 / GPIO19 | 原生 USB Serial/JTAG |

UART 固定 1 Mbps、8N1、无流控。USB Serial/JTAG 运行期用于完整 V2 二进制帧，Gateway 在应用启动后关闭文本日志；ROM/启动阶段杂讯由上位机重新同步丢弃。USB 使用随机会话 nonce、1 秒心跳、3 秒失效，独立于 BLE 会话。中心必须使用 V2 常规固件；CanSelfTest 专用固件没有 UART 服务。

## 构建与 VS Code

在仓库根目录执行：

```powershell
& Code/tools/esp32.ps1 -Action Setup
& Code/tools/esp32.ps1 -Action Build
& Code/tools/firmware.ps1 -Project Center -Action Build
```

默认生成 `Code/ESP32/build-gateway/energy_esp32_gateway.bin`、ELF/MAP，副本在该构建目录的 `artifacts/`。VS Code 默认 Build、FLASH 任务对应网关，ESP-IDF 扩展的 buildPath/sdkconfig 路径也指向 build-gateway。

本机 SDK 为 `D:/Data/Code/esp/ESP-IDF/v5.4/esp-idf`，工具为 `D:/Data/Code/esp/.espressif`；换机器用 `-IdfPath`、`-ToolsPath` 指定。脚本开启 Python UTF-8、关闭 ccache。构建目录不可跨绝对路径搬移复用。

下载入口仍单独提供，**只有明确执行 Flash 才下载**；这里的 COMx 是占位，需替换实际 ESP USB 端口：

```powershell
& Code/tools/esp32.ps1 -Action Flash -Port COMx
```

NVS 仅用于 SDK 初始化，未增加用户阈值/成绩/参数 Flash 保存。NVS 初始化错误直接报告，不自动整区擦除。BLE 不启用绑定持久化。最大连接数 3，控制器活动数 4，保留一份广播活动供新控制端发现。

## Windows 命令行

此工具用于调试协议，不是正式电脑界面。依赖单独安装到所选 Python 环境：

```powershell
python -m pip install -r Code/tools/requirements-ble.txt
python Code/tools/ble_debug.py --scan
python Code/tools/ble_debug.py --address AA:BB:CC:DD:EE:FF
```

扫描只读；连接后工具先重建 CONTROL 订阅对应的业务会话，再订阅通知并发 HELLO，不自动进入测试、不校准、不驱动电机。工具要求 INFO capability_revision >= 2，以兼容 Windows 保留底层连接的情况。使用 `--quiet` 可隐藏主动遥测。同一电脑共享同一 BLE 连接的多个程序会相互替换业务会话，不能当作多个独立控制端。

| 输入 | 功能 |
| --- | --- |
| state | 全量状态，含 C620 在线/停止确认标记 |
| leaves 或 leaves 3 | 所有叶片或指定叶片信息 |
| debug | 请求进入 HIT_TEST；需中心确认电机已停 |
| watch 3 | 观察叶片 3，自动每秒续租，十路快照控制台最多打印 2 Hz |
| leave | 取消本端观察，不影响其他观察端 |
| cal 3 | 重新校准叶片 3；等待最终结果 |
| idle / stop | 已停转时清除观察并进入 IDLE，不向电机发送制动电流 |
| query 8 | 查询本连接请求 8 的已有结果 |
| quit | 尽力退出观察并断开 |

电机未接/无反馈显示失联，`debug/cal/watch` 不会绕过停转检查。仍可读取叶片和接收诊断击打。另一控制端切换观察叶片时，所有观察者跟随中心的全局选择。串口工具 `debug_monitor.py` 对中心只保留 V1 status；叶片直接串口仍支持原工具。

请求超时显示 UNKNOWN；先 query 原序号，不要用新号反复执行校准。会话内请求号耗尽需重新连接。手机/微信正式 UI 本期没有新增，也未声称 Android/iPhone 实测兼容。

## V2 只读实板测试

先扫描确认目标地址，并开启 Windows 蓝牙。在仓库根目录执行以下命令；地址是占位，输出需使用新的目录：

```powershell
python Code/tools/ble_v2_test.py --address AA:BB:CC:DD:EE:FF --output tmp/ble-v2-check --count 200 --reconnects 2
```

该工具只允许 HELLO、GET_STATE、GET_LEAVES；不进入调试、不校准、不驱动电机，也不切换设备固件。默认包含 200 次循环查询、前后状态查询和 2 次重连，共 206 次经过 UART 的查询、3 次本地 HELLO。记录 `report.json` 与逐请求 `transactions.jsonl`，检查完整叶片批次、会话更换、设备身份连续性、重试/重组错误和中心 UART 错误计数增量。重复使用已有 report 目录会被拒绝，以保留旧结果。

当前这块实测板广播名为 `EnergyMechanism-2A60`；连接/重连是顺序进行，不能代替三端同时在线测试。正常结束会取消通知并关闭 GATT 客户端，但 Windows 可能仍保留物理链路。

如果烧录校验成功后只看到 `DOWNLOAD...waiting for download`，先确认 BOOT 已松开，再完整断电重上电。该板本次首次下载曾遇到此现象，完整重上电后恢复应用启动；不能仅凭 Flash 校验成功认定 BLE 应用已运行。

## 保留旧 UART 专用测试

`src/main.c` 保留原 1000 次 V1 STATUS 往返测试。显式选择后构建到独立目录：

```powershell
& Code/tools/esp32.ps1 -Action Build -Firmware UartTest
& Code/tools/esp32.ps1 -Action Flash -Firmware UartTest -Port COMx
& Code/tools/esp32.ps1 -Action Test -Firmware UartTest -Port COMx
```

固件为 `build-uart-test/energy_esp32_uart_test.bin`，不提供 BLE 网关。USB ASCII 命令仍为 `run / status / stop / help`，上电等待 run，不自动测试。VS Code 新增独立的 Build UART test / FLASH UART test 任务。普通 Test 动作只运行主机测试脚本，不切换设备上的固件。

2026-11-07 先前 UART 实板验证为 1000/1000 匹配、CRC/序号检查通过。本地原始记录：`../../outputs/ESP32与STM32_UART实测_20261107.md`。该旧记录不代表本次 BLE 固件已验收。

## 验证

```powershell
& Code/tests/run_tests.ps1
python -m unittest discover -s Code/ESP32/tests -p 'test_*.py' -v
```

覆盖共享 V1/V2 编码、分片、路由、重试、校准终态、压力租约、重启/断线以及原 UART 结果分析。新增 CONTROL 退订重建会话及旧应答隔离回归。实板已完成 Windows 扫描/顺序重连、UART V2、中心非零启动标识与板载 LED 验证，见 [实板记录](../HARDWARE_V2_TEST.md)。三端同时在线、20 Hz 压力、真实校准和手机后台行为尚未验收。
