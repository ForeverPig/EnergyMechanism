# 根据 energy-led-project (1).json 生成的CAD

输入：`D:/Download/energy-led-project (1).json`。原文件未修改；原始副本为 `project-source.json`。输入SHA256为 `9703556c130b1574c4912dd95d8b4bf414b268b53fe1699c04c012698f4667cd`。

全部尺寸为mm，板圆心为原点，X向右、Y向上，平面草图位于XY平面。灯位和电容按这份JSON实际极角生成，不重新均布；使用本体中心坐标，避免把EDA焊盘中心偏移引入机械模型。

## 文件选择

| 文件 | 内容 |
| --- | --- |
| `target-and-rings-sketch.SLDPRT` | 推荐用于标靶设计：板外径、十环分界、独立标靶草图、安装孔和焊盘外径草图；没有实体 |
| `energy-layout-sketch.SLDPRT` | 完整平面布局：上述内容，加308颗5050、24颗2020、332个0402电容的外形与LED中心点、电阻膜参考外形；没有实体 |
| `energy-board-3d.SLDPRT` | 可编辑三维零件，多实体PCB＋LED＋电容，并附分界线和标靶参考草图 |
| `energy-board-3d.step` | 三维实体通用交换文件；需要编辑标靶草图时使用SLDPRT或DXF |
| `target-and-rings.dxf` | 标靶与十环、板框和孔位的分层二维图 |
| `energy-layout-2d.dxf` | 完整分层二维图，另含位号文字；导入单位选择mm、比例1:1 |
| `layout-preview.png` / `.svg` | 标靶草图与完整灯位的对照预览 |
| `solidworks-3d-preview.png` | SolidWorks实际模型视图 |
| `body-centres.csv` / `chain-order.csv` | 本体中心坐标、当前链序与位号 |

SLDPRT使用SolidWorks 2024保存，草图可直接编辑；更早版本可用DXF或STEP导入。

## 草图分组与几何

在特征树中右键相应草图即可“编辑草图”，并可单独显示／隐藏。

- `PCB_Outline_D300`：板外径300。
- `Scoring_Rings_10_Boundaries`：R15、30、45、60、75、90、105、120、135、150；相应直径30～300。
- `Target_Outline_Union`：独立标靶轮廓，2个整圆、20段圆弧、28段直线，共50个几何元素，所有轮廓闭合。已去除圆带与十字臂重叠部分的内部边界，圆弧保持真正圆弧。
- 标靶尺寸沿用现有程序：圆带R15～35、55～75、115～135；四臂轴向50～150、名义外端宽35、侧边7°，按D300裁剪。这是现有图案尺寸重建，并非赛事官方CAD。
- `Mounting_Drill_D3`：16个直径3安装孔；`Mounting_Pad_OD_D5p8`：直径5.8参考圆。
- 孔在R30、60、90、120和45°、135°、225°、315°位置，保持JSON中的SCREW1～16。
- 完整二维文件另外有LED、电容本体及LED中心点草图。`Film_Reference_D301_D310`是电阻膜有效边缘与外形参考圆，分别R150.5和R155。

## 三维简化尺寸

- PCB：D300、厚1.6，Z=0～1.6，16个D3通孔。
- 5050：5×5×1.6；小灯：2.2×2.2×1.6，均从Z=1.6起。
- 0402电容：1×0.5×0.5，从Z=1.6起。
- 总共665个独立实体：PCB1＋LED332＋电容332。实体以LEDn／Cn命名。
- **标靶和环界是草图，未做成实体面层。** 三维文件中参考草图在XY基准面上，可单独显示；纯草图文件便于不受实体遮挡地查看。
- 元件高度为示意默认值；没有建模铜线、焊盘金属或电阻膜结构。安装孔焊盘外径保留为草图，未增加铜环实体。

## 已执行验证

- 输入原件与副本SHA256一致。
- 标靶端点闭合，150个边界采样点分别位于图案内外两侧。
- 原生文件保存成功、保存警告0；逐个重新打开，确认独立草图与实体数量。
- 664个组件中心与JSON一致，检查容差0.000001 mm。
- 三维实体总体积125505.235792 mm³，与圆板减16孔、加LED／电容的解析体积一致。
- STEP重新导入成功，665个实体，总包围盒为X/Y ±150、Z=0～3.2 mm，体积一致。
- 两个R12 DXF均成功导入SolidWorks零件草图，载入错误0；标靶图最大圆半径150 mm，完整图含电阻膜参考圆，最大半径155 mm。
- 详细报告：`geometry-validation.json`、`solidworks-validation.json`、`step-import-validation.json`、`dxf-import-validation.json`。

## 重建脚本

`export_project.cjs`使用当前编辑器模型读取项目；`cad_geometry.py`计算精确二维轮廓和DXF；`build_solidworks.py`通过本机SolidWorks COM生成原生文件和STEP；`preview_and_check.py`生成预览；`verify_step_import.py`回读STEP。

生成和导入API参考：[SolidWorks草图圆弧](https://help.solidworks.com/2013/English/Api/sldworksapi/SolidWorks.Interop.sldworks~SolidWorks.Interop.sldworks.ISketchManager~CreateArc.html)、[零件拉伸](https://help.solidworks.com/2017/english/api/sldworksapi/SolidWorks.Interop.sldworks~SolidWorks.Interop.sldworks.IFeatureManager~FeatureExtrusion3.html)、[文件保存](https://help.solidworks.com/2022/english/api/sldworksapi/SOLIDWORKS.Interop.sldworks~SOLIDWORKS.Interop.sldworks.IModelDocExtension~SaveAs.html)。
