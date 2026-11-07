# STM32G474 单扇叶测试程序

## 工程配置

工程按 STM32G474CBT6、STM32CubeG4 HAL、CMake 和 STM32CubeIDE for VS Code 组织。启动顺序为 GPIO、DMA、ADC1/2/3、TIM1、USB CDC、TIM6，然后在主循环调用 `App_Process()`。

## CMake 构建与 VS Code 调试

从工程根目录用 STM32CubeIDE for VS Code 打开文件夹，选择 `Debug` CMake configure/build preset，再使用“STM32Cube: Test_EnergyMechanism (ST-LINK)”启动调试。`.vscode/launch.json` 使用 ST 扩展解析当前工程生成的 ELF，并在调试前执行默认构建。

工程使用 `Ninja`、`arm-none-eabi-gcc` 和 STM32CubeG4 固件包。当前 CubeMX 固件包默认路径为 `D:/Data/STM32Cube/Repository/STM32Cube_FW_G4_V1.6.3`；如果路径不同，可设置环境变量 `STM32_CUBE_G4_ROOT`，或在 CMake cache 中设置同名变量。`cmake/stm32cubemx/CMakeLists.txt` 使用该变量引用 HAL、CMSIS 和 USB Device Library。

若以后重新生成 CMake 工程，请检查 `cmake/stm32cubemx/CMakeLists.txt` 中固件包路径仍引用 `${STM32_CUBE_G4_ROOT}`；CubeMX 可能重新写回本机绝对路径。

CubeMX 关键配置：

- SYSCLK、AHB、APB1、APB2 为 160 MHz；USB 使用 HSI48。
- ADC1 扫描 PA0～PA3，ADC2 扫描 PA4～PA7，ADC3 扫描 PB0～PB1；12 bit、single-ended、47.5 cycles、40 MHz ADC 时钟、TIM6 TRGO 上升沿触发、序列结束 DMA 请求。
- ADC1/2/3 分别使用 DMA1 Channel1/2/3，外设到内存、循环模式、半字宽、内存递增、高优先级。
- TIM6 PSC=159、ARR=99、TRGO=Update Event，每100 μs触发一次。
- TIM1_CH1 在 PA8 输出 PWM Mode 1，高有效，PSC=0、ARR=199；DMA2 Channel1 为 **Memory to Peripheral**、Normal、半字宽、内存递增、最高优先级。
- TIM1 PWM DMA 请求由 HAL 的 CH1 compare 请求驱动；PA8 保持定时器复用输出。TIM1 DMA 完成中断优先级高于 ADC DMA；USB 优先级低于 ADC DMA。

不要在 CubeMX 重新生成后覆盖 `app.c`、`hit_detector.c`、`led_controller.c`、`ws2812.c` 或相应头文件。生成后需复核 TIM1 DMA 方向以及 `DMA_HandleTypeDef` 与 TIM1 CH1 的链接。

## 传感器与击打事件

ADC 网络 ADC1～ADC10 依次映射到6、7、8、9、10、5、4、3、2、1环，映射表为 `APP_SENSOR_RING`。USB 快照和击打事件均使用该环号映射。三路 ADC 使用32帧循环缓冲区，每16帧半满/全满回调复制一块数据；主循环等待三个来源的块序号一致后再组合成10路样本。每路等效采样率为10 kHz。

上电后持续500 ms建立静态基线；若任一通道在校准区间内峰峰波动超过配置限值，就重新累计。校准期间不发击打事件，请勿触碰电阻膜。

检测状态包括校准、空闲、候选、等待释放和全局锁定。使用三点中值滤波、相对基线差值、越阈前0.2 ms上升量、峰值、脉宽和稳定释放共同判定。多个环在3 ms分组窗口内有效时选峰值较高者，峰值相同时优先更高环。一次事件后至少锁定80 ms，且所有通道稳定释放2 ms后重新允许击打。调试快照会保留各环最近一次候选峰值，直到该环再次进入新候选。

阈值在 `Core/Inc/app_config.h`：`APP_HIT_START_DELTA`=80、`APP_HIT_MIN_PEAK`=160、`APP_HIT_RISE_DELTA`=30、`APP_HIT_RELEASE_DELTA`=40、脉宽3～400个采样点（0.3～40 ms）、释放20点。它们是测试初值，应记录真实击打、振动及慢按波形后再定标。慢按可能在最后阶段快速越阈，因此软件阈值无法在没有采集样本的情况下完全区分它与弹丸击打。

应用事件结构 `HitEvent` 提供 `ring`、`peak`（ADC 计数）、`timestamp_ms`。弱回调 `App_OnHitEvent()` 在主循环上下文触发，可由上层强符号覆盖，之后在此连接 FDCAN 发送逻辑。该测试程序不设置 FDCAN 仲裁参数或报文 ID。

## WS2812

灯链总数 `APP_LED_COUNT` 为320颗，前30颗仍按 `APP_LED_RINGS` 配置为10环4颗、9环10颗、8环16颗，串接顺序由内向外；环内总数为 `APP_LED_RING_LED_COUNT`。灯珠索引、环偏移及编码循环使用16位计数器，支持跨越第255颗。WS2812 按 GRB、最高位先发；0/1 高电平分别56/112个160 MHz定时器时钟（0.35/0.70 μs），PWM周期1.25 μs。每帧7680个数据周期，后接256个复位低电平周期，共7936个DMA半字，约9.92 ms。

当前默认 `APP_LED_SOLID_MODE=1`：上电后320颗红灯常亮，停止动画切换，颜色由 `APP_LED_SOLID_COLOR` 配置。持续PWM刷新由 `APP_LED_CONTINUOUS_REFRESH=1` 保持开启。

设 `APP_LED_SOLID_MODE=0` 可恢复演示：每1秒切换一个画面，完整循环52秒。先在前30颗上用红色运行“全亮→8环→9环→10环→一字条”，再用蓝色运行相同5个画面；随后红色320颗全亮1秒、蓝色320颗全亮1秒；最后沿用蓝色，每8颗一组依次点亮第1～40组，每组1秒且上一组熄灭。第1组为1～8号，第32组249～256号，第33组257～264号，第40组313～320号。最后返回红色前30颗全亮，其余290颗熄灭。

一字条仍点亮编号1、3、5、10、15、23，编号从1开始，位置在 `APP_LED_LINE_LEDS` 中配置。切换间隔由 `APP_LED_ANIMATION_PERIOD_MS` 配置，红蓝颜色在 `APP_LED_ANIMATION_COLORS`，分组大小在 `APP_LED_GROUP_SIZE`，分组颜色在 `APP_LED_GROUP_COLOR`。默认全局亮度为255/255。

动画状态机位于 `led_controller.c`，主循环调用 `LedController_ProcessAnimation()`，使用 `HAL_GetTick()` 非阻塞调度，不使用 `HAL_Delay()`。计时支持32位回绕，主循环晚到时直接进入当前应显示的颜色与画面。动画只修改待渲染状态，WS2812 DMA空闲后才生成并发送新数据。运行时仍可调用 `App_SetRingColor()`、`App_SetRingBrightness()`、`App_SetGlobalBrightness()`；环号限于当前测试版的8、9、10。动画切换会更新各环颜色、亮度和灯珠遮罩，全局亮度继续生效。

`APP_LED_CONTINUOUS_REFRESH=1` 时持续刷新灯链：当前帧发送完成后，主循环调用 `WS2812_Repeat()` 再次发送已编码的PWM缓存，不等待1秒动画切换。画面变化时才重新渲染和编码，并且只在上一帧DMA完成后更新。每帧仍包含复位低电平；现有Normal DMA及CubeMX初始化配置保持原状。320颗一帧约9.92 ms，实际刷新间隔还包含主循环调度时间。设为0可恢复只在画面变化时发送。启动失败会保留画面更新标志供后续重试。

## USB CDC 调试

PC 可视化监控：运行 `python tools/serial_monitor.py --port COM3`，或双击 `tools/start_monitor.bat`。
十环图、十路 ADC 曲线、击打点和 CSV 保存说明见 [监控程序说明](tools/README_MONITOR.md)。

USB CDC 每50 ms发送一帧逗号分隔文本。`S`行包含时间戳、校准状态、DMA丢块数、ADC错误数和调试丢帧数；随后每个环一行：`ring,raw,baseline,delta,peak,state`。击打使用 `E,ring,peak,timestamp_ms` 行，并优先于下一帧快照发送。主机未连接或 CDC 正忙不会阻塞采集；丢弃数可通过 `App_GetDmaDropCount()`、`App_GetAdcErrorCount()` 和 `App_GetDebugDropCount()` 查询。

发生 ADC overrun/DMA 错误、队列溢出或三个 ADC 块序号不同步时，应用停止采样、清空队列、重新校准 ADC，并从基线校准状态恢复。错误次数/丢块数不会因恢复而清零。

## 调试步骤

1. 先打开 USB CDC 串口，保持薄膜无压力，复位并等待校准状态变为 READY。
2. 记录十路 `raw` 和 `baseline`，轻敲各环并观察 `delta`、`peak`、state，再观察邻环串扰和机械振铃。
3. 按实测波形调整 `app_config.h` 中起始差值、峰值、上升量、脉宽、释放阈值和锁定时间；重新编译并对振动及手按重复测量。
4. 测量 PA8：位周期应为1.25 μs，短脉冲高电平约0.35 μs，长脉冲约0.70 μs；确认复位低电平不少于320 μs。

主机逻辑回归可从工程根目录运行：

```powershell
gcc -std=c99 -Wall -Wextra -Werror -ICore\Inc Core\Src\hit_detector.c Core\Src\led_controller.c tests\test_firmware_logic.c -o $env:TEMP\firmware_logic_test.exe
& $env:TEMP\firmware_logic_test.exe
```

320颗PWM编码回归编译实际 `ws2812.c`，仅替换HAL接口；覆盖完整7936个DMA周期、跨255颗索引、末颗数据、复位尾部及DMA忙时缓冲区保护：

```powershell
gcc -std=c99 -Wall -Wextra -Werror -D__MAIN_H -include tests/ws2812_hal_stub.h -ICore/Inc Core/Src/ws2812.c tests/test_ws2812_encoding.c -o build/ws2812_encoding_test.exe
& build/ws2812_encoding_test.exe
```

主机逻辑回归不等同于目标交叉编译。目标 ELF 需通过 STM32CubeIDE for VS Code 的 CMake preset 构建；ADC 采样率、PWM 时序、USB 连断及阈值效果仍需在测试板上确认。

ADC DMA 回归覆盖两路/四路扫描的半满与全满数据，以及稳定 ADC3 数据的基线校准。
DMA 源缓冲区必须按外设实际 Rank 数紧密排列；ADC3 是两路，不能按四路行步长取数。
否则9、10环会混入未写入的零块，产生周期性归零、校准失败或击打判定异常。
