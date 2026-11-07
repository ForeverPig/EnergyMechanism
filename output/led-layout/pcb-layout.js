/* Placement planning only. Envelope dimensions are assumptions, not library pads. */
(function (global) {
  'use strict';
  const CAP = {value:'100nF',package:'0402',widthMm:1.0,heightMm:0.5,envelopeWidthMm:1.6,envelopeHeightMm:1.0};
  const LED_MARGIN = 0.3;
  const CLEARANCE = 0.25;
  const SCREW = {drillDiameterMm:3,outerDiameterMm:5.8,plated:true};
  const roundAngle = angle => Math.round(((angle%360+360)%360)*1e6)/1e6;
  const rad = degrees => degrees*Math.PI/180;
  function rotate(x,y,degrees) {
    const a=rad(degrees);
    return {x:x*Math.cos(a)-y*Math.sin(a),y:x*Math.sin(a)+y*Math.cos(a)};
  }
  function corners(part,envelope=false) {
    const width=envelope?part.envelopeWidthMm:part.widthMm;
    const height=envelope?part.envelopeHeightMm:part.heightMm;
    return [[-1,-1],[1,-1],[1,1],[-1,1]].map(([sx,sy])=>{
      const p=rotate(sx*width/2,sy*height/2,part.rotationDeg);
      return {x:p.x+part.x,y:p.y+part.y};
    });
  }
  function pointSegmentDistance(p,a,b) {
    const dx=b.x-a.x,dy=b.y-a.y;
    const t=Math.max(0,Math.min(1,((p.x-a.x)*dx+(p.y-a.y)*dy)/(dx*dx+dy*dy)));
    return Math.hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
  }
  function polygonGap(a,b) {
    let separated=false;
    for(const poly of [a,b]) for(let i=0;i<4;i++) {
      const edge={x:poly[(i+1)%4].x-poly[i].x,y:poly[(i+1)%4].y-poly[i].y};
      const projection=p=>-edge.y*p.x+edge.x*p.y;
      const pa=a.map(projection),pb=b.map(projection);
      if(Math.max(...pa)<Math.min(...pb)-1e-9 || Math.max(...pb)<Math.min(...pa)-1e-9) separated=true;
    }
    if(!separated) return 0;
    let gap=Infinity;
    for(let i=0;i<4;i++) for(let j=0;j<4;j++) {
      gap=Math.min(gap,pointSegmentDistance(a[i],b[j],b[(j+1)%4]),pointSegmentDistance(b[i],a[j],a[(j+1)%4]));
    }
    return gap;
  }
  function prepared(part,envelope) {
    if(part.shape==='circle') {
      const r=part.outerDiameterMm/2;
      return {part,left:part.x-r,right:part.x+r,bottom:part.y-r,top:part.y+r};
    }
    const points=corners(part,envelope);
    return {part,points,left:Math.min(...points.map(p=>p.x)),right:Math.max(...points.map(p=>p.x)),bottom:Math.min(...points.map(p=>p.y)),top:Math.max(...points.map(p=>p.y))};
  }
  function lowerGap(a,b) {
    return Math.hypot(Math.max(0,a.left-b.right,b.left-a.right),Math.max(0,a.bottom-b.top,b.bottom-a.top));
  }
  function circlePartGap(circle,part,envelope=false) {
    if(part.shape==='circle') return Math.hypot(circle.x-part.x,circle.y-part.y)-(circle.outerDiameterMm+part.outerDiameterMm)/2;
    const local=rotate(circle.x-part.x,circle.y-part.y,-part.rotationDeg);
    const width=envelope?part.envelopeWidthMm:part.widthMm;
    const height=envelope?part.envelopeHeightMm:part.heightMm;
    return Math.hypot(Math.max(0,Math.abs(local.x)-width/2),Math.max(0,Math.abs(local.y)-height/2))-circle.outerDiameterMm/2;
  }
  function stats(parts,envelope=false) {
    const list=parts.map(p=>prepared(p,envelope));
    let minimum=Infinity,overlaps=0,closest=null;
    const overlapPairs=[];
    for(let i=0;i<list.length;i++) for(let j=i+1;j<list.length;j++) {
      if(lowerGap(list[i],list[j])>minimum+1e-9) continue;
      const gap=Math.max(0,parts[i].shape==='circle'?circlePartGap(parts[i],parts[j],envelope):
        parts[j].shape==='circle'?circlePartGap(parts[j],parts[i],envelope):polygonGap(list[i].points,list[j].points));
      if(gap<minimum){minimum=gap;closest=[parts[i].designator,parts[j].designator];}
      if(gap<1e-8) {overlaps++;overlapPairs.push([parts[i].designator,parts[j].designator]);}
    }
    return {minGapMm:minimum,overlaps,closest,overlapPairs};
  }
  function ledParts(leds) {
    return leds.map(p=>({designator:'LED'+(p.id+1),ledId:p.id,type:'led',kind:p.kind,x:p.x,y:p.y,rotationDeg:p.rotationDeg,widthMm:p.sizeMm,heightMm:p.sizeMm,envelopeWidthMm:p.sizeMm+2*LED_MARGIN,envelopeHeightMm:p.sizeMm+2*LED_MARGIN}));
  }
  function planBaselineCapacitors(leds) {
    const obstacles=ledParts(leds).map(p=>prepared(p,true));
    const caps=[];
    for(const led of leds) {
      // At 0 degrees: the cap lies horizontally above the upper-left supply corner.
      // Its left envelope edge aligns with that corner; both rotate with the LED.
      const nominalX=-led.sizeMm/2+CAP.envelopeWidthMm/2;
      const nominalY=led.sizeMm/2+CAP.envelopeHeightMm/2+LED_MARGIN+CLEARANCE+0.05;
      let chosen=null;
      for(const dy of [0,0.25,0.5,0.75,1,1.5]) {
        for(const dx of [0,0.25,-0.25,0.5,-0.5,0.75,-0.75,1,-1]) {
          const localX=nominalX+dx,localY=nominalY+dy;
          const offset=rotate(localX,localY,led.rotationDeg);
          const cap={...CAP,designator:'C'+(led.id+1),ledId:led.id,type:'capacitor',x:led.x+offset.x,y:led.y+offset.y,rotationDeg:led.rotationDeg,localX,localY,adjusted:dx!==0||dy!==0};
          const candidate=prepared(cap,true);
          if(candidate.points.some(p=>Math.hypot(p.x,p.y)>150-0.25)) continue;
          if(obstacles.some(other=>lowerGap(candidate,other)<CLEARANCE && polygonGap(candidate.points,other.points)<CLEARANCE-1e-8)) continue;
          chosen=cap;obstacles.push(candidate);break;
        }
        if(chosen) break;
      }
      if(!chosen) throw new Error('No nearby capacitor position for LED'+(led.id+1)+' with the planning envelopes.');
      caps.push(chosen);
    }
    return caps;
  }
  function priorCapacitors(leds) {
    // This revision rotates each cap about its OWN centre by another 180 deg.
    // Apply after planning so centre positions and previous avoidance stay fixed.
    const flip=cap=>({...cap,rotationDeg:Math.round(((cap.rotationDeg+180)%360)*1e6)/1e6,additionalRotationDeg:180});
    if(!leds.length || !leds.every(p=>Number.isInteger(p.baselineId)&&Number.isFinite(p.baselineRotationDeg))) {
      return planBaselineCapacitors(leds).map(flip);
    }
    // Reconstruct the pre-edit board so its four adjusted capacitor positions
    // remain exactly the same reference. The removed centre is only an obstacle
    // during this baseline calculation; neither LED1 nor C1 is exported.
    // Reordering designators must never reorder the greedy placement planner:
    // keep its original physical order so all cap coordinates remain unchanged.
    const physicalOrder=[...leds].sort((a,b)=>a.baselineId-b.baselineId);
    const baseline=[{id:0,x:0,y:0,sizeMm:5,kind:'5050',rotationDeg:0},
      ...physicalOrder.map(p=>({...p,id:p.baselineId,rotationDeg:p.baselineRotationDeg}))];
    const capsById=new Map(planBaselineCapacitors(baseline).map(p=>[p.ledId,p]));
    const obstacles=ledParts(leds).map(p=>prepared(p,true));
    const caps=physicalOrder.map(led=>{
      const cap=capsById.get(led.baselineId);
      // Keep the 0-degree local left shift, then rotate the entire LED/cap pair.
      // The cap's position follows the LED about the LED centre, so it remains
      // beside the local upper-left supply corner after the 180-degree flip.
      const localX=cap.localX-1.5;
      const nominalY=cap.localY;
      for(const dy of [0,0.25,0.5,0.75,1,1.25,1.5,2,2.5,3]) {
        const localY=nominalY+dy;
        const offset=rotate(localX,localY,led.rotationDeg);
        const placed={...cap,designator:'C'+(led.id+1),ledId:led.id,
          baselineDesignator:cap.designator,baselineRotationDeg:cap.rotationDeg,
          x:led.x+offset.x,y:led.y+offset.y,rotationDeg:led.rotationDeg,
          localX,localY,shiftLocalMm:{x:-1.5,y:0},
          baselineAdjusted:cap.adjusted,flippedSideAdjustmentMm:dy,
          adjusted:cap.adjusted||dy!==0};
        const candidate=prepared(placed,true);
        if(candidate.points.some(p=>Math.hypot(p.x,p.y)>150-0.25)) continue;
        if(obstacles.some(other=>lowerGap(candidate,other)<CLEARANCE&&polygonGap(candidate.points,other.points)<CLEARANCE-1e-8)) continue;
        obstacles.push(candidate);
        return placed;
      }
      throw new Error('No supply-side capacitor position for LED'+(led.id+1)+' with the planning envelopes.');
    });
    const byId=new Map(caps.map(cap=>[cap.ledId,cap]));
    return leds.map(led=>flip(byId.get(led.id)));
  }
  function capacitors(leds) {
    // Plan exactly the previous physical placement first. A small-cap edit
    // rotates about its own centre; its centre must not follow the LED flip.
    const previousLeds=leds.map(led=>led.smallRotationAddedDeg?
      {...led,rotationDeg:roundAngle(led.rotationDeg-led.smallRotationAddedDeg)}:led);
    return priorCapacitors(previousLeds).map((cap,i)=>{
      const led=leds[i];
      if(!led.smallRotationAddedDeg) return cap;
      const shift=rotate(-1.5,0,led.rotationDeg);
      const x=cap.x+shift.x,y=cap.y+shift.y;
      const local=rotate(x-led.x,y-led.y,-led.rotationDeg);
      return {...cap,x,y,rotationDeg:roundAngle(cap.rotationDeg+180),localX:local.x,localY:local.y,
        smallEdit:{rotationAddedDeg:180,centreFollowsLed:false,shiftLocalMm:{x:-1.5,y:0},shiftReference:'current-led-local',previousX:cap.x,previousY:cap.y,previousRotationDeg:cap.rotationDeg}};
    });
  }
  function mountingHoles() {
    const holes=[];
    for(const [radiusMm,ringBoundary] of [[30,'9-8'],[60,'7-6'],[90,'5-4'],[120,'3-2']]) {
      for(const angleDeg of [45,135,225,315]) {
        const a=rad(angleDeg);
        const previousIndex=radiusMm===30?holes.length:radiusMm===60?holes.length:radiusMm===120?holes.length-4:null;
        holes.push({...SCREW,designator:'SCREW'+(holes.length+1),previousDesignator:previousIndex===null?null:'SCREW'+(previousIndex+1),type:'mounting-hole',shape:'circle',
          x:radiusMm*Math.cos(a),y:radiusMm*Math.sin(a),rotationDeg:0,radiusMm,angleDeg,ringBoundary,
          widthMm:SCREW.outerDiameterMm,heightMm:SCREW.outerDiameterMm,
          envelopeWidthMm:SCREW.outerDiameterMm,envelopeHeightMm:SCREW.outerDiameterMm});
      }
    }
    return holes;
  }
  function mountingClearance(holes,parts,envelope=false) {
    let minGapMm=Infinity,closest=null;
    const overlapPairs=[];
    for(const hole of holes) for(const part of parts) {
      const gap=circlePartGap(hole,part,envelope);
      if(gap<minGapMm){minGapMm=gap;closest=[hole.designator,part.designator];}
      if(gap<=1e-8) overlapPairs.push([hole.designator,part.designator]);
    }
    return {minGapMm,closest,overlaps:overlapPairs.length,overlapPairs,
      minBoardEdgeGapMm:Math.min(...holes.map(hole=>150-Math.hypot(hole.x,hole.y)-hole.outerDiameterMm/2))};
  }
  function generate(leds) {
    const ledsAsParts=ledParts(leds),caps=capacitors(leds);
    const edited=leds.some(p=>Number.isInteger(p.baselineId));
    const holes=edited?mountingHoles():[];
    return {leds:ledsAsParts,capacitors:caps,mountingHoles:holes,parts:[...ledsAsParts,...caps,...holes],
      assumptions:{cap:CAP,screw:SCREW,ledEnvelopeMarginMm:LED_MARGIN,envelopeClearanceMm:CLEARANCE,
        ledFacing:edited?'5050 local-top-outward; small local-top-inward':'local-top-inward',
        capOrientation:'LED angle plus 180 degrees; unchanged from prior revision',capPosition:'main unchanged; small own-centre rotation plus current-LED-local -X 1.5 mm',
        edit:edited?{mainRotationAddedFromOriginalDeg:180,smallRotationAddedFromPreviousDeg:180,
          smallCapRotationAddedDeg:180,smallCapCentreFollowsLed:false,smallCapShiftLocalMm:{x:-1.5,y:0},
          originalRemoved:['LED1','C1'],removedRing7SmallCount:8,renumber:'remove R52.5 small LED/cap pairs and close gaps; add R90 screws before R120; Ck pairs with LEDk'}:null,
        pinCorner:'0-degree upper-left marker; verify actual footprint supply pin'}};
  }
  function coordinateCsv(parts) {
    const quote=v=>'"'+String(v).replaceAll('"','""')+'"';
    const mm=v=>(Math.abs(v)<0.0000005?0:v).toFixed(6)+'mm';
    const rows=[['Designator','Mid X','Mid Y','Rotation'],...parts.map(p=>[p.designator,mm(p.x),mm(p.y),p.rotationDeg])];
    return '\uFEFF'+rows.map(row=>row.map(quote).join(',')).join('\r\n')+'\r\n';
  }
  const api={CAP,SCREW,LED_MARGIN,CLEARANCE,rotate,corners,polygonGap,circlePartGap,stats,ledParts,capacitors,mountingHoles,mountingClearance,generate,coordinateCsv};
  if(typeof module==='object'&&module.exports) module.exports=api;
  else global.PcbLayout=api;
})(typeof globalThis!=='undefined'?globalThis:this);
