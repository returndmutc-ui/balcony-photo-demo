#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ctype.h>
#include "esp_camera.h"
#include "mbedtls/base64.h"

struct UploadSettings {
  char ssid[33];
  char password[65];
  char url[241];
  char token[65];
};

constexpr char storageName[] = "photo-upload";
constexpr char storageKey[] = "config-v1";
UploadSettings settings{};
bool configured = false;
bool cameraReady = false;

bool setupCamera() {
  if (!psramFound()) {
    Serial.println("ERROR PSRAM: set OPI PSRAM in the board options");
    return false;
  }

  // XIAO ESP32-S3 Sense OV3660 camera wiring.
  camera_config_t config{};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 15;
  config.pin_d1 = 17;
  config.pin_d2 = 18;
  config.pin_d3 = 16;
  config.pin_d4 = 14;
  config.pin_d5 = 12;
  config.pin_d6 = 11;
  config.pin_d7 = 48;
  config.pin_xclk = 10;
  config.pin_pclk = 13;
  config.pin_vsync = 38;
  config.pin_href = 47;
  config.pin_sccb_sda = 40;
  config.pin_sccb_scl = 39;
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  const esp_err_t error = esp_camera_init(&config);
  if (error != ESP_OK) {
    Serial.printf("ERROR camera initialization: 0x%x\n", error);
    return false;
  }
  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor && sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);
  }
  return true;
}

bool loadSettings() {
  Preferences preferences;
  if (!preferences.begin(storageName, true)) {
    return false;
  }
  const bool loaded = preferences.getBytesLength(storageKey) == sizeof(settings) &&
                      preferences.getBytes(storageKey, &settings, sizeof(settings)) == sizeof(settings);
  preferences.end();
  return loaded;
}

bool decodeField(const char* encoded, char* value, size_t capacity) {
  size_t length = 0;
  if (mbedtls_base64_decode(reinterpret_cast<unsigned char*>(value), capacity - 1,
                            &length, reinterpret_cast<const unsigned char*>(encoded),
                            strlen(encoded)) != 0 ||
      length == 0 || memchr(value, '\0', length) != nullptr) {
    return false;
  }
  value[length] = '\0';
  return true;
}

bool writeSettings(const char* ssid, const char* password, const char* url, const char* token) {
  UploadSettings next{};
  if (!ssid || !password || !url || !token ||
      !decodeField(ssid, next.ssid, sizeof(next.ssid)) ||
      (strcmp(password, "-") != 0 && !decodeField(password, next.password, sizeof(next.password))) ||
      strncmp(url, "http://", 7) != 0 || strlen(url) <= 7 || strlen(url) >= sizeof(next.url) ||
      strlen(token) != sizeof(next.token) - 1) {
    Serial.println("ERROR invalid configuration");
    return false;
  }
  for (const char* digit = token; *digit != '\0'; ++digit) {
    if (!isxdigit(static_cast<unsigned char>(*digit))) {
      Serial.println("ERROR invalid token format");
      return false;
    }
  }
  strcpy(next.url, url);
  strcpy(next.token, token);

  Preferences preferences;
  if (!preferences.begin(storageName, false)) {
    Serial.println("ERROR unable to open settings storage");
    return false;
  }
  const size_t written = preferences.putBytes(storageKey, &next, sizeof(next));
  preferences.end();
  if (written != sizeof(next)) {
    Serial.println("ERROR unable to save configuration");
    return false;
  }

  WiFi.disconnect();
  settings = next;
  configured = true;
  Serial.println("CONFIG OK");
  return true;
}

bool connectHotspot() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.begin(settings.ssid, settings.password);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(150);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR Wi-Fi connection timed out");
    return false;
  }
  Serial.print("WIFI OK ");
  Serial.println(WiFi.localIP());
  return true;
}

void uploadPhoto() {
  if (!configured) {
    Serial.println("ERROR configure the hotspot and upload address over USB first");
    return;
  }
  if (!cameraReady) {
    Serial.println("ERROR camera is unavailable");
    return;
  }
  if (!connectHotspot()) {
    return;
  }

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, settings.url)) {
    Serial.println("ERROR unable to open upload endpoint");
    return;
  }
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Authorization", String("Bearer ") + settings.token);

  // Discard a queued JPEG so the upload reflects the current scene.
  camera_fb_t* queued = esp_camera_fb_get();
  if (queued) {
    esp_camera_fb_return(queued);
  }
  camera_fb_t* frame = esp_camera_fb_get();
  if (!frame) {
    Serial.println("ERROR unable to capture JPEG");
    http.end();
    return;
  }
  const size_t length = frame->len;
  const int status = http.POST(frame->buf, length);
  esp_camera_fb_return(frame);
  http.end();

  if (status >= 200 && status < 300) {
    Serial.printf("UPLOAD OK %d %u\n", status, static_cast<unsigned>(length));
  } else {
    Serial.printf("ERROR upload status %d\n", status);
  }
}

void handleCommand(char* line) {
  char* tail = nullptr;
  const char* command = strtok_r(line, " ", &tail);
  if (!command) {
    return;
  }
  if (strcmp(command, "SEND") == 0) {
    uploadPhoto();
  } else if (strcmp(command, "CONFIG") == 0) {
    const char* ssid = strtok_r(nullptr, " ", &tail);
    const char* password = strtok_r(nullptr, " ", &tail);
    const char* url = strtok_r(nullptr, " ", &tail);
    const char* token = strtok_r(nullptr, " ", &tail);
    if (strtok_r(nullptr, " ", &tail)) {
      Serial.println("ERROR invalid configuration");
    } else if (writeSettings(ssid, password, url, token)) {
      uploadPhoto();
    }
  } else {
    Serial.println("ERROR unknown command");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setRxBufferSize(512);
  delay(1500);
  cameraReady = setupCamera();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  configured = loadSettings();
  Serial.println("READY camera hotspot upload");
  if (configured) {
    uploadPhoto();
  }
}

void loop() {
  static char line[512];
  static size_t length = 0;
  static bool overflow = false;
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n') {
      if (overflow) {
        Serial.println("ERROR command too long");
      } else {
        line[length] = '\0';
        handleCommand(line);
      }
      memset(line, 0, length);
      length = 0;
      overflow = false;
    } else if (ch != '\r' && !overflow) {
      if (length < sizeof(line) - 1) {
        line[length++] = ch;
      } else {
        overflow = true;
      }
    }
  }
  delay(5);
}
