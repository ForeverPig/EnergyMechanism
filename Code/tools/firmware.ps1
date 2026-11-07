param(
  [Parameter(Mandatory=$true)][ValidateSet('Leaf','Center')][string]$Project,
  [ValidateSet('Build','Configure','Flash')][string]$Action='Build',
  [ValidateSet('Debug','Release')][string]$Configuration='Debug',
  [string]$Serial=''
)
$ErrorActionPreference='Stop'
$projectFolder=if($Project -eq 'Leaf'){'Fan Blade'}else{'Center Controller'}
$targetName=if($Project -eq 'Leaf'){'Fan_Blade'}else{'Center_Controller'}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\$projectFolder"))
$bundleRoot=if($env:CUBE_BUNDLE_PATH){$env:CUBE_BUNDLE_PATH}else{Join-Path $env:LOCALAPPDATA 'stm32cube\bundles'}
function Find-BundleTool([string]$Package,[string]$Executable) {
  $found=Get-ChildItem -LiteralPath (Join-Path $bundleRoot $Package) -Directory |
    Sort-Object Name -Descending | ForEach-Object {Join-Path $_.FullName "bin\$Executable"} |
    Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
  if(-not $found){throw "Missing $Package/$Executable in $bundleRoot. Install the STM32Cube VS Code bundle or set CUBE_BUNDLE_PATH."}
  return $found
}
if($Action -eq 'Flash' -and $Serial -notmatch '^[A-Za-z0-9]{8,64}$') {
  throw 'Flash requires an explicit ST-LINK serial number (-Serial). No device was accessed.'
}
$cmakeExe=Find-BundleTool 'cmake' 'cmake.exe'
$ninjaExe=Find-BundleTool 'ninja' 'ninja.exe'
$env:CUBE_BUNDLE_PATH=$bundleRoot
Push-Location -LiteralPath $projectRoot
try {
  & $cmakeExe --preset $Configuration "-DCMAKE_MAKE_PROGRAM=$ninjaExe"
  if($LASTEXITCODE -ne 0){throw "CMake configure failed: $LASTEXITCODE"}
  if($Action -eq 'Configure'){return}
  & $cmakeExe --build --preset $Configuration --parallel 4
  if($LASTEXITCODE -ne 0){throw "Build failed: $LASTEXITCODE"}
  if($Action -eq 'Flash'){
    $elf=Join-Path $projectRoot "build\$Configuration\$targetName.elf"
    if(-not (Test-Path -LiteralPath $elf)){throw "ELF missing: $elf"}
    $programmerExe=Find-BundleTool 'programmer' 'STM32_Programmer_CLI.exe'
    # Explicit Flash action only. Build/Configure never invoke a programmer or probe.
    & $programmerExe -c port=SWD "sn=$Serial" mode=Normal -w $elf -v -rst
    if($LASTEXITCODE -ne 0){throw "Flash/verify failed: $LASTEXITCODE"}
  }
} finally {Pop-Location}
