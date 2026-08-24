# Development convenience, not part of the product.
#
# Repoints the media source CLSID at C:\Users\Public\VCamBench so the DLL can be
# rebuilt and overwritten without an elevation prompt for every iteration.
# The Frame Server runs as LOCAL SERVICE and can read Public, which is the only
# property that matters here.
#
# The installer will register out of Program Files. This is only for the loop.

$ErrorActionPreference = 'Stop'
$log = Join-Path $PSScriptRoot 'register.log'
Start-Transcript -Path $log -Force | Out-Null

try {
    $old = Join-Path $env:ProgramFiles 'VCamBench\vcamsource.dll'
    if (Test-Path $old) {
        Start-Process regsvr32.exe -ArgumentList '/u', '/s', "`"$old`"" -Wait
    }

    $dev = 'C:\Users\Public\VCamBench\vcamsource.dll'
    if (-not (Test-Path $dev)) { throw "DLL 없음: $dev" }

    $p = Start-Process regsvr32.exe -ArgumentList '/s', "`"$dev`"" -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "regsvr32 실패 (exit=$($p.ExitCode))" }

    $key = 'Registry::HKEY_CLASSES_ROOT\CLSID\{351A1EA5-CE9E-4A6D-8806-8950D9AF4973}\InprocServer32'
    Write-Output ("REGISTERED = " + (Get-ItemProperty -Path $key).'(default)')
    Write-Output 'RESULT=OK'
} catch {
    Write-Output 'RESULT=FAILED'
    Write-Output $_.Exception.Message
} finally {
    Stop-Transcript | Out-Null
}
