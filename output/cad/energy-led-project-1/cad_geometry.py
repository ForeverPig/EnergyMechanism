"""Exact circles/arcs/lines for the union outline of the target; units mm."""
import collections
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TAU = 2 * math.pi


def rotate(point, angle):
    x, y = point
    c, s = math.cos(angle), math.sin(angle)
    return [x * c - y * s, x * s + y * c]


def rectangle(part):
    width, height = part.get('sizeMm', part.get('widthMm')), part.get('sizeMm', part.get('heightMm'))
    return [[part['x'] + p[0], part['y'] + p[1]] for p in
            [rotate([sx * width / 2, sy * height / 2], math.radians(part['rotationDeg']))
             for sx, sy in [(-1, -1), (1, -1), (1, 1), (-1, 1)]]]


def inside_target(x, y, data):
    r = math.hypot(x, y)
    if r > data['boardRadiusMm'] + 1e-9:
        return False
    if any(a - 1e-9 <= r <= b + 1e-9 for a, b in data['targetBandsMm']):
        return True
    spoke = data['spoke']
    t, u = max(abs(x), abs(y)), min(abs(x), abs(y))
    half = spoke['outerHalfWidth'] - (spoke['outerAxial'] - t) * math.tan(math.radians(spoke['sideAngleDeg']))
    return spoke['innerAxial'] - 1e-9 <= t <= spoke['outerAxial'] + 1e-9 and u <= half + 1e-9


def unique(values, tolerance=1e-10):
    result = []
    for value in sorted(values):
        if not result or abs(value - result[-1]) > tolerance:
            result.append(value)
    return result


def circle_intersections(a, b, radius):
    dx, dy = b[0] - a[0], b[1] - a[1]
    aa, bb, cc = dx * dx + dy * dy, 2 * (a[0] * dx + a[1] * dy), a[0] ** 2 + a[1] ** 2 - radius ** 2
    disc = bb * bb - 4 * aa * cc
    if disc < -1e-8:
        return []
    root = math.sqrt(max(0, disc))
    return [max(0, min(1, t)) for t in [(-bb - root) / (2 * aa), (-bb + root) / (2 * aa)] if -1e-9 <= t <= 1 + 1e-9]


def target_outline(data):
    spoke, radius = data['spoke'], data['boardRadiusMm']
    slope = math.tan(math.radians(spoke['sideAngleDeg']))
    intercept = spoke['outerHalfWidth'] - spoke['outerAxial'] * slope
    inner_x = spoke['innerAxial']
    inner_y = intercept + slope * inner_x
    outer_x = (-intercept * slope + math.sqrt((1 + slope ** 2) * radius ** 2 - intercept ** 2)) / (1 + slope ** 2)
    outer_y = intercept + slope * outer_x
    source = []
    for k in range(4):
        points = [rotate(p, k * math.pi / 2) for p in
                  [[inner_x, -inner_y], [outer_x, -outer_y], [outer_x, outer_y], [inner_x, inner_y]]]
        # The outer end is a circle arc, represented by the candidate R150 circle.
        source.extend([(points[0], points[1]), (points[2], points[3]), (points[3], points[0])])
    radii = [r for band in data['targetBandsMm'] for r in band] + [radius]
    result = []
    for r in radii:
        angles = [0, TAU]
        for a, b in source:
            for t in circle_intersections(a, b, r):
                x, y = a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])
                angles.append(math.atan2(y, x) % TAU)
        angles = unique(angles)
        spans = []
        for a, b in zip(angles, angles[1:]):
            m = (a + b) / 2
            p, q = [(rr * math.cos(m), rr * math.sin(m)) for rr in [r - .0001, r + .0001]]
            if inside_target(*p, data) != inside_target(*q, data):
                spans.append([a, b])
        if len(spans) == 1 and abs(spans[0][1] - spans[0][0] - TAU) < 1e-9:
            result.append({'type': 'circle', 'center': [0, 0], 'radius': r})
        else:
            if len(spans) > 1 and abs(spans[0][0]) < 1e-9 and abs(spans[-1][1] - TAU) < 1e-9:
                wrapped = [spans[-1][0], spans[0][1] + TAU]
                spans = spans[1:-1] + [wrapped]
            for a, b in spans:
                result.append({'type': 'arc', 'center': [0, 0], 'radius': r, 'start': a, 'end': b})
    for a, b in source:
        cuts = unique([0, 1] + [t for r in radii for t in circle_intersections(a, b, r)])
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        nx, ny = -dy / length * .0001, dx / length * .0001
        for t0, t1 in zip(cuts, cuts[1:]):
            tm = (t0 + t1) / 2
            x, y = a[0] + dx * tm, a[1] + dy * tm
            if inside_target(x + nx, y + ny, data) != inside_target(x - nx, y - ny, data):
                result.append({'type': 'line', 'a': [a[0] + dx * t0, a[1] + dy * t0], 'b': [a[0] + dx * t1, a[1] + dy * t1]})
    return result


def endpoints(entity):
    if entity['type'] == 'line':
        return entity['a'], entity['b']
    if entity['type'] == 'arc':
        r = entity['radius']
        return tuple([r * math.cos(a), r * math.sin(a)] for a in [entity['start'], entity['end']])
    return None


def validate_target(entities):
    degree = collections.Counter()
    for entity in entities:
        ends = endpoints(entity)
        if ends:
            for p in ends:
                degree[(round(p[0], 6), round(p[1], 6))] += 1
    assert degree and all(d == 2 for d in degree.values()), ('Unclosed target outline', degree)
    assert collections.Counter(e['type'] for e in entities) == {'circle': 2, 'arc': 20, 'line': 28}


def make_layers(data):
    board = data['board']
    layers = {
        'PCB_OUTLINE': [{'type': 'circle', 'center': [0, 0], 'radius': data['boardRadiusMm']}],
        'RING_BOUNDARIES': [{'type': 'circle', 'center': [0, 0], 'radius': r} for r in data['ringBoundariesMm']],
        'TARGET_PATTERN': target_outline(data),
        'MOUNT_DRILL': [{'type': 'circle', 'center': [h['x'], h['y']], 'radius': h['drillDiameterMm'] / 2} for h in board['mountingHoles']],
        'MOUNT_PAD_OD': [{'type': 'circle', 'center': [h['x'], h['y']], 'radius': h['outerDiameterMm'] / 2} for h in board['mountingHoles']],
        'LED_5050_BODY': [], 'LED_2020_BODY': [], 'CAP_0402_BODY': [],
        'LED_CENTRES': [], 'LABELS': [],
        'SENSOR_REF': [{'type': 'circle', 'center': [0, 0], 'radius': r} for r in [150.5, 155]],
    }
    validate_target(layers['TARGET_PATTERN'])
    for part in board['leds'] + board['capacitors']:
        key = 'CAP_0402_BODY' if part.get('type') == 'capacitor' else 'LED_5050_BODY' if part['kind'] == '5050' else 'LED_2020_BODY'
        poly = rectangle(part)
        layers[key].extend({'type': 'line', 'a': a, 'b': b, 'ref': part['designator']} for a, b in zip(poly, poly[1:] + poly[:1]))
        if part.get('type') != 'capacitor':
            layers['LED_CENTRES'].append({'type': 'point', 'at': [part['x'], part['y']], 'ref': part['designator']})
            layers['LABELS'].append({'type': 'text', 'at': [part['x'] + part['sizeMm'] / 2 + .6, part['y'] + part['sizeMm'] / 2 + .6], 'value': part['designator'], 'height': 1.2})
    for hole in board['mountingHoles']:
        layers['LABELS'].append({'type': 'text', 'at': [hole['x'] + 3.2, hole['y'] + 3.2], 'value': hole['designator'], 'height': 1.4})
    return layers


def write_dxf(path, layers):
    out = []
    def add(*pairs):
        for code, value in pairs:
            out.extend([str(code), f'{value:.12g}' if isinstance(value, float) else str(value)])
    add((0, 'SECTION'), (2, 'HEADER'), (9, '$ACADVER'), (1, 'AC1009'), (9, '$INSUNITS'), (70, 4),
        (9, '$MEASUREMENT'), (70, 1), (9, '$LUNITS'), (70, 2), (9, '$LUPREC'), (70, 6), (0, 'ENDSEC'))
    add((0, 'SECTION'), (2, 'TABLES'), (0, 'TABLE'), (2, 'LTYPE'), (70, 1),
        (0, 'LTYPE'), (2, 'CONTINUOUS'), (70, 0), (3, 'Solid line'), (72, 65), (73, 0), (40, 0.0), (0, 'ENDTAB'),
        (0, 'TABLE'), (2, 'STYLE'), (70, 1), (0, 'STYLE'), (2, 'STANDARD'), (70, 0), (40, 0.0), (41, 1.0), (50, 0.0), (71, 0), (42, 2.5), (3, 'txt'), (4, ''), (0, 'ENDTAB'),
        (0, 'TABLE'), (2, 'LAYER'), (70, len(layers)))
    colors = {'TARGET_PATTERN': 1, 'RING_BOUNDARIES': 8, 'LED_5050_BODY': 5, 'LED_2020_BODY': 30, 'CAP_0402_BODY': 3, 'MOUNT_DRILL': 2, 'MOUNT_PAD_OD': 2, 'SENSOR_REF': 9, 'LABELS': 7, 'LED_CENTRES': 8, 'PCB_OUTLINE': 7}
    for name in layers:
        add((0, 'LAYER'), (100, 'AcDbSymbolTableRecord'), (100, 'AcDbLayerTableRecord'), (2, name), (70, 0), (62, colors[name]), (6, 'CONTINUOUS'))
    add((0, 'ENDTAB'), (0, 'ENDSEC'), (0, 'SECTION'), (2, 'ENTITIES'))
    for layer, entities in layers.items():
        for e in entities:
            t = e['type']
            if t in ['circle', 'arc']:
                add((0, t.upper()), (100, 'AcDbEntity'), (8, layer), (100, 'AcDbCircle'), (10, e['center'][0]), (20, e['center'][1]), (30, 0), (40, e['radius']))
                if t == 'arc':
                    add((100, 'AcDbArc'), (50, math.degrees(e['start']) % 360), (51, math.degrees(e['end']) % 360))
            elif t == 'line':
                add((0, 'LINE'), (100, 'AcDbEntity'), (8, layer), (100, 'AcDbLine'), (10, e['a'][0]), (20, e['a'][1]), (30, 0), (11, e['b'][0]), (21, e['b'][1]), (31, 0))
            elif t == 'point':
                add((0, 'POINT'), (100, 'AcDbEntity'), (8, layer), (100, 'AcDbPoint'), (10, e['at'][0]), (20, e['at'][1]), (30, 0))
            elif t == 'text':
                add((0, 'TEXT'), (100, 'AcDbEntity'), (8, layer), (100, 'AcDbText'), (10, e['at'][0]), (20, e['at'][1]), (30, 0), (40, e['height']), (1, e['value']), (7, 'STANDARD'), (100, 'AcDbText'))
    add((0, 'ENDSEC'), (0, 'EOF'))
    # R12 uses simple entities and symbol tables, without R2000 subclasses.
    out = [value for i in range(0, len(out), 2) if out[i] != '100' for value in out[i:i+2]]
    path.write_text('\n'.join(out) + '\n', encoding='ascii')


def main():
    data = json.loads((ROOT / 'cad-geometry.json').read_text(encoding='utf-8'))
    layers = make_layers(data)
    (ROOT / 'sketch-entities.json').write_text(json.dumps(layers, indent=2), encoding='utf-8')
    write_dxf(ROOT / 'energy-layout-2d.dxf', layers)
    write_dxf(ROOT / 'target-and-rings.dxf', {k: layers[k] for k in ['PCB_OUTLINE', 'RING_BOUNDARIES', 'TARGET_PATTERN', 'MOUNT_DRILL', 'MOUNT_PAD_OD']})
    print(json.dumps({k: len(v) for k, v in layers.items()}), flush=True)


if __name__ == '__main__':
    main()
