param(
  [ValidateSet('Configure','Build','Test','Package')][string]$Action='Build',
  [string]$QtRoot='', [string]$CMake='cmake'
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repo=[IO.Path]::GetFullPath((Join-Path $project '..\..'))
$build=Join-Path $project 'build\windows'
if(-not $QtRoot){$QtRoot=Join-Path $repo 'tmp\desktop-build\Qt\6.8.3\msvc2022_64'}
if(-not (Test-Path -LiteralPath "$QtRoot\bin\Qt6Core.dll")){throw 'Pass -QtRoot for Qt 6.8.3 msvc2022_64.'}
if($CMake -eq 'cmake' -and -not (Get-Command cmake -ErrorAction SilentlyContinue)){
  $CMake=Join-Path $repo 'tmp\desktop-build\venv\Lib\site-packages\cmake\data\bin\cmake.exe'
}
$CMake=(Get-Command $CMake -ErrorAction Stop).Source
& $CMake -S $project -B $build -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" -DBUILD_TESTING=ON
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
if($Action -eq 'Configure'){return}
& $CMake --build $build --config Release --parallel
if($LASTEXITCODE -ne 0){throw 'Build failed'}
$env:PATH="$QtRoot\bin;"+$env:PATH
if($Action -in @('Test','Package')){
  $ctest=Join-Path (Split-Path $CMake) 'ctest.exe'
  & $ctest --test-dir $build -C Release --output-on-failure
  if($LASTEXITCODE -ne 0){throw 'Tests failed'}
}
if($Action -eq 'Package'){
  & (Join-Path $PSScriptRoot 'package-windows.ps1') -QtRoot $QtRoot -CMake $CMake -Build $build
}
