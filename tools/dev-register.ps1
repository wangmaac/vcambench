<#
.SYNOPSIS
  개발용 등록. 빌드한 vcamsource.dll 을 Program Files 에 복사하고 COM 등록한다.

.DESCRIPTION
  정식 설치 프로그램이 나오기 전까지 쓰는 임시 스크립트다.

  Program Files 에 두는 이유는 취향이 아니다. 이 DLL 을 로드하는 Windows Camera
  Frame Server 는 LOCAL SERVICE 계정으로 도는데, 사용자 폴더에 두면 그 계정이
  읽지 못해 로드가 조용히 실패한다.

.PARAMETER Unregister
  등록을 해제하고 설치 폴더를 지운다.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release'),
    [string]$InstallDir = (Join-Path $env:ProgramFiles 'VCamBench'),
    [switch]$Unregister
)

$ErrorActionPreference = 'Stop'
$clsidKey = 'Registry::HKEY_CLASSES_ROOT\CLSID\{351A1EA5-CE9E-4A6D-8806-8950D9AF4973}'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '관리자 권한이 필요합니다.'
}

# A running host holds the DLL open, which turns the copy into a confusing
# sharing violation.
Get-Process vcamctl, vcambench -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

if ($Unregister) {
    $dll = Join-Path $InstallDir 'vcamsource.dll'
    if (Test-Path $dll) {
        Start-Process regsvr32.exe -ArgumentList '/u', '/s', "`"$dll`"" -Wait
    }
    if (Test-Path $clsidKey) { Remove-Item $clsidKey -Recurse -Force }
    if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force }
    Write-Host '등록 해제 완료' -ForegroundColor Green
    return
}

$dllSource = Join-Path $BuildDir 'vcamsource.dll'
if (-not (Test-Path $dllSource)) { throw "빌드 산출물이 없습니다: $dllSource" }

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item $dllSource $InstallDir -Force

$dll = Join-Path $InstallDir 'vcamsource.dll'
$proc = Start-Process regsvr32.exe -ArgumentList '/s', "`"$dll`"" -Wait -PassThru
if ($proc.ExitCode -ne 0) { throw "regsvr32 실패 (exit=$($proc.ExitCode))" }

if (-not (Test-Path "$clsidKey\InprocServer32")) {
    throw '등록이 확인되지 않습니다.'
}
Write-Host "등록 완료: $((Get-ItemProperty "$clsidKey\InprocServer32").'(default)')" -ForegroundColor Green
