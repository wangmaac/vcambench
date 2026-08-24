# Development convenience: exercise the real installer from a clean state.
#
# Removes the dev registration that points at C:\Users\Public, stops the frame
# server so it drops any cached image of the media source, then runs Setup
# silently. Without the stop, COM keeps serving the previously loaded DLL and
# the test would measure the old build.

$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'install-test.log'
Start-Transcript -Path $log -Force | Out-Null

try {
    Get-Process vcambench, vcamctl -ErrorAction SilentlyContinue | Stop-Process -Force

    foreach ($name in @('FrameServer', 'FrameServerMonitor')) {
        & sc.exe stop $name | Out-Null
    }
    for ($i = 0; $i -lt 20; $i++) {
        $running = @('FrameServer', 'FrameServerMonitor') |
                   ForEach-Object { Get-Service -Name $_ -ErrorAction SilentlyContinue } |
                   Where-Object { $_ -and $_.Status -eq 'Running' }
        if (-not $running) { break }
        Start-Sleep -Milliseconds 500
    }
    Write-Output '프레임 서버 중지 완료'

    $dev = 'C:\Users\Public\VCamBench\vcamsource.dll'
    if (Test-Path $dev) {
        Start-Process regsvr32.exe -ArgumentList '/u', '/s', "`"$dev`"" -Wait
        Remove-Item -Recurse -Force 'C:\Users\Public\VCamBench' -ErrorAction SilentlyContinue
        Write-Output '개발용 등록 제거됨'
    }

    $setup = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\VCamBench-0.1.0-setup.exe'
    if (-not (Test-Path $setup)) { throw "설치 파일 없음: $setup" }

    $p = Start-Process $setup -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
    Write-Output "Setup exit=$($p.ExitCode)"

    $key = 'Registry::HKEY_CLASSES_ROOT\CLSID\{351A1EA5-CE9E-4A6D-8806-8950D9AF4973}\InprocServer32'
    if (Test-Path $key) {
        Write-Output ("REGISTERED = " + (Get-ItemProperty -Path $key).'(default)')
    } else {
        Write-Output 'REGISTERED = (없음)'
    }

    $app = Join-Path $env:ProgramFiles 'VCamBench'
    if (Test-Path $app) {
        Write-Output "설치된 파일: $((Get-ChildItem $app -File | ForEach-Object Name) -join ', ')"
    }

    $acl = Get-Acl (Join-Path $env:ProgramData 'VCamBench') -ErrorAction SilentlyContinue
    if ($acl) {
        $svc = $acl.Access | Where-Object { $_.IdentityReference -match 'LOCAL SERVICE' }
        Write-Output ("로그 폴더 LOCAL SERVICE 권한: " + $(if ($svc) { $svc.FileSystemRights } else { '없음' }))
    }

    Write-Output 'RESULT=OK'
} catch {
    Write-Output 'RESULT=FAILED'
    Write-Output $_.Exception.Message
} finally {
    Stop-Transcript | Out-Null
}
