# 能量机关桌面上位机

C++20 / Qt 6.8.3 Widgets；Windows 10 1809+/11 x64、Ubuntu Desktop 22.04/24.04 x64。

## 使用

Windows 解压整个便携包，运行 `EnergyMechanismHost.exe`。Ubuntu 解压后运行 `AppRun` 或 `EnergyMechanismHost`；首次必要时允许文件执行。必须保留随包的库、plugins 和 resources 文件夹。

USB 使用中心板 Type-C → ESP32 原生 USB → STM32，须烧录本仓库增加 USB V2 的 Gateway 固件。BLE 扫描 `EnergyMechanism-XXXX`。每个应用使用一种传输；网关最多同时接入 3 个独立 BLE 连接及 1 个 USB 控制端。关闭应用只释放连接/观察租约。

Ubuntu 依赖正常桌面系统和系统蓝牙服务。USB 无权限时，将当前用户加入 `dialout` 组并重新登录：`sudo usermod -a -G dialout "$USER"`。应用不自动提权、不安装系统包。不支持 Ubuntu Server 无桌面环境。

首页同步状态；调试页确认停转后进入击打测试；压力页选择叶片并开始观察，退出页面停止本端租约。所有观察者共享当前叶片。校准“已受理”不等于完成，以最终就绪结果为准。结果未知时可在调试页查询最近操作；缓存缺失不能推定未执行。

电机始终只读。电机没有反馈时显示失联，不能进入测试、压力观察或校准。电压、机关轴估算转速、训练设置、灯效及 Flash 持久化尚未开放。记录是最多 500 条的内存诊断列表，关闭清空，不是训练成绩。

`--demo` 为明确标识的模拟预览，不打开硬件。`--smoke-test <目录>` 生成模拟截图。`--probe-usb COM8 --report result.json --seconds 15` 或 `--probe-ble <地址>` 只查询状态并生成验证报告；Ubuntu 串口可用 `/dev/ttyACM0`。

## 开发与打包

Windows：Qt 6.8.3 MSVC 2022、Visual Studio 2022 C++ 工作负载、CMake 3.31.6。执行 `scripts/build.ps1 -Action Test -QtRoot <Qt目录>`，`-Action Package` 输出便携包。

Linux：在 Ubuntu 22.04 基线运行 `scripts/build-linux.sh <Qt目录>`。包内附带必要应用库，不携带系统 glibc 或显卡驱动。在 22.04/24.04 的干净桌面环境另做启动、蓝牙与 USB 实机验收。

Qt 动态链接分发；许可证及字体来源见 `resources` 与成品包 `licenses`。无自动更新或联网下载运行时依赖。V2 业务协议与 USB 会话说明见上级目录 PROTOCOL_V2.md。
