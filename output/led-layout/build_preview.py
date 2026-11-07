from pathlib import Path

folder = Path(__file__).resolve().parent
fragment = (folder / 'preview-fragment.html').read_text(encoding='utf-8')
core = (folder / 'layout.js').read_text(encoding='utf-8')
pcb_core = (folder / 'pcb-layout.js').read_text(encoding='utf-8')
ui = (folder / 'preview.js').read_text(encoding='utf-8')
css = (folder / 'standalone.css').read_text(encoding='utf-8')
# One dependency-ordered script: do not depend on cross-script window globals in
# the inline host. The CommonJS shim is local to this bundle, never host-owned.
scripts = '<script>\n(function () {\nconst module = {exports:{}};\n' + pcb_core
scripts += '\nconst PcbLayout = module.exports;\n' + core
scripts += '\nconst LedLayout = module.exports;\n' + ui + '\n})();\n</script>\n'
inline_path = Path(r'C:\Users\shuto\.codex\visualizations\2026\10\01\01a0f6b6-1c27-7a71-8719-b04c8265c9bf\energy-led-layout.html')
inline_path.write_text(fragment + '\n' + scripts, encoding='utf-8')
standalone = '<!doctype html>\n<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
standalone += '<title>300 mm 能量机关灯板布局预览</title><style>\n' + css + '\n</style></head><body><main>\n'
standalone += fragment + scripts
standalone += '<footer>依据手册印刷页 101、104 的示意尺寸重建。辐条按外端宽 35 mm、轴向长 100 mm、单侧 7° 建模并裁剪到 Ø300；非官方 CAD。电阻膜按图 3 的 R150.5 有效外边界、R155 外形叠加。已检查LED、电容本体及假定封装包络；未读取实际库焊盘或运行PCB DRC，未计走线、螺孔和装配公差。</footer></main></body></html>\n'
(folder / 'preview.html').write_text(standalone, encoding='utf-8')
print('Built preview.html and inline energy-led-layout.html')
