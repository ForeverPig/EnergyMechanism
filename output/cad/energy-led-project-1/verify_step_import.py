"""Reimport the generated STEP in SolidWorks; do not save the imported document."""
import json
import pythoncom
import win32com.client
from win32com.client import VARIANT
from cad_geometry import ROOT
from build_solidworks import value

app=win32com.client.GetActiveObject('SldWorks.Application')
path=ROOT/'energy-board-3d.step'
error=VARIANT(pythoncom.VT_BYREF|pythoncom.VT_I4,0)
import_data=app.GetImportFileData(str(path))
assert import_data is not None
import_data.MapConfigurationData=False
doc=app.LoadFile4(str(path),'r',import_data,error)
assert doc is not None and error.value==0, error.value
try:
    bodies=list(doc.GetBodies2(0,False) or [])
    assert len(bodies)==665, len(bodies)
    volume=sum(body.GetMassProperties(1.0)[3] for body in bodies)*1e9
    original=json.loads((ROOT/'solidworks-validation.json').read_text(encoding='utf-8'))
    assert abs(volume-original['model3d']['volumeMm3'])<.001,volume
    boxes=[list(value(body,'GetBodyBox')) for body in bodies]
    bbox=[min(box[i] for box in boxes)*1000 for i in range(3)]+[max(box[i] for box in boxes)*1000 for i in range(3,6)]
    assert all(abs(a-b)<1e-5 for a,b in zip(bbox,[-150,-150,0,150,150,3.2])),bbox
    report={'stepReimported':True,'loadErrors':error.value,'solidBodies':len(bodies),'volumeMm3':volume,'boundingBoxMm':bbox}
    (ROOT/'step-import-validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report),flush=True)
finally:
    app.CloseDoc(value(doc,'GetTitle'))
