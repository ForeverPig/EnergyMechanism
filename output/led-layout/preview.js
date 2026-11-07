(function (L,P) {
  'use strict';
  const root = document.getElementById('energy-led-layout');
  if (!root || !L || !P || root.dataset.ledInitialized) return;
  root.dataset.ledInitialized = 'true';
  const ns = 'http://www.w3.org/2000/svg';
  const fields = Object.fromEntries(Array.from(root.querySelectorAll('[data-field]')).map(e => [e.dataset.field, e]));
  const defaults = {layout:'hybrid341', pitch:8, packageSize:2, smallSize:2.2, supplement:true, mode:'target', rings:[2,4,6,8,10], sensor:true, outline:false, glow:false,capacitors:true,chain:true};
  let state = {...defaults};
  let leds = [];
  let geometryKey = '';
  let minimum = 0;
  let overlaps = 0;
  let bodyGap = 0;
  let pcbModel = null;
  let pcbBody = null;
  let pcbEnvelope = null;

  function restore(s) {
    if (!s || typeof s !== 'object') return;
    if (['polar1','polar2','hex','hybrid341'].includes(s.layout)) state.layout = s.layout;
    if (['target','rings'].includes(s.mode)) state.mode = s.mode;
    if (Number(s.pitch)>=5 && Number(s.pitch)<=16) state.pitch=Number(s.pitch);
    if ([2,3.5,5].includes(Number(s.packageSize))) state.packageSize=Number(s.packageSize);
    if ([2,2.2,3.5].includes(Number(s.smallSize))) state.smallSize=Number(s.smallSize);
    if (Array.isArray(s.rings)) state.rings=s.rings.filter(v=>Number.isInteger(v)&&v>=1&&v<=10);
    for (const k of ['sensor','outline','glow','supplement','capacitors','chain']) if (typeof s[k]==='boolean') state[k]=s[k];
  }
  // Version 12 corrects the small-cap shift, removes R52.5 smalls and adds R90 holes.
  try {
    const saved=window.openai?.widgetState;
    if([4,5,6,7,8,9,10,11,12].includes(saved?.modelContent?.schema)) {
      restore(saved.modelContent);
      restore(saved.privateContent);
    }
  } catch (_) { /* The first render must not depend on the host bridge. */ }

  function svgEl(tag, attrs={}, parent) {
    const e=document.createElementNS(ns,tag);
    for (const [k,v] of Object.entries(attrs)) e.setAttribute(k,String(v));
    if(parent) parent.appendChild(e);
    return e;
  }

  function targetShapes(group, scale, ox, oy, fill, opacity=1) {
    // Use a clip so the trapezoids remain inside the D300 board.
    for(const [inner,outer] of L.TARGET_BANDS) {
      svgEl('circle',{cx:ox,cy:oy,r:(inner+outer)/2*scale,fill:'none',stroke:fill,
        'stroke-width':(outer-inner)*scale,opacity},group);
    }
    const innerHalf=L.SPOKE.outerHalfWidth-100*Math.tan(7*Math.PI/180);
    for(let k=0;k<4;k++) {
      const a=k*Math.PI/2;
      const points=[[50,-innerHalf],[150,-17.5],[150,17.5],[50,innerHalf]].map(([t,u])=>{
        const x=t*Math.cos(a)-u*Math.sin(a), y=t*Math.sin(a)+u*Math.cos(a);
        return `${ox+x*scale},${oy-y*scale}`;
      }).join(' ');
      svgEl('polygon',{points,fill,opacity},group);
    }
  }

  function ringShapes(group, scale, ox, oy, fill, opacity=1) {
    for(const ring of state.rings) {
      const inner=(10-ring)*15, outer=inner+15;
      svgEl('circle',{cx:ox,cy:oy,r:(inner+outer)/2*scale,fill:'none',stroke:fill,
        'stroke-width':15*scale,opacity},group);
    }
  }

  function draw(svg, ideal) {
    const width=Math.max(250, svg.parentElement.clientWidth);
    const height=width+28;
    svg.setAttribute('viewBox',`0 0 ${width} ${height}`);
    svg.replaceChildren();
    const title=svgEl('title',{},svg);
    title.textContent=ideal?'理想连续轮廓':'灯珠中心按当前图案判断开关';
    const s=(width-28)/320, x=width/2, y=width/2;
    const defs=svgEl('defs',{},svg);
    const clipId=`energy-clip-${ideal?'ideal':'leds'}`;
    const clip=svgEl('clipPath',{id:clipId},defs);
    svgEl('circle',{cx:x,cy:y,r:150*s},clip);
    svgEl('circle',{cx:x,cy:y,r:150*s,fill:'var(--muted)'},svg);
    const shapeGroup=svgEl('g',{'clip-path':`url(#${clipId})`},svg);
    if(ideal || state.outline) {
      shapeGroup.setAttribute('opacity',ideal?.9:.16);
      (state.mode==='target'?targetShapes:ringShapes)(shapeGroup,s,x,y,'var(--viz-series-1)',1);
    }
    if(state.sensor) {
      for(const r of L.SENSOR_BOUNDARIES) {
        svgEl('circle',{cx:x,cy:y,r:r*s,fill:'none',stroke:'var(--foreground)',
          'stroke-opacity':.3,'stroke-width':.7},svg);
      }
      svgEl('circle',{cx:x,cy:y,r:155*s,fill:'none',stroke:'var(--foreground)',
        'stroke-opacity':.6,'stroke-width':1,'stroke-dasharray':'3 3'},svg);
    }
    if(!ideal) {
      if(state.chain&&state.layout==='hybrid341') {
        const chain=svgEl('g',{'data-chain':true},svg);
        const marker=svgEl('marker',{id:'energy-chain-direction',viewBox:'0 0 8 8',refX:4,refY:4,
          markerWidth:4,markerHeight:4,markerUnits:'userSpaceOnUse',orient:'auto'},defs);
        svgEl('polygon',{points:'0,0 8,4 0,8',fill:'var(--viz-series-3)'},marker);
        for(let i=0;i<leds.length-1;i++) {
          const from=leds[i],to=leds[i+1];
          const seam=from.row!==to.row&&L.ENTRY_ROWS.includes(to.row)&&to.kind==='small';
          const seamRadius=seam?'R'+(to.row*15+7.5):'';
          const ax=x+from.x*s,ay=y-from.y*s,bx=x+to.x*s,by=y-to.y*s;
          svgEl('polyline',{points:`${ax},${ay} ${(ax+bx)/2},${(ay+by)/2} ${bx},${by}`,
            fill:'none',stroke:'var(--viz-series-3)','stroke-width':seam?1.5:.65,'stroke-opacity':.65,
            'marker-mid':'url(#energy-chain-direction)','data-chain-from':from.id+1,'data-chain-to':to.id+1,
            'data-chain-seam':seamRadius,
            'data-tooltip':`LED${from.id+1} DOUT → LED${to.id+1} DIN${seam?'（'+seamRadius+'圈入口）':''}`},chain);
        }
      }
      const dots=svgEl('g',{},svg);
      for(const p of leds) {
        const lit=L.isLit(p,state.mode,state.rings);
        if(lit&&state.glow) {
          svgEl('circle',{cx:x+p.x*s,cy:y-p.y*s,r:4.5*s,fill:'var(--viz-series-1)',opacity:.16},dots);
          svgEl('circle',{cx:x+p.x*s,cy:y-p.y*s,r:3*s,fill:'var(--viz-series-1)',opacity:.24},dots);
        }
        const size=p.sizeMm*s;
        const px=x+p.x*s,py=y-p.y*s;
        svgEl('rect',{x:px-size/2,y:py-size/2,width:size,height:size,
          transform:`rotate(${-p.rotationDeg} ${px} ${py})`,
          fill:lit?'var(--viz-series-1)':'var(--foreground)',opacity:lit?1:.15,
          'data-led-id':p.id,'data-lit':String(lit),'data-package':p.kind,'data-radius':p.r.toFixed(6),
          'data-rotation':p.rotationDeg,
          'data-tooltip':`LED${p.id+1} · ${p.kind==='uniform'?p.sizeMm+'mm':p.kind==='5050'?'5050':'补光 '+p.sizeMm+'mm'} · ${p.rotationDeg.toFixed(1)}° · R${p.r.toFixed(1)} mm · ${p.ring} 环 · (${p.x.toFixed(1)}, ${p.y.toFixed(1)}) mm · ${lit?'亮':'灭'}`},dots);
        if(state.layout==='hybrid341') {
          const pin=P.rotate(-p.sizeMm/2+0.35,p.sizeMm/2-0.35,p.rotationDeg);
          svgEl('circle',{cx:px+pin.x*s,cy:py-pin.y*s,r:Math.max(.35*s,.7),fill:'var(--foreground)',opacity:.8,'data-pin-one':p.id,'data-tooltip':`LED${p.id+1} · 0°左上角标记（小灯供电脚需核对实际封装）`},dots);
        }
      }
      if(state.capacitors&&pcbModel) {
        for(const cap of pcbModel.capacitors) {
          const px=x+cap.x*s,py=y-cap.y*s;
          const collision=pcbBody.overlapPairs.some(pair=>pair.includes(cap.designator));
          svgEl('rect',{x:px-cap.widthMm*s/2,y:py-cap.heightMm*s/2,width:cap.widthMm*s,height:cap.heightMm*s,
            transform:`rotate(${-cap.rotationDeg} ${px} ${py})`,fill:'var(--viz-series-2)',
            stroke:collision?'var(--destructive)':'none','stroke-width':collision?1:0,
            'data-capacitor-id':cap.ledId,'data-rotation':cap.rotationDeg,'data-body-overlap':String(collision),
            'data-tooltip':`${cap.designator} · LED${cap.ledId+1} · 100nF / 0402 · ${cap.rotationDeg.toFixed(1)}° · (${cap.x.toFixed(2)}, ${cap.y.toFixed(2)}) mm`},dots);
        }
      }
      for(const hole of pcbModel?.mountingHoles??[]) {
        const px=x+hole.x*s,py=y-hole.y*s;
        const group=svgEl('g',{'data-screw-id':hole.designator,
          'data-tooltip':`${hole.designator} · 钻孔 Ø${hole.drillDiameterMm} / 焊盘 Ø${hole.outerDiameterMm} mm · (${hole.x.toFixed(3)}, ${hole.y.toFixed(3)}) mm · ${hole.ringBoundary}环交界`},svg);
        svgEl('circle',{cx:px,cy:py,r:hole.outerDiameterMm/2*s,fill:'var(--viz-series-4)',opacity:.85},group);
        svgEl('circle',{cx:px,cy:py,r:hole.drillDiameterMm/2*s,fill:'var(--background)'},group);
        svgEl('circle',{cx:px,cy:py,r:Math.max(5,hole.outerDiameterMm/2*s),fill:'transparent'},group);
      }
      if(state.chain&&state.layout==='hybrid341') {
        for(const [led,anchor,label] of [[leds[0],'start','1 起点'],[leds.at(-1),'end',`${leds.length} 终点`]]) {
          const px=x+led.x*s,py=y-led.y*s;
          svgEl('circle',{cx:px,cy:py,r:3,fill:'none',stroke:'var(--viz-series-3)','stroke-width':1.2,'data-chain-endpoint':anchor},svg);
          const labelEl=svgEl('text',{x:px+(anchor==='start'?6:-6),y:py-7,'text-anchor':anchor,class:'text-small'},svg);
          labelEl.textContent=label;
        }
      }
    }
    svgEl('circle',{cx:x,cy:y,r:150*s,fill:'none',stroke:'var(--foreground)',
      'stroke-opacity':.7,'stroke-width':1},svg);
    const text=svgEl('text',{x,y:height-6,'text-anchor':'middle'},svg);
    text.textContent='灯板 Ø300 mm';
  }

  function sync() {
    for(const [k,e] of Object.entries(fields)) {
      if(e.type==='checkbox') e.checked=state[k]; else e.value=String(state[k]);
    }
    root.querySelector('[data-value="pitch"]').textContent=`${state.pitch} mm`;
    root.querySelectorAll('[data-regular-field]').forEach(e=>e.hidden=state.layout==='hybrid341');
    root.querySelectorAll('[data-hybrid-field]').forEach(e=>e.hidden=state.layout!=='hybrid341');
    root.querySelector('[data-ring-panel]').hidden=state.mode!=='rings';
    for(const button of root.querySelectorAll('[data-ring]')) button.setAttribute('aria-pressed',String(state.rings.includes(Number(button.dataset.ring))));
  }

  function update() {
    sync();
    const key=`${state.layout}/${state.pitch}/${state.packageSize}/${state.smallSize}/${state.supplement}`;
    if(key!==geometryKey) {
      leds=L.generate(state);minimum=L.minDistance(leds);overlaps=L.bodyOverlaps(leds);bodyGap=L.minBodyGap(leds);
      pcbModel=state.layout==='hybrid341'?P.generate(leds):null;
      pcbBody=pcbModel?P.stats(pcbModel.parts):null;
      pcbEnvelope=pcbModel?P.stats(pcbModel.parts,true):null;
      geometryKey=key;
    }
    const on=leds.filter(p=>L.isLit(p,state.mode,state.rings)).length;
    const counts=state.layout==='hybrid341'?`5050 ${leds.filter(p=>p.kind==='5050').length} · 十字小灯 ${leds.filter(p=>p.kind==='small').length} · 合计 ${leds.length}`:`总灯数 ${leds.length}`;
    root.querySelector('[data-status]').textContent=`${counts} · 当前点亮 ${on} · 最小中心距 ${minimum.toFixed(2)} mm`;
    const pcbStatus=root.querySelector('[data-pcb-status]');
    pcbStatus.hidden=!pcbModel;
    if(pcbModel) pcbStatus.textContent=`0402 / 100nF 电容 ${pcbModel.capacitors.length} · 安装孔 ${pcbModel.mountingHoles.length} · 本体相交 ${pcbBody.overlaps} 处 · 假定包络相交 ${pcbEnvelope.overlaps} 处（需实际焊盘核对）`;
    const pcbWarning=root.querySelector('[data-pcb-warning]');
    const capPairs=pcbBody?.overlapPairs.filter(pair=>pair.some(ref=>ref.startsWith('C')))??[];
    pcbWarning.hidden=capPairs.length===0;
    pcbWarning.textContent=capPairs.length?`电容与LED本体相交：${capPairs.map(pair=>pair.join(' / ')).join('；')}。`:'';
    const omitted=state.layout==='hybrid341'&&state.supplement?L.SMALL_COUNT-leds.filter(p=>p.kind==='small').length:0;
    const touching=!overlaps&&bodyGap<1e-7;
    const warning=root.querySelector('[data-warning]');
    warning.hidden=overlaps===0&&omitted===0&&!touching;
    warning.textContent=[overlaps?`灯珠本体重叠 ${overlaps} 处。`:'',touching?'灯珠本体相贴，最小间隙为0。':'',omitted?`${omitted} 个补光候选灯位未通过保守板边余量检查。`:'',overlaps||omitted||touching?'需调整封装或灯位；建议此方案使用2020。':''].join(' ');
    root.querySelectorAll('[data-plot]').forEach(svg=>draw(svg,svg.dataset.plot==='ideal'));
  }

  function persist() {
    try {
      if(typeof window.openai?.setWidgetState !== 'function') return;
      Promise.resolve(window.openai.setWidgetState({modelContent:{schema:12,layout:state.layout,pitch:state.pitch,packageSize:state.packageSize,smallSize:state.smallSize,supplement:state.supplement,mode:state.mode,rings:state.rings,ledFacing:'5050-outward-small-inward',capPackage:'0402',capOrientation:'led-plus-180',capPosition:'small-own-centre-rotation-local-left-1.5',screwDrillMm:3,screwPadMm:5.8,idOrder:L.CHAIN_ORDER,revision:'20261003-ring7-small-removed-cap-left-r90-screws'},
        privateContent:{sensor:state.sensor,outline:state.outline,glow:state.glow,capacitors:state.capacitors,chain:state.chain}})).catch(()=>{});
    } catch (_) { /* Host RPC failure must not interrupt local interactions. */ }
  }

  const ringButtons=root.querySelector('[data-ring-buttons]');
  for(let ring=1;ring<=10;ring++) {
    const button=document.createElement('button');
    button.className='btn';button.type='button';button.dataset.ring=String(ring);button.textContent=`${ring} 环`;
    ringButtons.appendChild(button);
    button.addEventListener('click',()=>{
      state.rings=state.rings.includes(ring)?state.rings.filter(v=>v!==ring):[...state.rings,ring].sort((a,b)=>a-b);
      update();persist();
    });
  }
  for(const [k,e] of Object.entries(fields)) e.addEventListener(e.type==='range'?'input':'change',()=>{
    state[k]=e.type==='checkbox'?e.checked:['pitch','packageSize','smallSize'].includes(k)?Number(e.value):e.value;
    update();persist();
  });
  for(const e of root.querySelectorAll('[data-action]')) e.addEventListener('click',()=>{
    state.rings=e.dataset.action==='all'?[1,2,3,4,5,6,7,8,9,10]:e.dataset.action==='none'?[]:[2,4,6,8,10];
    update();persist();
  });
  for(const e of root.querySelectorAll('[data-export]')) e.addEventListener('click',()=>{
    const ext=e.dataset.export;
    const data=ext==='pcb'?P.coordinateCsv(pcbModel?.parts??P.ledParts(leds)):ext==='csv'?L.csv(leds):ext==='h'?L.cHeader(leds):JSON.stringify({units:'mm',origin:'centre',yAxis:'up',idOrder:state.layout==='hybrid341'?L.CHAIN_ORDER:'reference-layout',config:state,leds,capacitors:pcbModel?.capacitors??[],mountingHoles:pcbModel?.mountingHoles??[],chainRings:state.layout==='hybrid341'?L.chainRings(leds):[]},null,2);
    const url=URL.createObjectURL(new Blob([data],{type:ext==='json'?'application/json':'text/plain;charset=utf-8'}));
    const a=document.createElement('a');a.href=url;a.download=`led-layout-${state.layout}-${state.layout==='hybrid341'?leds.length+'beads':state.pitch+'mm'}${ext==='pcb'?'-pcb':''}.${ext==='pcb'?'csv':ext}`;a.click();
    setTimeout(()=>URL.revokeObjectURL(url),1000);
  });
  function onGlobals(e) {
    const saved=e.detail?.globals?.widgetState;
    if([4,5,6,7,8,9,10,11,12].includes(saved?.modelContent?.schema)) {
      const previous=JSON.stringify(state);
      restore(saved.modelContent);restore(saved.privateContent);
      if(JSON.stringify(state)!==previous) update();
    }
  }
  window.addEventListener('openai:set_globals',onGlobals);
  update();
  // Height changes (including the host sizing its iframe) do not alter the plots.
  // Observe only plot-container width, coalesce callbacks, and stop after removal.
  const plots=root.querySelector('.led-plots');
  let plotWidth=plots.getBoundingClientRect().width;
  let resizeFrame=0;
  if(typeof ResizeObserver==='function') {
    const observer=new ResizeObserver(entries=>{
      if(!root.isConnected) {
        observer.disconnect();
        window.removeEventListener('openai:set_globals',onGlobals);
        if(resizeFrame) cancelAnimationFrame(resizeFrame);
        return;
      }
      const width=entries[0].contentRect.width;
      if(Math.abs(width-plotWidth)<0.5) return;
      plotWidth=width;
      if(resizeFrame) return;
      resizeFrame=requestAnimationFrame(()=>{
        resizeFrame=0;
        if(root.isConnected) root.querySelectorAll('[data-plot]').forEach(svg=>draw(svg,svg.dataset.plot==='ideal'));
      });
    });
    observer.observe(plots);
  }
})(typeof LedLayout !== 'undefined' ? LedLayout : window.LedLayout,typeof PcbLayout !== 'undefined' ? PcbLayout : window.PcbLayout);
