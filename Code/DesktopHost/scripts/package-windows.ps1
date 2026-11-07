param([Parameter(Mandatory)][string]$QtRoot,[string]$CMake='cmake',[Parameter(Mandatory)][string]$Build,[string]$Python='python',[string]$CrtRoot=$env:EM_CRT_ROOT)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$out=Join-Path $repo 'outputs\DesktopHost'
$stage=Join-Path $out 'EnergyMechanismHost-windows-x64'
$resolved=[IO.Path]::GetFullPath($stage)
if(-not $resolved.StartsWith([IO.Path]::GetFullPath($out)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid package path'}
if(Test-Path -LiteralPath $stage){
  if(-not(Test-Path -LiteralPath (Join-Path $stage '.em-package'))){throw 'Refusing to replace an unmarked directory'}
  Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Set-Content -LiteralPath (Join-Path $stage '.em-package') -Value 'Generated portable package' -Encoding utf8
& $CMake --install $Build --config Release --prefix $stage
if($LASTEXITCODE -ne 0){throw 'Install staging failed'}
& "$QtRoot\bin\windeployqt.exe" --release --no-compiler-runtime --no-translations --no-quick-import --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --no-plugins "$stage\EnergyMechanismHost.exe"
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
New-Item -ItemType Directory -Force -Path "$stage\plugins\platforms" | Out-Null
foreach($plugin in 'qwindows.dll','qoffscreen.dll'){Copy-Item -LiteralPath "$QtRoot\plugins\platforms\$plugin" -Destination "$stage\plugins\platforms"}
Set-Content -LiteralPath "$stage\qt.conf" -Value "[Paths]`nPlugins=plugins" -Encoding utf8
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt=$CrtRoot
if(-not $crt){$crt=Get-ChildItem -LiteralPath "$vs\VC\Redist\MSVC" -Directory | Where-Object {$_.Name -match '^14\.'} | Sort-Object Name -Descending | ForEach-Object {Join-Path $_.FullName 'x64\Microsoft.VC143.CRT'} | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1}
if(-not $crt){throw 'MSVC x64 redistributable DLLs missing'}
$compilerFile=Get-ChildItem -LiteralPath "$Build\CMakeFiles" -Filter CMakeCXXCompiler.cmake -Recurse | Select-Object -First 1
$compilerConfig=Get-Content -LiteralPath $compilerFile.FullName -Raw
$compilerVersion=[regex]::Match($compilerConfig,'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)').Groups[1].Value
$toolsetVersion=[regex]::Match($compilerConfig,'/MSVC/([0-9.]+)/').Groups[1].Value
if(-not $toolsetVersion){throw 'Cannot identify the MSVC toolset version for redistributable validation'}
$runtimeVersion=(Get-Item -LiteralPath "$crt\vcruntime140.dll").VersionInfo.FileVersion
# cl.exe and runtime receive different patch build numbers; compare the installed toolset release.
if([version]$runtimeVersion -lt [version]$toolsetVersion){throw "MSVC runtime $runtimeVersion is older than toolset $toolsetVersion. Supply current app-local redistributable DLLs with -CrtRoot (or EM_CRT_ROOT)."}
Get-ChildItem -LiteralPath $crt -Filter '*.dll' | Where-Object {$_.Name -match '^(msvcp140|vcruntime140|concrt140|vccorlib140).*\.dll$'} | Copy-Item -Destination $stage
@{app='0.1.0';qt='6.8.3';compiler=$compilerVersion;toolset=$toolsetVersion;msvc_runtime=$runtimeVersion;source_commit=$env:GITHUB_SHA} | ConvertTo-Json | Set-Content -LiteralPath "$stage\BUILDINFO.json" -Encoding utf8
& $Python (Join-Path $PSScriptRoot 'collect_licenses.py') $QtRoot "$stage\licenses"
if($LASTEXITCODE -ne 0){throw 'License collection failed'}
# Verify the package without Qt/Anaconda/compiler directories on PATH.
$previousPath=$env:PATH;$previousPlatform=$env:QT_QPA_PLATFORM;$previousPlugins=$env:QT_PLUGIN_PATH
try {
  $env:PATH="$env:SystemRoot\System32;$env:SystemRoot";$env:QT_QPA_PLATFORM='offscreen';$env:QT_PLUGIN_PATH=''
  $shots=Join-Path $out 'portable-smoke-windows'
  $process=Start-Process -FilePath "$stage\EnergyMechanismHost.exe" -ArgumentList @('--smoke-test',('"'+$shots+'"')) -WindowStyle Hidden -PassThru -Wait
  if($process.ExitCode -ne 0){throw "Portable startup failed: $($process.ExitCode)"}
} finally {$env:PATH=$previousPath;$env:QT_QPA_PLATFORM=$previousPlatform;$env:QT_PLUGIN_PATH=$previousPlugins}
$manifest=Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
  $relative=$_.FullName.Substring($stage.Length+1).Replace('\','/')
  (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()+'  '+$relative
}
Set-Content -LiteralPath "$stage\SHA256SUMS" -Value $manifest -Encoding utf8
$archive=Join-Path $out 'EnergyMechanismHost-0.1.0-windows-x64.zip'
Compress-Archive -LiteralPath $stage -DestinationPath $archive -Force -CompressionLevel Optimal
$bytes=(Get-ChildItem -LiteralPath $stage -File -Recurse | Measure-Object Length -Sum).Sum
$report=[ordered]@{archive=$archive;archive_bytes=(Get-Item -LiteralPath $archive).Length;extracted_bytes=$bytes;sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash;qt='6.8.3';app='0.1.0';msvc_runtime=$runtimeVersion;portable_smoke='passed';clean_os_hardware_test='separate acceptance required'}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'windows-package.json') -Encoding utf8
$report | ConvertTo-Json
