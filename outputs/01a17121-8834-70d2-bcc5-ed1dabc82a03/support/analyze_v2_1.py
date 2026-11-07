"""Read the supplied final PCB and derive manufacturing centres; never edit PCB."""
import collections
import csv
import hashlib
import json
import math
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parent.parent
PROJECT = Path(r'D:\Download\ProDoc_v2_1_2026-10-25.epro')
COORDINATES = Path(r'D:\Download\PickAndPlace_PCB1_3_2026_10_25.xlsx')
NS = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
MIL = .0254


def documents(raw):
    result, current = [], None
    for line in raw.decode('utf-8-sig').splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if row[0] == 'DOCTYPE':
            current = {'type': row[1], 'rows': []}
            result.append(current)
        assert current is not None, 'Record before document header'
        current['rows'].append(row)
        if row[0] == 'HEAD':
            current['head'] = row[1]
    return result


def load_sheet(path):
    with zipfile.ZipFile(path) as archive:
        strings = [''.join(v.itertext()) for v in ET.fromstring(archive.read('xl/sharedStrings.xml'))]
        root = ET.fromstring(archive.read('xl/worksheets/sheet1.xml'))
        name = ET.fromstring(archive.read('xl/workbook.xml')).find('s:sheets/s:sheet', NS).get('name')
        result = []
        for row in root.findall('s:sheetData/s:row', NS):
            values = [None] * 15
            for cell in row.findall('s:c', NS):
                column = ''.join(c for c in cell.get('r') if c.isalpha())
                index = 0
                for char in column:
                    index = index * 26 + ord(char) - 64
                value_node = cell.find('s:v', NS)
                value = value_node.text if value_node is not None else None
                if cell.get('t') == 's':
                    value = strings[int(value)]
                elif value is not None and cell.get('t') in (None, 'n'):
                    numeric = float(value)
                    value = int(numeric) if numeric.is_integer() else numeric
                elif cell.get('t') == 'inlineStr':
                    value = ''.join(cell.find('s:is', NS).itertext())
                assert index <= 15, 'Unexpected columns in source table'
                values[index - 1] = value
            result.append(values)
        return name, result


def number(value):
    return float(str(value).removesuffix('mm'))


def rotate(point, angle):
    t = math.radians(angle)
    c, s = math.cos(t), math.sin(t)
    return point[0] * c - point[1] * s, point[0] * s + point[1] * c


def centre(points):
    return [(min(p[i] for p in points) + max(p[i] for p in points)) / 2 for i in (0, 1)]


def millimetres(value):
    return f'{0 if abs(value) < .0000005 else value:.6f}mm'


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    sheet_name, values = load_sheet(COORDINATES)
    header, table = values[0], values[1:]
    assert header == ['Designator', 'Device', 'Footprint', 'Mid X', 'Mid Y', 'Ref X', 'Ref Y', 'Pad X', 'Pad Y', 'Pins', 'Layer', 'Rotation', 'SMD', 'Comment', 'Name']
    with zipfile.ZipFile(PROJECT) as archive:
        project = json.loads(archive.read('project.json'))
        board_meta = project['boards']['v2_1']
        pcb_path = 'PCB/' + board_meta['pcb'] + '.epcb'
        sections = documents(archive.read(pcb_path))
        boards = [d for d in sections if d['type'] == 'PCB']
        assert len(boards) == 1
        pcb = boards[0]['rows']
        footprints = {d['head']['uuid']: d for d in sections if d['type'] == 'FOOTPRINT'}
        for name in archive.namelist():
            if name.startswith('FOOTPRINT/') and name.endswith('.efoo'):
                for document in documents(archive.read(name)):
                    uid = document['head']['uuid']
                    assert uid not in footprints, 'Conflicting local/library footprint'
                    footprints[uid] = document
    assert not [r for r in pcb if r[0] == 'PAD'], 'Unexpected board-level pad overrides need review'
    attributes = collections.defaultdict(dict)
    for row in pcb:
        if row[0] == 'ATTR':
            attributes[row[3]][row[7]] = row[8]
    parts = {}
    for row in pcb:
        if row[0] == 'COMPONENT':
            attr = attributes[row[1]]
            ref = attr['Designator']
            assert ref not in parts, 'Duplicate board designator'
            parts[ref] = {'raw': row, 'attrs': attr}
    assert len(table) == len(parts) == 683
    assert {r[0] for r in table} == set(parts), 'Source coordinates do not match final PCB inventory'
    corrected, details, changes = [], [], []
    maxima = {'ref_mm': 0, 'rotation_degrees': 0, 'mid_mm': 0, 'shape_vs_land_centre_mm': 0}
    package_counts = collections.Counter()
    for excel_row, source in enumerate(table, 2):
        row = source.copy()
        ref = row[0]
        component, attrs = parts[ref]['raw'], parts[ref]['attrs']
        _, cid, _, layer, x, y, angle, *_ = component
        assert row[10] == {1: 'T', 2: 'B'}[layer], (ref, 'layer mismatch')
        assert math.isfinite(angle)
        origin = [x * MIL, y * MIL]
        reference = [number(row[5]), number(row[6])]
        ref_error = max(abs(a - b) for a, b in zip(origin, reference))
        rotation_error = abs((number(row[11]) - angle + 180) % 360 - 180)
        maxima['ref_mm'] = max(maxima['ref_mm'], ref_error)
        maxima['rotation_degrees'] = max(maxima['rotation_degrees'], rotation_error)
        assert ref_error <= .0005001 and rotation_error <= .0005001, (ref, 'PCB/export pose mismatch', origin, reference, angle, row[11])
        footprint = footprints[attrs['Footprint']]
        pads = [p for p in footprint['rows'] if p[0] == 'PAD']
        assert int(row[9]) == len(pads), (ref, 'pad inventory mismatch')
        smd_pads = [p for p in pads if p[4] in (1, 2)]
        vias = [p for p in pads if p[4] == 12]
        local_all = [[p[6], p[7]] for p in pads]
        # Top-side PCB instances are CCW. Bottom-only parts are symmetric and
        # unchanged; keep their original manufacturing coordinates and rotation.
        all_world = [rotate(p, angle) for p in local_all]
        all_centre = centre(all_world)
        predicted_mid = [origin[i] + all_centre[i] * MIL for i in (0, 1)]
        original_mid = [number(row[3]), number(row[4])]
        mid_error = max(abs(a - b) for a, b in zip(predicted_mid, original_mid))
        maxima['mid_mm'] = max(maxima['mid_mm'], mid_error)
        assert mid_error <= .000501, (ref, 'Cannot reproduce source Mid X/Y', mid_error, predicted_mid, original_mid)
        target = ref.startswith('LED') or ref.startswith('C') and ref[1:].isdigit()
        change = target and bool(vias)
        pick = original_mid
        if target:
            required = 4 if ref.startswith('LED') else 2
            assert layer == 1 and len(smd_pads) == required
            numbers = {str(p[5]) for p in smd_pads}
            assert len(numbers) == required
            package_counts[row[1]] += 1
            # Four original LED lands / two capacitor lands form the original
            # package's symmetric placement centre. Added via pads are excluded.
            local_pick = [sum(p[6 + i] for p in smd_pads) / required for i in (0, 1)]
            assert all(abs(v) < .002 for v in local_pick), (ref, 'Asymmetric original SMD land pattern')
            outlines = [p for p in footprint['rows'] if p[0] == 'POLY' and p[4] == 48]
            if outlines:
                vertices = [v for v in outlines[0][6] if isinstance(v, (int, float))]
                body = centre(list(zip(vertices[::2], vertices[1::2])))
                shape_error = math.dist(body, local_pick) * MIL
                maxima['shape_vs_land_centre_mm'] = max(maxima['shape_vs_land_centre_mm'], shape_error)
                assert shape_error < .0011, (ref, 'Body outline and original land centre differ')
            delta = rotate(local_pick, angle)
            pick = [origin[i] + delta[i] * MIL for i in (0, 1)]
            if change:
                row[3], row[4] = map(millimetres, pick)
                # The added plated hole is a PCB interconnect, not a lead of the
                # physical 5050/2020/0402 component. These parts are surface mount.
                row[12] = 'Yes'
        if not change:
            assert row == source
        delta_mm = math.dist(pick, original_mid)
        details.append({'excelRow': excel_row, 'designator': ref, 'footprintUuid': attrs['Footprint'],
                        'pcbOriginMm': origin, 'pcbRotationDeg': angle, 'exportRotation': row[11],
                        'originalMidMm': original_mid, 'correctedMidMm': [number(row[3]), number(row[4])],
                        'deltaXmm': number(row[3]) - original_mid[0], 'deltaYmm': number(row[4]) - original_mid[1],
                        'deltaNormMm': delta_mm, 'surfacePads': len(smd_pads), 'viaPads': len(vias),
                        'changed': change, 'nativeMidErrorMm': mid_error})
        for index, (old, new) in enumerate(zip(source, row)):
            if old != new:
                assert index in (3, 4, 12)
                changes.append({'row': excel_row, 'column': index + 1, 'old': old, 'value': new})
        corrected.append(row)
    assert [d['designator'] for d in details if d['designator'] in ('LED3', 'LED4') and d['changed']] == []
    changed = [d for d in details if d['changed']]
    assert len(changed) == 662
    assert sum(d['designator'].startswith('LED') for d in changed) == 330
    assert sum(d['designator'].startswith('C') for d in changed) == 332
    report = {'boardName': 'v2_1', 'pcbUuid': board_meta['pcb'], 'sourceFile': PROJECT.name,
              'xlsxSource': COORDINATES.name, 'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (PROJECT, COORDINATES)},
              'componentCount': len(parts), 'changedLedCount': 330, 'changedCapCount': 332,
              'unchangedCount': len(details) - len(changed), 'packageCounts': dict(package_counts),
              'maxima': maxima, 'maxCorrectionMm': max(d['deltaNormMm'] for d in changed),
              'method': 'Actual final PCB pose; placement centre of original SMD lands, excluding added via pads; no layout movement, ring reconstruction, or rotation change.',
              'sourceClassification': 'Added via pads made SMD=No; physical LEDs/capacitors are marked Yes for assembly.',
              'sourcePadCountPreserved': True, 'sourceRowOrderPreserved': True}
    payload = {'sheetName': sheet_name, 'source': str(COORDINATES), 'header': header, 'sourceRows': table,
               'correctedRows': corrected, 'changes': changes, 'details': details, 'report': report}
    (ROOT / 'support/analysis.json').write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf-8')
    (ROOT / 'support/verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
