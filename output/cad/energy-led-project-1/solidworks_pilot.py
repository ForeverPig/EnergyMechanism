"""Small native CAD feasibility check, saves only in this output directory."""
from pathlib import Path
import json
import pythoncom
import win32com.client
from win32com.client import VARIANT

ROOT = Path(__file__).resolve().parent
app = win32com.client.GetActiveObject('SldWorks.Application')
app.Visible = False
doc = app.NewDocument(app.GetUserPreferenceStringValue(8), 0, 0.0, 0.0)
assert doc is not None
feature = doc.FirstFeature
while feature is not None:
    name = feature.Name
    kind = feature.GetTypeName2
    print('feature', name, kind, flush=True)
    if kind == 'RefPlane':
        assert feature.Select2(False, 0)
        break
    feature = feature.GetNextFeature
sk = doc.SketchManager
sk.InsertSketch(True)
circle = sk.CreateCircleByRadius(0.0, 0.0, 0.0, 0.15)
assert circle is not None
sk.InsertSketch(True)
f = None
candidate = doc.FirstFeature
while candidate is not None:
    if candidate.GetTypeName2 == 'ProfileFeature':
        f = candidate
    candidate = candidate.GetNextFeature
assert f is not None
f.Name = 'PCB_Outline_Pilot'
assert f.Select2(False, 0)
ext = doc.FeatureManager.FeatureExtrusion3(True, False, False, 0, 0, .0016, .0016,
    False, False, False, False, 0.0, 0.0, False, False, False, False, True, True, True, 0, 0.0, False)
assert ext is not None
ext.Name = 'PCB_1p6mm_Pilot'
doc.ClearSelection2(True)
errors, warnings = VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0), VARIANT(pythoncom.VT_BYREF | pythoncom.VT_I4, 0)
null = VARIANT(pythoncom.VT_DISPATCH, None)
native = str(ROOT / 'pilot.SLDPRT')
ok = doc.Extension.SaveAs(native, 0, 1, null, errors, warnings)
print('native save', ok, errors.value, warnings.value, flush=True)
assert ok and errors.value == 0
ok = doc.Extension.SaveAs(str(ROOT / 'pilot.step'), 0, 1, null, errors, warnings)
print('step save', ok, errors.value, warnings.value, flush=True)
assert ok and errors.value == 0
print('bodies', len(doc.GetBodies2(0, False)), flush=True)
app.CloseDoc(doc.GetTitle)
