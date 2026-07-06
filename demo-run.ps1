$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$PackRoot = Split-Path -Parent $ProjectRoot
$Tools = Join-Path $PackRoot "tools"
$Qemu = Join-Path $Tools "xpack-qemu-arm-7.2.5-1\bin\qemu-system-gnuarmeclipse.exe"
$Elf = Join-Path $ProjectRoot "build\Lab10-tickless-watchdog.elf"

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " Lab10 Demo: Tickless Idle + Software Watchdog" -ForegroundColor Cyan
Write-Host " Team: 2023211435 2023211406 2023211408 2023211344" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path -LiteralPath $Tools)) {
    throw "tools directory not found: $Tools"
}

if (-not (Test-Path -LiteralPath $Qemu)) {
    throw "QEMU not found: $Qemu"
}

$env:PATH = "$Tools\make;$Tools\arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi\bin;$Tools\xpack-qemu-arm-7.2.5-1\bin;$env:PATH"

Set-Location -LiteralPath $ProjectRoot

Write-Host "[1/3] Clean build directory..." -ForegroundColor Yellow
make clean
if ($LASTEXITCODE -ne 0) {
    throw "make clean failed"
}

Write-Host ""
Write-Host "[2/3] Build RTOS project..." -ForegroundColor Yellow
make all
if ($LASTEXITCODE -ne 0) {
    throw "make all failed"
}

if (-not (Test-Path -LiteralPath $Elf)) {
    throw "ELF was not generated: $Elf"
}

Write-Host ""
Write-Host "[3/3] Ready to run QEMU." -ForegroundColor Green
Write-Host ""
Write-Host "During the demo, point out these lines:" -ForegroundColor Cyan
Write-Host "  === start tickless idle test ==="
Write-Host "  tickless stats: enter ... suppressed ... max_window ..."
Write-Host "  === start software watchdog test ==="
Write-Host "  watchdog feed path expired=0"
Write-Host "  watchdog timeout path expired=2"
Write-Host "  === all test done ==="
Write-Host ""
Write-Host "To exit QEMU: press Ctrl+A, then X" -ForegroundColor Cyan
Write-Host ""

Read-Host "Press Enter to start QEMU for the live demo"

& $Qemu -M STM32F4-Discovery -kernel $Elf -nographic -serial null -serial mon:stdio
