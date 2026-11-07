"""Collect Qt's own SPDX notices plus SPDX standard license texts for distribution."""
import concurrent.futures
import json
from pathlib import Path
import re
import shutil
import sys
import time
import urllib.request
import urllib.error

qt, output = map(Path, sys.argv[1:3])
output.mkdir(parents=True, exist_ok=True)
ids = {'LGPL-3.0-only', 'GPL-3.0-only'}
for module in ('qtbase', 'qtconnectivity', 'qtserialport', 'qtwayland', 'qtsvg'):
    source = qt / 'sbom' / f'{module}-6.8.3.spdx.json'
    if not source.exists():
        if module not in ('qtwayland', 'qtsvg'):
            raise RuntimeError(f'Missing vendor license metadata: {source}')
        continue
    shutil.copy2(source, output / source.name)
    data = json.loads(source.read_text(encoding='utf-8'))
    for entry in data.get('hasExtractedLicensingInfos', []):
        (output / (entry['licenseId'] + '.txt')).write_text(entry['extractedText'], encoding='utf-8')
    for entry in data.get('packages', []):
        for key in ('licenseDeclared', 'licenseConcluded'):
            ids.update(re.findall(r'[A-Za-z0-9][A-Za-z0-9.+-]*', entry.get(key, '')))
ids = {x for x in ids if x not in {'AND', 'OR', 'WITH', 'NOASSERTION', 'NONE'} and not x.startswith('LicenseRef-')}
cache = Path(__file__).resolve().parents[1] / 'build/license-cache/spdx-v3.26.0'
cache.mkdir(parents=True, exist_ok=True)
def fetch(identifier):
    target = output / f'{identifier}.txt'
    if not target.exists():
        saved = cache / target.name
        if not saved.exists():
            url = f'https://raw.githubusercontent.com/spdx/license-list-data/v3.26.0/text/{identifier}.txt'
            for attempt in range(3):
                try:
                    with urllib.request.urlopen(url, timeout=25) as response:
                        data = response.read()
                    saved.write_bytes(data)
                    break
                except (OSError, urllib.error.URLError):
                    if attempt == 2:
                        raise
                    time.sleep(attempt + 1)
        shutil.copy2(saved, target)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(fetch, sorted(ids)))
(output / 'README.txt').write_text(
    'Qt 6.8.3 is dynamically linked under LGPL v3. You may replace the Qt shared libraries with compatible builds.\n'
    'Application build instructions and sources: https://github.com/ForeverPig/EnergyMechanism\n'
    'Qt corresponding source: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/\n'
    'Modules: qtbase, qtconnectivity, qtserialport; Linux also qtwayland and qtsvg. Qt sources are unmodified; binary search paths are adjusted during packaging.\n'
    'Vendor SPDX files contain component copyright and license attribution, including components not used by this application.\n'
    'Windows package also contains Microsoft Visual C++ runtime redistributable DLLs, copyright Microsoft Corporation.\n'
    'Font license/source are in resources/FONT-LICENSE.txt and FONT-SOURCE.txt.\n', encoding='utf-8')
print('License metadata collected:', len(ids), 'standard licenses')
