"""Receive a JPEG from the ESP32 and serve the latest photo to the viewer."""

import hmac
import json
import os
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


MAX_JPEG_BYTES = 2 * 1024 * 1024
VIEWER = Path(__file__).with_name("index.html")


class PhotoReceiver(ThreadingHTTPServer):
    def __init__(self, address, upload_token, view_token, allowed_origin=""):
        super().__init__(address, Handler)
        self.upload_token = upload_token
        self.view_token = view_token
        self.allowed_origin = allowed_origin
        self.latest = None
        self.photo_lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        self.request.settimeout(10)
        super().setup()

    def send_payload(self, status, payload, content_type, headers=None, *, cors=False):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if cors:
            self.send_header("Vary", "Origin")
            if self.server.allowed_origin and self.headers.get("Origin") == self.server.allowed_origin:
                self.send_header("Access-Control-Allow-Origin", self.server.allowed_origin)
                self.send_header("Access-Control-Expose-Headers", "X-Captured-At")
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(payload)

    def send_json(self, status, payload, *, cors=False):
        self.send_payload(status, json.dumps(payload, ensure_ascii=False).encode(),
                          "application/json; charset=utf-8", cors=cors)

    def authorized(self, token):
        actual = self.headers.get("Authorization", "").encode("utf-8")
        return hmac.compare_digest(actual, ("Bearer " + token).encode("utf-8"))

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            self.send_payload(200, VIEWER.read_bytes(), "text/html; charset=utf-8")
            return
        if path != "/api/photo":
            self.send_json(404, {"error": "页面不存在。"})
            return
        if not self.authorized(self.server.view_token):
            self.send_json(401, {"error": "查看密钥无效。"}, cors=True)
            return
        with self.server.photo_lock:
            photo = self.server.latest
        if photo is None:
            self.send_json(404, {"error": "还没有收到摄像头照片。"}, cors=True)
            return
        data, received_at = photo
        self.send_payload(200, data, "image/jpeg", {"X-Captured-At": received_at}, cors=True)

    def do_OPTIONS(self):
        if (self.path != "/api/photo" or not self.server.allowed_origin
                or self.headers.get("Origin") != self.server.allowed_origin
                or self.headers.get("Access-Control-Request-Method") != "GET"):
            self.send_json(403, {"error": "此来源无法跨站读取照片。"})
            return
        self.send_payload(204, b"", "text/plain", {
            "Access-Control-Allow-Methods": "GET, OPTIONS",
            "Access-Control-Allow-Headers": "Authorization",
        }, cors=True)

    def do_POST(self):
        if self.path != "/api/photo":
            self.send_json(404, {"error": "上传地址不存在。"})
            return
        if not self.authorized(self.server.upload_token):
            self.send_json(401, {"error": "设备令牌无效。"})
            return
        if self.headers.get_content_type() != "image/jpeg":
            self.send_json(415, {"error": "请上传 JPEG。"})
            return
        try:
            size = int(self.headers.get("Content-Length", ""))
        except ValueError:
            self.send_json(411, {"error": "缺少有效的照片长度。"})
            return
        if not 4 <= size <= MAX_JPEG_BYTES:
            self.send_json(413, {"error": "照片必须小于或等于 2 MiB。"})
            return
        try:
            data = self.rfile.read(size)
        except TimeoutError:
            self.send_json(408, {"error": "接收照片超时。"})
            return
        if len(data) != size or not (data.startswith(b"\xff\xd8") and data.endswith(b"\xff\xd9")):
            self.send_json(400, {"error": "JPEG 数据不完整。"})
            return
        with self.server.photo_lock:
            received_at = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
            self.server.latest = (data, received_at)
        self.send_json(201, {"received_at": received_at, "bytes": size})


def main():
    upload_token = os.environ.get("PHOTO_UPLOAD_TOKEN", "")
    view_token = os.environ.get("PHOTO_VIEW_TOKEN", "")
    if not upload_token or not view_token or upload_token == view_token:
        raise SystemExit("请设置两枚不同的密钥：PHOTO_UPLOAD_TOKEN 和 PHOTO_VIEW_TOKEN。")
    bind = os.environ.get("PHOTO_BIND", "127.0.0.1")
    port = int(os.environ.get("PHOTO_PORT", "8768"))
    origin = os.environ.get("PHOTO_ALLOWED_ORIGIN", "")
    with PhotoReceiver((bind, port), upload_token, view_token, origin) as receiver:
        print(f"照片接收页：http://{bind}:{receiver.server_port}/", flush=True)
        receiver.serve_forever()


if __name__ == "__main__":
    main()
