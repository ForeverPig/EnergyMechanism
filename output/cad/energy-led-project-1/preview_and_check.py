"""Generate technical previews and independently check analytic target boundaries."""
import collections
import hashlib
import json
import math
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, Arc, Polygon
import numpy as np
from PIL import Image
from cad_geometry import ROOT, make_layers, inside_target, endpoints, validate_target

data = json.loads((ROOT / 'cad-geometry.json').read_text(encoding='utf-8'))
layers = json.loads((ROOT / 'sketch-entities.json').read_text(encoding='utf-8'))
assert hashlib.sha256(Path(data['sourceFile']).read_bytes()).hexdigest() == data['sourceSha256']
assert hashlib.sha256((ROOT / 'project-source.json').read_bytes()).hexdigest() == data['sourceSha256']
validate_target(layers['TARGET_PATTERN'])
assert len(layers['RING_BOUNDARIES']) == 10
assert len(layers['MOUNT_DRILL']) == 16
assert len(layers['LED_CENTRES']) == 332
for e in layers['TARGET_PATTERN']:
    if e['type'] in ['arc', 'circle']:
        r = e['radius']
        for t in [.2, .5, .8]:
            a = e.get('start', 0) + t * (e.get('end', 2*math.pi) - e.get('start', 0))
            p, q = [(rr*math.cos(a), rr*math.sin(a)) for rr in [r-.0001, r+.0001]]
            assert inside_target(*p, data) != inside_target(*q, data)
    else:
        a,b=e['a'],e['b'];dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
        nx,ny=-dy/length*.0001,dx/length*.0001
        for t in [.2,.5,.8]:
            x,y=a[0]+dx*t,a[1]+dy*t
            assert inside_target(x+nx,y+ny,data)!=inside_target(x-nx,y-ny,data)


def draw_entities(ax, entities, color, width=1):
    for e in entities:
        t=e['type']
        if t=='circle':
            ax.add_patch(Circle(e['center'],e['radius'],fill=False,color=color,linewidth=width))
        elif t=='arc':
            ax.add_patch(Arc(e['center'],2*e['radius'],2*e['radius'],theta1=math.degrees(e['start']),theta2=math.degrees(e['end']),color=color,linewidth=width))
        elif t=='line':
            ax.plot([e['a'][0],e['b'][0]],[e['a'][1],e['b'][1]],color=color,linewidth=width)


fig,axes=plt.subplots(1,2,figsize=(13,6.8),layout='constrained')
for ax in axes:
    ax.set_aspect('equal');ax.set_xlim(-157,157);ax.set_ylim(-157,157)
    ax.set_xlabel('X / mm');ax.set_ylabel('Y / mm');ax.grid(alpha=.14)
    draw_entities(ax,layers['PCB_OUTLINE'],'#202020',1.1)
    draw_entities(ax,layers['RING_BOUNDARIES'],'#bcc3cb',.55)
    draw_entities(ax,layers['TARGET_PATTERN'],'#d42238',1.2)
    draw_entities(ax,layers['MOUNT_DRILL'],'#232323',.8)
    draw_entities(ax,layers['MOUNT_PAD_OD'],'#809090',.6)
axes[0].set_title('Independent target sketch + 10 scoring rings')
for part in data['board']['leds']+data['board']['capacitors']:
    from cad_geometry import rectangle
    color='#426b99' if part.get('kind')=='5050' else '#d98a24' if part.get('kind')=='small' else '#7d6655'
    axes[1].add_patch(Polygon(rectangle(part),closed=True,facecolor=color,edgecolor=color,linewidth=.25))
axes[1].set_title('Actual JSON layout: 308 large + 24 small + 332 capacitors')
fig.savefig(ROOT/'layout-preview.png',dpi=160)
fig.savefig(ROOT/'layout-preview.svg')
plt.close(fig)
if (ROOT/'solidworks-3d-preview.bmp').exists():
    Image.open(ROOT/'solidworks-3d-preview.bmp').save(ROOT/'solidworks-3d-preview.png')
report={'sourceHashMatches':True,'targetBoundarySamplesChecked':150,'targetOutlineClosed':True,
        'targetEntityTypes':dict(collections.Counter(e['type'] for e in layers['TARGET_PATTERN'])),
        'ringBoundariesMm':data['ringBoundariesMm'],'ledBodies':332,'capacitorBodies':332,'mountingHoles':16,
        'geometryUnits':'mm','nativeSolidworksVersion':'2024','targetIsSketchOnly':True}
(ROOT/'geometry-validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report),flush=True)
