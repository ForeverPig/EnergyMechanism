param(
  [ValidateSet('Setup','Build','Flash','Test')][string]$Action='Build',
  [ValidateSet('Gateway','UartTest')][string]$Firmware='Gateway',
  [string]$Port='',
  [string]$Output='',
  [string]$IdfPath='D:\Data\Code\esp\ESP-IDF\v5.4\esp-idf',
  [string]$ToolsPath='D:\Data\Code\esp\.espressif'
)
$ErrorActionPreference='Stop'
$env:PYTHONUTF8='1'
$env:PYTHONIOENCODING='utf-8'
$env:IDF_CCACHE_ENABLE='0'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project=Join-Path $repo 'Code\ESP32'
$build=Join-Path $project $(if($Firmware -eq 'Gateway'){'build-gateway'}else{'build-uart-test'})
$python=Join-Path $ToolsPath 'python_env\idf5.4_py3.11_env\Scripts\python.exe'
if(-not (Test-Path -LiteralPath $python) -or -not (Test-Path -LiteralPath "$IdfPath\tools\idf.py")) {
  throw 'ESP-IDF 5.4 is missing. Configure the VS Code ESP-IDF extension or pass -IdfPath and -ToolsPath.'
}
if($Action -in @('Flash','Test') -and $Port -notmatch '^COM\d+$') {
  throw 'Flash/Test requires an explicit -Port COMx.'
}
if($Action -eq 'Test') {
  if(-not $Output){$Output=Join-Path $repo ('tmp\esp32-uart\run-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
  & $python (Join-Path $PSScriptRoot 'esp32_uart_test.py') --port $Port --output $Output
} else {
  $env:IDF_TOOLS_PATH=$ToolsPath
  $env:Path=(Split-Path $python)+';'+$env:Path
  $env:IDF_TARGET='esp32c3'
  . (Join-Path $IdfPath 'export.ps1')
  if($Action -eq 'Setup') {
    & $python (Join-Path $IdfPath 'tools\idf.py') --version
  } else {
    $mode=if($Firmware -eq 'UartTest'){'ON'}else{'OFF'}
    $argsList=@('-C',$project,'-B',$build,'-DCCACHE_ENABLE=0',"-DEM_UART_TEST=$mode","-DSDKCONFIG=$build/sdkconfig")
    if($Action -eq 'Flash'){$argsList+=@('-p',$Port,'-b','460800','flash')}
    else {$argsList+='build'}
    & $python (Join-Path $IdfPath 'tools\idf.py') @argsList
    if($LASTEXITCODE -eq 0) {
      $artifacts=Join-Path $build 'artifacts'
      New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
      $base=if($Firmware -eq 'Gateway'){'energy_esp32_gateway'}else{'energy_esp32_uart_test'}
      foreach($name in @("$base.bin","$base.elf","$base.map")) {
        Copy-Item -LiteralPath (Join-Path $build $name) -Destination $artifacts -Force
      }
    }
  }
}
if($LASTEXITCODE -ne 0){throw "ESP32 $Action failed"}
