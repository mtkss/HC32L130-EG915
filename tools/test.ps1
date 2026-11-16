<#
.SYNOPSIS
编译真实业务模块并在独立ARM指令模型执行，未链接真实board.c。
.DESCRIPTION
仅tests/test_firmware.c提供板级替身；模型输出须明确包含零失败检查结果。
使用独立16KiB测试栈和较大RAM，不能以模型通过证明目标2KiB栈/8KiB RAM或真实IRQ时序正确。
.PARAMETER KeilRoot
ARMCC5安装根目录；测试依赖由setup_tests.ps1安装到本工程tools/test_runtime。
.NOTES
产物与日志在build/tests；失败保留证据并终止，不以模拟成功替代实板验收。
#>
param([string]$KeilRoot='C:\Keil_v5')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'build\tests'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$bin=Join-Path $KeilRoot 'ARM\ARMCC\bin'
$includes=@('core','services','components','platform','app','config') | ForEach-Object { '-I'+(Join-Path $root $_) }
# 此清单有意不含platform/board.c；所有真实硬件访问由测试替身替换。
$sources=@('tests\test_firmware.c','core\utc_calendar.c','services\protocol_codec.c','services\record_cache.c','services\identity_store.c','services\at_client.c','services\http_service.c','services\report_service.c','components\upload_schedule.c','components\sampling_service.c','components\network_manager.c','components\location_manager.c','app\application.c')
$objects=@()
$log=Join-Path $build 'test.log'
Set-Content -LiteralPath $log -Value '' -Encoding utf8
foreach($source in $sources){
 $object=Join-Path $build (($source -replace '[\\/]','_') -replace '\.c$','.o')
 & (Join-Path $bin 'armcc.exe') --cpu Cortex-M0plus --c99 --gnu --diag_error=223 --library_type=microlib -O1 --split_sections @includes -c (Join-Path $root $source) -o $object 2>&1 | Add-Content -LiteralPath $log -Encoding utf8
 if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $log;throw "Test compile failed: $source"}
 $objects+=$object
}
# 测试专用启动/退出对象仅用于指令模型，不进入目标固件。
$startup=Join-Path $build 'startup.o'
& (Join-Path $bin 'armasm.exe') --cpu Cortex-M0plus --pd '__MICROLIB SETA 1' (Join-Path $root 'tests\startup_test.s') -o $startup 2>&1 | Add-Content -LiteralPath $log -Encoding utf8
if($LASTEXITCODE -ne 0){throw 'Test assemble failed'}
$exitObject=Join-Path $build 'test_exit.o'
& (Join-Path $bin 'armasm.exe') --cpu Cortex-M0plus (Join-Path $root 'tests\test_exit.s') -o $exitObject 2>&1 | Add-Content -LiteralPath $log -Encoding utf8
if($LASTEXITCODE -ne 0){throw 'Test exit assembly failed'}
$objects+=$exitObject
$elf=Join-Path $build 'test_firmware.axf'
& (Join-Path $bin 'armlink.exe') --cpu Cortex-M0plus --library_type=microlib --scatter (Join-Path $root 'tests\test.sct') -o $elf @objects $startup 2>&1 | Add-Content -LiteralPath $log -Encoding utf8
if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $log;throw 'Test link failed'}
$python=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
if(!(Test-Path -LiteralPath $python)){$python='python'}
# 只有模型真正执行并产生完整结果才通过；不能把测试ELF编译成功当成测试执行成功。
& $python (Join-Path $PSScriptRoot 'run_arm_tests.py') 2>&1 | Add-Content -LiteralPath $log -Encoding utf8
if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $log;throw 'ARM instruction tests failed'}
Get-Content -LiteralPath $log
