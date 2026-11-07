/* Units: mm. Origin: board centre. +X right, +Y up.
 * Hybrid LED ids follow one proposed chain, inner to outer and CCW per ring.
 * Coordinate ordering is not a routed PCB netlist. */
(function (global) {
  'use strict';
  const RADIUS = 150;
  const RING_WIDTH = 15;
  const TARGET_BANDS = [[15, 35], [55, 75], [115, 135]];
  const SPOKE = {outerHalfWidth: 17.5, innerAxial: 50, outerAxial: 150, sideAngleDeg: 7};
  const SENSOR_BOUNDARIES = [15, 30, 45, 60, 75, 90, 105, 120, 135, 150.5];
  // Scoring-ring / target intersections plus a separate cross-only group.
  // Rings 2, 6, 9 are entirely target; ring 10 is entirely background.
  // Physical driver branches may be more numerous than these logical groups.
  const GROUPS = ['1:T','1:B','2:T','3:T','3:B','4:T','4:B','5:T','5:B','6:T','7:T','7:B','8:T','8:B','9:T','10:B','spoke-only:T'];
  const HYBRID_RING_COUNTS = [4,8,16,20,28,32,40,44,52,56]; // inside -> outside; sum 300
  const ENTRY_ROWS = [5,6]; // R82.5, R97.5: enter at the small bead below +X.
  const SMALL_COUNT = 32;
  const CHAIN_ORDER = 'inner-to-outer; counterclockwise per radius; start at +X except R82.5 and R97.5 start at the supplement just clockwise of +X';
  const normalizedAngle = angle => ((angle%(2*Math.PI))+2*Math.PI)%(2*Math.PI);

  function ringOf(x, y) {
    const r = Math.hypot(x, y);
    if (r > RADIUS + 1e-8) return null;
    return Math.max(1, 10 - Math.floor((r + 1e-8) / RING_WIDTH));
  }

  function targetParts(x, y) {
    const r = Math.hypot(x, y);
    if (r > RADIUS + 1e-8) return {band: false, spoke: false, on: false};
    const band = TARGET_BANDS.some(([a, b]) => r >= a - 1e-8 && r <= b + 1e-8);
    // Four tapered arms. 35 mm outer width, 100 mm axial length,
    // each side inclined 7 degrees to the corresponding axis; clipped to D300.
    // This is a dimension-based reconstruction, not official vector CAD.
    const t = Math.max(Math.abs(x), Math.abs(y));
    const u = Math.min(Math.abs(x), Math.abs(y));
    const halfWidth = SPOKE.outerHalfWidth - (SPOKE.outerAxial - t) * Math.tan(SPOKE.sideAngleDeg * Math.PI / 180);
    const spoke = t >= SPOKE.innerAxial - 1e-8 && t <= SPOKE.outerAxial + 1e-8 && u <= halfWidth + 1e-8;
    return {band, spoke, on: band || spoke};
  }

  function generateBaseline(config = {}) {
    const layout = config.layout || 'polar2';
    const pitch = Number(config.pitch ?? 8);
    const packageSize = Number(config.packageSize ?? 2);
    if (!['polar1', 'polar2', 'hex', 'hybrid341'].includes(layout)) throw new Error('Unknown layout');
    if (!(pitch >= 5 && pitch <= 16)) throw new Error('Pitch must be 5..16 mm');
    if (!(packageSize >= 1 && packageSize <= 6)) throw new Error('Package must be 1..6 mm');
    const leds = [];
    // Reserve the diagonal of a square LED package at the board edge.
    const edgeLimit = RADIUS - packageSize / Math.SQRT2;
    const add = (x, y, row, angle, sizeMm=packageSize, kind='uniform', purpose='ring') => {
      if (Math.hypot(x, y) > RADIUS-sizeMm/Math.SQRT2 + 1e-8) return;
      const parts = targetParts(x, y);
      const ring = ringOf(x, y);
      const groupKey = kind==='small'&&purpose==='spoke-fill'?'spoke-only:T':`${ring}:${parts.on ? 'T' : 'B'}`;
      leds.push({id: leds.length, x, y, r: Math.hypot(x, y), ring, row, angle, sizeMm, kind, purpose,
        group: GROUPS.indexOf(groupKey), groupKey,
        target: parts.on, targetBand: parts.band, targetSpoke: parts.spoke,
        rotationDeg:Math.hypot(x,y)<1e-8?0:Math.round((((Math.atan2(y,x)*180/Math.PI+90)%360+360)%360)*1e6)/1e6%360});
    };
    if (layout === 'hybrid341') {
      // Main board: 300 on ring centre lines + one centre bead, all 5050.
      add(0,0,-1,0,5,'5050','centre');
      HYBRID_RING_COUNTS.forEach((count,band)=>{
        const r=band*15+7.5;
        for(let k=0;k<count;k++) {
          const a=k*2*Math.PI/count;
          add(r*Math.cos(a),r*Math.sin(a),band,a,5,'5050','ring');
        }
      });
      if(config.supplement !== false) {
        const smallSize=Number(config.smallSize ?? 2.2);
        if(![2,2.2,3.5].includes(smallSize)) throw new Error('Small body must be 2, 2.2 or 3.5 mm');
        // Small beads share EXISTING circle radii; change only their angles.
        // Two flanking beads per spoke per chosen circle. These radii lie
        // outside the target annuli, so supplements remain cross-only.
        const detail=[[52.5,4.25],[82.5,6.5],[97.5,8],[112.5,9.5],[142.5,8]];
        for(let k=0;k<4;k++) {
          const axis=k*Math.PI/2;
          for(const [r,u] of detail) {
            for(const side of [-1,1]) {
              const angle=axis+side*Math.asin(u/r);
              add(r*Math.cos(angle),r*Math.sin(angle),Math.floor(r/15),angle,smallSize,'small','spoke-fill');
            }
          }
        }
      }
    } else if (layout === 'hex') {
      const dy = pitch * Math.sqrt(3) / 2;
      const limit = Math.ceil(RADIUS / dy);
      for (let j = -limit; j <= limit; j++) {
        const xs = [];
        for (let k = -Math.ceil(RADIUS / pitch) - 1; k <= Math.ceil(RADIUS / pitch) + 1; k++) {
          const x = (k + (Math.abs(j) % 2) * .5) * pitch;
          if (Math.hypot(x, j * dy) <= edgeLimit + 1e-8) xs.push(x);
        }
        if (Math.abs(j) % 2) xs.reverse();
        xs.forEach(x => add(x, j * dy, j, Math.atan2(j * dy, x)));
      }
    } else {
      add(0, 0, -1, 0);
      const rowsPerRing = layout === 'polar2' ? 2 : 1;
      for (let band = 0; band < 10; band++) {
        for (let row = 0; row < rowsPerRing; row++) {
          const r = band * RING_WIDTH + (row + .5) * RING_WIDTH / rowsPerRing;
          // Multiple of four ensures beads on the four spoke centre axes.
          const count = Math.max(4, 4 * Math.ceil((2 * Math.PI * r / pitch) / 4));
          const radialRow = band * rowsPerRing + row;
          for (let k = 0; k < count; k++) {
            const angle = k * 2 * Math.PI / count;
            add(r * Math.cos(angle), r * Math.sin(angle), radialRow, angle);
          }
        }
      }
    }
    return leds;
  }

  function generate(config = {}) {
    const baseline = generateBaseline(config);
    // Preserve the original geometry as the reference for the requested edit.
    // Keep the layout key for compatibility with saved preview selections.
    if (config.layout !== 'hybrid341') return baseline;
    // Use the integer radial row rather than the computed radius, whose last
    // floating-point digits vary around a circle. Mix both package sizes.
    const sorted=baseline.slice(1)
      .sort((a,b)=>a.row-b.row||normalizedAngle(a.angle)-normalizedAngle(b.angle)||a.id-b.id);
    // Reconstruct the full previous 340-bead chain before deleting R52.5 small
    // LEDs. Baseline IDs stay fixed so historical placements remain comparable.
    const previousOrder=[];
    for(let row=0;row<10;row++) {
      const members=sorted.filter(led=>led.row===row);
      if([3,5,6].includes(row)&&members.at(-1)?.kind==='small') members.unshift(members.pop());
      previousOrder.push(...members);
    }
    const previousIds=new Map(previousOrder.map((led,id)=>[led.id,id]));
    const ordered=[];
    for(let row=0;row<10;row++) {
      const members=sorted.filter(led=>led.row===row&&!(row===3&&led.kind==='small'));
      // Keep the remaining circles in CCW order and assign continuous IDs.
      if(ENTRY_ROWS.includes(row)&&members.at(-1)?.kind==='small') members.unshift(members.pop());
      const start=normalizedAngle(members[0].angle);
      for(const led of members) ordered.push({...led,id:ordered.length,
        previousId:previousIds.get(led.id),preCcwId:led.id-1,
        chainAngleDeg:normalizedAngle(led.angle)*180/Math.PI,
        chainProgressDeg:normalizedAngle(normalizedAngle(led.angle)-start)*180/Math.PI,
        baselineId:led.id,baselineRotationDeg:led.rotationDeg,
        smallRotationAddedDeg:led.kind==='small'?180:0,
        rotationDeg:Math.round(((led.rotationDeg+180+(led.kind==='small'?180:0))%360)*1e6)/1e6});
    }
    return ordered;
  }

  function chainRings(leds) {
    return Array.from(new Set(leds.map(p=>p.row))).map(row=>{
      const members=leds.filter(p=>p.row===row);
      return {row,ring:members[0].ring,radiusMm:row*15+7.5,
        count:members.length,main:members.filter(p=>p.kind==='5050').length,
        small:members.filter(p=>p.kind==='small').length,
        firstDesignator:'LED'+(members[0].id+1),lastDesignator:'LED'+(members.at(-1).id+1),
        startAngleDeg:members[0].chainAngleDeg};
    });
  }

  function isLit(led, mode, selectedRings = [2, 4, 6, 8, 10]) {
    return mode === 'target' ? led.target : led.kind!=='small'&&selectedRings.includes(led.ring);
  }

  function minDistance(leds) {
    let best = Infinity;
    for (let a = 0; a < leds.length; a++) {
      for (let b = a + 1; b < leds.length; b++) {
        best = Math.min(best, Math.hypot(leds[a].x - leds[b].x, leds[a].y - leds[b].y));
      }
    }
    return best;
  }

  const bodyCache=new WeakMap();
  function bodyStats(leds) {
    if(!bodyCache.has(leds)) {
      const P=typeof PcbLayout!=='undefined'?PcbLayout:
        typeof module==='object'&&module.exports?require('./pcb-layout.js'):global.PcbLayout;
      if(!P) throw new Error('pcb-layout.js is required for rotated geometry checks.');
      bodyCache.set(leds,P.stats(P.ledParts(leds)));
    }
    return bodyCache.get(leds);
  }
  function bodyOverlaps(leds) { return bodyStats(leds).overlaps; }
  function minBodyGap(leds) { return bodyStats(leds).minGapMm; }

  function csv(leds) {
    return 'id,x_mm,y_mm,ring,row,rotation_deg,target_on,target_band,target_spoke,logical_group,group_key,body_mm,package_kind,purpose\r\n' +
      leds.map(p => [p.id, p.x.toFixed(4), p.y.toFixed(4), p.ring, p.row, p.rotationDeg,
        +p.target, +p.targetBand, +p.targetSpoke, p.group, p.groupKey,p.sizeMm,p.kind,p.purpose].join(',')).join('\r\n') + '\r\n';
  }

  function cHeader(leds) {
    return '#pragma once\n#include <stdint.h>\n\n' +
      '// Hybrid ids: inner to outer, CCW per ring. R82.5/R97.5 start at the small LED below +X.\n' +
      '// Route DOUT -> DIN in this order; verify against the final PCB netlist.\n' +
      '#define LED_LAYOUT_COUNT ' + leds.length + 'u\n' +
      '#define LED_LOGICAL_GROUP_COUNT ' + GROUPS.length + 'u\n' +
      'typedef struct { int16_t x_centi_mm, y_centi_mm; uint16_t body_centi_mm; uint8_t ring, target, group, small_package; } led_layout_t;\n' +
      'static const led_layout_t led_layout[LED_LAYOUT_COUNT] = {\n' +
      leds.map(p => `  {${Math.round(p.x * 100)}, ${Math.round(p.y * 100)}, ${Math.round(p.sizeMm*100)}, ${p.ring}, ${+p.target}, ${p.group}, ${+(p.kind==='small')}}`).join(',\n') +
      '\n};\n';
  }

  const api = {RADIUS, RING_WIDTH, TARGET_BANDS, SPOKE, SENSOR_BOUNDARIES, GROUPS, HYBRID_RING_COUNTS, ENTRY_ROWS, SMALL_COUNT, CHAIN_ORDER,chainRings,
    ringOf, targetParts, generate, isLit, minDistance, bodyOverlaps, minBodyGap, csv, cHeader};
  if (typeof module === 'object' && module.exports) module.exports = api;
  else global.LedLayout = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
