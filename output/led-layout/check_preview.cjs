// A local smoke check, using the bundled Playwright library and installed Edge.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {chromium} = require('C:/Users/shuto/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const L = require('./layout.js');
const P = require('./pcb-layout.js');
const {placementCsv} = require('./export_jlceda.cjs');

async function main() {
  // Boundary and independent shape membership checks, rather than only counts.
  assert.equal(L.ringOf(0,0),10);
  assert.equal(L.ringOf(14.99,0),10);
  assert.equal(L.ringOf(15,0),9);
  assert.equal(L.ringOf(149.99,0),1);
  assert.equal(L.ringOf(151,0),null);
  assert.equal(L.targetParts(25,0).on,true);
  assert.equal(L.targetParts(10,0).on,false);
  assert.equal(L.targetParts(45,0).on,false);
  assert.equal(L.targetParts(85,0).spoke,true);
  assert.equal(L.targetParts(85,30).on,false);
  assert.equal(L.targetParts(0,85).spoke,true);
  assert.equal(L.targetParts(150,20).on,false);
  assert.equal(L.isLit({ring:3,target:true},'rings',[3]),true);
  assert.equal(L.isLit({ring:3,target:true},'rings',[2]),false);
  assert.equal(L.isLit({ring:3,target:true},'target',[]),true);
  for(const layout of ['polar1','polar2','hex']) {
    const beads=L.generate({layout,pitch:8,packageSize:2});
    assert.equal(new Set(beads.map(p=>p.id)).size,beads.length);
    assert.equal(new Set(beads.map(p=>p.ring)).size,10);
    assert.ok(beads.every(p=>p.r<=150-2/Math.SQRT2+1e-8));
    assert.ok(beads.every(p=>L.targetParts(p.x,p.y).on===p.target));
    assert.ok(beads.every(p=>p.group>=0 && p.group<16));
    for(const group of new Set(beads.map(p=>p.group))) {
      const members=beads.filter(p=>p.group===group);
      assert.equal(new Set(members.map(p=>p.ring)).size,1);
      assert.equal(new Set(members.map(p=>p.target)).size,1);
    }
  }
  assert.equal(new Set(L.generate({layout:'polar2',pitch:8,packageSize:2}).map(p=>p.group)).size,16);
  const mixed=L.generate({layout:'hybrid341',smallSize:2.2,supplement:true});
  assert.equal(mixed.length,332);
  assert.equal(mixed.filter(p=>p.kind==='5050').length,300);
  assert.equal(mixed.filter(p=>p.kind==='small').length,32);
  assert.equal(mixed.filter(p=>p.target).length,148);
  assert.ok(mixed.filter(p=>p.kind==='small').every(p=>p.targetSpoke&&!p.targetBand&&p.purpose==='spoke-fill'));
  assert.equal(mixed.filter(p=>L.isLit(p,'rings',[1,2,3,4,5,6,7,8,9,10])).length,300);
  assert.ok(mixed.filter(p=>p.kind==='small').every(p=>!L.isLit(p,'rings',[p.ring])));
  assert.equal(L.bodyOverlaps(mixed),0);
  assert.ok(L.minBodyGap(mixed)>2.8);
  const board=P.generate(mixed);
  // Deleting eight R52.5 pairs closes reference gaps. Reverse the prior +1.5 mm
  // shift to -1.5 mm, keeping all surviving LED/cap rotations fixed.
  const before=JSON.parse(fs.readFileSync(path.join(__dirname,'placement-before-ring7-removal-cap-reverse.json'),'utf8'));
  const removed=before.leds.filter(led=>led.kind==='small'&&led.ring===7);
  assert.deepEqual(removed.map(led=>led.id+1),[29,31,36,38,43,45,50,52]);
  const surviving=before.leds.filter(led=>!removed.includes(led));
  assert.equal(mixed.length,before.leds.length-8);
  assert.deepEqual(mixed.map(led=>led.previousId),surviving.map(led=>led.id));
  assert.equal(mixed.filter(led=>led.kind==='small'&&led.ring===7).length,0);
  for(const led of mixed) {
    const old=before.leds[led.previousId];
    const cap=board.capacitors[led.id],oldCap=before.capacitors[led.previousId];
    for(const field of ['x','y','rotationDeg','sizeMm','kind','target','ring','baselineId','chainAngleDeg']) assert.equal(led[field],old[field]);
    assert.equal(cap.rotationDeg,oldCap.rotationDeg);
    if(led.kind==='small') {
      const angle=led.rotationDeg*Math.PI/180;
      assert.ok(Math.abs(cap.x-oldCap.x+3*Math.cos(angle))<1e-8);
      assert.ok(Math.abs(cap.y-oldCap.y+3*Math.sin(angle))<1e-8);
      assert.ok(Math.abs(cap.x-oldCap.smallEdit.previousX+1.5*Math.cos(angle))<1e-8);
      assert.ok(Math.abs(cap.y-oldCap.smallEdit.previousY+1.5*Math.sin(angle))<1e-8);
      assert.equal(cap.smallEdit.centreFollowsLed,false);
      assert.equal(cap.smallEdit.shiftLocalMm.x,-1.5);
    } else {
      for(const field of ['x','y','rotationDeg','localX','localY']) assert.equal(cap[field],oldCap[field]);
    }
    assert.equal(cap.designator,'C'+(led.id+1));
    assert.equal(cap.ledId,led.id);
  }
  assert.equal(board.capacitors.length,332);
  assert.equal(board.capacitors.filter(cap=>cap.smallEdit).length,32);
  assert.deepEqual(P.stats(board.parts).overlapPairs,[]);
  assert.ok(P.stats(board.parts).minGapMm>.8499);
  assert.equal(P.stats(board.parts,true).overlaps,0);
  assert.ok(P.stats(board.parts,true).minGapMm>.2999);
  assert.deepEqual(board.capacitors.filter(p=>p.flippedSideAdjustmentMm),[]);
  assert.equal(mixed[28].previousId,29); // old LED30 -> new LED29, now a 5050.
  assert.equal(mixed[76].previousId,84); // old LED85 -> new LED77 at R82.5.
  assert.equal(mixed[116].previousId,124); // old LED125 -> new LED117 at R97.5.
  assert.deepEqual(mixed.map(p=>p.id),Array.from({length:332},(_,i)=>i));
  const expectedRingCounts=[4,8,16,20,28,40,48,52,52,64];
  for(let row=0;row<10;row++) {
    const members=mixed.filter(p=>p.row===row);
    assert.equal(members.length,expectedRingCounts[row]);
    assert.equal(members[0].chainProgressDeg,0);
    if(L.ENTRY_ROWS.includes(row)) {
      assert.equal(members[0].kind,'small');
      assert.ok(members[0].chainAngleDeg>350&&members[0].y<0);
    } else {
      assert.equal(members[0].chainAngleDeg,0);
      assert.ok(members[0].x>0&&Math.abs(members[0].y)<1e-8);
    }
    for(let i=1;i<members.length;i++) {
      assert.equal(members[i].id,members[i-1].id+1);
      assert.ok(members[i].chainProgressDeg>members[i-1].chainProgressDeg);
      assert.ok(members[i].chainProgressDeg<360);
    }
  }
  for(let i=1;i<mixed.length;i++) assert.ok(mixed[i].row>=mixed[i-1].row);
  assert.equal(mixed[28].kind,'5050'); // R52.5 contains only main LEDs.
  assert.equal(mixed[29].kind,'5050');
  assert.equal(mixed.at(-1).kind,'small'); // LED332 lies just before the +X seam.
  assert.equal(mixed.filter(p=>p.id>=300&&p.kind==='5050').length>0,true);
  for(const [x,y,expected] of [[7.5,0,270],[0,7.5,0],[-7.5,0,90],[0,-7.5,180]]) {
    const led=mixed.find(p=>Math.abs(p.x-x)<1e-8&&Math.abs(p.y-y)<1e-8);
    assert.ok(Math.abs(led.rotationDeg-expected)<1e-8);
  }
  for(const led of mixed.filter(p=>p.r>0)) {
    const top=P.rotate(0,1,led.rotationDeg);
    const facing=led.kind==='small'?-1:1;
    assert.ok(Math.abs(top.x-facing*led.x/led.r)<1e-8&&Math.abs(top.y-facing*led.y/led.r)<1e-8,'5050 faces outward; small LEDs face inward');
  }
  for(const cap of board.capacitors) {
    const led=mixed[cap.ledId];
    assert.equal(cap.package,'0402');
    assert.equal(cap.value,'100nF');
    assert.ok(Math.abs(cap.rotationDeg-(led.rotationDeg+180)%360)<1e-6);
    const local=P.rotate(cap.x-led.x,cap.y-led.y,-led.rotationDeg);
    assert.ok(Math.abs(local.x-cap.localX)<1e-8&&Math.abs(local.y-cap.localY)<1e-8);
    assert.ok(P.corners(cap,true).every(p=>Math.hypot(p.x,p.y)<150));
  }
  assert.equal(board.mountingHoles.length,16);
  const signs=[[1,1],[-1,1],[-1,-1],[1,-1]];
  for(let i=0;i<16;i++) {
    const hole=board.mountingHoles[i],r=[30,60,90,120][Math.floor(i/4)],sign=signs[i%4];
    assert.equal(hole.designator,'SCREW'+(i+1));
    assert.ok(Math.abs(hole.x-sign[0]*r/Math.SQRT2)<1e-8);
    assert.ok(Math.abs(hole.y-sign[1]*r/Math.SQRT2)<1e-8);
    assert.equal(hole.drillDiameterMm,3);
    assert.equal(hole.outerDiameterMm,5.8);
    assert.equal(hole.plated,true);
    if(r===90) assert.equal(hole.previousDesignator,null);
    else {
      const oldHole=before.mountingHoles.find(old=>old.designator===hole.previousDesignator);
      assert.equal(hole.x,oldHole.x);
      assert.equal(hole.y,oldHole.y);
    }
  }
  assert.ok(Math.abs(P.circlePartGap({shape:'circle',x:4,y:4,outerDiameterMm:2},{x:0,y:0,rotationDeg:0,widthMm:2,heightMm:2})-(Math.sqrt(18)-1))<1e-8);
  const holeClearance=P.mountingClearance(board.mountingHoles,[...board.leds,...board.capacitors],true);
  assert.equal(holeClearance.overlaps,0);
  assert.ok(holeClearance.minGapMm>1.26);
  assert.ok(holeClearance.minBoardEdgeGapMm>27);
  const placement={units:'mm',origin:'centre',yAxis:'up',leds:mixed,capacitors:board.capacitors,mountingHoles:board.mountingHoles};
  const pcbCsv=placementCsv(placement);
  assert.equal(pcbCsv.trim().split(/\r?\n/).length,681);
  assert.equal(pcbCsv,P.coordinateCsv(board.parts),'Preview and disk native coordinate exports must agree');
  assert.equal(pcbCsv,fs.readFileSync(path.join(__dirname,'jlceda-positions.csv'),'utf8'));
  assert.equal(placementCsv(placement,{include:'led'}).trim().split(/\r?\n/).length,333);
  assert.equal(placementCsv(placement,{include:'capacitor'}).trim().split(/\r?\n/).length,333);
  assert.equal(placementCsv(placement,{include:'screw'}).trim().split(/\r?\n/).length,17);
  assert.equal(placementCsv(placement,{include:'screw'}),fs.readFileSync(path.join(__dirname,'jlceda-screw-positions.csv'),'utf8'));
  const changes=fs.readFileSync(path.join(__dirname,'designator-renumber.csv'),'utf8');
  for(const led of removed) {
    assert.ok(changes.includes(`LED${led.id+1},DELETED\r\n`));
    assert.ok(changes.includes(`C${led.id+1},DELETED\r\n`));
  }
  assert.ok(changes.includes('LED30,LED29\r\n'));
  assert.ok(changes.includes('SCREW9,SCREW13\r\n'));
  const expectedSmallRefs=[77,79,87,89,97,99,107,109,117,119,129,131,141,143,153,155,166,177,179,190,192,203,205,216,270,284,286,300,302,316,318,332];
  assert.deepEqual(mixed.filter(led=>led.kind==='small').map(led=>led.id+1),expectedSmallRefs);
  assert.equal(fs.readFileSync(path.join(__dirname,'small-led-designators.csv'),'utf8').trim().split(/\r?\n/).length,33);
  assert.ok(placementCsv(placement,{prefix:'D',start:1,capStart:101,cx:150,cy:150}).includes('"C101"'));
  const mainRadii=[...new Set(mixed.filter(p=>p.kind==='5050').map(p=>p.r))];
  for(const p of mixed.filter(p=>p.kind==='small')) assert.ok(mainRadii.some(r=>Math.abs(p.r-r)<1e-8),'Small bead radius must match a main circle');
  assert.equal(new Set(mixed.map(p=>p.group)).size,16);
  for(const p of mixed) {
    for(const corner of P.corners(P.ledParts([p])[0])) assert.equal(L.ringOf(corner.x,corner.y),p.ring,`LED ${p.id} body crosses score boundary`);
  }
  assert.equal(L.generate({layout:'hybrid341',supplement:false}).length,300);
  const errors=[];
  const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  try {
    const page=await browser.newPage({viewport:{width:980,height:1100},deviceScaleFactor:1});
    page.on('pageerror',e=>errors.push(String(e)));
    await page.goto(pathToFileURL(path.join(__dirname,'preview.html')).href);
    await page.waitForSelector('[data-led-id]');
    assert.equal(await page.locator('[data-led-id]').count(),332);
    assert.equal(await page.locator('[data-package="5050"]').count(),300);
    assert.equal(await page.locator('[data-package="small"]').count(),32);
    assert.equal(await page.locator('[data-capacitor-id]').count(),332);
    assert.equal(await page.locator('[data-screw-id]').count(),16);
    assert.equal(await page.locator('[data-chain-from]').count(),331);
    const renderedChain=await page.locator('[data-chain-from]').evaluateAll(elements=>elements.map(e=>[Number(e.dataset.chainFrom),Number(e.dataset.chainTo)]));
    assert.deepEqual(renderedChain,Array.from({length:331},(_,i)=>[i+1,i+2]));
    assert.equal(await page.locator('[data-chain-seam="R52.5"]').count(),0);
    assert.equal(await page.locator('[data-chain-seam="R82.5"]').getAttribute('data-chain-from'),'76');
    assert.equal(await page.locator('[data-chain-seam="R82.5"]').getAttribute('data-chain-to'),'77');
    assert.equal(await page.locator('[data-chain-seam="R97.5"]').getAttribute('data-chain-from'),'116');
    assert.equal(await page.locator('[data-chain-seam="R97.5"]').getAttribute('data-chain-to'),'117');
    assert.equal(await page.locator('[data-chain-seam]:not([data-chain-seam=""])').count(),2);
    const renderedCaps=await page.locator('[data-capacitor-id]').evaluateAll(elements=>elements.map(e=>Number(e.dataset.rotation)));
    renderedCaps.forEach((angle,i)=>assert.ok(Math.abs(angle-board.capacitors[i].rotationDeg)<1e-6));
    await page.locator('[data-field="chain"]').uncheck();
    assert.equal(await page.locator('[data-chain-from]').count(),0);
    await page.locator('[data-field="chain"]').check();
    assert.equal(await page.locator('[data-pin-one]').count(),332);
    const actualRotations=await page.locator('[data-led-id]').evaluateAll(elements=>elements.map(e=>Number(e.dataset.rotation)));
    assert.equal(actualRotations.length,mixed.length);
    actualRotations.forEach((angle,i)=>assert.ok(Math.abs(angle-mixed[i].rotationDeg)<0.000001));
    assert.equal(await page.locator('[data-warning]').isVisible(),false);
    assert.equal(await page.locator('[data-pcb-warning]').isVisible(),false);
    assert.equal(await page.locator('[data-body-overlap="true"]').count(),0);
    const snapshot=await page.locator('#energy-led-layout').ariaSnapshot();
    fs.mkdirSync(path.join(__dirname,'../../output/playwright'),{recursive:true});
    fs.writeFileSync(path.join(__dirname,'../../output/playwright/led-preview-snapshot.txt'),snapshot);
    await page.locator('[data-field="layout"]').selectOption('polar1');
    assert.equal(await page.locator('[data-led-id]').count(),617);
    await page.locator('[data-field="mode"]').selectOption('rings');
    await page.locator('[data-action="none"]').click();
    assert.equal(await page.locator('[data-lit="true"]').count(),0);
    await page.locator('[data-ring="10"]').click();
    assert.equal(await page.locator('[data-lit="true"]').count(),9);
    await page.locator('[data-action="all"]').click();
    assert.equal(await page.locator('[data-lit="true"]').count(),617);
    await page.locator('[data-action="alternating"]').click();
    assert.equal(await page.locator('[data-lit="true"]').count(),L.generate({layout:'polar1',pitch:8,packageSize:2}).filter(p=>[2,4,6,8,10].includes(p.ring)).length);
    await page.locator('[data-field="layout"]').selectOption('polar2');
    await page.locator('[data-field="mode"]').selectOption('target');
    assert.equal(await page.locator('[data-lit="true"]').count(),620);
    await page.locator('[data-field="packageSize"]').selectOption('5');
    assert.equal(await page.locator('[data-warning]').isVisible(),true);
    await page.locator('[data-field="packageSize"]').selectOption('2');
    assert.equal(await page.locator('[data-warning]').isVisible(),false);
    await page.locator('[data-field="pitch"]').fill('10');
    const total10=L.generate({layout:'polar2',pitch:10,packageSize:2}).length;
    assert.equal(await page.locator('[data-led-id]').count(),total10);
    await page.locator('[data-field="pitch"]').fill('8');
    // Verify exports contain geometry rather than just a screenshot.
    const downloadEvent=page.waitForEvent('download');
    await page.locator('[data-export="csv"]').click();
    const download=await downloadEvent;
    const csvFile=await download.path();
    assert.equal(fs.readFileSync(csvFile,'utf8').trim().split(/\r?\n/).length,1218);
    await page.locator('[data-field="layout"]').selectOption('hybrid341');
    await page.locator('[data-field="supplement"]').uncheck();
    assert.equal(await page.locator('[data-led-id]').count(),300);
    assert.equal(await page.locator('[data-capacitor-id]').count(),300);
    await page.locator('[data-field="supplement"]').check();
    assert.equal(await page.locator('[data-led-id]').count(),332);
    await page.locator('[data-field="capacitors"]').uncheck();
    assert.equal(await page.locator('[data-capacitor-id]').count(),0);
    await page.locator('[data-field="capacitors"]').check();
    assert.equal(await page.locator('[data-capacitor-id]').count(),332);
    await page.locator('[data-field="smallSize"]').selectOption('3.5');
    assert.equal(await page.locator('[data-warning]').isVisible(),false);
    await page.locator('[data-field="smallSize"]').selectOption('2.2');
    assert.equal(await page.locator('[data-warning]').isVisible(),false);
    const mixedDownloadEvent=page.waitForEvent('download');
    await page.locator('[data-export="csv"]').click();
    const mixedDownload=await mixedDownloadEvent;
    const exported=fs.readFileSync(await mixedDownload.path(),'utf8');
    assert.equal(exported.trim().split(/\r?\n/).length,333);
    const csvColumns=exported.split(/\r?\n/)[0].split(',');
    for(const line of exported.trim().split(/\r?\n/).slice(1)) {
      const cells=line.split(',');
      if(cells[csvColumns.indexOf('package_kind')]==='small') {
        const r=Math.hypot(Number(cells[1]),Number(cells[2]));
        assert.ok([82.5,97.5,112.5,142.5].some(v=>Math.abs(v-r)<.0001),'CSV rounding must preserve the chosen radius');
      }
    }
    assert.ok(exported.split(/\r?\n/)[0].includes('package_kind'));
    const pcbDownloadEvent=page.waitForEvent('download');
    await page.locator('[data-export="pcb"]').click();
    const pcbDownload=await pcbDownloadEvent;
    assert.equal(fs.readFileSync(await pcbDownload.path(),'utf8'),pcbCsv);
    await page.locator('[data-field="outline"]').uncheck();
    await page.locator('[data-field="glow"]').check();
    await page.screenshot({path:path.join(__dirname,'../../output/playwright/led-preview-target.png'),fullPage:true});
    await page.locator('[data-field="mode"]').selectOption('rings');
    await page.locator('[data-action="alternating"]').click();
    assert.equal(await page.locator('[data-package="small"][data-lit="true"]').count(),0);
    await page.locator('[data-action="all"]').click();
    assert.equal(await page.locator('[data-lit="true"]').count(),300);
    await page.locator('[data-action="alternating"]').click();
    await page.screenshot({path:path.join(__dirname,'../../output/playwright/led-preview-rings.png'),fullPage:true});
    await page.setViewportSize({width:360,height:1000});
    assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth));
    await page.screenshot({path:path.join(__dirname,'../../output/playwright/led-preview-mobile.png'),fullPage:true});
    // Exercise the actual inline fragment inside an iframe at the conversation width.
    const fragmentPath='C:/Users/shuto/.codex/visualizations/2026/10/01/01a0f6b6-1c27-7a71-8719-b04c8265c9bf/energy-led-layout.html';
    const fragment=fs.readFileSync(fragmentPath,'utf8');
    const css=fs.readFileSync(path.join(__dirname,'standalone.css'),'utf8');
    await page.setViewportSize({width:760,height:1000});
    await page.setContent('<iframe style="width:736px;height:1000px;border:0" sandbox="allow-scripts"></iframe>');
    await page.locator('iframe').evaluate((e,args)=>{e.srcdoc='<style>'+args.css+'</style>'+args.fragment;},{css,fragment});
    const frame=page.frameLocator('iframe');
    await frame.locator('[data-led-id]').first().waitFor();
    await frame.locator('[data-field="mode"]').selectOption('rings');
    await frame.locator('[data-action="none"]').click();
    await frame.locator('[data-ring="1"]').click();
    assert.equal(await frame.locator('[data-lit="true"]').count(),mixed.filter(p=>p.ring===1&&p.kind==='5050').length);
    // Exercise the skill's real CSP wrapper, rather than only a permissive srcdoc.
    const sandboxPath=path.join(__dirname,'../../output/playwright/energy-led-layout-sandbox.html');
    const rendererRoot=path.join(require('node:os').homedir(),'.codex/plugins/cache/openai-bundled/visualize');
    const renderer=process.env.CODEX_VISUALIZE_RENDERER || fs.readdirSync(rendererRoot)
      .sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}))
      .map(version=>path.join(rendererRoot,version,'skills/visualize/scripts/render.py'))
      .find(file=>fs.existsSync(file));
    assert.ok(renderer&&fs.existsSync(renderer),'Installed visualization renderer not found');
    require('node:child_process').execFileSync('C:/Users/shuto/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',[
      renderer,fragmentPath,sandboxPath,'--force'
    ],{stdio:'pipe'});
    await page.goto(pathToFileURL(sandboxPath).href);
    const sandbox=page.frameLocator('#codex-visualization');
    const root=sandbox.locator('#energy-led-layout');
    await sandbox.locator('[data-led-id]').first().waitFor({timeout:5000});
    assert.equal(await sandbox.locator('[data-led-id]').count(),332);
    assert.equal(await sandbox.locator('[data-capacitor-id]').count(),332);
    assert.equal(await sandbox.locator('[data-lit="true"]').count(),148);
    // Replayed identical host snapshots must not rebuild the plot repeatedly.
    const replayMutations=await root.evaluate(async element=>{
      let count=0;
      const observer=new MutationObserver(records=>{count+=records.length;});
      observer.observe(element.querySelector('.led-plots'),{subtree:true,childList:true,attributes:true});
      const saved={modelContent:{schema:4,layout:'hybrid341',pitch:8,packageSize:2,smallSize:2.2,supplement:true,mode:'target',rings:[2,4,6,8,10]},privateContent:{sensor:true,outline:false,glow:false}};
      for(let i=0;i<30;i++) window.dispatchEvent(new CustomEvent('openai:set_globals',{detail:{globals:{widgetState:saved}}}));
      await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
      observer.disconnect();return count;
    });
    assert.equal(replayMutations,0,'Identical host state must not redraw SVG');
    // Timeout/throw, void-returning and unresolved bridges must leave local UI usable.
    await root.evaluate(()=>{window.openai.setWidgetState=()=>{throw new Error('TimeoutError: MCP sandbox RPC timed out');};});
    await sandbox.locator('[data-field="mode"]').selectOption('rings');
    await sandbox.locator('[data-action="all"]').click();
    assert.equal(await sandbox.locator('[data-lit="true"]').count(),300);
    await root.evaluate(()=>{window.openai.setWidgetState=()=>undefined;});
    await sandbox.locator('[data-action="none"]').click();
    assert.equal(await sandbox.locator('[data-lit="true"]').count(),0);
    await root.evaluate(()=>{window.openai.setWidgetState=()=>new Promise(()=>{});});
    await sandbox.locator('[data-ring="10"]').click();
    assert.equal(await sandbox.locator('[data-lit="true"]').count(),4);
    await sandbox.locator('[data-field="mode"]').selectOption('target');
    assert.equal(await sandbox.locator('[data-lit="true"]').count(),148);
    const heightMutations=await root.evaluate(async element=>{
      let count=0;
      const observer=new MutationObserver(records=>{count+=records.length;});
      observer.observe(element.querySelector('.led-plots'),{subtree:true,childList:true,attributes:true});
      element.style.paddingBottom='13px';
      await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
      observer.disconnect();return count;
    });
    assert.equal(heightMutations,0,'Root height changes must not redraw SVG');
    await page.setViewportSize({width:360,height:1200});
    // Let the sandbox iframe and responsive controls finish their resize before
    // the pointer click. Its outer height is updated independently of the SVG.
    await root.evaluate(async element=>{
      let previous='',stable=0;
      for(let i=0;i<30&&stable<4;i++) {
        await new Promise(resolve=>requestAnimationFrame(resolve));
        const r=element.getBoundingClientRect();
        const current=[r.x,r.y,r.width,r.height,document.documentElement.clientWidth].join('/');
        stable=current===previous?stable+1:0;previous=current;
      }
    });
    await sandbox.locator('[data-field="sensor"]').uncheck();
    assert.ok(await root.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth));
    await page.screenshot({path:path.join(__dirname,'../../output/playwright/led-inline-sandbox-mobile.png'),fullPage:true});
    await page.setViewportSize({width:760,height:1050});
    await sandbox.locator('[data-field="sensor"]').check();
    await page.screenshot({path:path.join(__dirname,'../../output/playwright/led-inline-sandbox.png'),fullPage:true});
    assert.deepEqual(errors,[]);
    console.log('PASS: remove eight R52.5 small LED/cap pairs and close reference gaps; 332 LEDs including 32 smalls; reverse small-cap shift to local -X 1.5 mm, preserving all surviving rotations and main-pair positions; 16 screws including four at R90 on diagonal lines, 3 mm drills / 5.8 mm pads; no body or planning-envelope overlaps, envelope clearance exceeds 0.2999 mm; 331 chain links and 680-row native CSV agree; deletion/renumber map and small-ref list verified; target/ring modes, CSP/RPC resilience and desktop/mobile fit pass; no JS errors. Actual PCB DRC not run.');
  } finally {await browser.close();}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
