import json
from collections import Counter
from pathlib import Path
import win32com.client

ROOT = Path(__file__).parent

def v(obj, key, *args):
    member = getattr(obj, key)
    return member(*args) if callable(member) and not hasattr(member, '_oleobj_') else member

def safe(obj, key, *args):
    try:
        return v(obj, key, *args)
    except Exception as exc:
        return {'error': str(exc)}

def pt(obj):
    return [v(obj, k)*1000 for k in ('X','Y','Z')] if obj is not None else None

app = win32com.client.GetActiveObject('SldWorks.Application')
doc = v(app, 'ActiveDoc')
report = {'title':v(doc,'GetTitle'),'path':v(doc,'GetPathName'),'saveFlag':v(doc,'GetSaveFlag')}
active = v(doc,'GetActiveSketch2')
if active is not None:
    report['activeSketchSegmentCount'] = len(v(active,'GetSketchSegments') or [])
for name in ['草图6', '遮罩墙参考']:
    f = doc.FeatureByName(name)
    assert f is not None, name
    sk = v(f, 'GetSpecificFeature2')
    transform = v(sk,'ModelToSketchTransform')
    item = {'transform':list(v(transform,'ArrayData')),'segments':[]}
    for seg in v(sk,'GetSketchSegments') or []:
        x = {'type':v(seg,'GetType'), 'construction':bool(v(seg,'ConstructionGeometry'))}
        try:
            c = v(seg,'GetCurve')
            x['isCircle'] = bool(v(c,'IsCircle'))
            if x['isCircle']:
                p = list(v(c,'CircleParams'))
                x['circle'] = {'center':[t*1000 for t in p[:3]],'axis':p[3:6],'radius':p[6]*1000}
        except Exception as exc:
            x['curveError']=str(exc)
        x['start'] = safe(seg,'GetStartPoint2')
        if not isinstance(x['start'],dict):x['start']=pt(x['start'])
        x['end'] = safe(seg,'GetEndPoint2')
        if not isinstance(x['end'],dict):x['end']=pt(x['end'])
        item['segments'].append(x)
    try:
        contours=list(v(sk,'GetSketchContours') or [])
        item['contours']=[{'closed':bool(v(c,'IsClosed')), 'segments':len(v(c,'GetSketchSegments') or [])} for c in contours]
    except Exception as exc:
        item['contourError']=str(exc)
    report[name]=item
    summary={'name':name,'transform':item['transform'],'actualTypes':dict(Counter(s['type'] for s in item['segments'] if not s['construction'])),
             'circles':[s['circle'] for s in item['segments'] if not s['construction'] and s.get('circle')],
             'contours':item.get('contours'), 'contourError':item.get('contourError')}
    print(json.dumps(summary,ensure_ascii=False),flush=True)
(ROOT/'baffle-sketch-inspection.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:val for k,val in report.items() if k not in ('草图6','遮罩墙参考')},ensure_ascii=False),flush=True)
