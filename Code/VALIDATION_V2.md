# V2 通信与调试验证记录

日期：2026-11-07。基于仓库 `34b07f9` 后的工作区改动；下文保留初版烧录前的构建与主机验证结果。随后已完成烧录、Windows BLE/UART V2 和运行灯实测，最终固件校验值及验证范围见 [实板记录](HARDWARE_V2_TEST.md)。

## 初版构建（烧录前）

| 工程/配置 | 结果 | 占用 |
| --- | --- | --- |
| 中心 STM32 Debug | 通过 | Flash 29,400 B；RAM 22,056 B |
| 叶片 STM32 Debug 回归 | 通过 | Flash 31,188 B；RAM 13,992 B |
| ESP32 Gateway / ESP-IDF 5.4 | 通过 | app bin 553,872 B；1 MiB app 分区剩余约 47% |
| ESP32 UartTest / ESP-IDF 5.4 | 通过 | app bin 212,416 B；1 MiB app 分区剩余约 80% |

应用 C 代码开启警告检查；构建日志没有编译 warning/error。两份 ESP32 配置使用独立构建目录，VS Code 默认任务为 Gateway。中心生成文件仅新增 LED 引脚定义/初始化及 USER CODE 调用。

本地产物 SHA256：

- `Center_Controller.bin`：`3009bfac8477bb846b2eafda4f26e7bfb397ec0eaf9e551ea0b2c82d5d9d1970`
- `energy_esp32_gateway.bin`：`c49da8d1392af0b55eb414fa87bec7c83de3a053428110e8a75302196ab2f3e4`
- `energy_esp32_uart_test.bin`：`3f8c3bc648d76aadc55b8d2a10e0ca47a5435f1a8912e4af96869721eb02c436`

构建产物和日志受 Git 忽略规则保护，仅保留本地。

## 主机测试

`Code/tests/run_tests.ps1`：7 个 C 测试程序、8 项 Python 测试通过。

- V2 CRC、最大载荷、13 片边界、错序、截断、超时、计时回绕、V1/V2 兼容。
- 三控制端同号路由、revision 冲突、断线 handle 复用、慢端队列满隔离。
- 状态/叶片、缺一条 LEAF 后同号补齐、GET_LEAVES 收尾。
- 观察选择共享、两级网关/中心租约、最后观察者离开后撤销、诊断事件广播。
- 无有效电机反馈拒绝进入调试；模拟有效零速反馈后允许进入；失联退出。
- 校准启动等待、ACCEPTED 与最终 READY 分离、中心和网关重复请求、V1 重试 ACK 已经 READY、结果查询。
- 中心重启、缓存淘汰、UART 序号耗尽换代。
- 原有全部 CAN ID 发送限制、MCP2515、ADC 4/4/2 打包、击打检测和 V1 工具回归。

`Code/ESP32/tests`：原 UART 结果校验的 3 项 Python 测试通过。新 BLE CLI 的 `--help`、编解码和模拟客户端重试通过；没有实际扫描、连接或驱动 BLE 硬件。

## 引脚、时钟、DMA 静态检查

- IOC 声明 30 个唯一引脚，与 PinsNb 一致；仅新增 PB4 STATUS_LED，已有信号分配保持一致。
- PB4 对应网表 LED1，经 R23 接阳极，阴极接 GND；高有效。主循环每 500 ms 翻转，不使用定时器或 DMA。
- 原 UART RX=DMA1_Channel1（Circular），TX=DMA1_Channel2（Normal），无重复通道；原 DMA 参数完全一致。
- SYSCLK 保持 160 MHz，PLLQ 保持 40 MHz。新增启动标识使用 RNG，CLK48 选择 PLLQ，IOC 的 RNG/CLK48 频率同步到 40 MHz；应用自行初始化 RNG，无新增 IRQ/DMA。
- STM32 USB 外设当前未启用，ESP32 原生 USB 独立。以后若启用 STM32 USB，需重新核对它与 RNG 共用的 CLK48 源。
- PB4 心跳之外，PA3/PA4 外接灯输出继续关闭。电机 CAN TX 白名单仍只允许叶片 `0x500`，未引入 C620 驱动发送。
- `git diff --check`、VS Code JSON 和文档 UTF-8 检查通过。

## 后续实板验证

上述初版构建阶段没有访问硬件。获得烧录授权后，同日完成中心及 ESP32 写入校验；发现并修复 Windows 快速重连导致旧请求序号残留的问题，INFO capability_revision 升为 2。新增会话重建/迟到应答隔离主机回归后，7 个 C 测试程序和 8 项 Python 测试再次通过。

最终实板结果为 UART V2 查询 206/206、Windows BLE 3 轮顺序业务会话和 PB4 闪灯通过。ESP32 最终 app 为 554,032 B；这里上表与旧 SHA256 保留为初版记录，**烧录后的最终 SHA256 以 [实板记录](HARDWARE_V2_TEST.md) 为准**。三端同时在线、压力 20 Hz、真实叶片校准和手机后台行为仍未验收。之前 CAN / UART V1 的实测成绩不可替代这些验证。

电机缺反馈按失联处理；本版不会主动驱动或制动电机，不能用 STOP 应答代替真实运动控制验收。灯效、用户参数 Flash 保存、大小符运动及可靠 CAN 击打升级继续预留。

使用说明：[V2 实现协议](PROTOCOL_V2.md)、[ESP32 与 Windows BLE 工具](ESP32/README.md)。
