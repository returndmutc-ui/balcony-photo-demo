# 阳台摄像头照片接收测试

电脑和 XIAO ESP32-S3 Sense 连到同一个 2.4 GHz 手机热点。XIAO 用 HTTP 把 JPEG 发到电脑，电脑上的网页显示最近收到的照片。

1. 在项目根目录用 PowerShell 7 运行 `& .\hotspot_photo_demo\start.ps1 -ShowViewKey`。输出包括电脑当前的热点地址和查看密钥。
2. 为 XIAO 编译并烧录 `camera_hotspot_upload/camera_hotspot_upload.ino`。板型选 XIAO ESP32S3，并启用 OPI PSRAM 和 USB CDC。保留 USB 数据线，在电脑上关闭串口监视器。
3. 运行 `& .\hotspot_photo_demo\configure_camera.ps1 -Port COM10`，在本机输入手机热点密码。看到 `UPLOAD OK 201` 即表示摄像头上传成功。配置成功后，重新给摄像头上电也会自动拍一张并上传。
4. 打开步骤 1 显示的地址，填查看密钥，然后点击「查看最新照片」。手机热点重开后，电脑 IP 如果变了，需要重启接收端并重新配置摄像头。

Windows 防火墙需要允许当前热点的设备访问接收端 TCP 8768。部分手机热点会隔离已连接设备，遇到这种情况需换一个允许设备互访的热点。

接收程序只在内存保留最近一张照片。上传和查看使用不同的密钥，由启动脚本保存在当前用户的 Windows 加密配置中。照片、Wi-Fi 密码及密钥都应留在设备和接收端。

## GitHub Pages

把 `index.html` 放到 GitHub Pages 的发布目录。网页上填写可访问的 HTTPS 接收端地址与查看密钥。接收端设置 `PHOTO_ALLOWED_ORIGIN` 为对应的 Pages 来源，例如 `https://你的用户名.github.io`，并由 HTTPS 反向代理提供访问。手机热点内的 HTTP 接收端供本地联调用。
