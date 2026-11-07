const fs = require('node:fs');
const path = require('node:path');
const L = require('./layout.js');
const P = require('./pcb-layout.js');
const config = {layout:'hybrid341',smallSize:2.2,supplement:true};
const leds = L.generate(config);
const pcb = P.generate(leds);
const previous=JSON.parse(fs.readFileSync(path.join(__dirname,'placement-before-ring7-removal-cap-reverse.json'),'utf8'));
const byPreviousId=new Map(leds.map(led=>[led.previousId,led]));
const deletedDesignators=previous.leds.filter(led=>!byPreviousId.has(led.id)).flatMap(led=>['LED'+(led.id+1),'C'+(led.id+1)]);
const placement = {units:'mm',origin:'centre',yAxis:'up',rotationPositive:'counterclockwise',idOrder:L.CHAIN_ORDER,revision:'20261003-ring7-small-removed-cap-left-r90-screws',config,leds,capacitors:pcb.capacitors,mountingHoles:pcb.mountingHoles,deletedDesignators,chainRings:L.chainRings(leds)};
const counts = Array.from({length:10},(_,k)=>({ring:k+1,count:leds.filter(p=>p.ring===k+1).length}));
fs.writeFileSync(path.join(__dirname,'led-positions.csv'),L.csv(leds),'utf8');
fs.writeFileSync(path.join(__dirname,'led-positions.json'),JSON.stringify({...placement,counts},null,2),'utf8');
fs.writeFileSync(path.join(__dirname,'led-layout.h'),L.cHeader(leds),'utf8');
const {placementCsv}=require('./export_jlceda.cjs');
for(const [file,include] of [['jlceda-positions.csv','all'],['jlceda-led-positions.csv','led'],['jlceda-capacitor-positions.csv','capacitor'],['jlceda-screw-positions.csv','screw']]) {
  fs.writeFileSync(path.join(__dirname,file),placementCsv(placement,{include}),'utf8');
}
const report={assumptions:pcb.assumptions,ledCount:leds.length,capacitorCount:pcb.capacitors.length,screwCount:pcb.mountingHoles.length,smallEdits:pcb.capacitors.filter(p=>p.smallEdit).map(p=>({designator:p.designator,...p.smallEdit})),adjustedCapacitors:pcb.capacitors.filter(p=>p.adjusted).map(p=>p.designator),supplySideAdjustments:pcb.capacitors.filter(p=>p.flippedSideAdjustmentMm).map(p=>({designator:p.designator,localYAddedMm:p.flippedSideAdjustmentMm})),body:P.stats(pcb.parts),planningEnvelope:P.stats(pcb.parts,true),mountingClearance:P.mountingClearance(pcb.mountingHoles,[...pcb.leds,...pcb.capacitors],true),actualPcbDrc:'not-run',smallLedSupplyPin:'verify-selected-part; 2020-V6 uses VDD pin 4'};
fs.writeFileSync(path.join(__dirname,'pcb-positions.json'),JSON.stringify({...placement,parts:pcb.parts,report},null,2),'utf8');
fs.writeFileSync(path.join(__dirname,'screw-positions.json'),JSON.stringify({units:'mm',origin:'centre',yAxis:'up',mountingHoles:pcb.mountingHoles},null,2),'utf8');
fs.writeFileSync(path.join(__dirname,'screw-positions.csv'),'\uFEFFDesignator,X_mm,Y_mm,Drill_mm,Pad_outer_mm,Plated,Radius_mm,Angle_deg,Ring_boundary\r\n'+pcb.mountingHoles.map(p=>[p.designator,p.x.toFixed(6),p.y.toFixed(6),p.drillDiameterMm,p.outerDiameterMm,p.plated,p.radiusMm,p.angleDeg,p.ringBoundary].join(',')).join('\r\n')+'\r\n','utf8');
const renumberRows=previous.leds.flatMap(old=>{
  const current=byPreviousId.get(old.id);
  return [`LED${old.id+1},${current?'LED'+(current.id+1):'DELETED'}`,`C${old.id+1},${current?'C'+(current.id+1):'DELETED'}`];
});
renumberRows.push(...pcb.mountingHoles.map(hole=>`${hole.previousDesignator??''},${hole.designator}`));
fs.writeFileSync(path.join(__dirname,'designator-renumber.csv'),'\uFEFFOld Designator,New Designator\r\n'+renumberRows.join('\r\n')+'\r\n','utf8');
const byOriginalId=new Map(leds.map(led=>[led.baselineId,led]));
const original=JSON.parse(fs.readFileSync(path.join(__dirname,'placement-before-20261002.json'),'utf8'));
fs.writeFileSync(path.join(__dirname,'designator-from-original.csv'),'\uFEFFOriginal Designator,Current Designator\r\n'+original.leds.flatMap(old=>{
  const current=byOriginalId.get(old.id);
  return [`LED${old.id+1},${current?'LED'+(current.id+1):'DELETED'}`,`C${old.id+1},${current?'C'+(current.id+1):'DELETED'}`];
}).join('\r\n')+'\r\n','utf8');
fs.writeFileSync(path.join(__dirname,'small-led-designators.csv'),'\uFEFFDesignator,Paired Capacitor,Ring,Radius_mm\r\n'+leds.filter(p=>p.kind==='small').map(p=>[`LED${p.id+1}`,`C${p.id+1}`,p.ring,(p.row*15+7.5).toFixed(1)].join(',')).join('\r\n')+'\r\n','utf8');
fs.writeFileSync(path.join(__dirname,'led-chain-order.csv'),'\uFEFFDesignator,Previous Designator,Radius_mm,Ring,Angle_deg,Package,Next Designator,Paired Capacitor\r\n'+leds.map(p=>[`LED${p.id+1}`,`LED${p.previousId+1}`,(p.row*15+7.5).toFixed(1),p.ring,p.chainAngleDeg.toFixed(6),p.kind,p.id+1<leds.length?`LED${p.id+2}`:'END',`C${p.id+1}`].join(',')).join('\r\n')+'\r\n','utf8');
console.log(JSON.stringify(report,null,2));
console.log(JSON.stringify({config,total:leds.length,large:leds.filter(p=>p.kind==='5050').length,small:leds.filter(p=>p.kind==='small').length,target:leds.filter(p=>p.target).length,minDistance:L.minDistance(leds),minBodyGap:L.minBodyGap(leds),overlaps:L.bodyOverlaps(leds),counts},null,2));
for(const layout of ['polar1','polar2','hex']) {
  const list=L.generate({layout,pitch:8,packageSize:2});
  console.log(JSON.stringify({layout,total:list.length,target:list.filter(p=>p.target).length}));
}
