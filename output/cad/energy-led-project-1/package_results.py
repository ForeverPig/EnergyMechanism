from pathlib import Path
import hashlib
import json
import zipfile

root=Path(__file__).resolve().parent
files=['README.md','target-and-rings-sketch.SLDPRT','energy-layout-sketch.SLDPRT','energy-board-3d.SLDPRT','energy-board-3d.step',
       'target-and-rings.dxf','energy-layout-2d.dxf','layout-preview.png','layout-preview.svg','solidworks-3d-preview.png',
       'body-centres.csv','chain-order.csv','project-source.json','geometry-validation.json','solidworks-validation.json',
       'step-import-validation.json','dxf-import-validation.json','cad-geometry.json','sketch-entities.json',
       'export_project.cjs','cad_geometry.py','build_solidworks.py','preview_and_check.py','verify_step_import.py','verify_dxf_import.py']
manifest={name:{'bytes':(root/name).stat().st_size,'sha256':hashlib.sha256((root/name).read_bytes()).hexdigest()} for name in files}
(root/'file-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
out=root/'energy-led-CAD.zip'
with zipfile.ZipFile(out,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for name in files+['file-manifest.json']:
        archive.write(root/name,name)
with zipfile.ZipFile(out) as archive:
    assert archive.testzip() is None
    assert len(archive.namelist())==len(files)+1
print(json.dumps({'archive':str(out),'bytes':out.stat().st_size,'files':len(files)+1,'zipIntegrity':'passed'}))
