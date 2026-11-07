(function (E,L,P,defaults) {
  'use strict';
  const $ = id => document.getElementById(id);
  const storageKey='energy-led-editor-v1-20261003';
  let project=E.create(defaults.baseline,defaults.footprints),board=null,analysis=null;
  let selectedUid=null,selectedRow=9,view={x:-160,y:-160,w:320,h:320};
  let undo=[],redo=[],drag=null,raf=0,checkTimer=0;
  const display={mode:'layout',caps:true,chain:false,labels:false};
  const svg=$('board'),ns='http://www.w3.org/2000/svg';
  function error(message) {$('error').textContent=message;$('error').hidden=!message;}
  try {
    const raw=localStorage.getItem(storageKey);
    if(raw) {
      const saved=JSON.parse(raw);
      project=E.upgrade(saved.project);
      selectedUid=typeof saved.selectedUid==='string'?saved.selectedUid:null;
      if(Number.isInteger(saved.selectedRow)&&saved.selectedRow>=0&&saved.selectedRow<=9) selectedRow=saved.selectedRow;
      if(saved.view&&['x','y','w','h'].every(k=>Number.isFinite(saved.view[k]))&&saved.view.w>=12&&saved.view.w<=700&&saved.view.h===saved.view.w) view=saved.view;
      if(saved.project.version===1) {
        localStorage.setItem(storageKey,JSON.stringify({project,selectedUid,selectedRow,view}));
        $('save-status').textContent='已恢复保存 · 最外圈小灯已转换为5050';
      } else $('save-status').textContent='已恢复自动保存';
    }
  } catch (_) {error('自动保存不可用或项目不兼容，已使用初始布局。');}
  function persist() {
    try {
      localStorage.setItem(storageKey,JSON.stringify({project,selectedUid,selectedRow,view}));
      $('save-status').textContent='已自动保存';
    } catch (_) {$('save-status').textContent='浏览器未允许自动保存，请保存项目JSON';}
  }
  function pushHistory(before) {undo.push(E.clone(before));if(undo.length>100)undo.shift();redo=[];}
  function commit(next) {
    E.validate(next);
    if(JSON.stringify(next)!==JSON.stringify(project)) {pushHistory(project);project=next;persist();}
    error('');render();
  }
  function attempt(action) {try{action();}catch(e){error(e.message);}}
  function node(tag,attrs,parent=svg) {
    const e=document.createElementNS(ns,tag);
    for(const [key,value] of Object.entries(attrs)) e.setAttribute(key,String(value));
    parent.appendChild(e);return e;
  }
  function text(content,x,y,size,parent=svg) {const e=node('text',{x,y,'font-size':size},parent);e.textContent=content;return e;}
  function draw() {
    svg.setAttribute('viewBox',`${view.x} ${view.y} ${view.w} ${view.h}`);
    svg.replaceChildren();
    const width=Math.max(280,svg.clientWidth),pixel=view.w/width,font=12*pixel;
    node('title',{},svg).textContent='灯板对称布局：拖动灯珠调整极角，空白处拖动画面';
    node('circle',{cx:0,cy:0,r:150,fill:'none',stroke:'var(--fg)','stroke-width':pixel});
    for(let r=15;r<150;r+=15) node('circle',{cx:0,cy:0,r,fill:'none',stroke:'var(--line)','stroke-width':pixel*.65});
    for(const [inner,outer] of L.TARGET_BANDS) node('circle',{cx:0,cy:0,r:(inner+outer)/2,fill:'none',stroke:'var(--active)','stroke-width':outer-inner,opacity:.07});
    const innerHalf=17.5-100*Math.tan(7*Math.PI/180);
    for(let k=0;k<4;k++) {
      const pts=[[50,-innerHalf],[150,-17.5],[150,17.5],[50,innerHalf]].map(([x,y])=>P.rotate(x,y,k*90));
      node('polygon',{points:pts.map(p=>`${p.x},${-p.y}`).join(' '),fill:'var(--active)',opacity:.07});
    }
    node('line',{x1:-150,y1:0,x2:150,y2:0,stroke:'var(--line)','stroke-width':pixel});
    node('line',{x1:0,y1:-150,x2:0,y2:150,stroke:'var(--line)','stroke-width':pixel});
    node('circle',{cx:0,cy:0,r:1.4,fill:'none',stroke:'var(--muted)','stroke-width':pixel});
    text('X',151,-2,font);text('Y',2,-151,font);
    if(display.chain) node('polyline',{points:board.leds.map(l=>`${l.x},${-l.y}`).join(' '),fill:'none',stroke:'var(--main)','stroke-width':pixel*.7,opacity:.55,'data-chain':true});
    const selected=board.leds.find(l=>l.uid===selectedUid),selectedGroup=selected?.groupId;
    for(const h of board.mountingHoles) {
      const bad=analysis?.bad.has(h.designator);
      node('circle',{cx:h.x,cy:-h.y,r:h.outerDiameterMm/2,fill:'none',stroke:bad?'var(--bad)':'var(--main)','stroke-width':pixel,'data-screw':h.designator});
      node('circle',{cx:h.x,cy:-h.y,r:h.drillDiameterMm/2,fill:'none',stroke:bad?'var(--bad)':'var(--main)','stroke-width':pixel*.7});
    }
    if(selected) node('line',{x1:0,y1:0,x2:selected.x,y2:-selected.y,stroke:'var(--select)','stroke-width':pixel*.8,opacity:.7});
    if(display.caps) for(const cap of board.capacitors) {
      const bad=analysis?.bad.has(cap.designator),related=board.leds[cap.ledId].groupId===selectedGroup;
      const g=node('g',{transform:`translate(${cap.x} ${-cap.y}) rotate(${-cap.rotationDeg})`,'pointer-events':'none','data-cap-uid':cap.ledUid});
      node('rect',{x:-.5,y:-.25,width:1,height:.5,fill:bad?'var(--bad)':related?'var(--select)':'var(--main)',opacity:related?1:.6},g);
    }
    for(const led of board.leds) {
      const related=led.groupId===selectedGroup,chosen=led.uid===selectedUid,bad=analysis?.bad.has(led.designator);
      const lit=display.mode==='layout'||(display.mode==='target'?led.target:led.kind==='5050'&&led.row===selectedRow);
      const color=related?'var(--select)':display.mode!=='layout'&&lit?'var(--active)':led.kind==='small'?'var(--small)':'var(--main)';
      const g=node('g',{transform:`translate(${led.x} ${-led.y}) rotate(${-led.rotationDeg})`,'data-led-uid':led.uid,'data-ref':led.designator,
        'data-group':led.groupId,'data-axis':led.groupType==='axis','data-related':related,'data-kind':led.kind});
      const s=led.sizeMm;
      node('rect',{x:-s/2,y:-s/2,width:s,height:s,fill:color,'fill-opacity':related?1:lit?.68:.16,
        stroke:bad?'var(--bad)':related?'var(--select)':'none','stroke-width':Math.max(.13,pixel*(chosen?2:1))},g);
      // The local upper-left marker identifies the given 0-degree pin-1 side.
      node('circle',{cx:-s/2+.35,cy:-s/2+.35,r:Math.min(.25,s/10),fill:'var(--fg)',opacity:.85,'pointer-events':'none'},g);
      const hit=Math.max(s,9*pixel);
      node('rect',{x:-hit/2,y:-hit/2,width:hit,height:hit,fill:'transparent',stroke:'none'},g);
      if(related||display.labels) text(led.designator,led.x+s/2+1,-led.y-s/2-1,font);
    }
  }
  function syncPanels() {
    const selected=board.leds.find(l=>l.uid===selectedUid);
    if(!selected) selectedUid=null;
    const main=board.leds.filter(l=>l.kind==='5050').length;
    $('totals').textContent=`5050 ${main} · 小灯 ${board.leds.length-main} · 电容 ${board.capacitors.length} · 安装孔 ${board.mountingHoles.length} · 总LED ${board.leds.length}`;
    if(selected) {
      const {group}=E.locate(project,selected.uid);
      $('selected-info').textContent=`${selected.designator} · ${selected.kind==='5050'?'5050':'小灯'} · 第${selected.ring}环 / R${selected.r} mm · 中心 (${selected.x.toFixed(3)}, ${selected.y.toFixed(3)}) mm`;
      $('angle').disabled=group.type==='axis';$('apply-angle').disabled=group.type==='axis';
      if(document.activeElement!==$('angle')) $('angle').value=selected.chainAngleDeg.toFixed(4);
      const refs=board.leds.filter(l=>l.groupId===group.id).map(l=>l.designator);
      $('selected-group').textContent=(group.type==='axis'?'X/Y轴固定组':group.type==='diagonal'?'45°四灯组，拖离后补成八颗':'八灯联动组')+'：'+refs.join('、');
    } else {
      $('selected-info').textContent='点击灯珠，或输入位号定位。';$('selected-group').textContent='';
      $('angle').disabled=true;$('apply-angle').disabled=true;$('angle').value='';
    }
    for(const tr of $('rings').querySelectorAll('tbody tr')) {
      const row=Number(tr.dataset.row),count=board.leds.filter(l=>l.row===row&&l.kind==='5050').length;
      const input=tr.querySelector('input');if(document.activeElement!==input) input.value=count;
      tr.querySelector('[data-small-count]').textContent=String(board.leds.filter(l=>l.row===row&&l.kind==='small').length);
      tr.classList.toggle('selected-row',row===selectedRow);
    }
    $('undo').disabled=!undo.length;$('redo').disabled=!redo.length;
  }
  function checks() {
    clearTimeout(checkTimer);analysis=E.analyse(board);
    $('clearance').textContent=`最小本体间隙 ${analysis.body.minGapMm.toFixed(3)} mm · 最小规划包络间隙 ${analysis.envelope.minGapMm.toFixed(3)} mm`;
    $('warnings').textContent=analysis.warnings.length?analysis.warnings.join('；')+'。红色草稿仍可导出。':'当前本体与规划包络无相交。';
    $('warnings').classList.toggle('has-warnings',analysis.warnings.length>0);
  }
  function profilesPanel() {
    $('profile-list').replaceChildren();
    for(const kind of ['5050','small','capacitor','screw']) {
      const profile=project.footprints[kind],offset=E.midOffset(profile,0),p=document.createElement('p');
      p.textContent=`${profile.name} · 0°中心补偿 (${offset.x.toFixed(6)}, ${offset.y.toFixed(6)}) mm`;$('profile-list').appendChild(p);
    }
  }
  function render(check=true) {
    board=E.materialize(project);
    if(check) checks();else {clearTimeout(checkTimer);checkTimer=setTimeout(()=>{checks();draw();},160);}
    syncPanels();draw();if(check)profilesPanel();
  }
  for(let row=9;row>=0;row--) {
    const tr=document.createElement('tr');tr.dataset.row=String(row);
    const label=document.createElement('td');label.textContent=`${10-row}环 / ${row*15+7.5}`;
    const countTd=document.createElement('td'),input=document.createElement('input');
    input.type='number';input.min='4';input.step='4';input.setAttribute('aria-label',`${10-row}环大灯数量`);countTd.appendChild(input);
    const smallTd=document.createElement('td');smallTd.dataset.smallCount='';
    const buttonTd=document.createElement('td'),button=document.createElement('button');button.textContent='应用';button.dataset.applyRow=String(row);
    button.setAttribute('aria-label',`应用${10-row}环大灯数量`);buttonTd.appendChild(button);
    tr.append(label,countTd,smallTd,buttonTd);$('rings').querySelector('tbody').appendChild(tr);
  }
  $('rings').addEventListener('click',event=>{
    const button=event.target.closest('[data-apply-row]');if(!button)return;
    attempt(()=>{const row=Number(button.dataset.applyRow),value=Number(button.closest('tr').querySelector('input').value);selectedRow=row;commit(E.setCount(project,row,value));});
  });
  $('angle-form').addEventListener('submit',event=>{event.preventDefault();attempt(()=>{if(!selectedUid)throw new Error('请先选择灯珠。');commit(E.setAngle(project,selectedUid,Number($('angle').value)));$('angle').value=board.leds.find(l=>l.uid===selectedUid).chainAngleDeg.toFixed(4);});});
  $('find-form').addEventListener('submit',event=>{
    event.preventDefault();attempt(()=>{
      const ref=$('find-ref').value.trim().toUpperCase(),led=board.leds.find(l=>l.designator===ref);
      if(!led)throw new Error('未找到位号 '+ref);selectedUid=led.uid;selectedRow=led.row;
      view={x:led.x-15,y:-led.y-15,w:30,h:30};syncPanels();draw();persist();error('');
    });
  });
  function historyMove(direction) {
    const from=direction==='undo'?undo:redo,to=direction==='undo'?redo:undo;
    if(!from.length)return;to.push(E.clone(project));project=from.pop();error('');render();persist();
  }
  $('undo').onclick=()=>historyMove('undo');$('redo').onclick=()=>historyMove('redo');
  document.addEventListener('keydown',event=>{
    if((event.ctrlKey||event.metaKey)&&!event.target.matches('input,textarea')) {
      if(event.key.toLowerCase()==='z'){event.preventDefault();historyMove(event.shiftKey?'redo':'undo');}
      if(event.key.toLowerCase()==='y'){event.preventDefault();historyMove('redo');}
    }
  });
  function world(event) {return new DOMPoint(event.clientX,event.clientY).matrixTransform(svg.getScreenCTM().inverse());}
  function zoom(factor,point=null) {
    const p=point??{x:view.x+view.w/2,y:view.y+view.h/2},w=Math.max(12,Math.min(700,view.w*factor)),t=w/view.w;
    view={x:p.x+(view.x-p.x)*t,y:p.y+(view.y-p.y)*t,w,h:w};draw();
  }
  svg.addEventListener('wheel',event=>{event.preventDefault();if(!drag)zoom(Math.exp(event.deltaY*.001),world(event));},{passive:false});
  $('zoom-in').onclick=()=>zoom(.75);$('zoom-out').onclick=()=>zoom(1/.75);
  $('fit').onclick=()=>{view={x:-160,y:-160,w:320,h:320};draw();};
  svg.addEventListener('pointerdown',event=>{
    if(event.button!==0||drag)return;
    const hit=event.target.closest('[data-led-uid]');
    if(hit) {
      const led=board.leds.find(l=>l.uid===hit.dataset.ledUid);selectedUid=led.uid;selectedRow=led.row;
      syncPanels();draw();if(led.groupType==='axis'){persist();return;}
      drag={type:'angle',pointer:event.pointerId,start:E.clone(project),uid:led.uid,x:event.clientX,y:event.clientY,last:null};
    } else drag={type:'pan',pointer:event.pointerId,x:event.clientX,y:event.clientY,startView:{...view}};
    svg.setPointerCapture(event.pointerId);event.preventDefault();
  });
  function processMove() {
    raf=0;if(!drag?.last)return;
    const e=drag.last;
    if(drag.type==='pan') {
      const scale=drag.startView.w/svg.getBoundingClientRect().width;
      view={...drag.startView,x:drag.startView.x-(e.clientX-drag.x)*scale,y:drag.startView.y-(e.clientY-drag.y)*scale};draw();
    } else if(Math.hypot(e.clientX-drag.x,e.clientY-drag.y)>=2) {
      const p=world(e),a=Math.atan2(-p.y,p.x)*180/Math.PI;
      attempt(()=>{project=E.setAngle(drag.start,drag.uid,a);render(false);});
    }
  }
  svg.addEventListener('pointermove',event=>{if(!drag||event.pointerId!==drag.pointer)return;drag.last={clientX:event.clientX,clientY:event.clientY};if(!raf)raf=requestAnimationFrame(processMove);});
  function endDrag(event,cancel=false) {
    if(!drag||event.pointerId!==drag.pointer)return;
    if(raf){cancelAnimationFrame(raf);raf=0;}if(!cancel)processMove();
    if(drag.type==='angle') {
      if(cancel)project=drag.start;
      else if(JSON.stringify(project)!==JSON.stringify(drag.start))pushHistory(drag.start);
    }
    const pointer=drag.pointer;drag=null;if(svg.hasPointerCapture(pointer))svg.releasePointerCapture(pointer);
    render();persist();
  }
  svg.addEventListener('pointerup',event=>endDrag(event));svg.addEventListener('pointercancel',event=>endDrag(event,true));
  $('display-mode').onchange=()=>{display.mode=$('display-mode').value;draw();};
  for(const [id,key] of [['show-caps','caps'],['show-chain','chain'],['show-labels','labels']]) $(id).onchange=()=>{display[key]=$(id).checked;draw();};
  function download(name,content,type) {
    const url=URL.createObjectURL(new Blob([content],{type})),a=document.createElement('a');
    a.href=url;a.download=name;document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),10000);
  }
  $('save-project').onclick=()=>download('energy-led-project.json',JSON.stringify(project,null,2),'application/json');
  $('export-pcb').onclick=()=>attempt(()=>download('energy-jlceda-positions.csv',E.exportCsv(project,'jlceda'),'text/csv;charset=utf-8'));
  $('export-extra').onclick=()=>attempt(()=>{const type=$('extra-export').value;download('energy-led-'+type+'.csv',E.exportCsv(project,type),'text/csv;charset=utf-8');});
  $('load-project').onclick=()=>$('project-file').click();
  $('project-file').onchange=async()=>{
    const file=$('project-file').files[0];if(!file)return;
    try {const next=E.upgrade(JSON.parse(await file.text()));commit(next);$('save-status').textContent='项目已载入并自动保存';}catch(e){error('载入失败：'+e.message);}finally{$('project-file').value='';}
  };
  $('save-profiles').onclick=()=>download('energy-footprint-profiles.json',JSON.stringify(project.footprints,null,2),'application/json');
  $('load-profiles').onclick=()=>$('profiles-file').click();
  $('profiles-file').onchange=async()=>{
    const file=$('profiles-file').files[0];if(!file)return;
    try {const profiles=E.validateFootprints(JSON.parse(await file.text())),next=E.clone(project);next.footprints=profiles;commit(next);}catch(e){error('封装预设载入失败：'+e.message);}finally{$('profiles-file').value='';}
  };
  new ResizeObserver(()=>draw()).observe(svg);
  render();
  // Read-only inspection used by the browser regression checks.
  window.energyEditor={getProject:()=>E.clone(project),getBoard:()=>E.clone(board),getView:()=>({...view})};
  $('led-editor').dataset.ready='true';
})(EditorModel,LedLayout,PcbLayout,EditorDefaults);
