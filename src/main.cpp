#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "HX711.h"

// ========================
// PINY
// ========================

// Rotary enkoder
const int ENC_SW = 15;
const int ENC_A  = 17;
const int ENC_B  = 16;

// LCD (ST7735 na HSPI)
const int TFT_SCK  = 12;
const int TFT_MOSI = 11;
const int TFT_CS   = 10;
const int TFT_RST  = 6;
const int TFT_DC   = 7;

// HX711 – použijeme piny, co máš napsané jako I2C
const int HX711_DOUT = 8;   // "SDA"
const int HX711_SCK  = 9;   // "SCL"

// ========================
// WiFi AP config
// ========================
const char *AP_SSID     = "ChytraVaha";
const char *AP_PASSWORD = "vaha1234";

IPAddress local_ip(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// ========================
// Web server
// ========================
WebServer server(80);

// ========================
// Váha / stav
// ========================
float currentWeight = 0.0f;
String currentItem  = "Nic";

// ========================
// LCD a váha objekty
// ========================
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
HX711 scale;

// ========================
// Rotary enkoder stav
// ========================
long encoderPosition = 0;
int  lastEncA        = HIGH;
bool lastButtonState = HIGH;

// ========================
// Čas/datum – jednoduchá fake implementace
// ========================
String getTimeString() {
  unsigned long seconds = millis() / 1000;
  uint8_t s = seconds % 60;
  uint8_t m = (seconds / 60) % 60;
  uint8_t h = (seconds / 3600) % 24;
  char buf[9];
  sprintf(buf, "%02u:%02u:%02u", h, m, s);
  return String(buf);
}

// pro teď pevný datum (můžeš si pak nahradit za RTC / NTP)
String getDateString() {
  return String("2025-01-01");
}

// ========================
// HTML stránka (UI) – PŮVODNÍ, BEZE ZMĚNY
// ========================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="cs">
<head>
  <meta charset="UTF-8">
  <title>Chytrá váha</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    :root {
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      color-scheme: dark light;
    }
    body {
      margin: 0;
      padding: 0;
      background: #05070b;
      color: #f5f5f5;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
    }
    .card {
      background: radial-gradient(circle at top, #1b2840, #05070b);
      border-radius: 18px;
      padding: 20px 24px;
      box-shadow: 0 0 30px rgba(0,0,0,0.7);
      max-width: 420px;
      width: 100%;
      box-sizing: border-box;
      border: 1px solid rgba(255,255,255,0.06);
    }
    .header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 16px;
    }
    .header h1 {
      font-size: 1.3rem;
      margin: 0;
    }
    .chip {
      font-size: 0.75rem;
      padding: 4px 10px;
      border-radius: 999px;
      border: 1px solid rgba(255,255,255,0.3);
      text-transform: uppercase;
      letter-spacing: 0.05em;
      opacity: 0.8;
    }
    .weight-display {
      text-align: center;
      margin: 18px 0;
    }
    .weight-value {
      font-size: 3rem;
      font-weight: 600;
      letter-spacing: 0.04em;
    }
    .weight-unit {
      font-size: 1rem;
      opacity: 0.8;
      margin-left: 6px;
    }
    .item-select {
      margin-top: 10px;
    }
    label {
      font-size: 0.85rem;
      opacity: 0.8;
      display: block;
      margin-bottom: 4px;
    }
    select, input {
      width: 100%;
      box-sizing: border-box;
      padding: 8px 10px;
      border-radius: 8px;
      border: 1px solid rgba(255,255,255,0.2);
      background: rgba(0,0,0,0.35);
      color: #f5f5f5;
      outline: none;
      font-size: 0.95rem;
    }
    select:focus, input:focus {
      border-color: #27e3a5;
      box-shadow: 0 0 0 1px rgba(39,227,165,0.4);
    }
    .row {
      display: flex;
      gap: 8px;
      margin-top: 6px;
    }
    button {
      margin-top: 10px;
      width: 100%;
      padding: 10px 12px;
      border-radius: 999px;
      border: none;
      background: linear-gradient(135deg, #27e3a5, #00b3ff);
      color: #020409;
      font-size: 0.95rem;
      font-weight: 600;
      cursor: pointer;
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }
    button:active {
      transform: translateY(1px);
      filter: brightness(0.9);
    }
    .meta {
      margin-top: 10px;
      font-size: 0.75rem;
      opacity: 0.7;
      display: flex;
      justify-content: space-between;
    }
  </style>
</head>
<body>
  <div class="card">
    <div class="header">
      <h1>Chytrá váha</h1>
      <div class="chip" id="connectionStatus">AP: ChytraVaha</div>
    </div>
    <div class="weight-display">
      <div>
        <span class="weight-value" id="weightValue">0.00</span>
        <span class="weight-unit">g</span>
      </div>
      <div style="font-size:0.9rem; opacity:0.8; margin-top:4px;">
        Položka: <span id="itemLabel">Nic</span>
      </div>
    </div>

    <div class="item-select">
      <label for="itemPreset">Co vážíš?</label>
      <select id="itemPreset">
        <option value="Nic">Nic / prázdno</option>
        <option value="Ingredience">Ingredience</option>
        <option value="Balík">Balík</option>
        <option value="Zvíře">Zvíře</option>
        <option value="Váha test">Test kalibrace</option>
        <option value="Vlastní">Vlastní…</option>
      </select>
      <div class="row">
        <div style="flex:3;">
          <label for="itemCustom">Vlastní název</label>
          <input id="itemCustom" placeholder="Např. 'Maso na gril'">
        </div>
      </div>
      <button id="saveItemBtn">Uložit položku</button>
    </div>

    <div class="meta">
      <span id="lastUpdate">Naposledy: -</span>
      <span id="rssiLabel">RSSI: -- dBm</span>
    </div>
  </div>

  <script>
    async function fetchState() {
      try {
        const res = await fetch('/api/state');
        if (!res.ok) return;
        const data = await res.json();
        document.getElementById('weightValue').textContent = data.weight.toFixed(2);
        document.getElementById('itemLabel').textContent = data.item || 'Nic';
        document.getElementById('lastUpdate').textContent = 'Naposledy: ' + new Date().toLocaleTimeString();
        document.getElementById('rssiLabel').textContent = 'RSSI: ' + (data.rssi ?? '--') + ' dBm';
        document.getElementById('connectionStatus').textContent = 'Připojeno k váze';
      } catch (e) {
        document.getElementById('connectionStatus').textContent = 'Chyba spojení…';
      }
    }

    async function saveItem() {
      const preset = document.getElementById('itemPreset').value;
      const custom = document.getElementById('itemCustom').value.trim();
      const item = (preset === 'Vlastní' && custom.length > 0) ? custom : preset;

      const params = new URLSearchParams();
      params.append('item', item);

      try {
        await fetch('/api/item', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: params.toString()
        });
        document.getElementById('itemLabel').textContent = item;
      } catch (e) {
        console.error(e);
      }
    }

    document.getElementById('saveItemBtn').addEventListener('click', saveItem);

    fetchState();
    setInterval(fetchState, 500);
  </script>
</body>
</html>
)rawliteral";

// ========================
// Helper: čtení váhy (bez kalibrace)
// ========================
void updateWeightFromScale() {
  if (scale.is_ready()) {
    // prozatím bez kalibrace – jednotky budou "něco", ale pohne se to :)
    currentWeight = scale.get_units(1);
  }
}

// ========================
// Rotary enkoder – jednoduché čtení
// ========================
void updateEncoder() {
  int a = digitalRead(ENC_A);
  int b = digitalRead(ENC_B);

  if (a != lastEncA) {
    // na hraně A se podíváme na B a podle toho směr
    if (a == HIGH) {
      if (b == LOW) {
        encoderPosition++;
      } else {
        encoderPosition--;
      }
    }
    lastEncA = a;
  }

  bool btn = digitalRead(ENC_SW);
  if (btn != lastButtonState) {
    lastButtonState = btn;
  }
}

// ========================
// LCD – pomocné vykreslení
// ========================
void drawBootMessage(const char *line1, const char *line2) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.println("Chytra vaha");
  tft.println("----------------");
  if (line1) tft.println(line1);
  if (line2) tft.println(line2);
}

void drawMainScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.println("Chytra vaha");

  tft.setCursor(0, 14);
  tft.print("W: ");
  tft.print(currentWeight, 1);
  tft.println(" (raw)");

  tft.setCursor(0, 28);
  tft.print("Item: ");
  tft.println(currentItem);

  tft.setCursor(0, 42);
  tft.print("Enc: ");
  tft.print(encoderPosition);

  tft.setCursor(0, 56);
  tft.print("BTN: ");
  tft.println(lastButtonState == LOW ? "PRESSED" : "released");

  tft.setCursor(0, 70);
  tft.print(getTimeString());
}

// ========================
// Handlery HTTP
// ========================
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleState() {
  updateWeightFromScale();
  int rssi = 0; // v AP modu nemá moc smysl

  String json = "{";
  json += "\"weight\":" + String(currentWeight, 2) + ",";
  json += "\"item\":\"" + currentItem + "\",";
  json += "\"rssi\":" + String(rssi);
  json += "}";
  server.send(200, "application/json", json);
}

void handleItemPost() {
  if (server.hasArg("item")) {
    currentItem = server.arg("item");
    Serial.print("New item: ");
    Serial.println(currentItem);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing 'item'");
  }
}

// nový endpoint: /api_json – pro tebe
void handleApiJson() {
  updateWeightFromScale();

  String json = "{";
  json += "\"weight\":" + String(currentWeight, 2) + ",";
  json += "\"item\":\"" + currentItem + "\",";
  json += "\"date\":\"" + getDateString() + "\",";
  json += "\"time\":\"" + getTimeString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ========================
// Setup
// ========================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("Chytra vaha - ESP32S3 AP + Web + LCD + HX711 + enkoder");

  // PINy enkoderu
  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(ENC_A,  INPUT_PULLUP);
  pinMode(ENC_B,  INPUT_PULLUP);
  lastEncA        = digitalRead(ENC_A);
  lastButtonState = digitalRead(ENC_SW);

  // LCD
  // LCD – ST7789 240x320
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  
  // šířka = 240, výška = 320 (nebo naopak podle orientace)
  // tady inicializujeme nativní rozlišení
  tft.init(240, 320);
  
  tft.setRotation(1);          // orientace naležato (320x240)
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  drawBootMessage("Startuji...", nullptr);
  
  // pokud bys měl obraz obrácený/barvy negativní, můžeš zkusit:
  // tft.invertDisplay(true);

  // HX711
  drawBootMessage("HX711 init", nullptr);
  scale.begin(HX711_DOUT, HX711_SCK);
  scale.set_scale(1.0f); // bez kalibrace
  scale.tare();
  delay(200);

  // Start AP
  drawBootMessage("Startuji AP", AP_SSID);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (ok) {
    Serial.println("AP started");
    Serial.print("SSID: ");
    Serial.println(AP_SSID);
    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);
    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Failed to start AP!");
  }

  // mDNS vaha.local
  drawBootMessage("mDNS: vaha.local", nullptr);
  if (MDNS.begin("vaha")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started: http://vaha.local");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  // Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/item", HTTP_POST, handleItemPost);
  server.on("/api_json", HTTP_GET, handleApiJson);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  // Úvodní hlavní obrazovka
  drawMainScreen();
}

// ========================
// Loop
// ========================
void loop() {
  server.handleClient();

  updateEncoder();
  updateWeightFromScale();

  static unsigned long lastDisp = 0;
  if (millis() - lastDisp > 200) {  // cca 5x za sekundu
    drawMainScreen();
    lastDisp = millis();
  }
}
