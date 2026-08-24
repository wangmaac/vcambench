# Development convenience: run the real uninstaller and check it left nothing.
#
# The frame server is stopped first for the same reason as the install test: a
# cached image of the media source would keep the DLL file locked and make the
# result meaningless.

$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'uninstall-test.log'
Start-Transcript -Path $log -Force | Out-Null

try {
    Get-Process vcambench, vcamctl -ErrorAction SilentlyContinue | Stop-Process -Force
    foreach ($name in @('FrameServer', 'FrameServerMonitor')) { & sc.exe stop $name | Out-Null }
    Start-Sleep -Seconds 3

    $uninst = Join-Path $env:ProgramFiles 'VCamBench\unins000.exe'
    if (-not (Test-Path $uninst)) { throw "제거 프로그램 없음: $uninst" }

    $p = Start-Process $uninst -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
    Write-Output "Uninstall exit=$($p.ExitCode)"
    Start-Sleep -Seconds 2

    $key = 'Registry::HKEY_CLASSES_ROOT\CLSID\{351A1EA5-CE9E-4A6D-8806-8950D9AF4973}'
    Write-Output ("CLSID 남음: " + (Test-Path $key))
    Write-Output ("설치 폴더 남음: " + (Test-Path (Join-Path $env:ProgramFiles 'VCamBench')))
    Write-Output ("로그 폴더 남음: " + (Test-Path (Join-Path $env:ProgramData 'VCamBench')))

    Write-Output 'RESULT=OK'
} catch {
    Write-Output 'RESULT=FAILED'
    Write-Output $_.Exception.Message
} finally {
    Stop-Transcript | Out-Null
}
