# Development convenience, not part of the product.
#
# COM caches an in-proc server per CLSID inside each host process. The Frame
# Server loaded the media source from its first registration path and kept that
# image, so repointing the CLSID and overwriting the new file changed nothing on
# the streaming path - the frames still came from the original build.
#
# Stop the frame server services and remove the stale copy so the next
# activation has no choice but to load the current one.

$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'drop.log'
Start-Transcript -Path $log -Force | Out-Null

foreach ($name in @('FrameServer', 'FrameServerMonitor')) {
    $svc = Get-Service -Name $name -ErrorAction SilentlyContinue
    if (-not $svc) { Write-Output "$name : 서비스 없음"; continue }
    Write-Output "$name : 시작 상태 $($svc.Status)"
    & sc.exe stop $name | Out-Null
}

# sc.exe returns immediately; wait for the services to actually leave Running.
for ($i = 0; $i -lt 20; $i++) {
    $running = @('FrameServer', 'FrameServerMonitor') | ForEach-Object {
        Get-Service -Name $_ -ErrorAction SilentlyContinue
    } | Where-Object { $_ -and $_.Status -eq 'Running' }
    if (-not $running) { break }
    Start-Sleep -Milliseconds 500
}

foreach ($name in @('FrameServer', 'FrameServerMonitor')) {
    $svc = Get-Service -Name $name -ErrorAction SilentlyContinue
    if ($svc) { Write-Output "$name : 이제 $($svc.Status)" }
}

$stale = Join-Path $env:ProgramFiles 'VCamBench'
if (Test-Path $stale) {
    try {
        Remove-Item -Recurse -Force $stale -ErrorAction Stop
        Write-Output "옛 DLL 폴더 삭제됨: $stale"
    } catch {
        # A failure here is itself the answer: the file is still mapped.
        Write-Output "삭제 실패 (아직 로드 중일 수 있음): $($_.Exception.Message)"
    }
} else {
    Write-Output "옛 DLL 폴더 없음: $stale"
}

Write-Output 'RESULT=OK'
Stop-Transcript | Out-Null
