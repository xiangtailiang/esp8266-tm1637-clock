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
#define DISPLAY_CONFIG_ADDR 102     // EEPROM中显示配置的存储地址
#define DEFAULT_DAY_BRIGHTNESS 3    // 默认白天亮度
#define DEFAULT_NIGHT_BRIGHTNESS 1  // 默认夜间亮度
#define DEFAULT_SHOW_DATE true      // 默认显示日期
#define DEFAULT_TOGGLE_INTERVAL 20   // 默认显示间隔（秒）

// 创建显示对象
TM1637Display display(CLK_PIN, DIO_PIN);

// 显示配置
struct {
  uint8_t dayBrightness;
  uint8_t nightBrightness;
  bool showDate;
  uint8_t toggleInterval;
} displayConfig = { 
  DEFAULT_DAY_BRIGHTNESS,
  DEFAULT_NIGHT_BRIGHTNESS,
  DEFAULT_SHOW_DATE,
  DEFAULT_TOGGLE_INTERVAL
};

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
        .brightness-config, .display-config { margin-top: 20px; padding-top: 20px; border-top: 1px solid #ddd; }
        label { display: block; margin: 10px 0 5px; }
        .checkbox-label { display: flex; align-items: center; }
        .checkbox-label input[type='checkbox'] { width: auto; margin-right: 10px; }
    </style>
    <script>
        window.onload = function() {
            fetch('/get_config')
                .then(response => response.json())
                .then(config => {
                    document.querySelector('input[name="day_brightness"]').value = config.dayBrightness;
                    document.querySelector('input[name="night_brightness"]').value = config.nightBrightness;
                    document.querySelector('input[name="show_date"]').checked = config.showDate;
                    document.querySelector('input[name="toggle_interval"]').value = config.toggleInterval;
                })
                .catch(error => console.error('Error:', error));
        };
    </script>
</head>
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
        .brightness-config, .display-config { margin-top: 20px; padding-top: 20px; border-top: 1px solid #ddd; }
        label { display: block; margin: 10px 0 5px; }
        .checkbox-label { display: flex; align-items: center; }
        .checkbox-label input[type='checkbox'] { width: auto; margin-right: 10px; }
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
            <div class='display-config'>
                <h3>显示设置</h3>
                <label class='checkbox-label'>
                    <input type='checkbox' name='show_date' checked>
                    显示日期
                </label>
                <label>显示日期的间隔时间（秒）</label>
                <input type='number' name='toggle_interval' min='5' max='60' value='20' required>
            </div>
            <button type='submit'>保存配置</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

// 亮度配置页面HTML
const char* displayConfigHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset='utf-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>显示配置</title>
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
    <script>
        window.onload = function() {
            fetch('/get_config')
                .then(response => response.json())
                .then(config => {
                    document.querySelector('input[name="day_brightness"]').value = config.dayBrightness;
                    document.querySelector('input[name="night_brightness"]').value = config.nightBrightness;
                    document.querySelector('input[name="show_date"]').checked = config.showDate;
                    document.querySelector('input[name="toggle_interval"]').value = config.toggleInterval;
                })
                .catch(error => console.error('Error:', error));
        };
    </script>
</head>
<!DOCTYPE html>
<html>
<head>
    <meta charset='utf-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>显示配置</title>
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
        <h2>显示配置</h2>
        <form action='/display_config' method='POST'>
            <div class='brightness-config'>
                <label>白天亮度 (7:00-20:00)</label>
                <input type='number' name='day_brightness' min='0' max='4' value='3' required>
                <label>夜间亮度</label>
                <input type='number' name='night_brightness' min='0' max='4' value='1' required>
            </div>
            <div class='display-config' style='margin-top: 20px; padding-top: 20px; border-top: 1px solid #ddd;'>
                <h3>显示设置</h3>
                <label style='display: flex; align-items: center;'>
                    <input type='checkbox' name='show_date' style='width: auto; margin-right: 10px;'> 显示日期
                </label>
                <label>显示日期的间隔时间（秒）</label>
                <input type='number' name='toggle_interval' min='5' max='60' value='20' required>
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


void saveDisplayConfig(uint8_t dayBrightness, uint8_t nightBrightness, bool showDate, uint8_t toggleInterval) {
  EEPROM.write(BRIGHTNESS_ADDR, dayBrightness);
  EEPROM.write(BRIGHTNESS_ADDR + 1, nightBrightness);
  EEPROM.write(DISPLAY_CONFIG_ADDR, showDate ? 1 : 0);
  EEPROM.write(DISPLAY_CONFIG_ADDR + 1, toggleInterval);
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

void handleGetConfig() {
  String json = "{\"dayBrightness\":" + String(displayConfig.dayBrightness) + 
                ",\"nightBrightness\":" + String(displayConfig.nightBrightness) + 
                ",\"showDate\":" + (displayConfig.showDate ? "true" : "false") + 
                ",\"toggleInterval\":" + String(displayConfig.toggleInterval) + "}"; 
  server.send(200, "application/json", json);
}

void handleRoot() {
  if (WiFi.getMode() == WIFI_AP) {
    server.send(200, "text/html", apConfigHTML);
  } else {
    server.send(200, "text/html", displayConfigHTML);
  }
}

void handleDisplayConfig() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String dayBrightness = server.arg("day_brightness");
  String nightBrightness = server.arg("night_brightness");
  bool showDate = server.hasArg("show_date");
  String toggleInterval = server.arg("toggle_interval");

  // 验证亮度值
  uint8_t dayBrightnessVal = dayBrightness.toInt();
  uint8_t nightBrightnessVal = nightBrightness.toInt();
  uint8_t toggleIntervalVal = toggleInterval.toInt();

  if (dayBrightnessVal > 4 || nightBrightnessVal > 4) {
    server.send(400, "text/plain", "亮度值必须在0-4之间");
    return;
  }

  if (toggleIntervalVal < 5 || toggleIntervalVal > 60) {
    server.send(400, "text/plain", "日期显示间隔必须在5-60秒之间");
    return;
  }

  // 保存显示配置
  saveDisplayConfig(dayBrightnessVal, nightBrightnessVal, showDate, toggleIntervalVal);
  displayConfig.dayBrightness = dayBrightnessVal;
  displayConfig.nightBrightness = nightBrightnessVal;
  displayConfig.showDate = showDate;
  displayConfig.toggleInterval = toggleIntervalVal;

  // 在AP模式下，保存WiFi配置
  if (WiFi.getMode() == WIFI_AP) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    if (ssid.length() == 0) {
      server.send(400, "text/plain", "WiFi名称不能为空");
      return;
    }

    // 保存WiFi配置
    saveWiFiConfig(ssid.c_str(), password.c_str());

    server.send(200, "text/html", "<html><head><meta charset='utf-8'><meta http-equiv='refresh' content='5;url=/'></head><body>配置已保存，设备将在5秒后重启...</body></html>");
    
    // 延迟5秒后重启
    delay(5000);
    ESP.restart();
  } else {
    server.send(200, "text/html", "<html><head><meta charset='utf-8'><meta http-equiv='refresh' content='3;url=/'></head><body>配置已保存</body></html>");
  }
}

void loadDisplayConfig() {
  displayConfig.dayBrightness = EEPROM.read(BRIGHTNESS_ADDR);
  displayConfig.nightBrightness = EEPROM.read(BRIGHTNESS_ADDR + 1);
  displayConfig.showDate = EEPROM.read(DISPLAY_CONFIG_ADDR) == 1;
  displayConfig.toggleInterval = EEPROM.read(DISPLAY_CONFIG_ADDR + 1);

  // 验证读取的值是否有效
  if (displayConfig.dayBrightness > 4) displayConfig.dayBrightness = DEFAULT_DAY_BRIGHTNESS;
  if (displayConfig.nightBrightness > 4) displayConfig.nightBrightness = DEFAULT_NIGHT_BRIGHTNESS;
  if (displayConfig.toggleInterval < 1 || displayConfig.toggleInterval > 10) displayConfig.toggleInterval = DEFAULT_TOGGLE_INTERVAL;
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
  loadDisplayConfig();                                   // 加载显示配置
  
  // 输出当前配置信息
  Serial.println("\n当前显示配置信息：");
  Serial.print("白天亮度 (7:00-20:00): ");
  Serial.println(displayConfig.dayBrightness);
  Serial.print("夜间亮度: ");
  Serial.println(displayConfig.nightBrightness);
  Serial.print("显示日期: ");
  Serial.println(displayConfig.showDate ? "开启" : "关闭");
  Serial.print("轮换间隔: ");
  Serial.print(displayConfig.toggleInterval);
  Serial.println("秒");
  
  display.setBrightness(displayConfig.nightBrightness);  // 初始使用夜间亮度
  display.showNumberDecEx(8888, 0x40, false, 4, 0);         // 显示8888

  // 尝试使用保存的WiFi配置连接
  if (!loadAndConnectWiFi()) {
    Serial.println("\n无法使用保存的配置连接WiFi，启动AP配网模式");
    startAPMode();

    // 设置Web服务器路由
    server.on("/", handleRoot);
    server.on("/configure", handleDisplayConfig);
    server.begin();

    // 在AP模式下循环运行Web服务器
    while (true) {
      server.handleClient();
      delay(10);
    }
  }

  // 在STA模式下也启动Web服务器
  server.on("/", handleRoot);
  server.on("/display_config", handleDisplayConfig);
  server.on("/reset", handleReset);
  server.on("/get_config", HTTP_GET, handleGetConfig);
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
    display.setBrightness(displayConfig.dayBrightness);  // 白天亮度
  } else {
    display.setBrightness(displayConfig.nightBrightness);  // 夜间亮度
  }

  // 每1000毫秒切换一次冒号状态
  if (millis() - lastToggle >= 1000) {
    colonOn = !colonOn;
    lastToggle = millis();
  }

  static bool showingTime = true;
  static unsigned long lastDisplayToggle = 0;

  // 根据配置的间隔显示日期，固定显示3秒
  unsigned long currentMillis = millis();
  unsigned long cyclePosition = (currentMillis / 1000) % displayConfig.toggleInterval;  // 当前在显示周期内的位置
  
  if (displayConfig.showDate) {
    showingTime = !(cyclePosition < 3);  // 固定显示3秒日期
  }

  if (showingTime || !displayConfig.showDate) {
    // 显示时间和冒号
    display.showNumberDecEx(currentTime, colonOn ? 0x40 : 0x00, true, 4, 0);
  } else {
    // 显示日期（月份和日期）
    time_t rawTime = timeClient.getEpochTime();
    tmElements_t tm;
    breakTime(rawTime, tm);
    int month = tm.Month;
    int day = tm.Day;
    display.showNumberDecEx(month * 100 + day, 0x00, false, 4, 0);
  }

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
