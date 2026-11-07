# EnergyMechanism · 能量机关

RoboMaster能量机关硬件资料、STM32基础调试固件与开发文档。

## 目录

| 目录 | 内容 |
| --- | --- |
| `Code/Fan Blade` | STM32G474叶片控制板工程：压力采样、击打检测与调试通信 |
| `Code/Center Controller` | STM32G474中心控制板工程：五路CAN、叶片监测、C620只读反馈 |
| `Code/Common` | 两板共享协议与通信代码 |
| `Code/ESP32` | ESP-IDF 5.4工程：ESP32与中心STM32的UART测试及USB控制台 |
| `Code/Test_EnergyMechanism` | 已有测试工程，作为检测逻辑参考 |
| `Code/tools`、`Code/tests` | Windows编译/烧录脚本、串口工具和主机回归测试 |
| `HardWare` | 叶片、中心控制板网表 |
| `output/led-layout`、`output/cad`（仅本地） | LED布局工具、机械设计与导出资料 |
| `outputs`（仅本地） | 开发大纲、CubeMX配置表、检查记录及设计交付资料 |

`output/`、`outputs/`、`tmp/`、`.easyeda/`及根目录比赛PDF、三张参考图片仅保留在本地，已被Git忽略，不随GitHub仓库分发。历史布局与导出资料不代表最终灯效拓扑。

## 开始使用

本机仓库路径为`D:/Data/HardWare/EnergyMechanism`。用VS Code打开[固件工作区](Code/EnergyMechanism.code-workspace)，再选择叶片、中心板或ESP32对应的Build任务。工作区与脚本使用相对路径；更换仓库位置后需重新生成构建目录。

- [编译、烧录入口与硬件调试说明](Code/README_HARDWARE.md)
- [基础调试协议V1](Code/PROTOCOL_DEBUG_V1.md)
- [ESP32蓝牙与上位机通信规范草案（需求与V2提案，尚未实现）](Code/PROTOCOL_BLE_DRAFT.md)
- 固件与上位机开发大纲（本地资料：`outputs/能量机关_固件与上位机开发大纲.md`）
- 编译检查记录（本地资料：`outputs/基础硬件调试工程_编译检查记录.md`）

当前两板Debug构建与主机回归测试通过；中心板五路CAN已完成外部连接实测（本地资料：`outputs/中心控制板_CAN实测_20261107.md`），8个方向各成功1000帧；ESP32与中心STM32完成UART实测（本地资料：`outputs/ESP32与STM32_UART实测_20261107.md`），1000/1000次查询应答成功。叶片和其余功能尚未实板验收。电机仅接收反馈，不发送驱动命令；灯效、Flash参数保存、电压采样和远程改阈值仍预留。ESP32 BLE桥接、正式上位机/微信小程序与完整训练状态机尚未实现。

构建依赖本机STM32CubeG4固件库和ST工具链，安装位置及配置方法见调试说明。Git仓库排除构建产物、临时校验目录、本机工具缓存、崩溃转储和下载依赖；本地这些文件仍保留。
