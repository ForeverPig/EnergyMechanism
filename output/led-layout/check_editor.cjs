/* Geometry and genuine Edge browser regression checks. No PCB writes. */
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {pathToFileURL}=require('node:url');
const E=require('./editor-model.js');
const {chromium}=require('C:/Users/shuto/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const defaults=JSON.parse(fs.readFileSync(path.join(__dirname,'editor-defaults.json'),'utf8'));
const initial=E.create(defaults.baseline,defaults.footprints);
const convertedRefs=['LED270','LED284','LED286','LED300','LED302','LED316','LED318','LED332'];
function legacyProject() {
  const project=E.clone(initial);
  project.version=1;project.baselineRevision='20261003-ring7-small-removed-cap-left-r90-screws';
  const group=project.rings[9].groups.find(g=>g.members.some(m=>m.uid==='led-270'));
  group.kind='small';
  return project;
}
const near=(a,b,tolerance=1e-8)=>assert.ok(Math.abs(a-b)<=tolerance,`${a} != ${b}`);
function checkGeometry(project) {
  const board=E.materialize(E.validate(project));
  assert.equal(board.leds.length,board.capacitors.length);
  assert.equal(board.mountingHoles.length,16);
  assert.equal(new Set(board.leds.map(l=>l.uid)).size,board.leds.length);
  assert.equal(new Set(board.parts.map(p=>p.designator)).size,board.parts.length);
  for(const led of board.leds) {
    near(Math.hypot(led.x,led.y),led.row*15+7.5);
    const siblings=board.leds.filter(l=>l.groupId===led.groupId);
    assert.equal(siblings.length,led.groupType==='generic'?8:4);
    for(const [x,y] of [[-led.x,led.y],[led.x,-led.y],[led.y,led.x],[-led.y,led.x]]) assert.ok(siblings.some(l=>Math.hypot(l.x-x,l.y-y)<1e-7),'missing symmetry partner');
    const cap=board.capacitors[led.id],angle=led.rotationDeg*Math.PI/180,dx=cap.x-led.x,dy=cap.y-led.y,t=project.capTemplates[led.kind];
    near(dx*Math.cos(angle)+dy*Math.sin(angle),t.x);near(-dx*Math.sin(angle)+dy*Math.cos(angle),t.y);
    near((cap.rotationDeg-led.rotationDeg+360)%360,180,1e-6);
    assert.equal(cap.designator,'C'+(led.id+1));assert.equal(cap.ledUid,led.uid);
  }
  for(const row of [5,6,7]) {
    const first=board.leds.find(l=>l.row===row);assert.equal(first.kind,'small');assert.ok(first.y<0&&first.x>0);
  }
  for(let row=0;row<10;row++) {
    const ring=board.leds.filter(l=>l.row===row);
    assert.ok(ring.every((l,i)=>i===0||l.chainProgressDeg>=ring[i-1].chainProgressDeg-1e-8));
  }
  return board;
}
// Independently simulate native placement with the footprint's pad-centre bounds.
function simulateImport(profile,angle,midX,midY) {
  const c=Math.cos(angle*Math.PI/180),s=Math.sin(angle*Math.PI/180);
  const x=profile.padCenters.map(p=>p[0]*c-p[1]*s),y=profile.padCenters.map(p=>p[0]*s+p[1]*c);
  const ox=midX-(Math.max(...x)+Math.min(...x))/2,oy=midY-(Math.max(...y)+Math.min(...y))/2;
  return [ox+profile.bodyCenter[0]*c-profile.bodyCenter[1]*s,oy+profile.bodyCenter[0]*s+profile.bodyCenter[1]*c];
}
function checkCsv(project) {
  const b=E.materialize(project),rows=E.exportCsv(project).trim().replace(/^\uFEFF/,'').split('\r\n').map(line=>line.split(',').map(v=>v.slice(1,-1)));
  assert.deepEqual(rows.shift(),['Designator','Mid X','Mid Y','Rotation']);assert.equal(rows.length,b.parts.length);
  rows.forEach((row,i)=>{
    const p=b.parts[i],kind=p.type==='led'?p.kind:p.type==='capacitor'?'capacitor':'screw';
    assert.equal(row[0],p.designator);
    const [x,y]=simulateImport(project.footprints[kind],Number(row[3]),parseFloat(row[1]),parseFloat(row[2]));
    near(x,p.x,6e-7);near(y,p.y,6e-7);
  });
}
function staticChecks() {
  const base=checkGeometry(initial);
  assert.equal(base.leds.length,332);assert.equal(base.leds.filter(l=>l.kind==='small').length,24);
  assert.equal(base.leds.filter(l=>l.kind==='5050').length,308);
  assert.equal(base.leds.filter(l=>l.row===9&&l.kind==='5050').length,64);
  for(const ref of convertedRefs) {const led=base.leds.find(l=>l.designator===ref);assert.equal(led.kind,'5050');assert.equal(led.sizeMm,5);}
  const source=JSON.parse(fs.readFileSync(path.join(__dirname,'led-positions.json'),'utf8'));
  for(const led of base.leds) {
    const originalId=Number(led.uid.slice(4))-1;
    const old=source.leds[originalId];near(led.x,old.x);near(led.y,old.y);
    if(convertedRefs.includes(led.designator)) near((led.rotationDeg-old.rotationDeg+360)%360,180,1e-6);
    else {near(led.rotationDeg,old.rotationDeg,1e-6);near(base.capacitors[led.id].x,source.capacitors[originalId].x,1e-7);near(base.capacitors[led.id].y,source.capacitors[originalId].y,1e-7);}
  }
  for(const led of base.leds) {
    const originalId=Number(led.uid.slice(4))-1;
    const old=defaults.baseline.leds[originalId],cap=base.capacitors[led.id],oldCap=defaults.baseline.capacitors[originalId];
    near(led.x,old.x);near(led.y,old.y);near(led.rotationDeg,old.rotationDeg,1.01e-6);
    near(cap.x,oldCap.x,1e-7);near(cap.y,oldCap.y,1e-7);near(cap.rotationDeg,oldCap.rotationDeg,1.01e-6);
    const number=originalId+1,expected=number===216?165:number>=165&&number<=215?number+1:number;
    assert.equal(led.designator,'LED'+expected);
  }
  const changed=base.leds.filter(l=>l.designator!=='LED'+l.uid.slice(4));
  assert.equal(changed.length,52);assert.equal(changed.filter(l=>l.kind==='5050').length,44);assert.equal(changed.filter(l=>l.kind==='small').length,8);
  assert.equal(base.leds[163].designator,'LED164');assert.equal(base.leds[164].uid,'led-216');assert.equal(base.leds[165].uid,'led-165');assert.equal(base.leds[215].uid,'led-215');assert.equal(base.leds[216].uid,'led-217');
  assert.deepEqual(E.analyse(base).warnings,[]);
  const legacy=legacyProject();
  legacy.rings[9].groups.find(g=>g.kind==='small').alpha=4.1234;
  legacy.rings[2].groups.find(g=>g.type==='generic').alpha=29;
  const beforeUpgrade=E.materialize(legacy),upgraded=E.upgrade(legacy),afterUpgrade=checkGeometry(upgraded);
  assert.equal(upgraded.version,2);assert.equal(afterUpgrade.leds.filter(l=>l.kind==='small').length,24);
  for(const led of beforeUpgrade.leds) {const next=afterUpgrade.leds.find(l=>l.uid===led.uid);near(next.x,led.x);near(next.y,led.y);}
  assert.deepEqual(E.upgrade(upgraded),upgraded);checkCsv(upgraded);
  assert.equal(legacy.version,1,'Upgrade must not mutate caller data');
  const moved=E.setAngle(initial,'led-14',30),mb=checkGeometry(moved);
  assert.equal(mb.leds.filter(l=>Math.hypot(l.x-base.leds.find(p=>p.uid===l.uid).x,l.y-base.leds.find(p=>p.uid===l.uid).y)>1e-7).length,8);
  const sm=E.setAngle(moved,'led-79',6),sb=checkGeometry(sm);
  assert.equal(sb.leds.filter(l=>Math.hypot(l.x-mb.leds.find(p=>p.uid===l.uid).x,l.y-mb.leds.find(p=>p.uid===l.uid).y)>1e-7).length,8);
  assert.throws(()=>E.setAngle(initial,'led-1',5),/固定/);
  const promoted=E.setAngle(initial,'led-6',40),pb=checkGeometry(promoted);
  assert.equal(pb.leds.length,336);assert.equal(pb.capacitors.length,336);assert.equal(pb.leds.filter(l=>!initial.baselineRefs[l.uid]).length,4);
  assert.equal(checkGeometry(E.setAngle(promoted,'led-6',45)).leds.length,336);
  assert.equal(E.locate(E.setAngle(promoted,'led-6',45),'led-6').group.alpha,44.99);
  const counted=E.setCount(sm,5,36),cb=checkGeometry(counted);
  assert.equal(cb.leds.filter(l=>l.row===5&&l.kind==='5050').length,36);
  const mains=cb.leds.filter(l=>l.row===5&&l.kind==='5050').sort((a,b)=>a.chainAngleDeg-b.chainAngleDeg);
  mains.forEach((l,i)=>near(l.chainAngleDeg,i*10));
  for(const l of cb.leds.filter(l=>l.kind==='small')) {
    const old=sb.leds.find(p=>p.uid===l.uid);near(l.x,old.x);near(l.y,old.y);
  }
  for(const old of sb.leds.filter(l=>l.kind==='5050'&&l.groupType==='axis')) assert.ok(cb.leds.some(l=>l.uid===old.uid&&l.x===old.x&&l.y===old.y));
  const reduced=E.setCount(counted,5,4);checkGeometry(reduced);
  assert.ok(E.exportCsv(reduced,'renumber').includes('DELETED'));assert.ok(E.exportCsv(promoted,'renumber').includes('NEW'));
  assert.deepEqual(E.validate(JSON.parse(JSON.stringify(counted))),counted);
  for(const n of [0,3,5,7,NaN,Infinity]) assert.throws(()=>E.setCount(initial,0,n));
  const invalid=E.clone(initial);invalid.rings[0].radiusMm=8;assert.throws(()=>E.validate(invalid));
  const duplicate=E.clone(initial);duplicate.rings[0].groups[0].members[1].uid='led-1';assert.throws(()=>E.validate(duplicate));
  const low=initial.rings.reduce((p,r)=>E.setCount(p,r.row,4),initial);
  assert.ok(E.analyse(E.materialize(low)).warnings.some(w=>w.includes('总灯数')));checkCsv(low);
  [initial,moved,sm,promoted,counted,reduced].forEach(checkCsv);
  for(const profile of Object.values(initial.footprints)) for(const angle of [0,30,45,90,270]) {
    const offset=E.midOffset(profile,angle),xy=simulateImport(profile,angle,12+offset.x,-23+offset.y);near(xy[0],12);near(xy[1],-23);
  }
  console.log('PASS 308 large / 24 small; outer replacements and legacy upgrade; R112.5 entry LED216 -> LED165, 44 large / 8 small renumbered, all physical poses preserved; symmetry, count reflow, cap rigidity and compensated CSV');
}
async function browserChecks() {
  const dir=path.resolve(__dirname,'../playwright');fs.mkdirSync(dir,{recursive:true});
  const downloadsPath=path.join(dir,'editor-download-cache');fs.mkdirSync(downloadsPath,{recursive:true});
  const browserExecutable=process.env.EDITOR_BROWSER_EXECUTABLE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
  const browser=await chromium.launch({executablePath:browserExecutable,headless:true,downloadsPath});
  try {
    const context=await browser.newContext({viewport:{width:1360,height:1080},acceptDownloads:true});
    const page=await context.newPage(),errors=[],requests=[];
    page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(/^https?:/.test(r.url()))requests.push(r.url());});
    // Accidentally opening the source template should lead to the complete app.
    await page.goto(pathToFileURL(path.join(__dirname,'editor-shell.html')).href);
    await page.waitForURL(url=>url.pathname.endsWith('/editor.html'));
    await page.locator('#led-editor[data-ready=true]').waitFor();
    assert.equal(await page.locator('.workspace').evaluate(e=>getComputedStyle(e).display),'grid');
    assert.equal(await page.getByText('这是源代码模板，不是完整程序。').count(),0);
    assert.equal(await page.locator('[data-led-uid]').count(),332);assert.equal(await page.locator('[data-cap-uid]').count(),332);assert.equal(await page.locator('[data-screw]').count(),16);
    assert.equal(await page.locator('#warnings').evaluate(e=>e.classList.contains('has-warnings')),false);
    assert.equal(await page.locator('[data-kind="small"]').count(),24);
    assert.equal(await page.locator('[data-kind="5050"]').count(),308);
    assert.equal(await page.locator('tr[data-row="9"] input').inputValue(),'64');
    await page.screenshot({path:path.join(dir,'editor-default.png'),fullPage:true});
    await page.emulateMedia({colorScheme:'dark'});
    await page.screenshot({path:path.join(dir,'editor-dark.png'),fullPage:true});
    await page.emulateMedia({colorScheme:'light'});
    const legacy=legacyProject();
    legacy.rings[2].groups.find(g=>g.type==='generic').alpha=29;
    await page.evaluate(p=>localStorage.setItem('energy-led-editor-v1-20261003',JSON.stringify({project:p,selectedRow:9})),legacy);
    await page.reload();await page.locator('#led-editor[data-ready=true]').waitFor();
    assert.deepEqual(await page.evaluate(()=>window.energyEditor.getProject()),E.upgrade(legacy));
    assert.equal(await page.evaluate(()=>JSON.parse(localStorage.getItem('energy-led-editor-v1-20261003')).project.version),2);
    await page.locator('#project-file').setInputFiles({name:'legacy-v1.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(legacyProject()))});
    await page.waitForFunction(()=>window.energyEditor.getBoard().leds.find(l=>l.uid==='led-14').chainAngleDeg===22.5);
    assert.equal(await page.locator('[data-kind="small"]').count(),24);
    async function find(ref) {await page.locator('#find-ref').fill(ref);await page.locator('#find-form button').click();}
    async function angle(value) {await page.locator('#angle').fill(String(value));await page.locator('#apply-angle').click();}
    await find('LED79');assert.equal(await page.locator('#angle').evaluate(e=>e.checkValidity()),true);
    await angle(6.1234);near(await page.evaluate(()=>window.energyEditor.getBoard().leds.find(l=>l.uid==='led-79').chainAngleDeg),6.1234);
    await page.locator('#undo').click();
    await find('LED1');assert.equal(await page.locator('#angle').isDisabled(),true);
    await find('LED14');await angle(30);
    assert.equal(await page.locator('[data-related=true]').count(),8);
    near(await page.evaluate(()=>window.energyEditor.getBoard().leds.find(l=>l.uid==='led-14').chainAngleDeg),30);
    await page.locator('#undo').click();near(await page.evaluate(()=>window.energyEditor.getBoard().leds.find(l=>l.uid==='led-14').chainAngleDeg),22.5);
    await page.locator('#redo').click();near(await page.evaluate(()=>window.energyEditor.getBoard().leds.find(l=>l.uid==='led-14').chainAngleDeg),30);
    const point=await page.evaluate(()=>{
      const l=window.energyEditor.getBoard().leds.find(l=>l.uid==='led-14'),s=document.getElementById('board'),a=34*Math.PI/180;
      const start=new DOMPoint(l.x,-l.y).matrixTransform(s.getScreenCTM());
      const end=new DOMPoint(l.r*Math.cos(a),-l.r*Math.sin(a)).matrixTransform(s.getScreenCTM());
      return {start:{x:start.x,y:start.y},end:{x:end.x,y:end.y}};
    });
    await page.mouse.move(point.start.x,point.start.y);await page.mouse.down();await page.mouse.move(point.end.x,point.end.y,{steps:7});await page.mouse.up();
    near(await page.evaluate(()=>window.energyEditor.getBoard().leds.find(l=>l.uid==='led-14').chainAngleDeg),34,1e-5);
    await page.locator('#undo').click();near(await page.evaluate(()=>window.energyEditor.getBoard().leds.find(l=>l.uid==='led-14').chainAngleDeg),30);
    await find('LED6');await angle(40);assert.equal(await page.locator('[data-led-uid]').count(),336);
    assert.equal(await page.locator('[data-cap-uid]').count(),336);assert.equal(await page.locator('[data-related=true]').count(),8);
    await page.locator('tr[data-row="5"] input').fill('36');await page.locator('[data-apply-row="5"]').click();
    assert.equal(await page.locator('tr[data-row="5"] input').inputValue(),'36');
    const prior=await page.evaluate(()=>window.energyEditor.getProject());checkGeometry(prior);checkCsv(prior);
    const dl=page.waitForEvent('download');await page.locator('#export-pcb').click();const download=await dl;
    const csvPath=path.join(dir,'editor-browser-export.csv');await download.saveAs(csvPath);
    assert.equal(fs.readFileSync(csvPath,'utf8'),E.exportCsv(prior));
    const saved=page.waitForEvent('download');await page.locator('#save-project').click();const projectDownload=await saved;
    const projectPath=path.join(dir,'editor-browser-project.json');await projectDownload.saveAs(projectPath);
    assert.deepEqual(JSON.parse(fs.readFileSync(projectPath,'utf8')),prior);
    await page.reload();await page.locator('#led-editor[data-ready=true]').waitFor();assert.deepEqual(await page.evaluate(()=>window.energyEditor.getProject()),prior);
    await page.locator('tr[data-row="5"] input').fill('40');await page.locator('[data-apply-row="5"]').click();
    await page.locator('#project-file').setInputFiles(projectPath);await page.waitForFunction(n=>window.energyEditor.getBoard().leds.length===n,E.materialize(prior).leds.length);
    assert.deepEqual(await page.evaluate(()=>window.energyEditor.getProject()),prior);
    await page.locator('#project-file').setInputFiles({name:'invalid-project.json',mimeType:'application/json',buffer:Buffer.from('{"format":"wrong"}')});
    await page.locator('#error').waitFor({state:'visible'});assert.deepEqual(await page.evaluate(()=>window.energyEditor.getProject()),prior);
    const profiles=E.clone(prior.footprints);profiles['5050']={name:'test centred footprint',bodyCenter:[0,0],padCenters:[[0,0]]};
    await page.locator('#profiles-file').setInputFiles({name:'profiles.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(profiles))});
    await page.waitForFunction(()=>window.energyEditor.getProject().footprints['5050'].name==='test centred footprint');
    const changedProfile=await page.evaluate(()=>window.energyEditor.getProject());checkCsv(changedProfile);
    await page.locator('#undo').click();assert.deepEqual(await page.evaluate(()=>window.energyEditor.getProject()),prior);
    for(const type of ['centres','renumber','small','chain']) {
      await page.locator('#extra-export').selectOption(type);
      const event=page.waitForEvent('download');await page.locator('#export-extra').click();const extra=await event;
      const target=path.join(dir,'editor-browser-'+type+'.csv');await extra.saveAs(target);
      assert.equal(fs.readFileSync(target,'utf8'),E.exportCsv(prior,type));
    }
    await page.locator('#fit').click();const before=await page.evaluate(()=>window.energyEditor.getView());
    const box=await page.locator('#board').boundingBox();await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.wheel(0,-250);
    await page.waitForFunction(w=>window.energyEditor.getView().w<w,before.w);
    await page.locator('#fit').click();
    const center=await page.locator('#board').boundingBox();await page.mouse.move(center.x+center.width/2,center.y+center.height/2);await page.mouse.down();await page.mouse.move(center.x+center.width/2+35,center.y+center.height/2+20,{steps:4});await page.mouse.up();
    assert.ok((await page.evaluate(()=>window.energyEditor.getView())).x!==-160);
    await page.locator('#fit').click();await find('LED14');
    await page.screenshot({path:path.join(dir,'editor-desktop.png'),fullPage:true});
    await page.locator('#fit').click();await page.setViewportSize({width:320,height:900});
    await page.waitForFunction(()=>document.documentElement.scrollWidth<=window.innerWidth);
    await page.screenshot({path:path.join(dir,'editor-mobile.png'),fullPage:true});
    assert.deepEqual(errors,[]);assert.deepEqual(requests,[]);
    console.log('PASS Chromium browser: selection, numeric angle, real pointer drag, promotion, undo/redo, counts, all CSV downloads, autosave, JSON import/rejection, profile replacement, zoom/pan, light/dark and 320px layout; no page errors or network requests');
  } finally {await browser.close();}
}
staticChecks();browserChecks().catch(e=>{console.error(e);process.exitCode=1;});
