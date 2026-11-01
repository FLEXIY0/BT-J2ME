# CRT Overlay installer for Windows 10 (2004+) / 11.
#   irm https://raw.githubusercontent.com/FLEXIY0/BT-J2ME/refs/heads/claude/amazing-fermi-l6s51a/crt-overlay/install.ps1 | iex
# To remove: $env:CRT_UNINSTALL=1; irm .../install.ps1 | iex
$ErrorActionPreference = 'Stop'
$base = if ($env:CRT_OVERLAY_BASE) { $env:CRT_OVERLAY_BASE } else { 'https://raw.githubusercontent.com/FLEXIY0/BT-J2ME/refs/heads/claude/amazing-fermi-l6s51a/crt-overlay' }
$dir = Join-Path $env:LOCALAPPDATA 'crt-overlay'
$exe = Join-Path $dir 'crt-overlay.exe'
$startup = Join-Path ([Environment]::GetFolderPath('Startup')) 'CRT Overlay.lnk'
$menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'CRT Overlay.lnk'

Get-Process crt-overlay -ErrorAction SilentlyContinue | Stop-Process -Force
if ($env:CRT_UNINSTALL) {
    Remove-Item $exe, $startup, $menu -ErrorAction SilentlyContinue
    Write-Host 'CRT Overlay removed.'
    return
}

New-Item -ItemType Directory -Force $dir | Out-Null
$zip = Join-Path $env:TEMP 'crt-overlay.zip'
$url = "$base/dist/crt-overlay-windows-x64.zip"
Write-Host "Downloading $url"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -UseBasicParsing $url -OutFile $zip
Expand-Archive -Force $zip $dir
Remove-Item $zip
Unblock-File $exe

$shell = New-Object -ComObject WScript.Shell
foreach ($link in @($menu, $startup)) {
    if ($link -eq $startup -and $env:CRT_NO_AUTOSTART) { continue }
    $s = $shell.CreateShortcut($link); $s.TargetPath = $exe; $s.WorkingDirectory = $dir; $s.Save()
}
Start-Process $exe
Write-Host "Installed to $exe and started. Use the tray icon to change settings or quit."
