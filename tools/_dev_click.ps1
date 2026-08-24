# Development convenience: press a control in a window by its control id, so GUI
# behaviour can be exercised the same way the rest of this project is verified -
# by running it and looking at the result.
param(
    [Parameter(Mandatory = $true)][string]$ProcessName,
    [Parameter(Mandatory = $true)][int]$ControlId,
    [int]$Times = 1
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinPoke {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  public const uint BM_CLICK = 0x00F5;
}
"@

$proc = Get-Process -Name $ProcessName -ErrorAction Stop |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { throw "창을 가진 $ProcessName 프로세스가 없습니다." }

$control = [WinPoke]::GetDlgItem($proc.MainWindowHandle, $ControlId)
if ($control -eq [IntPtr]::Zero) { throw "컨트롤 $ControlId 을 찾을 수 없습니다." }

for ($i = 0; $i -lt $Times; $i++) {
    [WinPoke]::SendMessageW($control, [WinPoke]::BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 400
}
Write-Output "clicked $ControlId x$Times"
