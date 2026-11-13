# Wrapper de conveniência na raiz do repositório.
# A fonte única de verdade do pipeline de build e deploy reside em scripts/build_module.ps1.
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "scripts\build_module.ps1") @args
