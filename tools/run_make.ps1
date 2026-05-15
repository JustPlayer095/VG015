param(
    [Parameter(Mandatory = $true)][string]$WorkingDirectory,
    [string]$TargetName = ""
)

$ErrorActionPreference = "Stop"

$make = $env:MAKE_EXE
if ([string]::IsNullOrWhiteSpace($make)) {
    $make = [System.IO.Path]::Combine(
        $env:USERPROFILE,
        ".niiet_aspect", "riscv_kit_windows", "bin", "make.exe"
    )
}
if (-not (Test-Path -LiteralPath $make)) {
    Write-Error @"
make.exe не найден.
Задайте переменную пользователя MAKE_EXE (задача VS Code «setup-toolchain-paths») или установите тулчейн по пути:
$make
"@
    exit 1
}

$compilerPath = $env:COMPILER_PATH
if ([string]::IsNullOrWhiteSpace($compilerPath)) {
    $compilerPath = [System.IO.Path]::Combine(
        $env:USERPROFILE,
        ".niiet_aspect", "riscv_gcc_windows", "bin"
    )
    $env:COMPILER_PATH = $compilerPath
}
$gcc = [System.IO.Path]::Combine($compilerPath, "riscv64-unknown-elf-gcc.exe")
if (-not (Test-Path -LiteralPath $gcc)) {
    Write-Error @"
riscv64-unknown-elf-gcc.exe не найден в COMPILER_PATH:
$compilerPath
Задайте переменную пользователя COMPILER_PATH (задача VS Code «setup-toolchain-paths») или установите GCC из комплекта NIIET.
"@
    exit 1
}

Push-Location -LiteralPath $WorkingDirectory
try {
    $makeArgs = @(
        "-j8",
        "all",
        "COMPILER_PATH=$($env:COMPILER_PATH)",
        "PREFIX=riscv64-unknown-elf-",
        "MARCH=rv32imfc_zicsr",
        "MABI=ilp32f",
        "OPTIMISATION=-Os"
    )
    if (-not [string]::IsNullOrWhiteSpace($TargetName)) {
        $makeArgs += "TARGET_NAME=$TargetName"
    }
    & $make @makeArgs
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
