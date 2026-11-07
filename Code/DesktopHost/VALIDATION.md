# 桌面上位机与 USB 网关验收记录

日期：2026-11-07。应用 0.1.0，Qt 6.8.3；Windows MSVC 2022，Linux Ubuntu 22.04 / GCC 11 基线。

## 交付

本机目录：`outputs/DesktopHost/`，成品包包含 Qt、必要平台插件、运行库、EnergyUI 中文字体、许可证、BUILDINFO.json 和 SHA256SUMS。

| 包 | 压缩后约 MB | 解压后约 MB | 启动 |
| --- | ---: | ---: | --- |
| EnergyMechanismHost-0.1.0-windows-x64.zip | 15.45 | 35.14 | 解压整个目录，运行 EnergyMechanismHost.exe |
| EnergyMechanismHost-0.1.0-linux-x64.tar.gz | 37.79 | 98.51 | 用 tar 解压，运行 AppRun，保留执行权限 |

MB 按 1,000,000 字节计。精确字节数和压缩包 SHA256 见同目录的 `windows-package.json` / `linux-package.json`。本机 Windows 包使用工具集 14.39 和 app-local 运行库 14.44.35211；CI 使用其 VS 2022 工具集构建独立的同版本 Windows 包，因此两份 ZIP 的校验值不相同。

构建源版本为 `76d0956e3f2372acdd1cb92791bdc82a4cb1d9f8`，独立分支 `codex/desktop-host-20261107`；[自动构建与目标系统检查](https://github.com/ForeverPig/EnergyMechanism/actions/runs/42379003910)。本记录之后的纯文档提交不改变已验证的程序。原本地分支、索引与既有未提交固件修改保持原状。

## 协议和软件测试

- Windows / Linux 的 CTest 包括：Qt 协议与状态模型、11 个模拟界面场景、C 实现的 USB 四端并发，共 3 组。
- Python 独立生成测试向量，C/C++ 共用实际固件编解码器，验证 CRC、USB 拆包/粘包、启动杂讯、帧超时、BLE 分片/缺片/乱序、重复快照。
- 覆盖请求同号重试、校准受理与终态、中心重启、旧应答隔离、自动只重连原设备、不恢复观察或重放控制、共享观察切换与各端租约。
- C USB 集成测试验证 3 BLE + 1 USB 独立路由、重复请求、nonce 替换及过期、部分写入、一个端退出不撤销其他端租约。
- 原有 `Code/tests/run_tests.ps1` 的 8 个 C 测试程序及 8 个 Python 用例通过，包含中心仲裁、校准、击打、ADC 4/4/2、CAN 电机发送白名单和 MCP2515 队列。
- Windows 带符号的 RelWithDebInfo 构建成功，生成应用 PDB；VS Code Windows/Linux 调试入口使用带符号目录，便携包仅取 Release。

## 界面和便携性

模拟截图覆盖首页、调试、十环压力、30 秒曲线、校准受理/完成、击打记录、900×600 紧凑布局、故障和离线。标准视口 1366×768，Windows 125% / 150% / 200% 缩放下均能完成截图；已检查 Windows 和 Linux 中文显示、布局及禁用状态。

Windows 从中文含空格目录启动，PATH 仅保留 Windows 系统目录。实际加载的 Qt/MSVC 模块全部来自便携目录，报告为 `validation/windows-runtime-modules.json`。这不能替代真实未安装开发环境的 Windows 10/11 纯净系统验收，后者尚未进行。

Ubuntu 22.04 与 24.04 使用同一份 Ubuntu 22.04 构建的 tar.gz。独立容器仅装系统图形/X11 测试组件，不安装 Qt SDK、Python、编译器或 .NET；执行时 `--network none --user 1000:1000`，从中文含空格路径运行，校验文件哈希及可执行权限，分别验证 offscreen 与 X11/Xvfb 启动。它属于隔离运行环境验证，不等同于真实 Ubuntu Desktop 的蓝牙/USB 或 Wayland 硬件验收。

## Windows 实板

中心 STM32 保持原 V2 只读固件；ESP32 已通过原 `Code/tools/esp32.ps1` 烧录 Gateway USB 扩展，仍为 ESP-IDF 5.4 / esptool 4.8.1，未引入独立 esptool 版本。

烧录的 ESP32 BIN 为 559232 字节，SHA256：`CCE3736E028408EC718C9A64BA7D16D088C28735E3001CE3B2BABDDC6657E84E`。本次烧录后直接启动；此前曾遇到的下载复位问题不能据此视为所有环境已解决。

| 测试 | 结果与证据 |
| --- | --- |
| USB + 一个独立 BLE 端，同时只读 25 秒 | 两端通过；无重试、无意外断连、UART 错误与丢帧增量为 0 |
| USB 进程被强制结束，等待超时后重新启动 | USB 新会话恢复；BLE 45 秒观察持续在线，UART 计数无新增 |
| 交付通信实现再次并发查询 20 秒 | USB / BLE 分别收到 39 / 35 个新鲜状态检查点，检查周期 500 ms；两端控制请求计数为 0 |
| 连接、退出及重新连接是否复位 | 各轮中心 boot 和网关 epoch 保持一致；未观察到设备复位 |

原始报告：`outputs/DesktopHost/validation/windows-*-concurrent.json`、`windows-usb-after-crash.json`、`windows-ble-during-usb-crash.json`、`final-windows-usb.json`、`final-windows-ble.json`。最后的禁用按钮配色修正没有改变通信代码。

这些测试仅发送 HELLO、GET_STATE、GET_LEAVES 和连接保活。`control_requests=0` 是客户端实际请求计数，不能当作示波器/CAN 分析仪测量。电机只读由中心 CAN 发送 ID 白名单及代码范围继续约束。

## 尚需现场验收

- Windows 10/11 纯净系统断网启动；Ubuntu Desktop 22.04/24.04 真机 USB、BLE、串口权限和 Wayland。
- 三台真实 BLE 控制端加 USB 的四端并发、无线慢端与长时间压力吞吐；现有四端证据属于协议模拟。
- 物理拔插与供电边界。已测进程异常退出和会话重连，未把它称为物理拔线测试。
- 真实五叶片的 20 Hz 压力、击打环数与校准受理/最终结果；当前未接 C620，不能绕过停转门禁进行观察或校准。

训练设置、大小符运动、灯效、电压采样、机关轴换算、Flash 保存及微信小程序保留接口或禁用入口。本期只有最近 500 条内存诊断击打记录，不实现训练成绩历史。
