"""Classify native sketch regions against the user's existing 1 mm wall strips."""
import json,math
from pathlib import Path
import numpy as np
from shapely.geometry import LineString,Polygon,Point
from shapely.ops import polygonize,unary_union

ROOT=Path(__file__).parent
data=json.loads((ROOT/'baffle-sketch-inspection.json').read_text(encoding='utf-8'))
segments=[s for s in data['草图6']['segments'] if not s['construction']]
circles=[s for s in segments if s['type']==1]
lines=[s for s in segments if s['type']==0]
assert len(circles)==20 and len(lines)==48
walls=[]
for inner,outer in zip(circles[10:],circles[:10]):
    r=inner['circle']['radius'];r2=outer['circle']['radius']
    assert abs(r2-r-1)<1e-6
    walls.append(Point(0,0).buffer(r2,resolution=4096).difference(Point(0,0).buffer(r,resolution=4096)))
for a,b in zip(lines[:24],lines[24:]):
    walls.append(Polygon([a['start'][:2],a['end'][:2],b['end'][:2],b['start'][:2]]))
wall=unary_union(walls)

def edge_path(edge):
    p=edge['params'];a=[x*1000 for x in p[:2]];b=[x*1000 for x in p[3:5]]
    if 'circle' not in edge:return [a,b]
    c=edge['circle'];cx,cy=c[0]*1000,c[1]*1000;r=c[6]*1000
    start=math.atan2(a[1]-cy,a[0]-cx)
    sweep=(p[7]-p[6])*(1 if c[5]>0 else -1)
    count=max(2,math.ceil(abs(sweep)/math.radians(.05))+1)
    points=[[cx+r*math.cos(t),cy+r*math.sin(t)] for t in np.linspace(start,start+sweep,count)]
    points[0]=a;points[-1]=b
    return points

regions=json.loads((ROOT/'baffle-regions-inspection.json').read_text(encoding='utf-8'))
selected=[];polys=[];report=[]
for region in regions:
    paths=[LineString([(round(x,7),round(y,7)) for x,y in edge_path(e)]) for e in region['edges']]
    candidates=list(polygonize(paths))
    assert candidates,region['index']
    poly=max(candidates,key=lambda p:p.boundary.length)
    overlap=poly.intersection(wall).area/poly.area
    choose=overlap>.99
    if choose:
        selected.append(region['index']);polys.append(poly)
    elif overlap>.01:
        raise AssertionError(('Ambiguous region',region['index'],poly.area,overlap))
    report.append({'index':region['index'],'areaMm2':poly.area,'wallCoverage':overlap,'selected':choose})
chosen=unary_union(polys)
missing=wall.difference(chosen.buffer(.0002)).area
extra=chosen.difference(wall.buffer(.0002)).area
assert missing<.1 and extra<.1,(missing,extra)
result={'selectedRegionIndices':selected,'regionCount':len(regions),'selectedCount':len(selected),
        'wallAreaMm2':wall.area,'selectedAreaMm2':chosen.area,'expectedVolumeMm3':chosen.area*8,
        'missingAreaMm2':missing,'extraAreaMm2':extra,'connectedWallBodies':len(chosen.geoms) if hasattr(chosen,'geoms') else 1,'regions':report}
(ROOT/'baffle-region-selection.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='regions'},indent=2))
