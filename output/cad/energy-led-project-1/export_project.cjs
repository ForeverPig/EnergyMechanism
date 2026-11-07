// Use the editor's current chain and body-centre geometry, not historical CSVs.
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const E=require('../../led-layout/editor-model.js');
const L=require('../../led-layout/layout.js');
const input=process.argv[2]||'D:/Download/energy-led-project (1).json';
const bytes=fs.readFileSync(input),project=E.upgrade(JSON.parse(bytes.toString('utf8')));
const board=E.materialize(project);
const data={sourceFile:path.resolve(input),sourceSha256:crypto.createHash('sha256').update(bytes).digest('hex'),
  coordinateReference:'body-centre',units:'mm',origin:'centre',yAxis:'up',
  boardRadiusMm:L.RADIUS,ringBoundariesMm:Array.from({length:10},(_,i)=>(i+1)*15),
  sensorBoundariesMm:L.SENSOR_BOUNDARIES,sensorOutlineRadiusMm:155,
  targetBandsMm:L.TARGET_BANDS,spoke:L.SPOKE,
  assumptions:{pcbThicknessMm:1.6,largeLedHeightMm:1.6,smallLedHeightMm:1.6,capacitorHeightMm:0.5,targetIsSketchOnly:true},
  project,board};
fs.writeFileSync(path.join(__dirname,'cad-geometry.json'),JSON.stringify(data,null,2));
fs.writeFileSync(path.join(__dirname,'project-source.json'),bytes);
fs.writeFileSync(path.join(__dirname,'body-centres.csv'),E.exportCsv(project,'centres'));
fs.writeFileSync(path.join(__dirname,'chain-order.csv'),E.exportCsv(project,'chain'));
console.log(JSON.stringify({output:__dirname,sourceSha256:data.sourceSha256,leds:board.leds.length,caps:board.capacitors.length,holes:board.mountingHoles.length}));
