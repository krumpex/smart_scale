#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>      // konfigurační portal

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "HX711.h"
#include <time.h>

// ========================
// PINY
// ========================

// Rotary enkoder
const int ENC_SW = 15;
const int ENC_A  = 17;
const int ENC_B  = 16;

// LCD (ST7789 na HSPI)
const int TFT_SCK  = 12;
const int TFT_MOSI = 11;
const int TFT_CS   = 10;
const int TFT_RST  = 6;
const int TFT_DC   = 7;

// HX711 – použijeme piny, co máš napsané jako I2C
const int HX711_DOUT = 8;   // "SDA"
const int HX711_SCK  = 9;   // "SCL"

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
unsigned long lastButtonEventMs = 0;

// dlouhý stisk
const unsigned long LONG_PRESS_MS = 1500;
unsigned long buttonPressStartMs  = 0;
bool buttonLongPressEvent         = false;
bool buttonLongPressFired        = false;  // aby se long press nevytvářel víckrát během jednoho stisku


// ========================
// UI režimy
// ========================
enum UiMode {
  UI_HUD = 0,
  UI_MENU = 1,
  UI_MENU_TARE = 2,
  UI_MENU2 = 3
};

UiMode uiMode = UI_HUD;

// menu (hlavní)
const int MENU_ITEMS = 3;
const char* MENU_LABELS[MENU_ITEMS] = {
  "Kalibrace",
  "Resetovat WiFi",
  "Zpet"
};

int  menuIndex    = 0;
long menuEncStart = 0;   // encoder position při vstupu do menu

// menu TARE (misky/hrnky)
const int MENU_TARE_ITEMS = 6;
const char* MENU_TARE_LABELS[MENU_TARE_ITEMS] = {
  "Zpet",
  "Mala miska plastova",
  "Maly hrnecek",
  "Velky hrnek",
  "melky talir 1",
  "melky talir 2"
};

int menuTareIndex = 0;

// menu2 placeholder
const int MENU2_ITEMS = 6;
const char* MENU2_LABELS[MENU2_ITEMS] = {
  "Zpet",
  "menu item 1",
  "menu item 2",
  "menu item 3",
  "menu item 4",
  "menu item 5"
};

int menu2Index = 0;

// baseline enkodéru pro HUD (pro detekci ±5 kroků)
long hudEncStart = 0;
unsigned long lastHudEncoderMoveMs = 0;
long hudEncLastPos = 0;

// ========================
// HUD – poslední vykreslené hodnoty
// ========================
float lastDrawnWeight = 999999.0f;
int   lastWifiLevel   = -1;
String lastTimeStr    = "";
String lastDateStr    = "";

// TAR stav
bool tarActive        = false;
bool tarDrawn         = false;
unsigned long tarStartMs = 0;

// barvy
uint16_t COLOR_BG      = 0x0000; // černá
uint16_t COLOR_TOPBAR1 = 0x0015; // tmavě modrá
uint16_t COLOR_TOPBAR2 = 0x025F; // světlejší modrá
uint16_t COLOR_TEXT    = 0xFFFF; // bílá
uint16_t COLOR_ACCENT  = 0x07E0; // zelená

// barvy pro další menu
uint16_t COLOR_MENU_TARE_ACCENT = 0xF800; // červená
uint16_t COLOR_MENU2_ACCENT     = 0xFFE0; // žlutá

// ========================
// Čas – NTP
// ========================
const long  gmtOffset_sec     = 3600;   // +1h
const int   daylightOffset_sec = 3600;  // další hodina v létě

bool getLocalTimeSafe(struct tm * timeinfo) {
  if (!getLocalTime(timeinfo)) {
    return false;
  }
  return true;
}

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTimeSafe(&timeinfo)) {
    return "--:--:--";
  }
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

String getDateString() {
  struct tm timeinfo;
  if (!getLocalTimeSafe(&timeinfo)) {
    return "----/--/--";
  }
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

// ========================
// HTML UI – původní
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
      <div class="chip" id="connectionStatus">WiFi</div>
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
        document.getElementById('connectionStatus').textContent = 'WiFi OK';
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
// Váha
// ========================
void updateWeightFromScale() {
  if (scale.is_ready()) {
    currentWeight = scale.get_units(1);  // bez kalibrace
  }
}

// ========================
// Rotary enkoder
// ========================
void updateEncoder() {
  int a = digitalRead(ENC_A);
  int b = digitalRead(ENC_B);

  if (a != lastEncA) {
    if (a == HIGH) {
      if (b == LOW) encoderPosition++;
      else          encoderPosition--;
    }
    lastEncA = a;
  }
}

// ========================
// Button – detekce krátkého stisku
// ========================
bool checkButtonClicked() {
  bool cur = digitalRead(ENC_SW);
  unsigned long now = millis();
  bool clicked = false;

  if (cur != lastButtonState) {
    // jednoduchý debounce
    if (now - lastButtonEventMs > 30) {
      lastButtonEventMs = now;

      // hrana dolů = začátek stisku
      if (lastButtonState == HIGH && cur == LOW) {
        buttonPressStartMs   = now;
        buttonLongPressFired = false;
      }
      // hrana nahoru = konec stisku
      else if (lastButtonState == LOW && cur == HIGH) {
        unsigned long pressDuration = now - buttonPressStartMs;
        // pokud jsme ještě nevystřelili long press, bereme to jako krátký klik
        if (!buttonLongPressFired && pressDuration < LONG_PRESS_MS) {
          clicked = true;
        }
        // když už long press proběhl, po puštění se nic dalšího neděje
      }

      lastButtonState = cur;
    }
  }

  // long press detekujeme během držení (tlačítko je LOW)
  if (lastButtonState == LOW && !buttonLongPressFired) {
    unsigned long pressDuration = now - buttonPressStartMs;
    if (pressDuration >= LONG_PRESS_MS) {
      buttonLongPressEvent = true;
      buttonLongPressFired = true;
    }
  }

  return clicked;
}

// ========================
// HUD – statická část
// ========================
void drawTopBarGradient() {
  // jednoduchý vertikální "glow" gradient
  for (int y = 0; y < 24; y++) {
    uint8_t mix = map(y, 0, 23, 0, 255);
    uint8_t r1 = (COLOR_TOPBAR1 >> 11) & 0x1F;
    uint8_t g1 = (COLOR_TOPBAR1 >> 5)  & 0x3F;
    uint8_t b1 = (COLOR_TOPBAR1)       & 0x1F;

    uint8_t r2 = (COLOR_TOPBAR2 >> 11) & 0x1F;
    uint8_t g2 = (COLOR_TOPBAR2 >> 5)  & 0x3F;
    uint8_t b2 = (COLOR_TOPBAR2)       & 0x1F;

    uint8_t r = (r1 * (255 - mix) + r2 * mix) / 255;
    uint8_t g = (g1 * (255 - mix) + g2 * mix) / 255;
    uint8_t b = (b1 * (255 - mix) + b2 * mix) / 255;

    uint16_t c = (r << 11) | (g << 5) | b;
    tft.drawFastHLine(0, y, 320, c);
  }
}

void drawStaticHUD() {
  tft.fillScreen(COLOR_BG);

  // top bar
  drawTopBarGradient();

  // "Chytra vaha" label vlevo nahoře
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(4, 6);
  tft.print("Chytra vaha");

  int boxX = 20;
  int boxY = 60;
  int boxW = 280;
  int boxH = 120;

  uint16_t glowColor = COLOR_TOPBAR2;
  tft.fillRoundRect(boxX - 4, boxY - 4, boxW + 8, boxH + 8, 14, glowColor);
  tft.fillRoundRect(boxX, boxY, boxW, boxH, 12, COLOR_BG);

  // label "g"
  tft.setTextSize(2);
  tft.setCursor(boxX + boxW - 35, boxY + boxH - 28);
  tft.setTextColor(COLOR_ACCENT);
  tft.print("g");

  // spodní status bar (enkoder / stav tlačítka)
  tft.drawFastHLine(0, 220, 320, COLOR_TOPBAR2);
}

void eraseWeightArea() {
  // smažeme jen oblast, kde je číslo
  int boxX = 20;
  int boxY = 60;
  int boxW = 280;
  int boxH = 120;
  // uvnitř ještě menší rect pro číslo
  tft.fillRect(boxX + 10, boxY + 20, boxW - 60, boxH - 40, COLOR_BG);
}

// ========================
// HUD – dynamická část
// ========================

// ikona WiFi podle síly
void drawWifiIcon(int level) {
  // úroveň 0–4 (0 = nic, 4 = plný)
  int x = 260;
  int y = 5;
  int barW = 5;
  int barSpacing = 3;

  // smaž ikonku
  tft.fillRect(x - 2, 2, 320 - x, 20, COLOR_BG);
  // malý "glow" pod tím
  tft.fillRect(x - 2, 20, 40, 2, COLOR_TOPBAR2);

  for (int i = 0; i < 4; i++) {
    int barH = 4 + i * 3;
    int bx = x + i * (barW + barSpacing);
    int by = 20 - barH;
    uint16_t col = (i < level) ? COLOR_ACCENT : 0x4208;
    tft.fillRect(bx, by, barW, barH, col);
  }
}

int wifiLevelFromRSSI(int rssi) {
  if (rssi == 0) return 0;
  if (rssi > -55) return 4;
  if (rssi > -65) return 3;
  if (rssi > -75) return 2;
  if (rssi > -85) return 1;
  return 0;
}

void updateTopBarHUD() {
  // čas + datum
  String timeStr = getTimeString();
  String dateStr = getDateString();

  if (timeStr != lastTimeStr || dateStr != lastDateStr) {
    // smažeme střed top baru (pod textem) – necháme gradient pod tím
    tft.fillRect(110, 4, 130, 18, COLOR_BG);  // malý "průhled" – přes něj text
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(115, 6);
    tft.print(timeStr);
    tft.setCursor(115, 14);
    tft.print(dateStr);

    lastTimeStr = timeStr;
    lastDateStr = dateStr;
  }

  // WiFi síla
  int rssi = 0;
  int level = 0;
  if (WiFi.status() == WL_CONNECTED) {
    rssi = WiFi.RSSI();
    level = wifiLevelFromRSSI(rssi);
  } else {
    level = 0;
  }

  if (level != lastWifiLevel) {
    drawWifiIcon(level);
    lastWifiLevel = level;
  }
}

void updateWeightHUD() {
  // jen když se změnila (trochu)
  if (fabs(currentWeight - lastDrawnWeight) < 0.05f) {
    return;
  }

  eraseWeightArea();

  // velké číslo – tady by šel použít Orbitron jako vlastní font,
  // pro teď použijeme default s větším textSize
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(4);

  char buf[16];
  dtostrf(currentWeight, 0, 1, buf);

  // spočítáme šířku textu nahrubo (4 px * size + mezery)
  int len = strlen(buf);
  int charW = 6 * 4; // 6px * size4
  int totalW = len * charW;

  int boxX = 20;
  int boxW = 280;
  int x = boxX + (boxW - totalW) / 2;
  int y = 90;

  // "glow" – nejdřív tmavší stín
  tft.setTextColor(COLOR_TOPBAR2);
  tft.setCursor(x + 2, y + 2);
  tft.print(buf);

  // pak ostrý text
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(x, y);
  tft.print(buf);

  lastDrawnWeight = currentWeight;
}

void updateBottomHUD() {
  // encoder info dole
  tft.fillRect(0, 222, 320, 18, COLOR_BG);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(4, 224);
  tft.print("ENC: ");
  tft.print(encoderPosition);

  tft.setCursor(120, 224);
  tft.print("BTN: ");
  tft.print(lastButtonState == LOW ? "PRESS" : "----");
}

// vykreslí jednu položku menu (jen daný řádek)
void drawMenuItem(int index, bool selected) {
  int startY = 40;
  int lineH  = 24;
  int y = startY + index * lineH;

  if (selected) {
    // highlight + glow
    tft.fillRoundRect(10, y - 2, 300, lineH, 8, COLOR_TOPBAR2);
    tft.setTextColor(COLOR_BG);
  } else {
    tft.fillRoundRect(10, y - 2, 300, lineH, 8, COLOR_BG);
    tft.drawRoundRect(10, y - 2, 300, lineH, 8, COLOR_TOPBAR2);
    tft.setTextColor(COLOR_TEXT);
  }

  tft.setTextSize(2);
  tft.setCursor(20, y + 2);
  tft.print(MENU_LABELS[index]);
}


// ========================
// MENU – kreslení
// ========================
void drawMenuScreen() {
  tft.fillScreen(COLOR_BG);

  // top bar znovu použijeme jako header menu
  drawTopBarGradient();
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(4, 6);
  tft.print("Menu");

  // položky menu
  for (int i = 0; i < MENU_ITEMS; i++) {
    drawMenuItem(i, i == menuIndex);
  }

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(4, 224);
  tft.print("Otacej pro vyber, stisk pro potvrzeni");
}

void updateMenuSelectionFromEncoder() {
  long diff = encoderPosition - menuEncStart;
  int newIndex = (int)diff;

  if (newIndex < 0) newIndex = 0;
  if (newIndex >= MENU_ITEMS) newIndex = MENU_ITEMS - 1;

  if (newIndex != menuIndex) {
    // přepni jen řádky, ne celé menu
    drawMenuItem(menuIndex, false);
    drawMenuItem(newIndex, true);
    menuIndex = newIndex;
  }
}

// ---- TARE menu (červené) ----

void drawTareMenuItem(int index, bool selected) {
  int startY = 40;
  int lineH  = 24;
  int y = startY + index * lineH;

  if (selected) {
    tft.fillRoundRect(10, y - 2, 300, lineH, 8, COLOR_MENU_TARE_ACCENT);
    tft.setTextColor(COLOR_BG);
  } else {
    tft.fillRoundRect(10, y - 2, 300, lineH, 8, COLOR_BG);
    tft.drawRoundRect(10, y - 2, 300, lineH, 8, COLOR_MENU_TARE_ACCENT);
    tft.setTextColor(COLOR_TEXT);
  }

  tft.setTextSize(2);
  tft.setCursor(20, y + 2);
  tft.print(MENU_TARE_LABELS[index]);
}

void drawTareMenuScreen() {
  tft.fillScreen(COLOR_BG);

  // top bar v červené
  tft.fillRect(0, 0, 320, 24, COLOR_MENU_TARE_ACCENT);
  tft.setTextColor(COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(4, 6);
  tft.print("Nadoba / miska");

  for (int i = 0; i < MENU_TARE_ITEMS; i++) {
    drawTareMenuItem(i, i == menuTareIndex);
  }

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(4, 224);
  tft.print("Otacej, stisk pro potvrzeni");
}

void updateTareMenuSelectionFromEncoder() {
  long diff = encoderPosition - menuEncStart;
  int newIndex = (int)diff;

  if (newIndex < 0) newIndex = 0;
  if (newIndex >= MENU_TARE_ITEMS) newIndex = MENU_TARE_ITEMS - 1;

  if (newIndex != menuTareIndex) {
    drawTareMenuItem(menuTareIndex, false);
    drawTareMenuItem(newIndex, true);
    menuTareIndex = newIndex;
  }
}

// ---- MENU2 (žluté) ----

void drawMenu2Item(int index, bool selected) {
  int startY = 40;
  int lineH  = 24;
  int y = startY + index * lineH;

  if (selected) {
    tft.fillRoundRect(10, y - 2, 300, lineH, 8, COLOR_MENU2_ACCENT);
    tft.setTextColor(COLOR_BG);
  } else {
    tft.fillRoundRect(10, y - 2, 300, lineH, 8, COLOR_BG);
    tft.drawRoundRect(10, y - 2, 300, lineH, 8, COLOR_MENU2_ACCENT);
    tft.setTextColor(COLOR_TEXT);
  }

  tft.setTextSize(2);
  tft.setCursor(20, y + 2);
  tft.print(MENU2_LABELS[index]);
}

void drawMenu2Screen() {
  tft.fillScreen(COLOR_BG);

  // top bar ve zluté
  tft.fillRect(0, 0, 320, 24, COLOR_MENU2_ACCENT);
  tft.setTextColor(COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(4, 6);
  tft.print("MENU2");

  for (int i = 0; i < MENU2_ITEMS; i++) {
    drawMenu2Item(i, i == menu2Index);
  }

  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(4, 224);
  tft.print("Otacej, stisk pro potvrzeni");
}

void updateMenu2SelectionFromEncoder() {
  long diff = encoderPosition - menuEncStart;
  int newIndex = (int)diff;

  if (newIndex < 0) newIndex = 0;
  if (newIndex >= MENU2_ITEMS) newIndex = MENU2_ITEMS - 1;

  if (newIndex != menu2Index) {
    drawMenu2Item(menu2Index, false);
    drawMenu2Item(newIndex, true);
    menu2Index = newIndex;
  }
}

// ========================
// TAR – zobrazení
// ========================
void drawTarMessage() {
  eraseWeightArea();

  tft.setTextSize(4);
  tft.setTextColor(COLOR_ACCENT);

  const char* txt = "TAR";
  int len = strlen(txt);
  int charW = 6 * 4; // 6 px * size4
  int totalW = len * charW;

  int boxX = 20;
  int boxW = 280;
  int x = boxX + (boxW - totalW) / 2;
  int y = 90;

  tft.setCursor(x, y);
  tft.print(txt);
}

// ========================
// HTTP handlery
// ========================
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleState() {
  updateWeightFromScale();
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

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

// /api_json – tvoje API
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
// Přepínání UI módů
// ========================
void enterHudMode() {
  uiMode = UI_HUD;
  drawStaticHUD();
  lastDrawnWeight = 999999.0f;
  lastWifiLevel   = -1;
  lastTimeStr     = "";
  lastDateStr     = "";
  tarActive       = false;
  tarDrawn        = false;
  hudEncStart     = encoderPosition;
  lastHudEncoderMoveMs = millis();
  hudEncLastPos        = encoderPosition;
}


void enterMenuMode() {
  uiMode = UI_MENU;
  menuEncStart = encoderPosition;
  menuIndex    = 0;
  drawMenuScreen();
}

void enterTareMenuMode() {
  uiMode = UI_MENU_TARE;
  menuEncStart  = encoderPosition;
  menuTareIndex = 0;
  drawTareMenuScreen();
}

void enterMenu2Mode() {
  uiMode = UI_MENU2;
  menuEncStart = encoderPosition;
  menu2Index   = 0;
  drawMenu2Screen();
}

void handleMenuSelection() {
  const char* sel = MENU_LABELS[menuIndex];

  if (strcmp(sel, "Kalibrace") == 0) {
    Serial.println("[MENU] Kalibrace (zatim nic nedelej)");
    // tady později uděláme wizard kalibrace
  } else if (strcmp(sel, "Resetovat WiFi") == 0) {
    Serial.println("[MENU] Resetovat WiFi (zatim nic nedelej)");
    // tady potom dáme WiFiManager reset
  } else if (strcmp(sel, "Zpet") == 0) {
    Serial.println("[MENU] Zpet -> HUD");
    enterHudMode();
    return;
  }

  // po potvrzení Kalibrace / Reset zatím zůstaneme v menu
}

void handleTareMenuSelection() {
  const char* sel = MENU_TARE_LABELS[menuTareIndex];

  if (strcmp(sel, "Zpet") == 0) {
    Serial.println("[MENU_TARE] Zpet -> HUD");
    enterHudMode();
    return;
  }

  Serial.print("[MENU_TARE] Vybrano: ");
  Serial.println(sel);
  // zatím nic nedělá – jen placeholder do budoucna
}


void handleMenu2Selection() {
  const char* sel = MENU2_LABELS[menu2Index];

  if (strcmp(sel, "Zpet") == 0) {
    Serial.println("[MENU2] Zpet -> HUD");
    enterHudMode();
    return;
  }

  Serial.print("[MENU2] Vybrano: ");
  Serial.println(sel);
  // zatím nic nedělá – jen placeholder
}


// ========================
// Setup
// ========================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Chytra vaha - WiFi portal + HUD + menu");

  // PINy enkoderu
  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(ENC_A,  INPUT_PULLUP);
  pinMode(ENC_B,  INPUT_PULLUP);
  lastEncA        = digitalRead(ENC_A);
  lastButtonState = digitalRead(ENC_SW);

  // LCD – ST7789 240x320
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 320);
  tft.setRotation(1);      // landscape 320x240
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, 10);
  tft.println("Startuji...");

  // HX711
  tft.setCursor(10, 30);
  tft.println("HX711 init");
  scale.begin(HX711_DOUT, HX711_SCK);
  scale.set_scale(1.0f); // bez kalibrace
  scale.tare();
  delay(200);

  // WiFi portal – konfig domácí WiFi
  tft.setCursor(10, 50);
  tft.println("WiFi portal...");
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalBlocking(true);
  bool res = wm.autoConnect("ChytraVaha-Setup");

  if (!res) {
    tft.setCursor(10, 70);
    tft.println("WiFi fail, reboot");
    delay(2000);
    ESP.restart();
  }

  tft.setCursor(10, 70);
  tft.print("WiFi OK: ");
  tft.println(WiFi.localIP());

  // NTP time
  tft.setCursor(10, 90);
  tft.println("NTP sync...");
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");

  // mDNS vaha.local
  if (MDNS.begin("vaha")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started: http://vaha.local");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  // Web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/item", HTTP_POST, handleItemPost);
  server.on("/api_json", HTTP_GET, handleApiJson);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");

  enterHudMode();
}

// ========================
// Loop
// ========================
void loop() {
  server.handleClient();

  updateEncoder();
  updateWeightFromScale();

  bool clicked = checkButtonClicked();
  bool longPress = false;
  if (buttonLongPressEvent) {
    longPress = true;
    buttonLongPressEvent = false;
  }

  if (uiMode == UI_HUD) {
    // dlouhý stisk v HUD -> TAR overlay
    if (longPress) {
      Serial.println("[BTN] Long press -> TAR");
      tarActive   = true;
      tarDrawn    = false;
      tarStartMs  = millis();
      // (zatím neděláme scale.tare(), jen vizuál)
    }

    // otoceni enkoderem v HUD -> menu TARE nebo MENU2 (jen když neběží TAR)
    // otoceni enkoderem v HUD -> menu TARE nebo MENU2 (jen když neběží TAR)
    if (!tarActive) {
      // sledování pohybu enkodéru a 2s timeout na „nasbírané“ kroky
      if (encoderPosition != hudEncLastPos) {
        hudEncLastPos        = encoderPosition;
        lastHudEncoderMoveMs = millis();
      } else {
        if (millis() - lastHudEncoderMoveMs > 2000) { // 2 s bez pohybu
          hudEncStart = encoderPosition;              // reset baseline
        }
      }

      long diff = encoderPosition - hudEncStart;
      if (diff >= 5) {
        Serial.println("[ENC] HUD -> MENU_TARE");
        enterTareMenuMode();
        return;  // nevolej v tomto kole HUD kreslení
      } else if (diff <= -5) {
        Serial.println("[ENC] HUD -> MENU2");
        enterMenu2Mode();
        return;
      }
    }


    // klik v HUD -> menu (ale jen pokud neběží TAR)
    if (clicked && !tarActive) {
      enterMenuMode();
      return;  // z tohoto průchodu loopu už NIC dalšího nekresli (zabráníme jednorázovému překreslení HUDu přes menu)
    }
  

    static unsigned long lastHudUpdate = 0;
    if (millis() - lastHudUpdate > 200) {
      // pokud je aktivní TAR, zobraz hlášku 3s místo váhy
      if (tarActive) {
        updateTopBarHUD();
        updateBottomHUD();

        if (!tarDrawn) {
          drawTarMessage();
          tarDrawn = true;
        }

        if (millis() - tarStartMs >= 3000) {
          tarActive = false;
          tarDrawn  = false;
          lastDrawnWeight = 999999.0f; // vynutíme překreslení váhy
          hudEncStart          = encoderPosition; // reset baseline pro otáčení v HUD
          lastHudEncoderMoveMs = millis();
          hudEncLastPos        = encoderPosition;
        }        
      } else {
        updateTopBarHUD();
        updateWeightHUD();
        updateBottomHUD();
      }

      lastHudUpdate = millis();
    }

  } else if (uiMode == UI_MENU) {
    // encoder posouvá položky
    updateMenuSelectionFromEncoder();

    // klik v menu -> potvrzení
    if (clicked) {
      handleMenuSelection();
    }
    // dlouhý stisk v menu zatím ignorujeme

  } else if (uiMode == UI_MENU_TARE) {
    updateTareMenuSelectionFromEncoder();

    if (clicked) {
      handleTareMenuSelection();
      // zatím v menu zůstáváme
    }

  } else if (uiMode == UI_MENU2) {
    updateMenu2SelectionFromEncoder();

    if (clicked) {
      handleMenu2Selection();
      // zatím v menu zůstáváme
    }
  }
}
