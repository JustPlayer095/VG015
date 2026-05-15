param(
    [Parameter(Mandatory = $true)][string]$WorkingDirectory,
    [Parameter(Mandatory = $true)][string]$InterfaceCfg,
    [Parameter(Mandatory = $true)][string]$TargetCfg
)

$ErrorActionPreference = "Stop"

$kitRoot = [System.IO.Path]::Combine($env:USERPROFILE, ".niiet_aspect", "riscv_kit_windows")
$exe = $env:OPENOCD_EXE
if ([string]::IsNullOrWhiteSpace($exe)) {
    $exe = [System.IO.Path]::Combine($kitRoot, "bin", "openocd.exe")
}
$scriptsRoot = $env:OPENOCD_SCRIPTS_ROOT
if ([string]::IsNullOrWhiteSpace($scriptsRoot)) {
    $scriptsRoot = $kitRoot
}

if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error @"
openocd.exe не найден: $exe
Задайте переменную пользователя OPENOCD_EXE или выполните задачу «setup-toolchain-paths».
"@
    exit 1
}
if (-not (Test-Path -LiteralPath $scriptsRoot)) {
    Write-Error @"
Каталог скриптов OpenOCD не найден: $scriptsRoot
Задайте переменную пользователя OPENOCD_SCRIPTS_ROOT (корень набора NIIET, где лежат interface/ и target/).
"@
    exit 1
}

Push-Location -LiteralPath $WorkingDirectory
try {
    & $exe `
        "-s", $scriptsRoot `
        "-s", ([System.IO.Path]::Combine($scriptsRoot, "interface")) `
        "-s", ([System.IO.Path]::Combine($scriptsRoot, "target")) `
        "-f", $InterfaceCfg `
        "-f", $TargetCfg `
        "-c", "init" `
        "-c", "halt" `
        "-d2"
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
