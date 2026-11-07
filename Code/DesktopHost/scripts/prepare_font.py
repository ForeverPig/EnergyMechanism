"""Build-only font subsetting. Runtime never downloads assets."""
import hashlib
from pathlib import Path
import sys
import urllib.request
from fontTools import subset

root = Path(__file__).resolve().parents[1]
cache = Path(sys.argv[1])
cache.mkdir(parents=True, exist_ok=True)
base = 'https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans/'
source = cache / 'NotoSansCJKsc-Regular.otf'
if not source.exists():
    urllib.request.urlretrieve(base + 'OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf', source)
license_file = root / 'resources/FONT-LICENSE.txt'
if not license_file.exists():
    urllib.request.urlretrieve(base.replace('/Sans/', '/') + 'LICENSE', license_file)
text = ''.join(chr(i) for i in range(32, 127)) + '—…°·秒环在线离线运行停止完成失败未知'
for first in range(0xb0, 0xf8):
    for second in range(0xa1, 0xff):
        text += bytes((first, second)).decode('gb2312', errors='ignore')
for path in (root / 'src').glob('*'):
    text += path.read_text(encoding='utf-8')
options = subset.Options()
options.name_IDs = ['*']
options.name_legacy = True
options.name_languages = ['*']
font = subset.load_font(str(source), options)
subsetter = subset.Subsetter(options=options)
subsetter.populate(text=text)
subsetter.subset(font)
for name in font['name'].names:
    if name.nameID in (1, 3, 4, 6, 16):
        name.string = 'EnergyUI'.encode(name.getEncoding())
output = root / 'resources/EnergyUI.otf'
subset.save_font(font, str(output), options)
(root / 'resources/FONT-SOURCE.txt').write_text(
    'EnergyUI is a subset of Noto Sans CJK SC Regular 2.004, licensed under SIL OFL 1.1.\n'
    + base + 'OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf\n'
    + 'source_sha256=' + hashlib.sha256(source.read_bytes()).hexdigest() + '\n'
    + 'subset_sha256=' + hashlib.sha256(output.read_bytes()).hexdigest() + '\n', encoding='utf-8')
print('Font subset:', output.stat().st_size, 'bytes')
