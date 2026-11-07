$ErrorActionPreference='Stop'
$codeRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=Join-Path $codeRoot 'tests\build'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location -LiteralPath $codeRoot
try {
  $cases=@(
    @{Name='protocol';Args=@('-ICommon/Inc','Common/Src/debug_protocol.c','tests/test_protocol.c')},
    @{Name='hit_detector';Args=@('-IFan Blade/App/Inc','Fan Blade/App/Src/hit_detector.c','tests/test_hit_detector.c')},
    @{Name='sampler';Args=@('-Itests/stubs','-IFan Blade/App/Inc','Fan Blade/App/Src/pressure_sampler.c','tests/test_sampler.c')},
    @{Name='mcp2515';Args=@('-Itests/stubs','-ICenter Controller/App/Inc','-ICommon/Inc','Center Controller/App/Src/mcp2515.c','Common/Src/debug_protocol.c','tests/test_mcp2515.c')}
  )
  foreach($case in $cases){
    $exe=Join-Path $out ($case.Name+'.exe')
    $compileArgs=@('-std=c11','-Wall','-Wextra','-Werror')+$case.Args+@('-o',$exe)
    & gcc @compileArgs
    if($LASTEXITCODE -ne 0){throw "Compile failed: $($case.Name)"}
    & $exe
    if($LASTEXITCODE -ne 0){throw "Test failed: $($case.Name)"}
  }
  & python -m unittest discover -s tests -p 'test_*.py' -v
  if($LASTEXITCODE -ne 0){throw 'Python tests failed'}
} finally {Pop-Location}
