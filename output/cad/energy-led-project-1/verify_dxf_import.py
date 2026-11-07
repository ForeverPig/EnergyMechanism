"""Validate the portable drawing by importing it as a SolidWorks part sketch."""
import json
import pythoncom
import win32com.client
from win32com.client import VARIANT
from cad_geometry import ROOT
from build_solidworks import value, features

app=win32com.client.GetActiveObject('SldWorks.Application')
reports=[]
for name in ['target-and-rings.dxf','energy-layout-2d.dxf']:
    p=ROOT/name
    options=app.GetImportFileData(str(p))
    prop=options._oleobj_.GetIDsOfNames('ImportMethod')
    options._oleobj_.Invoke(prop,0,pythoncom.DISPATCH_PROPERTYPUT,0,'',2)
    error=VARIANT(pythoncom.VT_BYREF|pythoncom.VT_I4,0)
    doc=app.LoadFile4(str(p),'',options,error)
    assert doc is not None and error.value==0,(name,error.value)
    try:
        all_segments=[]
        for f in features(doc):
            if value(f,'GetTypeName2')=='ProfileFeature':
                sketch=value(f,'GetSpecificFeature2')
                all_segments.extend(list(value(sketch,'GetSketchSegments') or []))
        assert len(all_segments)>=90,(name,len(all_segments))
        radii=[]
        for seg in all_segments:
            curve=value(seg,'GetCurve')
            if curve is not None and value(curve,'IsCircle'):
                radii.append(value(curve,'CircleParams')[6]*1000)
        assert abs(max(radii)-(155 if name=='energy-layout-2d.dxf' else 150))<1e-6,(name,max(radii))
        reports.append({'file':name,'nativeSketchImport':True,'loadErrors':error.value,'sketchSegments':len(all_segments),'maximumCircleRadiusMm':max(radii)})
        print(json.dumps(reports[-1]),flush=True)
    finally:
        app.CloseDoc(value(doc,'GetTitle'))
(ROOT/'dxf-import-validation.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')
