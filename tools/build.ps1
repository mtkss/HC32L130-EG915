<#
.SYNOPSIS
使用本机ARMCC5构建独立目标，不烧录设备。
.DESCRIPTION
编译app/core/components/services/platform及选定原厂驱动，使用身份区保留链接脚本，输出AXF/HEX/map/调用图。
编译失败立即停止，日志在build/build.log；只写工程内build目录，不清理源工程或使用历史HEX冒充新产物。
.PARAMETER KeilRoot
Keil安装根目录，默认C:\Keil_v5；实际版本记录到交付文档，脚本不自动下载/替换编译器。
.NOTES
运行上下文为前台PowerShell。源路径与对象名一一对应；原厂枚举诊断66被显式抑制，不代表所有厂商代码无告警。
#>
param([string]$KeilRoot='C:\Keil_v5')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$buildRoot=Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$compiler=Join-Path $KeilRoot 'ARM\ARMCC\bin\armcc.exe'
$assembler=Join-Path $KeilRoot 'ARM\ARMCC\bin\armasm.exe'
$linker=Join-Path $KeilRoot 'ARM\ARMCC\bin\armlink.exe'
$converter=Join-Path $KeilRoot 'ARM\ARMCC\bin\fromelf.exe'
# 自有模块与vendor头目录显式列举，避免从旧工程输出目录意外取得头文件。
$includePaths=@('core','app','components','services','platform','config','vendor\common','vendor\driver\inc','vendor\CMSIS\Include')
$includes=$includePaths | ForEach-Object { '-I'+(Join-Path $projectRoot $_) }
$ownSources=Get-ChildItem -LiteralPath (Join-Path $projectRoot 'core'),(Join-Path $projectRoot 'app'),(Join-Path $projectRoot 'components'),(Join-Path $projectRoot 'services'),(Join-Path $projectRoot 'platform') -Filter '*.c'
$driverNames=@('adc','bgr','bt','ddl','flash','gpio','i2c','lpm','rtc','sysctrl','uart')
$sourcePaths=@($ownSources.FullName)+@($driverNames | ForEach-Object { Join-Path $projectRoot ('vendor\driver\src\'+$_+'.c') })+@((Join-Path $projectRoot 'vendor\common\system_hc32l13x.c'))
$objects=@()
$log=Join-Path $buildRoot 'build.log'
Set-Content -LiteralPath $log -Value '' -Encoding utf8
# 每个翻译单元独立写日志；首个错误即退出，不继续链接上次遗留对象。
foreach($sourcePath in $sourcePaths){
  $relative=$sourcePath.Substring($projectRoot.Length+1)
  $object=Join-Path $buildRoot (($relative -replace '[\\/]','_') -replace '\.c$','.o')
  $arguments=@('--cpu','Cortex-M0plus','--c99','--gnu','--library_type=microlib','-O1','--split_sections','--diag_style=ide','--diag_suppress=66')+$includes+@('-c',$sourcePath,'-o',$object)
  $result=& $compiler @arguments 2>&1
  $result | Add-Content -LiteralPath $log -Encoding utf8
  if($LASTEXITCODE -ne 0){$result | Write-Output; throw "Compile failed: $relative"}
  $objects+=$object
}
# 启动汇编显式定义MicroLIB，使栈符号导出与无堆运行库一致。
$startupObject=Join-Path $buildRoot 'startup.o'
& $assembler --cpu Cortex-M0plus --pd "__MICROLIB SETA 1" (Join-Path $projectRoot 'platform\startup_hc32l130.s') -o $startupObject 2>&1 | Add-Content -LiteralPath $log -Encoding utf8
if($LASTEXITCODE -ne 0){throw 'Assembly failed'}
$objects+=$startupObject
$elf=Join-Path $buildRoot 'HC32L130_Refactored.axf'
# scatter把可执行区限制到0xEE00之前；链接器负责RAM/Flash越界拒绝。
& $linker --cpu Cortex-M0plus --library_type=microlib --scatter (Join-Path $projectRoot 'MDK\HC32L130_Refactored.sct') --map --xref --info sizes,totals,stack --list (Join-Path $buildRoot 'HC32L130_Refactored.map') --callgraph -o $elf @objects 2>&1 | Add-Content -LiteralPath $log -Encoding utf8
if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $log | Select-Object -Last 30;throw 'Link failed'}
& $converter --i32combined --output (Join-Path $buildRoot 'HC32L130_Refactored.hex') $elf 2>&1 | Add-Content -LiteralPath $log -Encoding utf8
if($LASTEXITCODE -ne 0){throw 'HEX conversion failed'}
Get-Content -LiteralPath $log
