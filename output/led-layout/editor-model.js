/* Offline editor geometry. All positions are BODY centres, mm, Y up. */
(function (global) {
  'use strict';
  const L = typeof LedLayout !== 'undefined' ? LedLayout : require('./layout.js');
  const P = typeof PcbLayout !== 'undefined' ? PcbLayout : require('./pcb-layout.js');
  const clone = value => JSON.parse(JSON.stringify(value));
  const norm = a => ((a % 360) + 360) % 360;
  const round = a => Math.round(norm(a) * 1e6) / 1e6 % 360;
  const angleDelta = (a,b) => (a-b+540)%360-180;
  const fold = a => Math.min(norm(a)%90,90-norm(a)%90);
  const EPS = 1e-7;
  const CHAIN_ENTRY_ROWS = [5,6,7]; // R82.5/R97.5/R112.5: enter below +X at the small LED.
  function slots(type) {
    return Array.from({length:4},(_,k)=>type==='generic'?[{k,sign:1},{k,sign:-1}]:[{k,sign:1}]).flat();
  }
  const slotAngle = (group,slot) => norm(slot.k*90+slot.sign*group.alpha);
  function freshUid(project) {
    const used = new Set(project.rings.flatMap(r=>r.groups.flatMap(g=>g.members.map(m=>m.uid))));
    let uid;
    do { uid='led-'+project.nextUid++; } while(used.has(uid)||project.baselineRefs[uid]);
    return uid;
  }
  function makeGroup(project,kind,type,alpha,old=null) {
    const group=old?clone(old):{id:'group-'+project.nextGroup++,kind,type,alpha,members:[]};
    group.alpha=alpha;
    if(!old) group.members=slots(type).map(s=>({...s,uid:freshUid(project)}));
    return group;
  }
  function create(baseline,footprints) {
    const project={format:'energy-led-editor',version:2,units:'mm',origin:'centre',yAxis:'up',
      baselineRevision:baseline.revision,nextUid:baseline.leds.length+1,nextGroup:1,
      baselineRefs:{},capTemplates:{'5050':{x:-3.2,y:3.6,rotation:180},small:{x:0.3,y:-2.2,rotation:180}},
      sizes:{'5050':5,small:2.2},footprints:clone(footprints),mountingHoles:clone(baseline.mountingHoles),rings:[]};
    baseline.leds.forEach(led=>project.baselineRefs['led-'+(led.id+1)]='LED'+(led.id+1));
    for(let row=0;row<10;row++) {
      const ring={row,ring:10-row,radiusMm:row*15+7.5,groups:[]};
      project.rings.push(ring);
      for(const kind of ['5050','small']) {
        const leds=baseline.leds.filter(l=>l.row===row&&l.kind===kind);
        const buckets=new Map();
        for(const led of leds) {
          const a=norm(Math.atan2(led.y,led.x)*180/Math.PI);
          const alpha=fold(a),key=Math.round(alpha*1e6);
          if(!buckets.has(key)) buckets.set(key,[]);
          buckets.get(key).push({led,a,alpha});
        }
        for(const members of [...buckets.values()].sort((a,b)=>a[0].alpha-b[0].alpha)) {
          const alpha=members.reduce((s,m)=>s+m.alpha,0)/members.length;
          const type=alpha<EPS?'axis':Math.abs(alpha-45)<EPS?'diagonal':'generic';
          const group={id:'group-'+project.nextGroup++,kind,type,alpha:type==='axis'?0:type==='diagonal'?45:alpha,members:[]};
          for(const slot of slots(type)) {
            const found=members.find(m=>Math.abs(angleDelta(m.a,slotAngle(group,slot)))<1e-5);
            if(!found) throw new Error('初始布局不能组成对称组。');
            group.members.push({...slot,uid:'led-'+(found.led.id+1)});
          }
          ring.groups.push(group);
        }
      }
    }
    return validate(project);
  }
  function validateFootprints(profiles) {
    for(const kind of ['5050','small','capacitor','screw']) {
      const p=profiles?.[kind];
      if(!p||typeof p.name!=='string'||p.name.length>200||!Array.isArray(p.bodyCenter)||p.bodyCenter.length!==2||
        !p.bodyCenter.every(Number.isFinite)||!Array.isArray(p.padCenters)||!p.padCenters.length||p.padCenters.length>100||
        !p.padCenters.every(pt=>Array.isArray(pt)&&pt.length===2&&pt.every(Number.isFinite))) throw new Error('封装预设缺失或格式错误：'+kind);
    }
    return profiles;
  }
  function upgrade(project) {
    if(project?.format!=='energy-led-editor'||![1,2].includes(project.version)) throw new Error('不是兼容的灯板编辑项目。');
    const result=clone(project);
    if(result.version===1) {
      if(!Array.isArray(result.rings)||!Array.isArray(result.rings[9]?.groups)) throw new Error('旧项目缺少最外圈数据。');
      for(const group of result.rings[9].groups) if(group.kind==='small') group.kind='5050';
      result.version=2;
      result.baselineRevision='20261012-outer-eight-5050';
    }
    return validate(result);
  }
  function validate(project) {
    if(project?.format!=='energy-led-editor'||project.version!==2||project.units!=='mm'||project.origin!=='centre'||project.yAxis!=='up') throw new Error('不是兼容的灯板编辑项目。');
    if(!Array.isArray(project.rings)||project.rings.length!==10) throw new Error('项目必须有10个固定半径的圆环。');
    if(!Number.isSafeInteger(project.nextUid)||project.nextUid<1||!Number.isSafeInteger(project.nextGroup)||project.nextGroup<1) throw new Error('项目标识计数错误。');
    const uids=new Set(),gids=new Set();
    for(let row=0;row<10;row++) {
      const r=project.rings[row];
      if(r.row!==row||r.ring!==10-row||r.radiusMm!==row*15+7.5||!Array.isArray(r.groups)) throw new Error('圆环半径或环号被改变。');
      let axes=0,diagonals=0,small=0;
      for(const g of r.groups) {
        if(!/^group-[1-9]\d*$/.test(g.id)||gids.has(g.id)||!['5050','small'].includes(g.kind)||!['axis','diagonal','generic'].includes(g.type)) throw new Error('对称组类型或标识错误。');
        gids.add(g.id);
        if(g.type==='axis') {if(g.kind!=='5050'||g.alpha!==0) throw new Error('轴线灯必须固定。');axes++;}
        else if(g.type==='diagonal') {if(g.kind!=='5050'||g.alpha!==45) throw new Error('45°组错误。');diagonals++;}
        else if(!Number.isFinite(g.alpha)||g.alpha<0.01-EPS||g.alpha>44.99+EPS) throw new Error('八灯组代表角须为0.01°～44.99°。');
        const expected=slots(g.type);
        if(!Array.isArray(g.members)||g.members.length!==expected.length) throw new Error('对称组成员数量错误。');
        const actualSlots=new Set();
        for(const m of g.members) {
          const key=m.k+':'+m.sign;
          if(!/^led-[1-9]\d*$/.test(m.uid)||uids.has(m.uid)||!expected.some(s=>s.k===m.k&&s.sign===m.sign)||actualSlots.has(key)) throw new Error('灯标识或对称位置重复。');
          actualSlots.add(key);uids.add(m.uid);
        }
        if(g.kind==='small') {if(g.type!=='generic') throw new Error('小灯必须是八灯组。');small+=g.members.length;}
      }
      if(axes!==1||diagonals>1||small!==([5,6,7].includes(row)?8:0)) throw new Error('固定轴组或24颗小灯配置错误。');
    }
    for(const kind of ['5050','small']) {
      if(!(project.sizes?.[kind]>0&&project.sizes[kind]<=6)) throw new Error('灯本体尺寸错误。');
      if(!['x','y','rotation'].every(k=>Number.isFinite(project.capTemplates?.[kind]?.[k]))) throw new Error('电容局部变换错误。');
    }
    if(project.nextGroup<=Math.max(...[...gids].map(id=>Number(id.slice(6))))) throw new Error('对称组标识计数落后于已有组。');
    if(!project.baselineRefs||typeof project.baselineRefs!=='object'||Array.isArray(project.baselineRefs)||Object.entries(project.baselineRefs).some(([uid,ref])=>!/^led-[1-9]\d*$/.test(uid)||!/^LED[1-9]\d*$/.test(ref))) throw new Error('原位号记录错误。');
    if(new Set(Object.values(project.baselineRefs)).size!==Object.keys(project.baselineRefs).length) throw new Error('原位号重复。');
    if(!Array.isArray(project.mountingHoles)||project.mountingHoles.length!==16) throw new Error('必须保留16个安装孔。');
    project.mountingHoles.forEach((h,i)=>{
      const radius=[30,60,90,120][Math.floor(i/4)],a=(45+i%4*90)*Math.PI/180;
      if(h.designator!=='SCREW'+(i+1)||![h.x,h.y].every(Number.isFinite)||Math.abs(h.x-radius*Math.cos(a))>1e-6||Math.abs(h.y-radius*Math.sin(a))>1e-6||h.drillDiameterMm!==3||h.outerDiameterMm!==5.8||h.rotationDeg!==0||h.type!=='mounting-hole'||h.shape!=='circle'||h.plated!==true) throw new Error('安装孔位置或尺寸被改变。');
    });
    validateFootprints(project.footprints);
    return project;
  }
  function materialize(project) {
    const leds=[];
    for(const r of project.rings) {
      const members=r.groups.flatMap(g=>g.members.map(slot=>{
        const degrees=slotAngle(g,slot),a=degrees*Math.PI/180,x=r.radiusMm*Math.cos(a),y=r.radiusMm*Math.sin(a),parts=L.targetParts(x,y);
        return {uid:slot.uid,groupId:g.id,slot:{k:slot.k,sign:slot.sign},groupType:g.type,row:r.row,ring:r.ring,r:r.radiusMm,
          x:Math.abs(x)<1e-12?0:x,y:Math.abs(y)<1e-12?0:y,angle:a,chainAngleDeg:degrees,kind:g.kind,sizeMm:project.sizes[g.kind],
          rotationDeg:round(degrees+(g.kind==='5050'?-90:90)),target:parts.on,targetBand:parts.band,targetSpoke:parts.spoke,
          purpose:g.kind==='small'?'spoke-fill':'ring'};
      }));
      const seam=CHAIN_ENTRY_ROWS.includes(r.row)?members.filter(l=>l.kind==='small'&&l.chainAngleDeg>270).sort((a,b)=>b.chainAngleDeg-a.chainAngleDeg)[0]?.chainAngleDeg??0:0;
      members.sort((a,b)=>norm(a.chainAngleDeg-seam)-norm(b.chainAngleDeg-seam)||a.uid.localeCompare(b.uid));
      for(const led of members) {
        const id=leds.length,groupKey=led.kind==='small'?'spoke-only:T':`${led.ring}:${led.target?'T':'B'}`;
        leds.push({...led,id,designator:'LED'+(id+1),chainProgressDeg:norm(led.chainAngleDeg-seam),groupKey,group:L.GROUPS.indexOf(groupKey)});
      }
    }
    const capacitors=leds.map(led=>{
      const t=project.capTemplates[led.kind],v=P.rotate(t.x,t.y,led.rotationDeg);
      return {...P.CAP,type:'capacitor',designator:'C'+(led.id+1),uid:'cap-'+led.uid,ledId:led.id,ledUid:led.uid,
        x:led.x+v.x,y:led.y+v.y,rotationDeg:round(led.rotationDeg+t.rotation),localX:t.x,localY:t.y};
    });
    const ledParts=P.ledParts(leds).map((p,i)=>({...p,uid:leds[i].uid}));
    const holes=clone(project.mountingHoles);
    return {units:'mm',origin:'centre',yAxis:'up',leds,capacitors,mountingHoles:holes,parts:[...ledParts,...capacitors,...holes]};
  }
  function locate(project,uid) {
    for(const ring of project.rings) for(const group of ring.groups) {
      const member=group.members.find(m=>m.uid===uid);
      if(member) return {ring,group,member};
    }
    throw new Error('所选灯已被删除。');
  }
  function setAngle(project,uid,worldDegrees) {
    if(!Number.isFinite(worldDegrees)) throw new Error('请输入有效极角。');
    const result=clone(project),{group,member}=locate(result,uid);
    if(group.type==='axis') throw new Error('X/Y轴上的灯固定，不能调角。');
    const alpha=member.sign*angleDelta(norm(worldDegrees),member.k*90);
    if(group.type==='diagonal'&&Math.abs(alpha-45)<EPS) return result;
    if(group.type==='diagonal') {
      group.type='generic';
      for(let k=0;k<4;k++) group.members.push({uid:freshUid(result),k,sign:-1});
    }
    group.alpha=Math.max(0.01,Math.min(44.99,alpha));
    return validate(result);
  }
  function setCount(project,row,count) {
    if(!Number.isSafeInteger(count)||count<4||count%4!==0) throw new Error('每环大灯数量须为至少4的4倍数。');
    const result=clone(project),ring=result.rings[row];
    if(!ring) throw new Error('环号错误。');
    const old=ring.groups.filter(g=>g.kind==='5050'),generic=old.filter(g=>g.type==='generic').sort((a,b)=>a.alpha-b.alpha),axis=old.find(g=>g.type==='axis'),diagonal=old.find(g=>g.type==='diagonal');
    const next=[clone(axis)];
    let index=0;
    for(let j=1;j<count/8-EPS;j++) {
      const alpha=j*360/count;
      next.push(makeGroup(result,'5050','generic',alpha,generic[index++]??null));
    }
    if(count%8===0) next.push(makeGroup(result,'5050','diagonal',45,diagonal??null));
    ring.groups=[...next,...ring.groups.filter(g=>g.kind==='small')];
    return validate(result);
  }
  function analyse(board) {
    const body=P.stats(board.parts),envelope=P.stats(board.parts,true),bad=new Set(),warnings=[];
    [...body.overlapPairs,...envelope.overlapPairs].flat().forEach(ref=>bad.add(ref));
    if(body.overlaps) warnings.push(`本体相交 ${body.overlaps} 处`);
    if(envelope.overlaps) warnings.push(`规划包络相交 ${envelope.overlaps} 处`);
    for(const part of board.parts.filter(p=>p.type!=='mounting-hole')) {
      if(P.corners(part,true).some(p=>Math.hypot(p.x,p.y)>150+EPS)) {bad.add(part.designator);warnings.push(part.designator+' 超出板边');}
    }
    for(const led of board.leds) if(led.kind==='small'&&!led.targetSpoke) {bad.add(led.designator);warnings.push(led.designator+' 离开十字区域');}
    const total=board.leds.length;
    if(total<300||total>=400) warnings.push(`总灯数 ${total} 超出300～399范围`);
    return {body,envelope,bad,warnings};
  }
  function midOffset(profile,rotation) {
    const points=profile.padCenters.map(([x,y])=>P.rotate(x-profile.bodyCenter[0],y-profile.bodyCenter[1],rotation));
    return {x:(Math.min(...points.map(p=>p.x))+Math.max(...points.map(p=>p.x)))/2,
      y:(Math.min(...points.map(p=>p.y))+Math.max(...points.map(p=>p.y)))/2};
  }
  const csv = rows => '\uFEFF'+rows.map(row=>row.map(v=>'"'+String(v).replaceAll('"','""')+'"').join(',')).join('\r\n')+'\r\n';
  const mm = n => (Math.abs(n)<5e-7?0:n).toFixed(6)+'mm';
  function exportCsv(project,type='jlceda') {
    validate(project);
    const board=materialize(project),byUid=new Map(board.leds.map(l=>[l.uid,l]));
    if(type==='jlceda') {
      const rows=[['Designator','Mid X','Mid Y','Rotation']];
      for(const part of board.parts) {
        const kind=part.type==='led'?part.kind:part.type==='capacitor'?'capacitor':'screw';
        const offset=midOffset(project.footprints[kind],part.rotationDeg);
        rows.push([part.designator,mm(part.x+offset.x),mm(part.y+offset.y),part.rotationDeg]);
      }
      return csv(rows);
    }
    if(type==='centres') return csv([['Designator','Body X_mm','Body Y_mm','Rotation_deg','Type','Paired LED'],...board.parts.map(p=>[p.designator,p.x.toFixed(6),p.y.toFixed(6),p.rotationDeg,p.kind??p.type,p.type==='capacitor'?'LED'+(p.ledId+1):''])]);
    if(type==='small') return csv([['Designator','Paired Capacitor','Ring','Radius_mm','Angle_deg'],...board.leds.filter(l=>l.kind==='small').map(l=>[l.designator,'C'+(l.id+1),l.ring,l.r,l.chainAngleDeg.toFixed(6)])]);
    if(type==='chain') return csv([['Designator','Next Designator','Paired Capacitor','Ring','Radius_mm','Angle_deg','Package'],...board.leds.map((l,i)=>[l.designator,board.leds[i+1]?.designator??'END','C'+(l.id+1),l.ring,l.r,l.chainAngleDeg.toFixed(6),l.kind])]);
    if(type==='renumber') {
      const rows=[['Old Designator','New Designator','Status']];
      for(const [uid,old] of Object.entries(project.baselineRefs)) {
        const led=byUid.get(uid),ref=led?.designator??'DELETED';
        rows.push([old,ref,led?'KEPT':'DELETED'],[old.replace('LED','C'),led?'C'+(led.id+1):'DELETED',led?'KEPT':'DELETED']);
      }
      for(const led of board.leds.filter(l=>!project.baselineRefs[l.uid])) rows.push(['',led.designator,'NEW'],['','C'+(led.id+1),'NEW']);
      board.mountingHoles.forEach(h=>rows.push([h.designator,h.designator,'KEPT']));
      return csv(rows);
    }
    throw new Error('未知导出类型。');
  }
  const api={CHAIN_ENTRY_ROWS,create,upgrade,validate,validateFootprints,materialize,setAngle,setCount,locate,analyse,midOffset,exportCsv,clone,norm,fold};
  if(typeof module==='object'&&module.exports) module.exports=api;else global.EditorModel=api;
})(typeof globalThis!=='undefined'?globalThis:this);
