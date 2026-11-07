"""Apply the requested extrusion to the user's currently open SolidWorks part."""
import json,shutil,math
from datetime import datetime
from pathlib import Path
import pythoncom,win32com.client
from win32com.client import VARIANT

ROOT=Path(__file__).parent
TARGET=ROOT/'energy-board-3d.SLDPRT'
NAME='挡光板_8mm'
def v(obj,key,*args):
    m=getattr(obj,key)
    return m(*args) if callable(m) and not hasattr(m,'_oleobj_') else m

app=win32com.client.GetActiveObject('SldWorks.Application')
doc=v(app,'ActiveDoc')
assert Path(v(doc,'GetPathName')).resolve()==TARGET.resolve(),'Active part changed'
assert doc.FeatureByName(NAME) is None,'Baffle extrusion already exists; do not duplicate'
profile=doc.FeatureByName('草图6')
assert profile is not None
sketch=v(profile,'GetSpecificFeature2')
segments=v(sketch,'GetSketchSegments') or []
assert len(segments)==102 and sum(bool(v(s,'ConstructionGeometry')) for s in segments)==34,'Sketch changed'
assert not v(doc,'GetSaveFlag'),'User has new unsaved edits; refresh inspection before continuing'
before=len(doc.GetBodies2(0,False) or [])
assert before==665,before
backup=ROOT/('energy-board-3d-before-baffle8mm-'+datetime.now().strftime('%Y%m%d-%H%M%S')+'.SLDPRT')
shutil.copy2(TARGET,backup)
print('BACKUP',backup.name,backup.stat().st_size,flush=True)

if v(doc,'GetActiveSketch2') is not None:
    assert len(v(v(doc,'GetActiveSketch2'),'GetSketchSegments') or [])==102,'Another sketch is active'
    doc.SketchManager.InsertSketch(True)
assert v(doc,'GetActiveSketch2') is None
doc.ClearSelection2(True)
regions=list(v(sketch,'GetSketchRegions') or [])
selection=json.loads((ROOT/'baffle-region-selection.json').read_text(encoding='utf-8'))
assert len(regions)==selection['regionCount']
assert profile.Select2(False,0)
selectData=v(doc.SelectionManager,'CreateSelectData')
selectData.Mark=4
for index in selection['selectedRegionIndices']:
    assert regions[index].Select2(True,selectData),('Region selection failed',index)
assert doc.SelectionManager.GetSelectedObjectCount2(4)==selection['selectedCount']
print('SELECTED',selection['selectedCount'],'wall regions',flush=True)
feature=doc.FeatureManager.FeatureExtrusion3(True,False,False,0,0,.008,.008,
    False,False,False,False,0.0,0.0,False,False,False,False,False,True,True,0,0.0,False)
assert feature is not None,'Extrusion was not created'
feature.Name=NAME
definition=v(feature,'GetDefinition')
depth=definition.GetDepth(True)*1000
assert abs(depth-8)<1e-8,depth
warning=VARIANT(pythoncom.VT_BYREF|pythoncom.VT_BOOL,False)
error=feature.GetErrorCode2(warning)
assert error==0,(error,warning.value)
assert v(doc,'EditRebuild3'),'Rebuild failed'
print('EXTRUSION',v(feature,'Name'),'depth',depth,'featureError',error,flush=True)

bodies=list(doc.GetBodies2(0,False) or [])
new=[]
for body in bodies:
    box=[x*1000 for x in v(body,'GetBodyBox')]
    if box[5]>3.21:
        assert abs(box[2]-1.6)<1e-6 and abs(box[5]-9.6)<1e-6,box
        volume=body.GetMassProperties(1.0)[3]*1e9
        new.append((body,box,volume))
assert len(bodies)-before==len(new)==selection['connectedWallBodies'],(len(bodies),before,len(new))
actualVolume=sum(x[2] for x in new)
assert abs(actualVolume-selection['expectedVolumeMm3'])<.05,(actualVolume,selection['expectedVolumeMm3'])
new.sort(key=lambda x:max(abs(c) for c in x[1][:2]+x[1][3:5]))
for i,(body,box,volume) in enumerate(new,1):
    body.Name=f'挡光板_{i}'
    body.MaterialPropertyValues2=VARIANT(pythoncom.VT_ARRAY|pythoncom.VT_R8,[.12,.12,.12,.3,.7,.15,.1,0,0])
doc.ClearSelection2(True)
doc.ShowNamedView2('',7)
v(doc,'ViewZoomtofit2')
v(doc,'GraphicsRedraw2')
errors=VARIANT(pythoncom.VT_BYREF|pythoncom.VT_I4,0)
warnings=VARIANT(pythoncom.VT_BYREF|pythoncom.VT_I4,0)
ok=doc.Save3(1,errors,warnings)
assert ok and errors.value==0,(ok,errors.value,warnings.value)
report={'file':str(TARGET),'backup':str(backup),'feature':NAME,'sourceSketch':'草图6',
        'extrusionDepthMm':depth,'featureError':error,'startZmm':1.6,'endZmm':9.6,'mergeWithElectronics':False,
        'selectedRegions':selection['selectedCount'],'bodiesBefore':before,'bodiesAfter':len(bodies),
        'baffleBodies':len(new),'baffleVolumeMm3':actualVolume,'saveErrors':errors.value,'saveWarnings':warnings.value,
        'bodies':[{'name':v(b,'Name'),'boxMm':box,'volumeMm3':volume} for b,box,volume in new]}
try:
    report['previewSaved']=bool(doc.SaveBMP(str(ROOT/'baffle-8mm-preview.bmp'),1400,1100))
except Exception as exc:report['previewError']=str(exc)
(ROOT/'baffle-8mm-validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2),flush=True)
