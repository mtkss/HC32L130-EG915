<#
.SYNOPSIS
按锁定clang-format检查/格式化自有C/H，排除vendor和baseline。
.PARAMETER Check
指定后只核验并以非零状态报告格式差异；不指定才写回文件。
.NOTES
依赖只从本工程test_runtime读取；格式检查不是注释语义合规证明。
#>
param([switch]$Check)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$formatter=Join-Path $PSScriptRoot 'test_runtime\clang_format\data\bin\clang-format.exe'
if(!(Test-Path -LiteralPath $formatter)){throw '请先运行tools/setup_tests.ps1安装锁定的格式化工具。'}
$directories=@('core','app','components','services','platform','config','tests') | ForEach-Object {Join-Path $root $_}
foreach($source in (Get-ChildItem -LiteralPath $directories -File | Where-Object {$_.Extension -in '.c','.h'})){
 if($Check){& $formatter --dry-run --Werror $source.FullName}else{& $formatter -i $source.FullName}
 if($LASTEXITCODE -ne 0){throw ('格式核验失败：'+$source.Name)}
}
