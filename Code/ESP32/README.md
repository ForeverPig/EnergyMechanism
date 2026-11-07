# ESP32与中心STM32串口测试

已于2026-11-07完成烧录及1000/1000次UART往返实测，见测试记录（本地资料：`../../outputs/ESP32与STM32_UART实测_20261107.md`）。

目标为当前中心板U14：ESP32-C3-WROOM-02-N4，4 MB Flash。使用现有VS Code ESP-IDF扩展与原生ESP-IDF 5.4环境；所有实际通信引脚按中心板网表显式配置。项目不依赖PlatformIO。

| 信号 | ESP32 | STM32/接口 | 配置 |
| --- | --- | --- | --- |
| ESP发送 | GPIO4 / UART1 TX | PA10 / USART1 RX | 1 Mbps、8N1、无流控 |
| ESP接收 | GPIO5 / UART1 RX | PA9 / USART1 TX | 同上 |
| USB D− | GPIO18 | R21 → DN → Type-C | 原生USB Serial/JTAG |
| USB D+ | GPIO19 | R20 → DP → Type-C | 下载及测试控制台 |

GPIO4/5使用GPIO矩阵路由，模块RXD/TXD脚没有连接本链路。USB控制台与UART1分开，日志不会混入STM32协议。接线与运行方式依据[官方UART文档](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-reference/peripherals/uart.html)和[USB Serial/JTAG文档](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32c3/api-guides/usb-serial-jtag-console.html)。不启用Wi-Fi或BLE，不实现正式手机/电脑界面。

## 配套STM32固件

使用`Code/Center Controller/build/Debug/Center_Controller.elf`常规中心固件。之前的`CanSelfTest`专用固件没有启动UART业务，不能用于本测试。中心不需要新增协议命令，继续使用[基础协议V1](../PROTOCOL_DEBUG_V1.md)的`CMD_STATUS`。

中心常规固件会维持现有CAN叶片通信，但电机驱动发送仍禁用。这个ESP32程序只发送状态查询，不下发校准、灯效、运动或Flash参数写入命令。

## 测试过程

上电进入READY，等待USB命令，不自动开始测试。通过USB发送以换行结束的ASCII命令：

- `run`：1000次查询，发起间隔至少20 ms，单次超时300 ms；最多累计10次超时即结束为FAIL。
- `status`：JSON状态/结果，含发送、匹配应答、CRC/帧错误、超时、UART错误、STM32错误计数和往返延迟。
- `stop`：停止本轮，返回STOPPED。
- `help`：显示命令。

每次只保留一个待应答请求，序号非0且递增。只有CRC正确、中心状态结构正确、序号匹配的状态包才算成功；中心每500 ms主动发送的seq=0状态不能替代应答。发送交替采用整帧和跨头部分两段（间隔2 ms），验证接收器处理字节流而非依赖单次read边界。

PASS要求1000/1000匹配、零超时、零帧错误、零串口硬件错误，且STM32的UART错误/发送丢包计数不增长、没有检测到STM32复位。计数起始值单独记录，避免把上次运行的历史错误隐藏。完成后不再发查询，可继续通过`status`读取结果或用`run`重测。该负载用于基本链路验证，不是满带宽压力测试。

## 编译、下载、测试

从仓库根目录执行，或打开`Code/EnergyMechanism.code-workspace`使用ESP32任务，也可以单独打开`Code/ESP32`使用ESP-IDF扩展。Setup只激活并检查现有环境，不下载工具或修改全局Python。

本机SDK：`D:/Data/Code/esp/ESP-IDF/v5.4/esp-idf`；工具目录：`D:/Data/Code/esp/.espressif`。脚本已对应该安装；在另一台电脑需调整设置或传入`-IdfPath`、`-ToolsPath`，目标为`esp32c3`。ESP-IDF扩展2.3若要求选择Setup，请选择已有5.4环境；本工程的`ESP32: Build`等任务通过脚本独立选定环境，不依赖已弃用的`idf.espIdfPathWin`设置。

仓库已迁移到纯英文路径`D:/Data/HardWare/EnergyMechanism`。脚本与VS Code ESP-IDF扩展统一使用工程内的`Code/ESP32/build`，`idf.buildPath`设置为相对路径`build`，不再依赖本机临时目录。继续启用Python UTF-8并关闭ccache。常规操作使用下面的脚本或已配置的VS Code任务；原生IDF终端进入`Code/ESP32`，设置`PYTHONUTF8=1`后执行`idf.py build`。以后移动工程时需重新生成构建目录，不能复用记录了旧绝对路径的CMake缓存。

```powershell
& Code/tools/esp32.ps1 -Action Setup
& Code/tools/esp32.ps1 -Action Build
# 明确指定当前USB端口，以下示例COM8需以设备管理器为准。
& Code/tools/esp32.ps1 -Action Flash -Port COM8
& Code/tools/esp32.ps1 -Action Test -Port COM8
```

Build不访问设备，Flash明确写入ESP32，Test通过USB开始一轮硬件通信。UART1的1 Mbps与USB控制台中显示的115200串口设置不是同一链路。测试工具使用ESP-IDF自带的Python/pyserial环境。结果默认保存到`tmp/esp32-uart/run-时间`，可以用`-Output`指定新的目录。脚本构建后将ELF、BIN和MAP复制到工程`build/artifacts/`；下载使用工程`build/`中完整的bootloader、partition table和app文件，不能只把app BIN烧到地址0。

手动使用USB串口终端时，选择本板USB设备端口，发送带LF的命令。程序使用ESP32原生USB下载；无法自动进入下载模式时，可按住板上BOOT（GPIO9）后重新上电，再释放。如果首次写入后仍输出`waiting for download`，应松开BOOT并完整断电重上电，再运行Test；本次实测也经过此步骤。不得同时接USB-TTL驱动STM32 PA10。

测试源码通过`src/debug_protocol_adapter.c`复用`Code/Common/Src/debug_protocol.c`及对应头文件，串口二进制帧与STM32/Python工具一致。适配文件让共享源文件保持稳定的本地编译入口，不复制另一份CRC或帧解析实现。Flash参数保存仍未实现，测试结果只保留在RAM与电脑采集文件中。
