#include <TM1637Display.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

// 定义TM1637显示模块的引脚
#define CLK_PIN D5  // CLK引脚连接到D5
#define DIO_PIN D6  // DIO引脚连接到D6
#define EEPROM_SIZE 512
#define BRIGHTNESS_ADDR 100         // EEPROM中亮度值的存储地址
#define DEFAULT_DAY_BRIGHTNESS 3    // 默认白天亮度
#define DEFAULT_NIGHT_BRIGHTNESS 1  // 默认夜间亮度

// 创建显示对象
TM1637Display display(CLK_PIN, DIO_PIN);

// 亮度配置
struct {
  uint8_t dayBrightness;
  uint8_t nightBrightness;
} brightnessConfig = { DEFAULT_DAY_BRIGHTNESS, DEFAULT_NIGHT_BRIGHTNESS };

// 创建UDP和NTP对象
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.tencent.com", 28800);  // 使用腾讯NTP服务器，偏移量为8小时(28800秒)

// WiFi连接超时时间（毫秒）
const unsigned long WIFI_TIMEOUT = 180000;  // 3分钟

// 创建Web服务器对象，端口80
ESP8266WebServer server(80);

// AP模式的配置
const char* AP_SSID = "ESP8266-Config";
const char* AP_PASSWORD = "12345678";

// 配网页面HTML
const char* apConfigHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset='utf-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>设备配置</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }
        .container { max-width: 400px; margin: 0 auto; }
        input { width: 100%; padding: 8px; margin: 10px 0; box-sizing: border-box; }
        button { width: 100%; padding: 10px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; }
        button:hover { background-color: #45a049; }
        .brightness-config { margin-top: 20px; padding-top: 20px; border-top: 1px solid #ddd; }
        label { display: block; margin: 10px 0 5px; }
    </style>
</head>
<body>
    <div class='container'>
        <h2>设备配置</h2>
        <form action='/configure' method='POST'>
            <div class='wifi-config'>
                <h3>WiFi配置</h3>
                <input type='text' name='ssid' placeholder='WiFi名称' required>
                <input type='password' name='password' placeholder='WiFi密码' required>
            </div>
            <div class='brightness-config'>
                <h3>亮度设置</h3>
                <label>白天亮度 (7:00-20:00)</label>
                <input type='number' name='day_brightness' min='0' max='4' value='3' required>
                <label>夜间亮度</label>
                <input type='number' name='night_brightness' min='0' max='4' value='1' required>
            </div>
            <button type='submit'>保存配置</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

// 亮度配置页面HTML
const char* brightnessConfigHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset='utf-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>亮度配置</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }
        .container { max-width: 400px; margin: 0 auto; }
        input { width: 100%; padding: 8px; margin: 10px 0; box-sizing: border-box; }
        button { width: 100%; padding: 10px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; margin-bottom: 10px; }
        button:hover { background-color: #45a049; }
        .brightness-config { margin-top: 20px; }
        label { display: block; margin: 10px 0 5px; }
        .reset-button { background-color: #f44336; }
        .reset-button:hover { background-color: #da190b; }
    </style>
</head>
<body>
    <div class='container'>
        <h2>亮度配置</h2>
        <form action='/brightness' method='POST'>
            <div class='brightness-config'>
                <label>白天亮度 (7:00-20:00)</label>
                <input type='number' name='day_brightness' min='0' max='4' value='3' required>
                <label>夜间亮度</label>
                <input type='number' name='night_brightness' min='0' max='4' value='1' required>
            </div>
            <button type='submit'>保存配置</button>
        </form>
        <form action='/reset' method='POST' onsubmit='return confirm("确定要恢复出厂设置吗？这将清除所有配置并重启设备。")'>
            <button type='submit' class='reset-button'>恢复出厂设置</button>
        </form>
    </div>
</body>
</html>
)rawliteral";


void saveBrightnessConfig(uint8_t dayBrightness, uint8_t nightBrightness) {
  EEPROM.write(BRIGHTNESS_ADDR, dayBrightness);
  EEPROM.write(BRIGHTNESS_ADDR + 1, nightBrightness);
  EEPROM.commit();
}

void saveWiFiConfig(const char* ssid, const char* password) {
  // 写入SSID长度和内容
  EEPROM.write(0, strlen(ssid));
  for (int i = 0; i < strlen(ssid); i++) {
    EEPROM.write(1 + i, ssid[i]);
  }

  // 写入密码长度和内容
  EEPROM.write(33, strlen(password));
  for (int i = 0; i < strlen(password); i++) {
    EEPROM.write(34 + i, password[i]);
  }

  EEPROM.commit();
}

bool loadAndConnectWiFi() {
  // 读取SSID
  int ssidLength = EEPROM.read(0);
  if (ssidLength > 32 || ssidLength == 0) return false;

  char ssid[33] = { 0 };
  for (int i = 0; i < ssidLength; i++) {
    ssid[i] = EEPROM.read(1 + i);
  }

  // 读取密码
  int passwordLength = EEPROM.read(33);
  if (passwordLength > 64 || passwordLength == 0) return false;

  char password[65] = { 0 };
  for (int i = 0; i < passwordLength; i++) {
    password[i] = EEPROM.read(34 + i);
  }

  // 尝试连接WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > WIFI_TIMEOUT) {
      return false;
    }
    delay(500);
    Serial.print(".");
  }

  return true;
}

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println("\nAP模式已启动");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("密码: ");
  Serial.println(AP_PASSWORD);
  Serial.print("IP地址: ");
  Serial.println(WiFi.softAPIP());

  display.showNumberDecEx(9999, 0x40, false, 4, 0);  // 显示9999表示等待配置
}

void handleRoot() {
  if (WiFi.getMode() == WIFI_AP) {
    server.send(200, "text/html", apConfigHTML);
  } else {
    server.send(200, "text/html", brightnessConfigHTML);
  }
}

void handleConfigure() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  if (WiFi.getMode() == WIFI_AP) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String dayBrightness = server.arg("day_brightness");
    String nightBrightness = server.arg("night_brightness");

    if (ssid.length() == 0 || password.length() == 0) {
      server.send(400, "text/plain", "请输入WiFi信息");
      return;
    }

    // 验证并保存亮度配置
    uint8_t dayBrightnessVal = dayBrightness.toInt();
    uint8_t nightBrightnessVal = nightBrightness.toInt();

    if (dayBrightnessVal > 4 || nightBrightnessVal > 4) {
      server.send(400, "text/plain", "亮度值必须在0-4之间");
      return;
    }

    // 保存亮度配置
    saveBrightnessConfig(dayBrightnessVal, nightBrightnessVal);

    server.send(200, "text/html", "<html><head><meta charset='utf-8'><meta http-equiv='refresh' content='5;url=/'></head><body>正在保存配置并重启，请稍候...</body></html>");

    // 保存WiFi配置
    saveWiFiConfig(ssid.c_str(), password.c_str());

    // 延迟3秒后重启
    delay(3000);
    ESP.restart();
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void handleBrightness() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String dayBrightness = server.arg("day_brightness");
  String nightBrightness = server.arg("night_brightness");

  // 验证亮度值
  uint8_t dayBrightnessVal = dayBrightness.toInt();
  uint8_t nightBrightnessVal = nightBrightness.toInt();

  if (dayBrightnessVal > 4 || nightBrightnessVal > 4) {
    server.send(400, "text/plain", "亮度值必须在0-4之间");
    return;
  }

  // 保存亮度配置
  saveBrightnessConfig(dayBrightnessVal, nightBrightnessVal);
  brightnessConfig.dayBrightness = dayBrightnessVal;
  brightnessConfig.nightBrightness = nightBrightnessVal;

  server.send(200, "text/html", "<html><head><meta charset='utf-8'><meta http-equiv='refresh' content='3;url=/'></head><body>亮度配置已保存</body></html>");
}

void loadBrightnessConfig() {
  brightnessConfig.dayBrightness = EEPROM.read(BRIGHTNESS_ADDR);
  brightnessConfig.nightBrightness = EEPROM.read(BRIGHTNESS_ADDR + 1);

  // 验证读取的值是否有效
  if (brightnessConfig.dayBrightness > 4) brightnessConfig.dayBrightness = DEFAULT_DAY_BRIGHTNESS;
  if (brightnessConfig.nightBrightness > 4) brightnessConfig.nightBrightness = DEFAULT_NIGHT_BRIGHTNESS;
}

void handleReset() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  // 清除EEPROM中的所有数据
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();

  server.send(200, "text/html", "<html><head><meta charset='utf-8'><meta http-equiv='refresh' content='5;url=/'></head><body>正在恢复出厂设置并重启，请稍候...</body></html>");

  // 延迟3秒后重启
  delay(3000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  display.clear();
  loadBrightnessConfig();                                   // 加载亮度配置
  display.setBrightness(brightnessConfig.nightBrightness);  // 初始使用夜间亮度
  display.showNumberDecEx(8888, 0x40, false, 4, 0);         // 显示8888

  // 尝试使用保存的WiFi配置连接
  if (!loadAndConnectWiFi()) {
    Serial.println("\n无法使用保存的配置连接WiFi，启动AP配网模式");
    startAPMode();

    // 设置Web服务器路由
    server.on("/", handleRoot);
    server.on("/configure", handleConfigure);
    server.begin();

    // 在AP模式下循环运行Web服务器
    while (true) {
      server.handleClient();
      delay(10);
    }
  }

  // 在STA模式下也启动Web服务器
  server.on("/", handleRoot);
  server.on("/brightness", handleBrightness);
  server.on("/reset", handleReset);
  server.begin();

  Serial.println("\nWiFi连接成功");
  Serial.println("IP地址: " + WiFi.localIP().toString());

  // 初始化NTP客户端
  timeClient.begin();

  // 上电后尝试同步NTP时间，最多重试3次
  Serial.println("开始初始NTP同步...");
  display.showNumberDecEx(0000, 0x40, false, 4, 0);  // 显示0000表示正在同步时间

  int retryCount = 0;
  const int maxRetries = 3;
  bool syncSuccess = false;

  while (retryCount < maxRetries && !syncSuccess) {
    if (timeClient.update()) {
      syncSuccess = true;
      Serial.println("NTP同步成功 #" + String(retryCount + 1));
    } else {
      retryCount++;
      Serial.println("NTP同步失败 #" + String(retryCount));
      if (retryCount < maxRetries) {
        Serial.println("3秒后重试...");
        delay(3000);
      }
    }
  }

  if (!syncSuccess) {
    display.showNumberDecEx(0000, 0x40, true, 4, 0);  // 同步失败显示0000
    Serial.println("NTP同步失败，已达到最大重试次数");
  }
}

void loop() {
  static bool colonOn = false;          // 控制冒号显示状态
  static unsigned long lastToggle = 0;  // 上次切换冒号状态的时间
  static unsigned long lastUpdate = 0;  // 上次NTP更新时间

  // 每1小时更新一次NTP时间
  if (millis() - lastUpdate >= 3600000) {
    if (timeClient.update()) {
      Serial.println("NTP同步成功");
      lastUpdate = millis();
    } else {
      Serial.println("NTP同步失败");
    }
  }

  // 获取当前时间
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  int currentTime = hours * 100 + minutes;

  // 根据时间设置亮度（7:00-20:00为白天）
  if (hours >= 7 && hours < 20) {
    display.setBrightness(brightnessConfig.dayBrightness);  // 白天亮度
  } else {
    display.setBrightness(brightnessConfig.nightBrightness);  // 夜间亮度
  }

  // 每1000毫秒切换一次冒号状态
  if (millis() - lastToggle >= 1000) {
    colonOn = !colonOn;
    lastToggle = millis();
  }

  // 显示时间和冒号
  display.showNumberDecEx(currentTime, colonOn ? 0x40 : 0x00, true, 4, 0);

  static unsigned long lastWiFiCheck = 0;  // 上次检查WiFi状态的时间

  // 每60秒检查一次WiFi连接状态
  if (millis() - lastWiFiCheck >= 60000) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      display.showNumberDecEx(8888, 0x40, false, 4, 0);  // 显示8888表示正在重连
      Serial.println("WiFi已断开，正在重新连接...");
    }
  }

  // 处理Web服务器客户端请求
  server.handleClient();
  delay(100);  // 减小延时以提高Web服务器响应速度
}
