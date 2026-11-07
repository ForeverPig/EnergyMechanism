const fs = require('node:fs');
const path = require('node:path');

// Native EasyEDA Pro coordinate import: positions existing components by Designator.
// Mid X/Y mean the footprint's pad centre; verify it matches the LED optical centre.
function placementCsv(data, {prefix = 'LED', start = 1, capPrefix = 'C', capStart = 1, include = 'all', cx = 0, cy = 0} = {}) {
  if (data.units !== 'mm' || data.origin !== 'centre' || data.yAxis !== 'up') {
    throw new Error('Expected centre-origin, Y-up coordinates in mm.');
  }
  if (![prefix,capPrefix].every(s=>/^[A-Za-z][A-Za-z0-9_]*$/.test(s)) || ![start,capStart].every(n=>Number.isInteger(n)&&n>=1)) {
    throw new Error('Use a letter-led designator prefix and a positive integer start.');
  }
  if (![cx, cy].every(Number.isFinite)) throw new Error('Invalid centre offset.');
  if (!['all','led','capacitor','screw'].includes(include)) throw new Error('include must be all, led, capacitor or screw.');
  const ids = new Set();
  const refs = new Set();
  const quote = value => '"' + String(value).replaceAll('"', '""') + '"';
  const mm = value => (Math.abs(value) < 0.0000005 ? 0 : value).toFixed(6) + 'mm';
  const rows = [['Designator', 'Mid X', 'Mid Y', 'Rotation']];
  for (const led of ['all','led'].includes(include)?data.leds:[]) {
    if (!Number.isInteger(led.id) || led.id < 0 || ids.has(led.id)) {
      throw new Error('Duplicate or invalid layout ID.');
    }
    if (![led.x, led.y, led.rotationDeg ?? 0].every(Number.isFinite)) {
      throw new Error('Invalid coordinates for layout ID ' + led.id);
    }
    ids.add(led.id);
    const ref=prefix+(start+led.id);refs.add(ref);
    rows.push([ref, mm(led.x + cx), mm(led.y + cy), led.rotationDeg ?? 0]);
  }
  const capIds=new Set();
  for (const cap of ['all','capacitor'].includes(include)?(data.capacitors??[]):[]) {
    const ref=capPrefix+(capStart+cap.ledId);
    if (!Number.isInteger(cap.ledId) || cap.ledId<0 || capIds.has(cap.ledId) || refs.has(ref) || ![cap.x,cap.y,cap.rotationDeg].every(Number.isFinite)) {
      throw new Error('Invalid or duplicate capacitor placement.');
    }
    capIds.add(cap.ledId);refs.add(ref);
    rows.push([ref,mm(cap.x+cx),mm(cap.y+cy),cap.rotationDeg]);
  }
  for(const hole of ['all','screw'].includes(include)?(data.mountingHoles??[]):[]) {
    const ref=hole.designator;
    if(!/^SCREW[1-9]\d*$/.test(ref)||refs.has(ref)||![hole.x,hole.y,hole.rotationDeg].every(Number.isFinite)) {
      throw new Error('Invalid or duplicate mounting-hole placement.');
    }
    refs.add(ref);
    rows.push([ref,mm(hole.x+cx),mm(hole.y+cy),hole.rotationDeg]);
  }
  // Layer omitted intentionally: the native importer defaults to TOP.
  return '\uFEFF' + rows.map(row => row.map(quote).join(',')).join('\r\n') + '\r\n';
}

if (require.main === module) {
  const options = {prefix: 'LED', start: 1, capPrefix: 'C', capStart: 1, include:'all', cx: 0, cy: 0};
  const args = process.argv.slice(2);
  for (let i = 0; i < args.length; i += 2) {
    const key = args[i].replace(/^--/, '').replace(/-([a-z])/g,(_,c)=>c.toUpperCase());
    if (!['prefix', 'start', 'capPrefix', 'capStart', 'include', 'cx', 'cy'].includes(key) || args[i + 1] === undefined) {
      throw new Error('Usage: node export_jlceda.cjs [--prefix LED] [--start 1] [--cap-prefix C] [--cap-start 1] [--include all|led|capacitor|screw] [--cx 0] [--cy 0]');
    }
    options[key] = ['prefix','capPrefix','include'].includes(key) ? args[i + 1] : Number(args[i + 1]);
  }
  const data = JSON.parse(fs.readFileSync(path.join(__dirname, 'led-positions.json'), 'utf8'));
  data.capacitors??=require('./pcb-layout.js').capacitors(data.leds);
  const output = path.join(__dirname, 'jlceda-positions.csv');
  fs.writeFileSync(output, placementCsv(data, options), 'utf8');
  console.log(JSON.stringify({output,ledCount:['all','led'].includes(options.include)?data.leds.length:0,capacitorCount:['all','capacitor'].includes(options.include)?data.capacitors.length:0,screwCount:['all','screw'].includes(options.include)?(data.mountingHoles??[]).length:0,...options}));
}

module.exports = {placementCsv};
