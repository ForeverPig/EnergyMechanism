import json
from pathlib import Path
import win32com.client

def v(obj,key,*args):
    m=getattr(obj,key)
    return m(*args) if callable(m) and not hasattr(m,'_oleobj_') else m

app=win32com.client.GetActiveObject('SldWorks.Application')
doc=v(app,'ActiveDoc')
sk=v(doc.FeatureByName('草图6'),'GetSpecificFeature2')
regions=list(v(sk,'GetSketchRegions') or [])
print('REGIONS',len(regions),flush=True)
report=[]
for i,region in enumerate(regions):
    edges=[]
    for edge in v(region,'GetEdges') or []:
        params=list(v(edge,'GetCurveParams2'))
        curve=v(edge,'GetCurve')
        item={'params':params}
        if v(curve,'IsCircle'):item['circle']=list(v(curve,'CircleParams'))
        edges.append(item)
    report.append({'index':i,'edges':edges})
    if i<3:print(json.dumps(report[-1]),flush=True)
Path(__file__).with_name('baffle-regions-inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('DONE',len(report),flush=True)
