# USB 串口十环监控

从工程根目录运行，默认自动连接 COM3：

```powershell
python tools/serial_monitor.py --port COM3
```

也可以双击 `tools/start_monitor.bat`。Python 需包含 Tkinter；唯一额外依赖为 pyserial。
本工程已在 `tools/_deps` 安装 pyserial，脚本会自动使用该目录，无需修改系统 Python。
换电脑时可执行：

```powershell
python -m pip install -r tools/requirements.txt
# 若不想修改当前 Python 环境：
python -m pip install --target tools/_deps -r tools/requirements.txt
```

窗口左侧显示十个同心环和十路数值，右侧显示原始 ADC 和基线差值曲线。
点击环或表格行选中通道，该环曲线加粗。默认曲线自动缩放，可勾选固定 ADC 0～4095 范围。
数值表新增 ADC 网络列，从 `Core/Inc/app_config.h` 的 `APP_SENSOR_RING` 读取接线映射。
当前 ADC1～ADC10 依次对应6、7、8、9、10、5、4、3、2、1环。
这里的 ADC1～ADC10 是网表网络名，不是芯片 ADC 外设实例。
固件已经在快照和事件中发送交换后的环号，Python 按报文环号直接显示曲线、击打点、计数和 CSV，不再次交换。
网络列是本地源码映射提示，无法自动确认板上固件版本；修改映射后应烧录固件并重启监控，清空旧曲线和计数。
收到 E 击打事件时，该环高亮两秒，标出橙色点，并记录环号、事件峰值、MCU 时间及命中累计数。
USB 断开后每两秒自动重连；手动“断开”会释放串口。“保存 CSV”会保存十路完整快照和击打事件。

重要的数据含义：

- `S,timestamp_ms,calibration,dma_drops,adc_errors,debug_drops` 后必须有十路互不重复的数据，才显示为完整快照。
- 通道行：`ring,raw,baseline,delta,peak,state`。状态 0/1/2/3/4 为校准中/空闲/候选/等待释放/锁定。
- `E,ring,peak,timestamp_ms` 为击打事件，峰值是相对基线的 ADC 计数。
- 固件只给出环号，没有角度信息，所以窗口的击打点是对应环上的示意位置。
- 固件约每 50 ms 发送一帧调试快照，曲线刷新数据约 20 Hz，不能还原每路 10 kHz 的全部样本。
  短脉冲可能不出现在快照曲线上，但其有效击打事件的峰值会单独画在差值图上。
- 曲线横轴使用 PC 单调接收时间；事件表保留 MCU 时间，MCU 复位或时间计数回绕不会让曲线跳回。
- 图中阈值读取本工程 `Core/Inc/app_config.h` 的 `APP_HIT_START_DELTA`、`APP_HIT_MIN_PEAK`，
  修改源码后需要重启监控程序；程序无法判断板上运行的固件是否已同步更新。
- GUI 队列和曲线历史都有固定上限；显示 PC 丢包、非法行、不完整帧数，不拼接跨帧数据。
- USB CDC 的 115200 是串口打开参数；当前固件不按该参数改变 USB 吞吐率。

其他运行方式：

```powershell
python tools/serial_monitor.py --demo                 # 模拟数据展示，不打开串口
python tools/serial_monitor.py --port COM3 --capture 5 # 读实际串口五秒，输出诊断 JSON
python -m unittest discover -s tests -p test_serial_monitor.py -v
python tools/serial_monitor.py --smoke-test           # 检查 GUI 构造与定时绘制
```

同一串口只能由一个程序打开；启动前关闭其他串口助手。程序只读取数据，不发送控制命令。
