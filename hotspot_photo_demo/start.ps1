param([switch] $ShowViewKey)

$ErrorActionPreference = 'Stop'
$port = 8768
$ip = Get-NetIPAddress -InterfaceAlias 'WLAN' -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notlike '169.254.*' -and $_.AddressState -eq 'Preferred' } |
    Select-Object -First 1 -ExpandProperty IPAddress
if (!$ip) { throw '请先将电脑连接到手机热点，再启动照片接收端。' }

$stateDir = Join-Path $env:LOCALAPPDATA 'BalconyPhotoDemo'
$stateFile = Join-Path $stateDir 'keys.clixml'
if (!(Test-Path -LiteralPath $stateFile)) {
    New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
    @{
        Upload = ConvertTo-SecureString ([Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)).ToLowerInvariant()) -AsPlainText -Force
        View = ConvertTo-SecureString ([Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)).ToLowerInvariant()) -AsPlainText -Force
    } | Export-Clixml -LiteralPath $stateFile
}
$keys = Import-Clixml -LiteralPath $stateFile
$viewKey = [System.Net.NetworkCredential]::new('', $keys.View).Password
$listener = Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue
if ($listener) {
    $process = Get-CimInstance Win32_Process -Filter ('ProcessId = ' + $listener.OwningProcess)
    if ($listener.LocalAddress -ne $ip -or !$process.CommandLine.Contains((Join-Path $PSScriptRoot 'receiver.py'))) {
        throw '8768 端口已被占用，或接收端还在使用之前的热点 IP。请先关闭旧接收端。'
    }
} else {
    $python = py -3 -c 'import sys; print(sys.executable)'
    if ($LASTEXITCODE -ne 0) { throw '无法找到 Python 3。' }
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $python
    $startInfo.ArgumentList.Add((Join-Path $PSScriptRoot 'receiver.py'))
    $startInfo.WorkingDirectory = $PSScriptRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.Environment['PHOTO_BIND'] = $ip
    $startInfo.Environment['PHOTO_PORT'] = [string] $port
    $startInfo.Environment['PHOTO_UPLOAD_TOKEN'] = [System.Net.NetworkCredential]::new('', $keys.Upload).Password
    $startInfo.Environment['PHOTO_VIEW_TOKEN'] = $viewKey
    [System.Diagnostics.Process]::Start($startInfo) | Out-Null
}
Write-Host "同一热点的照片页  http://${ip}:${port}/"
if ($ShowViewKey) { Write-Host "查看密钥  $viewKey" }
