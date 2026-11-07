"""Build a self-contained offline editor; preserve the original preview/exports."""
import json
import math
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ARCHIVE = ROOT.parent.parent / 'tmp/easyeda-data-15mil-20261003/after-project.epro2'


def read_profiles():
    with zipfile.ZipFile(ARCHIVE) as archive:
        source = archive.read(next(n for n in archive.namelist() if n.endswith('.epru'))).decode('utf-8-sig')
    docs, active = {}, None
    for line in source.splitlines():
        pair = line.split('||', 1)
        if len(pair) != 2:
            continue
        head, raw = json.loads(pair[0]), pair[1].rstrip('|')
        if head.get('type') == 'DOCHEAD':
            doc = json.loads(raw)
            active = doc.get('uuid')
            docs.setdefault(active, {})
        elif active and head.get('id'):
            if raw:
                docs[active][head['id']] = (head, json.loads(raw))
            else:
                docs[active].pop(head['id'], None)
    result = {}
    identities = {'5050': ('f4c9fb87a490f84f', '5050 / 含侧面GND通孔'),
                  'small': ('6ce305d8e7dd334b', '2020 / 当前小灯封装'),
                  'capacitor': ('2a49eaf22f715844', '0402 / 100 nF')}
    for kind, (uuid, label) in identities.items():
        records = docs[uuid].values()
        pads = [[v['centerX'] * .0254, v['centerY'] * .0254]
                for h, v in records if h.get('type') == 'PAD']
        outlines = [v['path'] for h, v in records if h.get('type') == 'POLY' and v.get('layerId') == 48]
        body_center = [0, 0]
        if outlines:
            path = outlines[0]
            numbers = [v for v in path if isinstance(v, (int, float))]
            xs, ys = numbers[::2], numbers[1::2]
            if len(xs) == len(ys) and xs:
                body_center = [(min(xs) + max(xs)) * .0127, (min(ys) + max(ys)) * .0127]
        result[kind] = {'name': label, 'footprintUuid': uuid, 'source': '2026-10-03 saved PCB',
                        'bodyCenter': body_center, 'padCenters': pads}
    result['screw'] = {'name': 'Ø3 / Ø5.8 同心安装孔', 'source': 'layout mounting holes',
                       'bodyCenter': [0, 0], 'padCenters': [[0, 0]]}
    return result


def main():
    baseline = json.loads((ROOT / 'led-positions.json').read_text(encoding='utf-8-sig'))
    # The original placement remains the PCB/history reference. The editor's
    # current default upgrades the outer eight supplements in place to 5050.
    caps = {cap['ledId']: cap for cap in baseline['capacitors']}
    for led in baseline['leds']:
        if led['row'] != 9 or led['kind'] != 'small':
            continue
        led.update(kind='5050', sizeMm=5, purpose='ring', convertedFromSmall=True,
                   rotationDeg=round((led['rotationDeg'] + 180) % 360, 6),
                   smallRotationAddedDeg=0, groupKey='1:T' if led['target'] else '1:B',
                   group=0 if led['target'] else 1)
        angle = math.radians(led['rotationDeg'])
        cap = caps[led['id']]
        cap.update(x=led['x'] - 3.2 * math.cos(angle) - 3.6 * math.sin(angle),
                   y=led['y'] - 3.2 * math.sin(angle) + 3.6 * math.cos(angle),
                   rotationDeg=round((led['rotationDeg'] + 180) % 360, 6),
                   localX=-3.2, localY=3.6, convertedFromSmall=True)
        cap.pop('smallEdit', None)
    baseline['revision'] = '20261012-outer-eight-5050'
    for ring in baseline.get('chainRings', []):
        members = [led for led in baseline['leds'] if led['row'] == ring['row']]
        ring['main'] = sum(led['kind'] == '5050' for led in members)
        ring['small'] = sum(led['kind'] == 'small' for led in members)
    profiles = read_profiles()
    defaults = {'baseline': baseline, 'footprints': profiles}
    (ROOT / 'editor-defaults.json').write_text(json.dumps(defaults, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    (ROOT / 'editor-footprints.json').write_text(json.dumps(profiles, ensure_ascii=False, indent=2), encoding='utf-8')
    script = '(function(){\nconst module={exports:{}};\n'
    script += (ROOT / 'pcb-layout.js').read_text(encoding='utf-8') + '\nconst PcbLayout=module.exports;\n'
    script += (ROOT / 'layout.js').read_text(encoding='utf-8') + '\nconst LedLayout=module.exports;\n'
    script += (ROOT / 'editor-model.js').read_text(encoding='utf-8') + '\nconst EditorModel=module.exports;\n'
    encoded = json.dumps(defaults, ensure_ascii=False, separators=(',', ':')).replace('<', '\\u003c')
    script += 'const EditorDefaults=' + encoded + ';\n'
    script += (ROOT / 'editor-ui.js').read_text(encoding='utf-8') + '\n})();'
    html = (ROOT / 'editor-shell.html').read_text(encoding='utf-8')
    html = re.sub(r'<!--EDITOR_TEMPLATE_NOTICE_START-->.*?<!--EDITOR_TEMPLATE_NOTICE_END-->', '', html, flags=re.S)
    html = html.replace('/*EDITOR_CSS*/', (ROOT / 'editor.css').read_text(encoding='utf-8'))
    html = html.replace('/*EDITOR_BUNDLE*/', script)
    (ROOT / 'editor.html').write_text(html, encoding='utf-8')
    print('Built editor.html, editor-defaults.json and editor-footprints.json')


if __name__ == '__main__':
    main()
