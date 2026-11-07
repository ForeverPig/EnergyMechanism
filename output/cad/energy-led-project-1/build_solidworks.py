"""Create editable SW2024 sketches and a simplified, named-body 3D model."""
import json
import math
import time
from pathlib import Path
import pythoncom
import win32com.client
from win32com.client import VARIANT
from cad_geometry import ROOT, endpoints


def value(obj, name, *args):
    member = getattr(obj, name)
    # COM dispatch objects are themselves callable via their default member;
    # they are already property results, not Python method wrappers.
    return member(*args) if callable(member) and not hasattr(member, '_oleobj_') else member


def features(doc):
    f = value(doc, 'FirstFeature')
    while f is not None:
        yield f
        f = value(f, 'GetNextFeature')


def body_list(doc):
    return list(doc.GetBodies2(0, False) or [])


def select_xy(doc):
    doc.ClearSelection2(True)
    plane = next(f for f in features(doc) if value(f, 'GetTypeName2') == 'RefPlane')
    assert plane.Select2(False, 0)


def create_sketch(doc, name, entities):
    select_xy(doc)
    manager = doc.SketchManager
    manager.InsertSketch(True)
    manager.AddToDB = True
    manager.DisplayWhenAdded = False
    try:
        for e in entities:
            kind = e['type']
            if kind == 'circle':
                segment = manager.CreateCircleByRadius(e['center'][0] / 1000, e['center'][1] / 1000, 0.0, e['radius'] / 1000)
            elif kind == 'arc':
                a, b = endpoints(e)
                segment = manager.CreateArc(0.0, 0.0, 0.0, a[0] / 1000, a[1] / 1000, 0.0, b[0] / 1000, b[1] / 1000, 0.0, 1)
            elif kind == 'line':
                a, b = e['a'], e['b']
                segment = manager.CreateLine(a[0] / 1000, a[1] / 1000, 0.0, b[0] / 1000, b[1] / 1000, 0.0)
            elif kind == 'point':
                segment = manager.CreatePoint(e['at'][0] / 1000, e['at'][1] / 1000, 0.0)
            else:
                continue
            assert segment is not None, (name, kind)
    finally:
        manager.DisplayWhenAdded = True
        manager.AddToDB = False
        manager.InsertSketch(True)
    f = [f for f in features(doc) if value(f, 'GetTypeName2') == 'ProfileFeature'][-1]
    f.Name = name
    sketch = value(f, 'GetSpecificFeature2')
    segments = list(value(sketch, 'GetSketchSegments') or [])
    expected = sum(e['type'] != 'point' for e in entities)
    assert len(segments) == expected, (name, len(segments), expected)
    print('SKETCH', name, len(segments), flush=True)
    return f


def extrusion(doc, sketch, name, height_mm, offset_mm=0.0, merge=False):
    doc.ClearSelection2(True)
    assert sketch.Select2(False, 0)
    f = doc.FeatureManager.FeatureExtrusion3(True, False, False, 0, 0, height_mm / 1000, height_mm / 1000,
        False, False, False, False, 0.0, 0.0, False, False, False, False, merge, True, True,
        3 if offset_mm else 0, offset_mm / 1000, False)
    assert f is not None, ('Extrusion failed', name)
    f.Name = name
    return f


def save(doc, path):
    doc.ClearSelection2(True)
    errors, warnings = VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0), VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0)
    ok = doc.Extension.SaveAs(str(path), 0, 1, VARIANT(pythoncom.VT_DISPATCH, None), errors, warnings)
    assert ok and errors.value == 0 and path.is_file(), (path.name, ok, errors.value, warnings.value)
    print('SAVED', path.name, path.stat().st_size, 'warnings', warnings.value, flush=True)
    return {'file': path.name, 'bytes': path.stat().st_size, 'errors': errors.value, 'warnings': warnings.value}


def set_color(body, color):
    body.MaterialPropertyValues2 = VARIANT(pythoncom.VT_ARRAY | pythoncom.VT_R8, [*color, .25, .75, .3, .3, 0.0, 0.0])


def assign_and_check(doc, parts, pcb_thickness):
    bodies = body_list(doc)
    expected = 1 + len(parts)
    assert len(bodies) == expected, (len(bodies), expected)
    remaining = {p['designator']: p for p in parts}
    volumes = []
    for body in bodies:
        box = list(value(body, 'GetBodyBox'))
        center = [(box[0] + box[3]) * 500, (box[1] + box[4]) * 500]
        volume = body.GetMassProperties(1.0)[3] * 1e9
        assert volume > 0
        if box[3] - box[0] > .299:
            body.Name = 'PCB_D300_T1p6_16xD3'
            set_color(body, [.10, .38, .23])
            assert abs(box[2]) < 1e-9 and abs(box[5] * 1000 - pcb_thickness) < 1e-7, box
        else:
            ref, part = min(remaining.items(), key=lambda item: math.hypot(item[1]['x'] - center[0], item[1]['y'] - center[1]))
            assert math.hypot(part['x'] - center[0], part['y'] - center[1]) < 1e-6, (ref, center)
            assert abs(box[2] * 1000 - pcb_thickness) < 1e-7, (ref, box)
            body.Name = ref
            set_color(body, [.85, .85, .79] if part.get('kind') == '5050' else [.95, .69, .32] if part.get('kind') == 'small' else [.32, .31, .28])
            remaining.pop(ref)
        volumes.append(volume)
    assert not remaining
    return {'bodyCount': len(bodies), 'volumeMm3': sum(volumes), 'componentsNamed': len(parts), 'pcbThicknessMm': pcb_thickness,
            'componentCenterToleranceMm': 1e-6}


def verify_native(app, path, expected_bodies, expected_sketches):
    errors, warnings = VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0), VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0)
    doc = app.OpenDoc6(str(path), 1, 1, '', errors, warnings)
    assert doc is not None and errors.value == 0, (path.name, errors.value, warnings.value)
    assert len(body_list(doc)) == expected_bodies
    actual = {value(f, 'Name'): value(f, 'GetTypeName2') for f in features(doc)}
    assert all(name in actual for name in expected_sketches), (path.name, expected_sketches, actual)
    app.CloseDoc(value(doc, 'GetTitle'))
    return {'file': path.name, 'reopened': True, 'bodyCount': expected_bodies, 'sketchesVerified': expected_sketches, 'loadWarnings': warnings.value}


def main():
    data = json.loads((ROOT / 'cad-geometry.json').read_text(encoding='utf-8'))
    layers = json.loads((ROOT / 'sketch-entities.json').read_text(encoding='utf-8'))
    app = win32com.client.GetActiveObject('SldWorks.Application')
    original_visibility = app.Visible
    app.Visible = False
    app.CommandInProgress = True
    report = {'solidworksRevision': value(app, 'RevisionNumber'), 'sourceSha256': data['sourceSha256'], 'files': [], 'reopens': []}
    created = []
    template = app.GetUserPreferenceStringValue(8)
    def new_document():
        doc = app.NewDocument(template, 0, 0.0, 0.0)
        assert doc is not None
        created.append(doc)
        return doc
    try:
        names = {'PCB_OUTLINE': 'PCB_Outline_D300', 'RING_BOUNDARIES': 'Scoring_Rings_10_Boundaries',
                 'TARGET_PATTERN': 'Target_Outline_Union', 'MOUNT_DRILL': 'Mounting_Drill_D3', 'MOUNT_PAD_OD': 'Mounting_Pad_OD_D5p8',
                 'LED_5050_BODY': 'LED_5050_Bodies_308', 'LED_2020_BODY': 'LED_2020_Bodies_24',
                 'CAP_0402_BODY': 'Capacitors_0402_Bodies_332', 'LED_CENTRES': 'LED_Centres_332', 'SENSOR_REF': 'Film_Reference_D301_D310'}
        doc = new_document()
        for key, name in names.items():
            create_sketch(doc, name, layers[key])
        assert len(body_list(doc)) == 0
        doc.ShowNamedView2('', 1)
        value(doc, 'ViewZoomtofit2')
        report['files'].append(save(doc, ROOT / 'energy-layout-sketch.SLDPRT'))
        app.CloseDoc(value(doc, 'GetTitle'));created.remove(doc)
        report['reopens'].append(verify_native(app, ROOT / 'energy-layout-sketch.SLDPRT', 0, list(names.values())))
        doc = new_document()
        target_names = []
        for key in ['PCB_OUTLINE', 'RING_BOUNDARIES', 'TARGET_PATTERN', 'MOUNT_DRILL', 'MOUNT_PAD_OD']:
            create_sketch(doc, names[key], layers[key]);target_names.append(names[key])
        doc.ShowNamedView2('', 1);value(doc, 'ViewZoomtofit2')
        report['files'].append(save(doc, ROOT / 'target-and-rings-sketch.SLDPRT'))
        app.CloseDoc(value(doc, 'GetTitle'));created.remove(doc)
        report['reopens'].append(verify_native(app, ROOT / 'target-and-rings-sketch.SLDPRT', 0, target_names))
        doc = new_document()
        pcb = create_sketch(doc, 'PCB_Profile_16_Holes', layers['PCB_OUTLINE'] + layers['MOUNT_DRILL'])
        extrusion(doc, pcb, 'PCB_Thickness_1p6mm', 1.6, merge=True)
        assert len(body_list(doc)) == 1
        for key, name, height in [('LED_5050_BODY', 'LED5050_Simplified_1p6mm', 1.6), ('LED_2020_BODY', 'LED2020_Simplified_1p6mm', 1.6), ('CAP_0402_BODY', 'Cap0402_Simplified_0p5mm', .5)]:
            profile = create_sketch(doc, name + '_Profile', layers[key])
            extrusion(doc, profile, name, height, 1.6, False)
            print('BODIES', name, len(body_list(doc)), flush=True)
        parts = data['board']['leds'] + data['board']['capacitors']
        report['model3d'] = assign_and_check(doc, parts, 1.6)
        expected_volume = (math.pi * 150**2 - sum(math.pi * (h['drillDiameterMm'] / 2)**2 for h in data['board']['mountingHoles'])) * 1.6
        expected_volume += sum(l['sizeMm']**2 * 1.6 for l in data['board']['leds']) + len(data['board']['capacitors']) * 1.0 * .5 * .5
        assert abs(report['model3d']['volumeMm3'] - expected_volume) < 1e-3, (report['model3d']['volumeMm3'], expected_volume)
        report['model3d']['expectedVolumeMm3'] = expected_volume
        for key in ['RING_BOUNDARIES', 'TARGET_PATTERN', 'MOUNT_PAD_OD']:
            create_sketch(doc, names[key], layers[key])
        assert value(doc, 'EditRebuild3')
        doc.ShowNamedView2('', 7);value(doc, 'ViewZoomtofit2');value(doc, 'GraphicsRedraw2')
        report['files'].append(save(doc, ROOT / 'energy-board-3d.SLDPRT'))
        report['files'].append(save(doc, ROOT / 'energy-board-3d.step'))
        try:
            report['previewSaved'] = bool(doc.SaveBMP(str(ROOT / 'solidworks-3d-preview.bmp'), 1400, 1100))
        except Exception as exc:
            report['previewError'] = str(exc)
        app.CloseDoc(value(doc, 'GetTitle'));created.remove(doc)
        report['reopens'].append(verify_native(app, ROOT / 'energy-board-3d.SLDPRT', 665, ['PCB_Profile_16_Holes', names['RING_BOUNDARIES'], names['TARGET_PATTERN']]))
        report['stepSolidBreps'] = (ROOT / 'energy-board-3d.step').read_text(encoding='ascii', errors='replace').count('MANIFOLD_SOLID_BREP')
        assert report['stepSolidBreps'] == 665
        (ROOT / 'solidworks-validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)
    finally:
        app.CommandInProgress = False
        app.Visible = original_visibility
        for doc in created:
            try:
                app.CloseDoc(value(doc, 'GetTitle'))
            except Exception:
                pass


if __name__ == '__main__':
    main()
