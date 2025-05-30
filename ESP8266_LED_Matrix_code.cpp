#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#define MAX_DEVICES 4
#define DATA_PIN 13  // D7
#define CS_PIN   15  // D8
#define CLK_PIN  14  // D5
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
WiFiServer server(80);

const uint8_t MESG_SIZE = 255;
char curMessage[MESG_SIZE] = "";
char newMessage[MESG_SIZE] = "";
bool newMessageAvailable = false;
int brightness = 5;  // Default brightness
bool messageReceived = false;  // Track if a new message has been received

const char WebPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LED Matrix Control</title>
<style>
body { font-family: Arial; background-color: #000000; text-align: center; margin-top: 40px; }
#container { background-color: #00FFFF; padding: 20px; border-radius: 12px; display: inline-block; }
input[type="text"] { width: 200px; padding: 8px; }
input[type="range"] { width: 200px; }
</style>
<script>
function sendText() {
  var msg = document.getElementById('msg').value;
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/?msg=" + encodeURIComponent(msg), true);
  xhr.send();
}
function setBrightness(val) {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/?brightness=" + val, true);
  xhr.send();
}
</script>
</head>
<body>
<h1 style="color:white;">LED Matrix WiFi Control</h1>
<div id="container">
  <p><b>Message:</b><br>
  <input type="text" id="msg" maxlength="255">
  <button onclick="sendText()">Send</button></p>
  <p><b>Brightness:</b><br>
  <input type="range" min="0" max="15" value="5" onchange="setBrightness(this.value)">
  </p>
</div>
</body>
</html>
)rawliteral";

// Function to scroll message once
void scrollMessageOnce(const char* msg) {
  strncpy(curMessage, msg, MESG_SIZE - 1);
  curMessage[MESG_SIZE - 1] = '\0';
  mx.clear();
  uint16_t len = strlen(msg) * 8 + MAX_DEVICES * 8;
  for (uint16_t i = 0; i < len; i++) {
    scrollText();
    delay(75);
    yield();
  }
  mx.clear();
}

void setup() {
  Serial.begin(115200);
  mx.begin();
  mx.setShiftDataInCallback(scrollDataSource);
  mx.control(MD_MAX72XX::INTENSITY, brightness);
  mx.clear();

  scrollMessageOnce("Finding WiFi...");

  WiFiManager wifiManager;
  wifiManager.resetSettings();  // 🔥 This erases stored WiFi credentials
  wifiManager.autoConnect("ESP8266_LED_AP");

  Serial.println("WiFi connected: " + WiFi.localIP().toString());

  // Set IP as default message
  snprintf(curMessage, MESG_SIZE, "IP: %s", WiFi.localIP().toString().c_str());

  server.begin();
}

void loop() {
  handleClient();

  // If no message from web yet, always scroll IP
  if (!messageReceived) {
    static uint32_t lastScroll = 0;
    if (millis() - lastScroll >= 75) {
      mx.transform(MD_MAX72XX::TSL);
      lastScroll = millis();
    }
  } else {
    scrollText();  // Scroll custom user message
  }
}

void handleClient() {
  WiFiClient client = server.available();
  if (client) {
    String req = client.readStringUntil('\r');
    client.flush();

    // Handle message
    if (req.indexOf("/?msg=") >= 0) {
      int start = req.indexOf("/?msg=") + 6;
      int end = req.indexOf(" ", start);
      if (end == -1) end = req.length();
      String msg = req.substring(start, end);
      msg.replace("+", " ");
      msg.replace("%20", " ");
      msg.toCharArray(newMessage, MESG_SIZE);
      newMessageAvailable = true;
      messageReceived = true;
    }

    // Handle brightness
    if (req.indexOf("/?brightness=") >= 0) {
      int start = req.indexOf("/?brightness=") + 13;
      int end = req.indexOf(" ", start);
      if (end == -1) end = req.length();
      brightness = constrain(req.substring(start, end).toInt(), 0, 15);
      mx.control(MD_MAX72XX::INTENSITY, brightness);
    }

    // Send webpage
    client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
    client.println(FPSTR(WebPage));
    delay(1);
    client.stop();
  }
}

uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t) {
  static enum { S_IDLE, S_NEXT_CHAR, S_SHOW_CHAR, S_SHOW_SPACE } state = S_IDLE;
  static char *p;
  static uint8_t cBuf[8];
  static uint16_t curLen, showLen;
  uint8_t colData = 0;

  switch (state) {
    case S_IDLE:
      p = curMessage;
      if (newMessageAvailable) {
        strncpy(curMessage, newMessage, MESG_SIZE - 1);
        newMessageAvailable = false;
      }
      state = S_NEXT_CHAR;
      break;

    case S_NEXT_CHAR:
      if (*p == '\0') state = S_IDLE;
      else {
        showLen = mx.getChar(*p++, sizeof(cBuf), cBuf);
        curLen = 0;
        state = S_SHOW_CHAR;
      }
      break;

    case S_SHOW_CHAR:
      colData = cBuf[curLen++];
      if (curLen < showLen) break;
      showLen = (*p != '\0') ? 1 : (MAX_DEVICES * 8) / 2;
      curLen = 0;
      state = S_SHOW_SPACE;
      break;

    case S_SHOW_SPACE:
      if (++curLen == showLen) state = S_NEXT_CHAR;
      break;
  }
  return colData;
}

void scrollText() {
  static uint32_t last = 0;
  if (millis() - last >= 75) {
    mx.transform(MD_MAX72XX::TSL);  // Scroll Left
    last = millis();
  }
}
