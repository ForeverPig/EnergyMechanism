import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';
import {execFileSync} from 'node:child_process';
// Windows denied a junction; resolve the published API from the loader runtime.
const runtimeRequire=createRequire('C:/Users/shuto/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/loader.cjs');
const {FileBlob,SpreadsheetFile}=await import(pathToFileURL(runtimeRequire.resolve('@oai/artifact-tool')).href);

const support=path.dirname(fileURLToPath(import.meta.url));
const output=path.dirname(support);
const data=JSON.parse(await fs.readFile(path.join(support,'analysis.json'),'utf8'));
const workbook=await SpreadsheetFile.importXlsx(await FileBlob.load(data.source));
const sheet=workbook.worksheets.getItem(data.sheetName);
const expected=[data.header,...data.sourceRows];
assert.deepEqual(sheet.getRange(`A1:O${expected.length}`).values,expected,'Workbook import changed source values');
const summary=await workbook.inspect({kind:'region',sheetId:data.sheetName,range:'A1:F5',maxChars:1800,tableMaxRows:5,tableMaxCols:6});
console.log(summary.ndjson);

async function renderPreview(filename) {
  const blob=await workbook.render({sheetName:data.sheetName,range:'A1:M16',scale:1.5,format:'png'});
  await fs.writeFile(path.join(support,filename),new Uint8Array(await blob.arrayBuffer()));
}
if(process.argv.includes('--verify-compatible')) {
  const final=await SpreadsheetFile.importXlsx(await FileBlob.load(path.join(output,'V2_1_PickAndPlace_SMT_JLC_compatible.xlsx')));
  assert.deepEqual(final.worksheets.getItem(data.sheetName).getRange(`A1:O${expected.length}`).values,[data.header,...data.correctedRows]);
  const preview=await final.render({sheetName:data.sheetName,range:'A1:M16',scale:1.5,format:'png'});
  await fs.writeFile(path.join(support,'jlc-compatible-preview.png'),new Uint8Array(await preview.arrayBuffer()));
  console.log('PASS: compatible XLSX reopened; all 683 records, headers and corrected values match; visual preview saved.');
} else if(process.argv.includes('--inspect-only')) {
  await renderPreview('source-preview.png');
  console.log('READ ONLY: source workbook imported and previewed');
} else {
  assert.ok(process.argv.includes('--write'),'Use --inspect-only or --write');
  sheet.getRange(`D2:E${expected.length}`).values=data.correctedRows.map(r=>[r[3],r[4]]);
  sheet.getRange(`M2:M${expected.length}`).values=data.correctedRows.map(r=>[r[12]]);
  workbook.recalculate();
  assert.deepEqual(sheet.getRange(`A1:O${expected.length}`).values,[data.header,...data.correctedRows]);
  const check=await workbook.inspect({kind:'region',sheetId:data.sheetName,range:'A1:M7',maxChars:2400,tableMaxRows:7,tableMaxCols:13});
  console.log(check.ndjson);
  const errors=await workbook.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:20},summary:'Final formula error scan',maxChars:1200});
  console.log(errors.ndjson);
  await renderPreview('corrected-preview.png');
  const nativePackaging=process.argv.includes('--jlc-compatible');
  const target=path.join(output,nativePackaging?'V2_1_PickAndPlace_SMT_JLC_compatible.xlsx':'V2_1_PickAndPlace_SMT_corrected.xlsx');
  const authored=nativePackaging?path.join(support,'artifact-authoring.xlsx'):target;
  await (await SpreadsheetFile.exportXlsx(workbook)).save(authored);
  if(nativePackaging) {
    console.log(execFileSync('C:/Users/shuto/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',[
      path.join(support,'jlc_native_packaging.py'),authored,target
    ],{encoding:'utf8'}));
  }
  const quote=v=>'"'+String(v??'').replaceAll('"','""')+'"';
  const csv='\uFEFF'+[data.header,...data.correctedRows].map(r=>r.map(quote).join(',')).join('\r\n')+'\r\n';
  await fs.writeFile(path.join(output,'V2_1_PickAndPlace_SMT_corrected.csv'),csv,'utf8');
  console.log(JSON.stringify({output:target,rows:data.correctedRows.length,changedComponents:662,changedCells:data.changes.length}));
}
