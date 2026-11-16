<#
.SYNOPSIS
将锁定测试/格式依赖安装到本工程，不修改系统Python环境。
.PARAMETER Python
可选Python解释器路径；未提供时优先本机Codex依赖解释器，再用PATH中的python。
.NOTES
需要访问包索引；安装失败立即报错，不静默替换锁定版本。
#>
param([string]$Python='')
$ErrorActionPreference='Stop'
if(!$Python){$Python=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe';if(!(Test-Path -LiteralPath $Python)){$Python='python'}}
& $Python -m pip install --disable-pip-version-check --target (Join-Path $PSScriptRoot 'test_runtime') -r (Join-Path $PSScriptRoot 'test_requirements.txt')
if($LASTEXITCODE -ne 0){throw 'Test dependency install failed'}
