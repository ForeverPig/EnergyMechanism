# 能量机关基础硬件调试工程

本期实现两块STM32的基础调试固件、ESP32 V2 BLE网关及独立UART测试配置。**电机只读反馈，不发送驱动指令。** 2026-11-07完成中心板五路CAN实测（本地资料：`../outputs/中心控制板_CAN实测_20261107.md`），随后换回常规中心固件并完成ESP32 UART实测（本地资料：`../outputs/ESP32与STM32_UART实测_20261107.md`）。叶片与其余功能尚未实板验收。新增的[V2通信/调试](PROTOCOL_V2.md)已完成烧录及部分实板验证：206 次 UART V2 只读查询、Windows BLE 顺序连接/重连和 PB4 运行灯通过，见[实板记录](HARDWARE_V2_TEST.md)。三端同时在线、压力与校准尚未实测。Windows正式界面、微信小程序与大小符训练状态机尚未实现。

## 工程与入口

| 路径 | 内容 |
| --- | --- |
| `Fan Blade/` | 叶片工程，五块叶片烧录同一固件 |
| `Center Controller/` | 中心工程，按CAN1～5物理端口标识叶片 |
| `Common/` | 两工程共享的串口帧协议、FDCAN收发、压力快照编解码 |
| `tools/firmware.ps1` | 明确分开的Configure、Build、Flash入口 |
| `tools/debug_monitor.py` | Windows串口命令行观察/校准工具，不带正式界面，不自动导出CSV |
| `tools/center_can_selftest.py` | 通过ST-Link执行中心板独立CAN互测；需显式`--flash`才写入测试固件 |
| `tests/` | 主机回归及ADC/MCP2515模拟HAL测试 |
| `PROTOCOL_DEBUG_V1.md` / `PROTOCOL_V2.md` | 叶片V1与中心/ESP32 V2实现协议 |
| `tools/ble_debug.py` | Windows BLE调试命令行，支持状态、观察与校准 |
| `tools/ble_v2_test.py` | 只读BLE/UART V2连续查询及重连实测，保存JSON记录 |

CubeMX生成代码在各工程`Core`目录，应用在`App`目录；`main.c`只在USER CODE区域调用`BoardApp_Init/Process`。`Test_EnergyMechanism`仅作为参考，未修改。

## 已实现功能

### 叶片

- ADC1/2/3保留4/4/2扫描，TIM6以10 kHz触发，DMA半块为16帧。三个独立队列按块序号组合，ADC3按两路紧密步长读取。
- IRQ只复制采样块；主循环运行击打检测。丢块、回调次序异常或ADC错误会停止使用不完整数据，约1秒后重新启动采样并重新建立基线，错误计数保留。
- 检测内核由参考工程移植并与采样/通信解耦，保留三点中值、500 ms稳定校准、基线跟踪、起始差值80、最低峰值160、上升量30、释放差值40、脉宽3～600点、稳定释放20点、组窗口30点及锁定800点。多环峰值相同时选高环；分组仍保持参考代码的“候选全部释放后结算”行为，没有改成固定3 ms截断。
- 新板软件环号映射：`{6,7,8,9,10,5,4,3,1,2}`；映射位于`App/Src/board_app.c`，阈值初值位于`App/Inc/detector_defaults.h`。最终以逐环实测为准。
- FDCAN2心跳、UID、击打事件及单叶片20 Hz压力快照；USART2也可直接观察同样的检测数据并重新校准。
- PA12 `PWM_OE`持续保持Low；PWM通道不启动。Flash能力接口返回false，未分配参数页、未增加写入逻辑；远程改阈值及电压采样仍未实现。

### 中心

- CAN1/2/3分别为FDCAN1/2/3；CAN4为SPI2+MCP2515，CAN5为SPI1+MCP2515。MCP2515按16 MHz、经典CAN 1 Mbps配置；SPI为5 MHz、Mode 0。
- 叶片200 ms心跳、1000 ms失联判定，中心约500 ms上报状态；一次只转发选中叶片的完整压力快照。
- 重新校准命令有ACK、300 ms重试间隔和最多3次尝试；叶片对同一校准请求短期去重。ACK表示采样/校准已启动，是否完成要继续看校准状态变为READY。
- 接收CAN1的C620 `0x201～0x208`反馈，显示转子rpm、编码器角度原值、电流原值、温度、数据年龄及在线状态。不推断机关轴rpm，不输出不存在的母线电压数据。
- 中心FDCAN和MCP2515的发送入口只允许`0x500`叶片命令，不存在电机电流控制API。**本版不能主动制动由其他设备驱动的电机，也未实现训练过程的失联停车状态机。**
- TIM7只提供1 kHz调度计数诊断；PB4板载LED1由主循环每500 ms翻转。PA3/PA4外接灯输出保持Low，无灯效、无Flash参数保存。
- V2模式门禁要求有效C620停止反馈；无电机数据按失联处理，不能进入HIT_TEST。未实现主动停车；运动代码大纲见`App/Inc/motor_control_plan.h`。中心V1修改命令已关闭，状态查询仍保留。
- 详细BLE连接、调试命令及新旧ESP32固件构建入口见[ESP32说明](ESP32/README.md)。

## 编译与VS Code

用VS Code打开`EnergyMechanism.code-workspace`，或单独打开某一工程文件夹。安装ST的STM32Cube扩展及CMake Tools后，执行对应工程的默认Build任务。多根工作区需要明确选择Leaf或Center任务。

本机依赖：STM32CubeMX 6.14.1、STM32CubeG4 V1.6.3、GNU Arm GCC 14.3.1、CMake/Ninja（ST VS Code bundles）。固件库当前位于`D:/Data/STM32Cube/Repository/STM32Cube_FW_G4_V1.6.3`；生成的HAL/CMSIS引用此路径。搬移工程时须安装该固件包并调整CubeMX固件路径。`Common`与`tools`必须与两工程一起保留。

从本仓库根目录执行：

```powershell
& '.\Code\tools\firmware.ps1' -Project Leaf -Action Build
& '.\Code\tools\firmware.ps1' -Project Center -Action Build
```

默认Debug，使用`-Og -g3`；可加`-Configuration Release`。脚本优先使用`CUBE_BUNDLE_PATH`，否则使用当前用户`LOCALAPPDATA/stm32cube/bundles`。终端若使用了隔离的LOCALAPPDATA，可显式指定：

```powershell
$env:CUBE_BUNDLE_PATH='C:\Users\shuto\AppData\Local\stm32cube\bundles'
```

产物分别为`build/Debug/Fan_Blade.elf/.hex/.bin/.map`、`build/Debug/Center_Controller.elf/.hex/.bin/.map`。工程目录/IOC名称仍保留空格，CMake目标使用下划线，避免CubeMX生成的项目名被误解析成多个参数。

两工程栈预留从1 KiB增加为4 KiB，IOC与链接脚本同步；Flash布局仍完整128 KiB，没有划参数区。应用与共享代码开启`-Wall -Wextra -Wpedantic -Werror`。

### 烧录入口

VS Code的`Flash Leaf/Center`任务会要求ST-LINK序列号，避免在多个探针存在时默认选择第一块板。调试启动配置名称明确含`FLASH + debug`，F5同样会写入固件。**普通Build任务不会运行烧录器或访问探针。**

未来需要烧录时，才运行下面的命令，并替换成目标板探针的实际序列号：

```powershell
& '.\Code\tools\firmware.ps1' -Project Leaf -Action Flash -Serial '实际STLINK序列号'
& '.\Code\tools\firmware.ps1' -Project Center -Action Flash -Serial '实际STLINK序列号'
```

新板CN2线序为3.3V / SWDIO / SWCLK / GND，不能直接套用旧测试板的四芯线序。本版Flash任务使用SWD普通连接，调试配置使用软件复位；没有假定NRST已经接到探针。

## 串口调试

先安装命令行工具依赖：

```powershell
python -m pip install -r '.\Code\tools\requirements.txt'
```

- 叶片：3.3V USB-TTL接USART2，PB3是STM32 TX、PA15是STM32 RX，共地，115200/8N1。新板没有测试工程的USB CDC。
- 中心：USART1，PA9为STM32 TX、PA10为STM32 RX，1000000/8N1。Type-C数据接ESP32；默认[ESP32程序](ESP32/README.md)为V2 BLE网关，另保留独立UartTest配置。USB不是通用二进制透传桥；原`debug_monitor.py`不能直接连接该USB控制台。使用USB-TTL时需避免它与ESP32的TX同时驱动PA10。
- 工具只在显式指定`--port`后打开串口；`debug_monitor.py`尚未用于本板实测，旧版ESP32 UART验收使用独立的`esp32_uart_test.py`，V2网关验收使用`ble_v2_test.py`。

```powershell
# 单叶片：观察十路数据和击打，0表示直接连接的本片
python '.\Code\tools\debug_monitor.py' --board leaf --port COM5 --watch 0

# 中心当前固件：V1 串口工具仅查询状态；观察/校准使用 BLE V2 工具
python Code/tools/debug_monitor.py --board center --port COM6
```

输出为可读JSON行，包含完整十路raw/baseline/delta/peak/state及击打事件；`--quiet-snapshots`可隐藏压力刷屏，`--duration 10`可观察10秒后退出。没有CSV导出，也没有正式训练成绩统计界面。

## 首次实板检查顺序

1. 单叶片确认上电校准能到READY、压力空闲稳定；依次击打1～10环核对映射。检查DMA丢块/ADC错误计数是否增长。
2. 单叶片接中心CAN1，再逐步接入CAN2/3/4/5，检查物理端口与UID对应。总线两端终端电阻及收发器供电按实际接线核对。
3. 选中一片观察20 Hz快照、击打事件、重新校准；拔掉该片应显示离线，缺片数据不得继续显示为新数据。
4. 对CAN4/5观察MCP初始化ready位及接收丢帧计数；对CAN1观察C620只读反馈。本期不测试电机转动。
5. 最后持续运行检查采样、UART、CAN错误计数。灯带、Flash、阈值远程写入和电压采样等后续接入后另做验收。

MCP2515初始化失败或SPI故障会将该端口标为未就绪；本期没有在线重新初始化命令，排查供电/接线后复位中心板。CAN状态是诊断信息，不能代替示波器/逻辑分析仪及实板总线检查。

## 回归测试

本机需要MinGW `gcc`和Python；从仓库根目录运行：

```powershell
& '.\Code\tests\run_tests.ps1'
```

覆盖检测判据及新映射、ADC3实际采样模块的两路DMA步长、不同ADC回调到达次序、丢回调/队列溢出/部分启动失败、串口CRC与分包恢复、快照分片缺失/跨帧混合、全部2048个标准CAN ID的中心发送白名单、C620有符号解析、实际MCP驱动在模拟寄存器上的收发与故障路径，以及Python工具解码。

上述回归是主机模拟和目标交叉编译证据；五路CAN的独立实测证据与边界见前述实测记录。共享FDCAN驱动已新增Queue/FIFO发送可用性回归，修复Queue模式下TFFL为0导致发送被误拦截的问题。
