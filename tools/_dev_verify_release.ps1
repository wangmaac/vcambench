# Development convenience: install the release build from a clean state and
# check that a camera actually appears and delivers frames.
#
# The frame server is stopped first because COM caches an in-proc server per
# CLSID per host process; without that, this would measure whichever build the
# service happened to be holding.

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$log = Join-Path $PSScriptRoot 'verify.log'
Start-Transcript -Path $log -Force | Out-Null

try {
    Get-Process vcambench, vcamctl -ErrorAction SilentlyContinue | Stop-Process -Force

    # Remove any previous install so this measures the new installer, not a leftover.
    $old = Join-Path $env:ProgramFiles 'VCamBench\unins000.exe'
    if (Test-Path $old) {
        Start-Process $old -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait
        Start-Sleep -Seconds 2
        Write-Output '이전 설치 제거됨'
    }

    foreach ($name in @('FrameServer', 'FrameServerMonitor')) { & sc.exe stop $name | Out-Null }
    Start-Sleep -Seconds 3

    $setup = Join-Path $root 'dist\VCamBench-0.1.1-setup.exe'
    if (-not (Test-Path $setup)) { throw "설치 파일 없음: $setup" }

    $p = Start-Process $setup -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
    Write-Output "Setup exit=$($p.ExitCode)"

    $app = Join-Path $env:ProgramFiles 'VCamBench'
    Write-Output ("설치된 파일: " + ((Get-ChildItem $app -File | ForEach-Object Name) -join ', '))

    # Add-Remove Programs should show the version we just built.
    $entry = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
                              'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' `
             -ErrorAction SilentlyContinue |
             Where-Object { $_.DisplayName -like 'VCamBench*' } | Select-Object -First 1
    if ($entry) { Write-Output "프로그램 및 기능: $($entry.DisplayName) / $($entry.DisplayVersion)" }

    Write-Output 'RESULT=OK'
} catch {
    Write-Output 'RESULT=FAILED'
    Write-Output $_.Exception.Message
} finally {
    Stop-Transcript | Out-Null
}
