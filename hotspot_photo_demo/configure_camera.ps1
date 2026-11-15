param(
    [string] $Port = 'COM10',
    [switch] $UseSavedHotspot
)

$ErrorActionPreference = 'Stop'
$wlan = netsh wlan show interfaces
$ssidLine = $wlan | Select-String '^\s*SSID\s*:\s*(.+)$' | Select-Object -First 1
if (!$ssidLine) { throw '请先将电脑连上手机热点。' }
$ssid = $ssidLine.Matches[0].Groups[1].Value.Trim()
$receiver = Get-NetTCPConnection -State Listen -LocalPort 8768
$keys = Import-Clixml -LiteralPath (Join-Path $env:LOCALAPPDATA 'BalconyPhotoDemo/keys.clixml')
$password = $null
if ($UseSavedHotspot) {
    # Keep the saved Wi-Fi password local and pass it only to the ESP32 over USB.
    $profileLine = $wlan | Select-String '^\s*Profile\s*:\s*(.+)$' | Select-Object -First 1
    if (!$profileLine) { throw '无法定位当前热点的 Windows Wi-Fi 配置。' }
    $profile = $profileLine.Matches[0].Groups[1].Value.Trim()
    $keyLine = netsh wlan show profile "name=$profile" key=clear |
        Select-String '^\s*(?:Key Content|关键内容|密钥内容)\s*:\s*(.+)$' | Select-Object -First 1
    if (!$keyLine) { throw 'Windows 未提供热点密码，可直接运行本脚本在本机输入。' }
    $password = $keyLine.Matches[0].Groups[1].Value.TrimEnd()
} else {
    $securePassword = Read-Host '输入手机的热点密码' -AsSecureString
    $password = [System.Net.NetworkCredential]::new('', $securePassword).Password
}
$settings = @{
    port = $Port
    ssid = $ssid
    password = $password
    url = "http://$($receiver.LocalAddress):8768/api/photo"
    token = [System.Net.NetworkCredential]::new('', $keys.Upload).Password
}

$provision = @'
import base64
import json
import sys
from time import monotonic
from serial import Serial

config = json.load(sys.stdin)
def encode(name):
    return base64.b64encode(config[name].encode("utf-8")).decode("ascii") or "-"

command = " ".join(["CONFIG", encode("ssid"), encode("password"), config["url"], config["token"]])
with Serial(baudrate=115200, timeout=0.25, write_timeout=2) as port:
    port.port = config["port"]
    port.rts = False
    port.open()
    port.reset_input_buffer()
    port.write(("\n" + command + "\n").encode("ascii"))
    deadline = monotonic() + 40
    while monotonic() < deadline:
        line = port.readline().decode("ascii", errors="ignore").strip()
        if line.startswith(("CONFIG OK", "WIFI OK ", "UPLOAD OK ", "ERROR ")):
            print(line, flush=True)
        if line.startswith("UPLOAD OK "):
            break
        if line.startswith("ERROR "):
            raise SystemExit(1)
    else:
        raise SystemExit("摄像头未确认上传。请检查固件和 USB 数据线。")
'@
$settings | ConvertTo-Json -Compress -EscapeHandling EscapeNonAscii | py -3 -c $provision
if ($LASTEXITCODE -ne 0) { throw '摄像头配置或上传测试未完成。' }
