# Development convenience, not part of the product.
#
# The Windows Camera Frame Server keeps the media source DLL loaded across
# camera sessions, so a rebuilt DLL is not picked up on the streaming path until
# the service restarts. Symptom: the camera list and the creation-time log show
# new behaviour while the actual frames still come from the previous build.
#
# End users never hit this - they install once. Developers hit it every rebuild.

$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'restart.log'
Start-Transcript -Path $log -Force | Out-Null

foreach ($name in @('FrameServer', 'FrameServerMonitor')) {
    $svc = Get-Service -Name $name -ErrorAction SilentlyContinue
    if (-not $svc) {
        Write-Output "$name : 없음"
        continue
    }
    Write-Output "$name : $($svc.Status) -> 중지 시도"
    try {
        Stop-Service -Name $name -Force -ErrorAction Stop
        Write-Output "$name : 중지됨"
    } catch {
        Write-Output "$name : 중지 실패 - $($_.Exception.Message)"
    }
}

Start-Sleep -Seconds 2
foreach ($name in @('FrameServer', 'FrameServerMonitor')) {
    $svc = Get-Service -Name $name -ErrorAction SilentlyContinue
    if ($svc) { Write-Output "$name : 현재 $($svc.Status)" }
}

Write-Output 'RESULT=OK'
Stop-Transcript | Out-Null
