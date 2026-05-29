/*
 * BambuMonitor — Arduino UNO Q (STM32U585)
 *
 * 6 info screens, cycled with 2 buttons:
 *   D2 → previous screen
 *   D3 → next screen
 *
 * OLED I2C SH1106 128x64:
 *   VCC → 3.3V  GND → GND
 *   SCK → SCL (A5)   SDA → SDA (A4)
 *
 * Libraries: Arduino_RouterBridge, Arduino_LED_Matrix, U8g2, ArduinoJson
 */

#include <Arduino_RouterBridge.h>
#include <Arduino_LED_Matrix.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>

// ── OLED ──────────────────────────────────────────────────────────────────────
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ── LED Matrix ────────────────────────────────────────────────────────────────
ArduinoLEDMatrix matrix;
uint8_t frame[8 * 13];

// ── Buttons ───────────────────────────────────────────────────────────────────
const int BTN_PREV = 2;
const int BTN_NEXT = 3;
bool      btnPrevState = false, btnNextState = false;
unsigned long btnPrevMs = 0, btnNextMs = 0;
const unsigned long DEBOUNCE_MS = 50;

// ── Screens ───────────────────────────────────────────────────────────────────
enum Screen { SCR_PROGRESS, SCR_TEMPS, SCR_FANS, SCR_PRINTINFO, SCR_FILAMENT, SCR_SYSTEM };
const int SCR_COUNT = 6;
int currentScreen = SCR_PROGRESS;

// ── Print state ───────────────────────────────────────────────────────────────
struct State {
  int  pct  = 0;    // completion %
  int  rem  = 0;    // remaining minutes
  int  ln   = 0;    // current layer
  int  tln  = 0;    // total layers
  int  nt   = 0;    // nozzle temp actual
  int  ntt  = 0;    // nozzle temp target
  int  bt   = 0;    // bed temp actual
  int  btt  = 0;    // bed temp target
  int  ct   = 0;    // chamber temp
  int  cf   = 0;    // cooling fan %
  int  spd  = 100;  // print speed %
  int  ar   = 0;    // AMS filament remaining %
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

// ── Helpers ───────────────────────────────────────────────────────────────────
void fmtTime(char* buf, int mins) {
  int h = mins / 60, m = mins % 60;
  if (h > 0) snprintf(buf, 10, "%dh %02dm", h, m);
  else        snprintf(buf, 10, "%dm", m);
}

// ── Icons (11x11 px drawn with U8g2 primitives) ───────────────────────────────
void drawIcon(int x, int y, int scr) {
  switch (scr) {
    case SCR_PROGRESS:
      u8g2.drawBox(x,   y+7, 3, 4);
      u8g2.drawBox(x+4, y+4, 3, 7);
      u8g2.drawBox(x+8, y+1, 3, 10);
      u8g2.drawHLine(x, y+11, 11);
      break;
    case SCR_TEMPS:
      u8g2.drawBox(x+4, y+1, 3, 6);
      u8g2.drawCircle(x+5, y+9, 3, U8G2_DRAW_ALL);
      u8g2.drawBox(x+5, y+6, 1, 4);
      break;
    case SCR_FANS:
      u8g2.drawCircle(x+5, y+5, 5, U8G2_DRAW_ALL);
      u8g2.drawCircle(x+5, y+5, 1, U8G2_DRAW_ALL);
      u8g2.drawLine(x+5, y+1, x+5, y+4);
      u8g2.drawLine(x+5, y+6, x+5, y+9);
      u8g2.drawLine(x+1, y+5, x+4, y+5);
      u8g2.drawLine(x+6, y+5, x+9, y+5);
      break;
    case SCR_PRINTINFO:
      u8g2.drawFrame(x+1, y, 9, 11);
      u8g2.drawHLine(x+3, y+3, 5);
      u8g2.drawHLine(x+3, y+5, 5);
      u8g2.drawHLine(x+3, y+7, 5);
      break;
    case SCR_FILAMENT:
      u8g2.drawCircle(x+5, y+5, 5, U8G2_DRAW_ALL);
      u8g2.drawCircle(x+5, y+5, 2, U8G2_DRAW_ALL);
      u8g2.drawPixel(x+5, y+5);
      break;
    case SCR_SYSTEM:
      u8g2.drawCircle(x+5, y+5, 3, U8G2_DRAW_ALL);
      u8g2.drawBox(x+4, y,   3, 2);
      u8g2.drawBox(x+4, y+9, 3, 2);
      u8g2.drawBox(x,   y+4, 2, 3);
      u8g2.drawBox(x+9, y+4, 2, 3);
      break;
  }
}

// ── Page dots ─────────────────────────────────────────────────────────────────
void drawPageDots() {
  int startX = (128 - SCR_COUNT * 8 + 2) / 2;
  for (int i = 0; i < SCR_COUNT; i++) {
    int x = startX + i * 8;
    if (i == currentScreen) u8g2.drawBox(x, 61, 4, 3);
    else                    u8g2.drawFrame(x, 61, 4, 3);
  }
}

// ── Screen header ─────────────────────────────────────────────────────────────
void drawHeader(int scr, const char* title) {
  drawIcon(0, 0, scr);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(14, 10, title);
  u8g2.drawHLine(0, 12, 128);
}

// ── Screens ───────────────────────────────────────────────────────────────────
void drawProgress() {
  drawHeader(SCR_PROGRESS, "PROGRESS");
  u8g2.drawFrame(0, 15, 128, 7);
  int filled = (int)(128.0f * prn.pct / 100.0f);
  if (filled > 0) u8g2.drawBox(0, 15, filled, 7);

  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[24], t[10];
  snprintf(buf, sizeof(buf), "%d%%", prn.pct);
  u8g2.drawStr(106, 10, buf);

  fmtTime(t, prn.rem);
  snprintf(buf, sizeof(buf), "Rem    %s", t);
  u8g2.drawStr(0, 34, buf);

  fmtTime(t, calcElapsed());
  snprintf(buf, sizeof(buf), "Elap   %s", t);
  u8g2.drawStr(0, 44, buf);

  snprintf(buf, sizeof(buf), "Layer  %d / %d", prn.ln, prn.tln);
  u8g2.drawStr(0, 54, buf);
}

void drawTemps() {
  drawHeader(SCR_TEMPS, "TEMPERATURES");
  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[24];
  snprintf(buf, sizeof(buf), "Nozzle %3d/%3d \xb0""C", prn.nt, prn.ntt);
  u8g2.drawStr(0, 26, buf);
  snprintf(buf, sizeof(buf), "Bed    %3d/%3d \xb0""C", prn.bt, prn.btt);
  u8g2.drawStr(0, 38, buf);
  snprintf(buf, sizeof(buf), "Chamber   %3d \xb0""C", prn.ct);
  u8g2.drawStr(0, 50, buf);
}

void drawFans() {
  drawHeader(SCR_FANS, "FANS & SPEED");
  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[24];
  snprintf(buf, sizeof(buf), "Print    %3d%%", prn.spd); u8g2.drawStr(0, 32, buf);
  snprintf(buf, sizeof(buf), "Cooling  %3d%%", prn.cf);  u8g2.drawStr(0, 48, buf);
}

void drawPrintInfo() {
  drawHeader(SCR_PRINTINFO, "PRINT INFO");
  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[24], t[10];
  u8g2.drawStr(0, 26, prn.nm);

  fmtTime(t, calcElapsed());
  snprintf(buf, sizeof(buf), "Elapsed  %s", t);
  u8g2.drawStr(0, 38, buf);

  fmtTime(t, prn.rem + calcElapsed());
  snprintf(buf, sizeof(buf), "Total    %s", t);
  u8g2.drawStr(0, 50, buf);
}

void drawFilament() {
  drawHeader(SCR_FILAMENT, "FILAMENT");
  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[24];
  snprintf(buf, sizeof(buf), "Slot   %s", prn.as_);  u8g2.drawStr(0, 24, buf);
  snprintf(buf, sizeof(buf), "Type   %s", prn.at);   u8g2.drawStr(0, 34, buf);
  snprintf(buf, sizeof(buf), "Brand  %s", prn.ab);   u8g2.drawStr(0, 44, buf);
  snprintf(buf, sizeof(buf), "Rem    %d%%", prn.ar); u8g2.drawStr(0, 55, buf);
  u8g2.drawFrame(60, 49, 50, 5);
  int f = (int)(50.0f * prn.ar / 100.0f);
  if (f > 0) u8g2.drawBox(60, 49, f, 5);
}

void drawSystem() {
  drawHeader(SCR_SYSTEM, "SYSTEM");
  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[24];
  snprintf(buf, sizeof(buf), "State   %s", prn.st);   u8g2.drawStr(0, 26, buf);
  snprintf(buf, sizeof(buf), "Nozzle  %smm", prn.nd); u8g2.drawStr(0, 38, buf);
  snprintf(buf, sizeof(buf), "Speed   %d%%", prn.spd); u8g2.drawStr(0, 50, buf);
}

// ── OLED main ─────────────────────────────────────────────────────────────────
void drawOLED() {
  u8g2.clearBuffer();
  if (millis() - lastReceived > 10000) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(10, 30, "Connecting...");
    u8g2.drawStr(8, 44, "Check config.py");
    u8g2.sendBuffer();
    return;
  }
  switch (currentScreen) {
    case SCR_PROGRESS:  drawProgress();  break;
    case SCR_TEMPS:     drawTemps();     break;
    case SCR_FANS:      drawFans();      break;
    case SCR_PRINTINFO: drawPrintInfo(); break;
    case SCR_FILAMENT:  drawFilament();  break;
    case SCR_SYSTEM:    drawSystem();    break;
  }
  drawPageDots();
  u8g2.sendBuffer();
}

// ── LED Matrix ────────────────────────────────────────────────────────────────
int animStep = 0;
unsigned long animLastMs = 0;

void setPixel(int r, int c, uint8_t v) {
  if (r >= 0 && r < 8 && c >= 0 && c < 13) frame[r*13+c] = v;
}
void clearFrame() { memset(frame, 0, sizeof(frame)); }

void updateMatrix() {
  unsigned long now = millis();
  bool dirty = false;

  if (strcmp(prn.st, "RUNNING") == 0) {
    if (now - animLastMs > 120) {
      animLastMs = now; animStep = (animStep+1)%13;
      clearFrame();
      for (int r = 0; r < 8; r++) {
        setPixel(r, (animStep+r)%13, 255);
        setPixel(r, (animStep+r+1)%13, 120);
      }
      dirty = true;
    }
  } else if (strcmp(prn.st, "PAUSE") == 0) {
    static bool vis = true;
    if (now - animLastMs > 600) {
      animLastMs = now; vis = !vis;
      clearFrame();
      if (vis) for (int r=0;r<8;r++) { setPixel(r,2,255);setPixel(r,3,255);setPixel(r,9,255);setPixel(r,10,255); }
      dirty = true;
    }
  } else if (strcmp(prn.st, "FAILED") == 0) {
    static bool vis = true;
    if (now - animLastMs > 200) {
      animLastMs = now; vis = !vis;
      clearFrame();
      if (vis) for (int r=0;r<8;r++) { setPixel(r,(r*12)/7,255); setPixel(r,12-(r*12)/7,255); }
      dirty = true;
    }
  } else if (strcmp(prn.st, "FINISH") == 0) {
    if (now - animLastMs > 2000) {
      animLastMs = now; clearFrame();
      setPixel(7,0,255);setPixel(6,1,255);setPixel(5,2,255);
      setPixel(4,3,255);setPixel(3,4,255);setPixel(2,5,255);
      setPixel(1,6,255);setPixel(0,7,255);setPixel(0,8,255);
      setPixel(1,9,255);setPixel(2,10,255);setPixel(3,11,255);setPixel(4,12,255);
      dirty = true;
    }
  } else {
    if (now - animLastMs > 1000) { animLastMs = now; clearFrame(); dirty = true; }
  }
  if (dirty) matrix.draw(frame);
}

// ── Buttons ───────────────────────────────────────────────────────────────────
void checkButtons() {
  unsigned long now = millis();
  bool prev = (digitalRead(BTN_PREV) == LOW);
  if (prev && !btnPrevState && (now-btnPrevMs > DEBOUNCE_MS)) {
    btnPrevState = true; btnPrevMs = now;
    currentScreen = (currentScreen-1+SCR_COUNT)%SCR_COUNT;
  } else if (!prev) btnPrevState = false;

  bool next = (digitalRead(BTN_NEXT) == LOW);
  if (next && !btnNextState && (now-btnNextMs > DEBOUNCE_MS)) {
    btnNextState = true; btnNextMs = now;
    currentScreen = (currentScreen+1)%SCR_COUNT;
  } else if (!next) btnNextState = false;
}

// ── Setup & Loop ──────────────────────────────────────────────────────────────
void setup() {
  Bridge.begin();
  Bridge.provide("upd1", handleUpd1);
  Bridge.provide("upd2", handleUpd2);

  matrix.begin();
  matrix.setGrayscaleBits(8);

  u8g2.begin();
  u8g2.setContrast(200);

  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_8x13_tf);
  u8g2.drawStr(10, 28, "BambuMonitor");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(20, 44, "Connecting...");
  u8g2.sendBuffer();
}

unsigned long lastOledMs = 0;

void loop() {
  Bridge.update();
  checkButtons();
  updateMatrix();
  if (millis() - lastOledMs > 100) {
    lastOledMs = millis();
    drawOLED();
  }
}
