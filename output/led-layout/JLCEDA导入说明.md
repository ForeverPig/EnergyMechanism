# 嘉立创EDA专业版导入灯珠位置

专业版原生入口：**PCB编辑器 → 文件 → 导入 → 坐标文件**。按位号匹配已有元件，再更新位置；不会创建、删除或重新编号元件。官方说明：[坐标文件导入](https://prodocs.lceda.cn/cn/pcb/file-import-pick-and-place-file/)。

## 当前文件

`jlceda-positions.csv`共**680个元件：332颗LED＋332颗100nF／0402电容＋16个安装孔**，另外有一行表头。

- `LED1`～`LED332`：300颗5050与32颗十字2020混合编号，从内到外逐圈逆时针。R82.5／97.5mm圈从3点钟下侧小灯进入，分别为LED77／LED117。R52.5mm已删除8颗小灯，仅留20颗5050。LED1仍在(7.5,0)mm。全部小灯位号见`small-led-designators.csv`。
- `C1`～`C332`：每LED一颗100nF／0402电容，Ck对应LEDk。本体按1.0×0.5mm，假定包络1.6×1.0mm。
- 单位mm，X向右、Y向上，灯板圆心(0,0)，角度逆时针为正；5050局部上边朝外，小灯局部上边朝内，保留上一版旋转角。
- 小灯电容偏移以移位前中心为基准，由局部+X 1.5mm改为局部−X 1.5mm；相对上一版右移后的坐标，沿同一局部方向反向移动3mm。不镜像、不再次旋转。5050电容位置和角度完全不变。
- `SCREW1`～`SCREW16`：带铜安装孔，钻孔Ø3mm、焊盘外径Ø5.8mm；坐标文件只定位已有封装，不设置孔径或镀铜属性。
- 列为`Designator,Mid X,Mid Y,Rotation`，未写Layer，导入器默认顶层。
- 位号同时表示预定WS2812单链顺序：控制器→LED1 DIN，LEDk DOUT→LED(k+1) DIN，最后到LED332。当前只规划连接顺序，未布线。

本轮修改前的完整快照在`placement-before-ring7-removal-cap-reverse.json`。删除上一版LED29、LED31、LED36、LED38、LED43、LED45、LED50、LED52及对应同号电容，其余位号按原顺序连续顺延，例如旧LED30→新LED29、旧LED85→新LED77、旧LED340→新LED332。删除／顺延／安装孔新增对照在`designator-renumber.csv`。最初341颗方案的对照在`designator-from-original.csv`，也包含中心灯及7环小灯的删除项。此前圈入口调整的历史表保留为`designator-renumber-entries-84-124.csv`和`designator-renumber-r525.csv`。

## 安装孔位置

每组从右上45°开始，逆时针依次为45°、135°、225°、315°。

| 位号 | 环交界 | 半径mm | 坐标mm（依次右上、左上、左下、右下） |
| --- | --- | ---: | --- |
| SCREW1～SCREW4 | 9-8环 | 30 | (±21.213203, ±21.213203) |
| SCREW5～SCREW8 | 7-6环 | 60 | (±42.426407, ±42.426407) |
| SCREW9～SCREW12（新增） | 5-4环 | 90 | (±63.639610, ±63.639610) |
| SCREW13～SCREW16（原9～12） | 3-2环 | 120 | (±84.852814, ±84.852814) |

各组坐标符号依次为(+,+)、(-,+)、(-,-)、(+,-)，均在y=±x线上。精确孔径和坐标见`screw-positions.csv`；只导入孔位时用`jlceda-screw-positions.csv`。Ø5.8mm外圆与LED／电容假定包络最小间距约1.262mm。

## 从内到外的编号范围

| 计分环 | 半径mm | 5050 | 小灯 | 当前位号范围 |
| --- | ---: | ---: | ---: | --- |
| 10 | 7.5 | 4 | 0 | LED1～LED4 |
| 9 | 22.5 | 8 | 0 | LED5～LED12 |
| 8 | 37.5 | 16 | 0 | LED13～LED28 |
| 7 | 52.5 | 20 | 0 | LED29～LED48 |
| 6 | 67.5 | 28 | 0 | LED49～LED76 |
| 5 | 82.5 | 32 | 8 | LED77～LED116 |
| 4 | 97.5 | 40 | 8 | LED117～LED164 |
| 3 | 112.5 | 44 | 8 | LED165～LED216 |
| 2 | 127.5 | 52 | 0 | LED217～LED268 |
| 1 | 142.5 | 56 | 8 | LED269～LED332 |

通常每圈从+X轴上的5050开始逆时针混排。R82.5／97.5mm圈先从+X轴下侧小灯进入，再跨过360°到+X轴上的5050继续逆时针。当前两处小灯入口为LED76→LED77、LED116→LED117；LED28→LED29现为两颗5050。各圈走完后进入下一圈，不接回本圈起点。

## 导入步骤

1. 按`designator-renumber.csv`先删除标为DELETED的8对小灯／电容，再把其余LED、电容及原SCREW9～12改为新位号。可先改成临时唯一位号再应用新编号，避免重复。旧位号空白的4项为新建SCREW9～12。更早的工程先按历史对照转换；新工程按`led-chain-order.csv`和`small-led-designators.csv`选择封装。
2. 用**设计 → 从原理图导入变更**同步到PCB，并准备SCREW1～SCREW16的安装孔封装，设置钻孔Ø3mm、铜焊盘外径Ø5.8mm。电容电气上跨接对应LED的VDD和GND；级联网络按当前LED1→LED332顺序核对。
3. 在PCB中选择**文件 → 导入 → 坐标文件**，导入`jlceda-positions.csv`，查看日志确认680个位号均匹配。也可分别导入`jlceda-led-positions.csv`、`jlceda-capacitor-positions.csv`、`jlceda-screw-positions.csv`，分别为332、332、16个元件行。CSV不会删除、改号或创建元件。
4. 以灯板坐标原点(0,0)为圆心画D300板框。四轴5050角度为右270°、上0°、左90°、下180°，保留所有存留元件的上一版角度。角度约定参见[专业版官方API文档](https://prodocs.lceda.cn/cn/api/guide/cli.html)。
5. 核对真实焊盘、安排DIN/DOUT级联和供电布线，并运行实际PCB DRC。

当前LED、电容及安装孔外圆本体无重叠，整体最小边缘间隙约0.85mm；假定包络无相交，最小间距约0.30mm。安装孔外圆与元件假定包络最小间距约1.262mm。详细报告见`pcb-positions.json`的`report`，尚未读取实际库焊盘或运行真实PCB DRC。

## 位号前缀和圆心偏移

在此目录运行：

```powershell
node export_jlceda.cjs
```

例如使用D1～D332，圆心在(150,150)mm，电容从C101开始：

```powershell
node export_jlceda.cjs --prefix D --start 1 --cap-start 101 --cx 150 --cy 150
```

此命令重写`jlceda-positions.csv`，位号为D1～D332、C101～C432和SCREW1～SCREW16；保留当前几何，所有对象均应用圆心偏移。`--include led`、`--include capacitor`、`--include screw`只输出对应类别。非连续位号应建立逐项对应表。

## 旋转和平移约定

```text
small_led_theta_new = small_led_theta_previous
small_led_position_new = small_led_position_previous
small_cap_theta_new = small_cap_theta_previous
small_cap_position_new = small_cap_position_previous + rotate(-3, 0, small_led_theta_new)
main_led_and_cap_positions_and_angles = unchanged
```

该段公式以上一版已错误右移1.5mm的`placement-before-ring7-removal-cap-reverse.json`为参考。移位前的未平移中心在小灯电容的`smallEdit.previousX/Y`中，当前以此中心为基准沿局部−X移动1.5mm；两次方案差为局部−X 3mm。不镜像、不再次旋转。右侧主灯LED1仍为270°，C1仍在(11.1,3.2)mm、角度90°。

5050的0°引脚方向按用户给定：左上1=5V、左下2=DOUT、右上4=DIN、右下3=GND。小灯脚号不可直接照搬：WS2812B-2020-V6为1=DO、2=GND、3=DI、4=VDD，小灯库封装的0°方向与电容位置仍需核对实际器件。

官方[坐标导出说明](https://prodocs.lceda.cn/cn/pcb/export-pick-and-place-file/)定义Mid X／Mid Y为封装焊盘中央点。本布局坐标表示LED本体／发光中心，应核对库封装的中心是否一致。有偏移时按实际封装修正。

已验证：删除7环8对小灯／电容，其余元件位号连续顺延；32颗小灯电容从错误右移修正为局部−X 1.5mm，保留存留元件角度和主灯电容中心；4个R90安装孔新增并顺延R120孔位号；16个孔的尺寸、对角线和环交界及圆形间距检查通过；本体及假定包络相交均为0；331条预览连接与680行磁盘坐标CSV一致，小灯位号及删除／改号表、桌面／手机预览和RPC兼容检查通过。尚未在实际嘉立创EDA工程中执行删除、改号、导入或布线。

