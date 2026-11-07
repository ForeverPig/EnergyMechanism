# EnergyMechanism · 能量机关

RoboMaster能量机关硬件资料、STM32基础调试固件与开发文档。

## 目录

| 目录 | 内容 |
| --- | --- |
| `Code/Fan Blade` | STM32G474叶片控制板工程：压力采样、击打检测与调试通信 |
| `Code/Center Controller` | STM32G474中心控制板工程：五路CAN、叶片监测、C620只读反馈 |
| `Code/Common` | 两板共享协议与通信代码 |
| `Code/Test_EnergyMechanism` | 已有测试工程，作为检测逻辑参考 |
| `Code/tools`、`Code/tests` | Windows编译/烧录脚本、串口工具和主机回归测试 |
| `HardWare` | 叶片、中心控制板网表 |
| `output/led-layout`、`output/cad` | LED布局工具、机械设计与导出资料 |
| `outputs` | 开发大纲、CubeMX配置表、检查记录及设计交付资料 |

根目录保留比赛规则PDF及参考图片。历史布局与导出资料不代表最终灯效拓扑。

## 开始使用

用VS Code打开[双工程工作区](Code/EnergyMechanism.code-workspace)，再选择对应工程的Build任务。

- [编译、烧录入口与硬件调试说明](Code/README_HARDWARE.md)
- [基础调试协议V1](Code/PROTOCOL_DEBUG_V1.md)
- [固件与上位机开发大纲](outputs/能量机关_固件与上位机开发大纲.md)
- [编译检查记录](outputs/基础硬件调试工程_编译检查记录.md)

当前两板Debug构建与主机回归测试通过，尚未烧录或完成新板实测。电机仅接收反馈，不发送驱动命令；灯效、Flash参数保存、电压采样和远程改阈值仍预留。ESP32、正式上位机/微信小程序与完整训练状态机尚未实现。

构建依赖本机STM32CubeG4固件库和ST工具链，安装位置及配置方法见调试说明。Git仓库排除构建产物、临时校验目录、本机工具缓存、崩溃转储和下载依赖；本地这些文件仍保留。
