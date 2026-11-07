"""Relocatable, FUSE-free Qt directory. Build on Ubuntu 22.04; retain tar executable bits."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile

qt, build = (Path(x).resolve() for x in sys.argv[1:3])
project = Path(__file__).resolve().parents[1]
output = project.parents[1] / 'outputs/DesktopHost'
stage = output / 'EnergyMechanismHost-linux-x64'
if stage.exists():
    if not (stage / '.em-package').is_file() or stage.resolve().parent != output.resolve():
        raise RuntimeError('Refusing to replace an unmarked package directory')
    shutil.rmtree(stage)
stage.mkdir(parents=True)
(stage / '.em-package').write_text('Generated portable package\n')
subprocess.run(['cmake', '--install', str(build), '--prefix', str(stage)], check=True)
(stage / 'lib').mkdir()
(stage / 'plugins/platforms').mkdir(parents=True)
for name in ('libqxcb.so', 'libqoffscreen.so', 'libqwayland-egl.so', 'libqwayland-generic.so'):
    source = qt / 'plugins/platforms' / name
    if source.exists():
        shutil.copy2(source, stage / 'plugins/platforms' / name)
for group in ('xcbglintegrations', 'wayland-decoration-client', 'wayland-graphics-integration-client', 'wayland-shell-integration'):
    source = qt / 'plugins' / group
    if source.exists():
        shutil.copytree(source, stage / 'plugins' / group)
# OS ABI and graphics-driver libraries remain system-owned. Bundle application dependencies.
system = re.compile(r'^(linux-vdso|ld-linux|lib(c|m|dl|rt|pthread|resolv|util|nsl)\.so|lib(GL|EGL|GLX|OpenGL|GLdispatch|drm|gbm))')
env = dict(os.environ, LD_LIBRARY_PATH=f'{stage / "lib"}:{qt / "lib"}')
queue = [stage / 'EnergyMechanismHost', *stage.glob('plugins/**/*.so')]
seen = set()
while queue:
    elf = queue.pop()
    if elf in seen:
        continue
    seen.add(elf)
    text = subprocess.check_output(['ldd', str(elf)], env=env, text=True)
    if 'not found' in text:
        raise RuntimeError(f'Unresolved dependencies for {elf}:\n{text}')
    for line in text.splitlines():
        match = re.match(r'\s*(\S+) => (/\S+)', line)
        if not match or system.match(match[1]):
            continue
        name, source = match.groups()
        destination = stage / 'lib' / name
        if not destination.exists():
            shutil.copy2(source, destination, follow_symlinks=True)
            queue.append(destination)
            # Keep distro copyright notices for bundled system libraries.
            if not str(Path(source).resolve()).startswith(str(qt)):
                lookup = subprocess.run(['dpkg-query', '-S', source], capture_output=True, text=True)
                if lookup.returncode == 0:
                    package = lookup.stdout.split(':')[0]
                    notice = Path('/usr/share/doc') / package / 'copyright'
                    if notice.exists():
                        (stage / 'licenses/system').mkdir(parents=True, exist_ok=True)
                        shutil.copy2(notice, stage / 'licenses/system' / (package + '.txt'))
for elf in seen:
    relative = os.path.relpath(stage / 'lib', elf.parent)
    subprocess.run(['patchelf', '--set-rpath', '$ORIGIN/' + relative, str(elf)], check=True)
(stage / 'qt.conf').write_text('[Paths]\nPlugins=plugins\n')
(stage / 'AppRun').write_text('#!/bin/sh\nset -eu\nAPPDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexport QT_PLUGIN_PATH="$APPDIR/plugins"\nexec "$APPDIR/EnergyMechanismHost" "$@"\n')
(stage / 'AppRun').chmod(0o755)
(stage / 'EnergyMechanismHost').chmod(0o755)
subprocess.run([sys.executable, str(project / 'scripts/collect_licenses.py'), str(qt), str(stage / 'licenses')], check=True)
for elf in seen:
    if 'not found' in subprocess.check_output(['ldd', str(elf)], env=dict(os.environ, LD_LIBRARY_PATH=''), text=True):
        raise RuntimeError('Package is missing a dependency')
manifest = []
for path in sorted(stage.rglob('*')):
    if path.is_file():
        manifest.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(stage).as_posix())
(stage / 'SHA256SUMS').write_text('\n'.join(manifest) + '\n')
archive = output / 'EnergyMechanismHost-0.1.0-linux-x64.tar.gz'
with tarfile.open(archive, 'w:gz') as tar:
    tar.add(stage, arcname=stage.name)
report = dict(archive=str(archive), archive_bytes=archive.stat().st_size,
              extracted_bytes=sum(p.stat().st_size for p in stage.rglob('*') if p.is_file()),
              sha256=hashlib.sha256(archive.read_bytes()).hexdigest(), qt='6.8.3', app='0.1.0')
(output / 'linux-package.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
