/*
 * BambuMonitor — Arduino UNO Q (STM32U585)
 * GC9A01 240×240 Round TFT version
 *
 * Wiring (all 3.3V — no level shifter needed):
 *   VCC → 3.3V    GND → GND
 *   SCL → D13     SDA → D11
 *   CS  → D10     DC  → D9
 *   RST → D8      BLK → D7  (backlight)
 *
 * Buttons (pin → GND, INPUT_PULLUP):
 *   D2 → previous screen
 *   D3 → next screen
 *
 * Libraries: Arduino_RouterBridge (bundled), Arduino_LED_Matrix (bundled),
 *            Adafruit_GC9A01A, Adafruit_GFX, ArduinoJson 6
 */

#include <Arduino_RouterBridge.h>
#include <Arduino_LED_Matrix.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <ArduinoJson.h>

// ── TFT ───────────────────────────────────────────────────────────────────────
#define TFT_CS  10
#define TFT_DC   9
#define TFT_RST  8
#define TFT_BL   7
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

// ── Colors ────────────────────────────────────────────────────────────────────
#define COL_BG      0x0000
#define COL_WHITE   0xFFFF
#define COL_LGRAY   0xC618
#define COL_DGRAY   0x2945
#define COL_GREEN   0x07E0
#define COL_DKGREEN 0x0280
#define COL_ORANGE  0xFC40
#define COL_RED     0xF800
#define COL_YELLOW  0xFFE0
#define COL_CYAN    0x07FF
#define COL_BLUE    0x325F

// ── LED Matrix ────────────────────────────────────────────────────────────────
ArduinoLEDMatrix matrix;
uint8_t frame[8 * 13];

// ── Buttons ───────────────────────────────────────────────────────────────────
const int BTN_PREV = 2;
const int BTN_NEXT = 3;
bool btnPrevState = false, btnNextState = false;
unsigned long btnPrevMs = 0, btnNextMs = 0;
const unsigned long DEBOUNCE_MS = 50;

// ── Screens ───────────────────────────────────────────────────────────────────
enum Screen { SCR_PROGRESS, SCR_TEMPS, SCR_FANS, SCR_PRINTINFO, SCR_FILAMENT, SCR_SYSTEM };
const int SCR_COUNT = 6;
int  currentScreen  = SCR_PROGRESS;
int  prevScreen     = -1;   // forces full redraw on first draw

// ── Print state ───────────────────────────────────────────────────────────────
struct State {
  int  pct  = 0;
  int  rem  = 0;
  int  ln   = 0;
  int  tln  = 0;
  int  nt   = 0;
  int  ntt  = 0;
  int  bt   = 0;
  int  btt  = 0;
  int  ct   = 0;
  int  cf   = 0;
  int  spd  = 100;
  int  ar   = 0;
  char st[12] = "IDLE";
  char nm[11] = "";
  char nd[4]  = "0.4";
  char at[6]  = "--";
  char as_[4] = "--";
  char ab[9]  = "--";
} prn;

unsigned long lastReceived = 0;

int calcElapsed() {
  if (prn.pct <= 0 || prn.pct >= 100) return 0;
  return max(0, (int)(prn.rem / (1.0f - prn.pct / 100.0f) - prn.rem));
}

// ── Bridge handlers ───────────────────────────────────────────────────────────
void parseJson(const String& json) {
  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    lastReceived = millis();
    return;
  }
  prn.pct = doc["pct"] | prn.pct;
  prn.rem = doc["rem"] | prn.rem;
  prn.ln  = doc["ln"]  | prn.ln;
  prn.tln = doc["tln"] | prn.tln;
  prn.nt  = doc["nt"]  | prn.nt;
  prn.ntt = doc["ntt"] | prn.ntt;
  prn.bt  = doc["bt"]  | prn.bt;
  prn.btt = doc["btt"] | prn.btt;
  prn.ct  = doc["ct"]  | prn.ct;
  prn.cf  = doc["cf"]  | prn.cf;
  prn.spd = doc["spd"] | prn.spd;
  prn.ar  = doc["ar"]  | prn.ar;
  if (doc["st"].is<const char*>())  snprintf(prn.st,  sizeof(prn.st),  "%s", (const char*)doc["st"]);
  if (doc["nm"].is<const char*>())  snprintf(prn.nm,  sizeof(prn.nm),  "%s", (const char*)doc["nm"]);
  if (doc["nd"].is<const char*>())  snprintf(prn.nd,  sizeof(prn.nd),  "%s", (const char*)doc["nd"]);
  if (doc["at"].is<const char*>())  snprintf(prn.at,  sizeof(prn.at),  "%s", (const char*)doc["at"]);
  if (doc["as"].is<const char*>())  snprintf(prn.as_, sizeof(prn.as_), "%s", (const char*)doc["as"]);
  if (doc["ab"].is<const char*>())  snprintf(prn.ab,  sizeof(prn.ab),  "%s", (const char*)doc["ab"]);
  lastReceived = millis();
}
void handleUpd1(String json) { parseJson(json); }
void handleUpd2(String json) { parseJson(json); }

// ── Drawing helpers ───────────────────────────────────────────────────────────
// Print centered text at given y, return actual y after text
void centerText(const char* txt, int y, uint16_t color, const GFXfont* font = nullptr) {
  tft.setFont(font);
  tft.setTextColor(color, COL_BG);
  tft.setTextSize(1);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  tft.setCursor((240 - w) / 2, y);
  tft.print(txt);
}

// Clear a horizontal band
void clearBand(int y, int h) {
  tft.fillRect(0, y, 240, h, COL_BG);
}

// Format time
void fmtTime(char* buf, int mins) {
  int h = mins / 60, m = mins % 60;
  if (h > 0) snprintf(buf, 10, "%dh %02dm", h, m);
  else        snprintf(buf, 10, "%dm", m);
}

// Draw arc ring (progress indicator) — center cx,cy, radius r, thickness thick
// startAngle=-90 (top), progress 0-100
void drawProgressRing(int cx, int cy, int r, int thick, int pct) {
  // Background ring (dark green)
  for (int t = 0; t < thick; t++) {
    tft.drawCircle(cx, cy, r - t, COL_DKGREEN);
  }
  // Foreground arc
  float endDeg = -90.0f + 360.0f * pct / 100.0f;
  uint16_t col = pct < 30 ? COL_CYAN : pct < 70 ? COL_GREEN : COL_ORANGE;
  for (float a = -90.0f; a <= endDeg; a += 1.2f) {
    float rad = a * DEG_TO_RAD;
    for (int t = 0; t < thick; t++) {
      int px = cx + (r - t) * cosf(rad);
      int py = cy + (r - t) * sinf(rad);
      tft.drawPixel(px, py, col);
    }
  }
}

// Horizontal bar (x, y, total_w, h, percent, fg, bg)
void drawBar(int x, int y, int w, int h, int pct, uint16_t fg, uint16_t bg) {
  tft.fillRect(x, y, w, h, bg);
  int filled = (int)(w * pct / 100.0f);
  if (filled > 0) tft.fillRect(x, y, filled, h, fg);
  tft.drawRect(x, y, w, h, COL_LGRAY);
}

// ── Page dots ─────────────────────────────────────────────────────────────────
void drawPageDots() {
  int dotR = 4, spacing = 14;
  int totalW = (SCR_COUNT - 1) * spacing;
  int startX = (240 - totalW) / 2;
  for (int i = 0; i < SCR_COUNT; i++) {
    int x = startX + i * spacing;
    if (i == currentScreen)
      tft.fillCircle(x, 218, dotR, COL_WHITE);
    else
      tft.drawCircle(x, 218, dotR, COL_DGRAY);
  }
}

// ── State color ───────────────────────────────────────────────────────────────
uint16_t stateColor() {
  if (strcmp(prn.st, "RUNNING") == 0) return COL_GREEN;
  if (strcmp(prn.st, "PAUSE")   == 0) return COL_YELLOW;
  if (strcmp(prn.st, "FAILED")  == 0) return COL_RED;
  if (strcmp(prn.st, "FINISH")  == 0) return COL_CYAN;
  return COL_LGRAY;
}

// ── Screens ───────────────────────────────────────────────────────────────────
void drawProgress() {
  // Progress ring
  drawProgressRing(120, 108, 105, 11, prn.pct);

  // Large percentage in center
  char pctStr[6]; snprintf(pctStr, sizeof(pctStr), "%d%%", prn.pct);
  clearBand(78, 36);
  centerText(pctStr, 110, COL_WHITE, &FreeSansBold18pt7b);

  // Status pill below %
  clearBand(116, 16);
  centerText(prn.st, 128, stateColor(), &FreeSans9pt7b);

  // Data rows
  char buf[24], t[10];
  clearBand(138, 60);

  fmtTime(t, prn.rem);
  snprintf(buf, sizeof(buf), "Rem  %s", t);
  centerText(buf, 152, COL_LGRAY, &FreeSans9pt7b);

  fmtTime(t, calcElapsed());
  snprintf(buf, sizeof(buf), "Elap %s", t);
  centerText(buf, 170, COL_LGRAY, &FreeSans9pt7b);

  snprintf(buf, sizeof(buf), "Lay %d / %d", prn.ln, prn.tln);
  centerText(buf, 188, COL_DGRAY, &FreeSans9pt7b);
}

void drawTemps() {
  centerText("TEMPERATURES", 30, COL_WHITE, &FreeSans9pt7b);
  tft.drawFastHLine(40, 38, 160, COL_DGRAY);

  char buf[24];
  // Nozzle — orange
  snprintf(buf, sizeof(buf), "Nozzle");
  centerText(buf, 70, COL_LGRAY, &FreeSans9pt7b);
  snprintf(buf, sizeof(buf), "%d / %d \xb0""C", prn.nt, prn.ntt);
  centerText(buf, 92, COL_ORANGE, &FreeSansBold12pt7b);

  // Bed — cyan
  snprintf(buf, sizeof(buf), "Bed");
  centerText(buf, 118, COL_LGRAY, &FreeSans9pt7b);
  snprintf(buf, sizeof(buf), "%d / %d \xb0""C", prn.bt, prn.btt);
  centerText(buf, 140, COL_CYAN, &FreeSansBold12pt7b);

  // Chamber — light gray
  snprintf(buf, sizeof(buf), "Chamber");
  centerText(buf, 166, COL_LGRAY, &FreeSans9pt7b);
  snprintf(buf, sizeof(buf), "%d \xb0""C", prn.ct);
  centerText(buf, 188, COL_WHITE, &FreeSansBold12pt7b);
}

void drawFans() {
  centerText("FANS & SPEED", 30, COL_WHITE, &FreeSans9pt7b);
  tft.drawFastHLine(40, 38, 160, COL_DGRAY);

  char buf[16];

  // Print speed
  centerText("Print speed", 68, COL_LGRAY, &FreeSans9pt7b);
  snprintf(buf, sizeof(buf), "%d%%", prn.spd);
  centerText(buf, 90, COL_WHITE, &FreeSansBold18pt7b);
  drawBar(55, 100, 130, 8, prn.spd, COL_BLUE, COL_DGRAY);

  // Cooling fan
  centerText("Cooling fan", 128, COL_LGRAY, &FreeSans9pt7b);
  snprintf(buf, sizeof(buf), "%d%%", prn.cf);
  centerText(buf, 150, COL_CYAN, &FreeSansBold18pt7b);
  drawBar(55, 160, 130, 8, prn.cf, COL_CYAN, COL_DGRAY);
}

void drawPrintInfo() {
  centerText("PRINT INFO", 30, COL_WHITE, &FreeSans9pt7b);
  tft.drawFastHLine(40, 38, 160, COL_DGRAY);

  // Job name
  tft.fillRect(10, 50, 220, 30, COL_BG);
  centerText(prn.nm, 74, COL_YELLOW, &FreeSansBold12pt7b);

  char buf[24], t[10];

  fmtTime(t, calcElapsed());
  snprintf(buf, sizeof(buf), "Elapsed  %s", t);
  centerText(buf, 112, COL_LGRAY, &FreeSans9pt7b);

  fmtTime(t, prn.rem + calcElapsed());
  snprintf(buf, sizeof(buf), "Total    %s", t);
  centerText(buf, 132, COL_LGRAY, &FreeSans9pt7b);

  fmtTime(t, prn.rem);
  snprintf(buf, sizeof(buf), "Remain   %s", t);
  centerText(buf, 152, COL_GREEN, &FreeSans9pt7b);

  snprintf(buf, sizeof(buf), "Layer  %d / %d", prn.ln, prn.tln);
  centerText(buf, 172, COL_LGRAY, &FreeSans9pt7b);
}

void drawFilament() {
  centerText("FILAMENT", 30, COL_WHITE, &FreeSans9pt7b);
  tft.drawFastHLine(40, 38, 160, COL_DGRAY);

  char buf[24];

  snprintf(buf, sizeof(buf), "Slot  %s", prn.as_);
  centerText(buf, 72, COL_LGRAY, &FreeSans9pt7b);

  centerText(prn.at, 98, COL_WHITE, &FreeSansBold18pt7b);
  centerText(prn.ab, 122, COL_LGRAY, &FreeSans9pt7b);

  // Remaining bar + %
  snprintf(buf, sizeof(buf), "Rem  %d%%", prn.ar);
  centerText(buf, 148, COL_GREEN, &FreeSans9pt7b);
  drawBar(40, 155, 160, 10, prn.ar, COL_GREEN, COL_DGRAY);
}

void drawSystem() {
  centerText("SYSTEM", 30, COL_WHITE, &FreeSans9pt7b);
  tft.drawFastHLine(40, 38, 160, COL_DGRAY);

  // State — large colored
  centerText(prn.st, 88, stateColor(), &FreeSansBold18pt7b);

  char buf[24];
  snprintf(buf, sizeof(buf), "Nozzle  %smm", prn.nd);
  centerText(buf, 126, COL_LGRAY, &FreeSans9pt7b);

  snprintf(buf, sizeof(buf), "Speed   %d%%", prn.spd);
  centerText(buf, 148, COL_LGRAY, &FreeSans9pt7b);

  snprintf(buf, sizeof(buf), "Layer  %d / %d", prn.ln, prn.tln);
  centerText(buf, 170, COL_DGRAY, &FreeSans9pt7b);
}

// ── TFT main draw ─────────────────────────────────────────────────────────────
void drawTFT() {
  bool connected = (millis() - lastReceived) < 10000;
  bool screenChanged = (currentScreen != prevScreen);

  if (!connected) {
    if (screenChanged) {
      tft.fillScreen(COL_BG);
      prevScreen = currentScreen;
    }
    clearBand(90, 60);
    centerText("Connecting...", 112, COL_LGRAY, &FreeSans9pt7b);
    centerText("Check config.py", 132, COL_DGRAY, &FreeSans9pt7b);
    return;
  }

  if (screenChanged) {
    tft.fillScreen(COL_BG);
    prevScreen = currentScreen;
    drawPageDots();
  }

  switch (currentScreen) {
    case SCR_PROGRESS:  drawProgress();  break;
    case SCR_TEMPS:     drawTemps();     break;
    case SCR_FANS:      drawFans();      break;
    case SCR_PRINTINFO: drawPrintInfo(); break;
    case SCR_FILAMENT:  drawFilament();  break;
    case SCR_SYSTEM:    drawSystem();    break;
  }

  // Redraw dots only when screen changed (already done above)
  // but keep them visible by not overwriting
}

// ── LED Matrix ────────────────────────────────────────────────────────────────
int animStep = 0;
unsigned long animLastMs = 0;

void setPixel(int r, int c, uint8_t v) {
  if (r>=0&&r<8&&c>=0&&c<13) frame[r*13+c]=v;
}
void clearFrame() { memset(frame,0,sizeof(frame)); }

void updateMatrix() {
  unsigned long now = millis();
  bool dirty = false;
  if (strcmp(prn.st,"RUNNING")==0) {
    if (now-animLastMs>120) {
      animLastMs=now; animStep=(animStep+1)%13; clearFrame();
      for(int r=0;r<8;r++){setPixel(r,(animStep+r)%13,255);setPixel(r,(animStep+r+1)%13,120);}
      dirty=true;
    }
  } else if (strcmp(prn.st,"PAUSE")==0) {
    static bool vis=true;
    if(now-animLastMs>600){animLastMs=now;vis=!vis;clearFrame();
      if(vis)for(int r=0;r<8;r++){setPixel(r,2,255);setPixel(r,3,255);setPixel(r,9,255);setPixel(r,10,255);}
      dirty=true;}
  } else if (strcmp(prn.st,"FAILED")==0) {
    static bool vis=true;
    if(now-animLastMs>200){animLastMs=now;vis=!vis;clearFrame();
      if(vis)for(int r=0;r<8;r++){setPixel(r,(r*12)/7,255);setPixel(r,12-(r*12)/7,255);}
      dirty=true;}
  } else if (strcmp(prn.st,"FINISH")==0) {
    if(now-animLastMs>2000){animLastMs=now;clearFrame();
      setPixel(7,0,255);setPixel(6,1,255);setPixel(5,2,255);
      setPixel(4,3,255);setPixel(3,4,255);setPixel(2,5,255);
      setPixel(1,6,255);setPixel(0,7,255);setPixel(0,8,255);
      setPixel(1,9,255);setPixel(2,10,255);setPixel(3,11,255);setPixel(4,12,255);
      dirty=true;}
  } else {
    if(now-animLastMs>1000){animLastMs=now;clearFrame();dirty=true;}
  }
  if(dirty) matrix.draw(frame);
}

// ── Buttons ───────────────────────────────────────────────────────────────────
void checkButtons() {
  unsigned long now = millis();
  bool prev = (digitalRead(BTN_PREV)==LOW);
  if(prev&&!btnPrevState&&(now-btnPrevMs>DEBOUNCE_MS)){
    btnPrevState=true;btnPrevMs=now;
    currentScreen=(currentScreen-1+SCR_COUNT)%SCR_COUNT;
  } else if(!prev) btnPrevState=false;
  bool next = (digitalRead(BTN_NEXT)==LOW);
  if(next&&!btnNextState&&(now-btnNextMs>DEBOUNCE_MS)){
    btnNextState=true;btnNextMs=now;
    currentScreen=(currentScreen+1)%SCR_COUNT;
  } else if(!next) btnNextState=false;
}

// ── Setup & Loop ──────────────────────────────────────────────────────────────
void setup() {
  Bridge.begin();
  Bridge.provide("upd1", handleUpd1);
  Bridge.provide("upd2", handleUpd2);

  matrix.begin();
  matrix.setGrayscaleBits(8);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(COL_BG);

  // Splash
  centerText("BambuMonitor", 104, COL_GREEN, &FreeSansBold12pt7b);
  centerText("Connecting...", 132, COL_DGRAY, &FreeSans9pt7b);

  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
}

unsigned long lastTFTms = 0;

void loop() {
  Bridge.update();
  checkButtons();
  updateMatrix();
  if (millis() - lastTFTms > 500) {  // 2Hz refresh (TFT is larger than OLED)
    lastTFTms = millis();
    drawTFT();
  }
}
