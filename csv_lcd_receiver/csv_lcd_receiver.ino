/*
  csv_lcd_receiver.ino
  ------------------------------------------------------------------
  csv_lcd_sender.html 에서 Web Serial로 보내는 패킷을 받아
  ESP32 + ST7789 TFT에 표시하는 스케치입니다.

    W2|날짜|지역|날씨상태|기온|습도|강수량|풍속|체감|주의   → 일반 모드(날씨 카드)
    G2|타입|제목|X축명|개수|라벨목록|값목록               → 그래프 모드
    F1|열개수|행개수|헤더목록|셀목록(행 우선)              → 자유 모드(실전 데이터 표, 최대 5열x10행)

  응답: OK / ERR|FORMAT / ERR|TOO_LONG

  배선은 prac4.ino와 완전히 동일합니다 (변경 없음):
    TFT_MOSI 23 / TFT_MISO 19 / TFT_SCLK 14
    TFT_CS   33 / TFT_DC   13 / TFT_RST  25
    VSPI(mySPI) + Adafruit_ST7789 사용

  필요 라이브러리: Adafruit GFX Library, Adafruit ST7735 and ST7789 Library
  ------------------------------------------------------------------
*/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ===================== 배선 (prac4.ino 그대로, 수정 금지) =====================
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_SCLK 14
#define TFT_CS   33
#define TFT_DC   13
#define TFT_RST  25

SPIClass mySPI(VSPI);
Adafruit_ST7789 tft = Adafruit_ST7789(&mySPI, TFT_CS, TFT_DC, TFT_RST);

// ===================== 시리얼 프로토콜 설정 (csv_lcd_sender.html과 동일) =====================
#define BAUD_RATE    115200
#define LINE_MAX_LEN 800     // 이 길이를 넘는 한 줄은 ERR|TOO_LONG 처리
#define MAX_POINTS   24      // 그래프 데이터 최대 개수 (HTML의 최대 포인트 수와 동일)
#define MAX_FREE_COLS  5     // 자유 모드 최대 열 수 (HTML과 동일)
#define MAX_FREE_ROWS  10    // 자유 모드 최대 행 수 (HTML과 동일)
#define MAX_FREE_CELLS (MAX_FREE_COLS * MAX_FREE_ROWS)

// ===================== RGB565 색상 팔레트 =====================
// 기준 이미지에 맞춘 다크 네이비 + 청록 포인트 색상
#define TFT_BLACK      0x0000
#define TFT_BLUE       0x001F
#define TFT_RED        0xFA08
#define TFT_CYAN       0x1F1D
#define TFT_YELLOW     0xFEE7
#define TFT_WHITE      0xE77E
#define TFT_ORANGE     0xFCC3
#define TFT_DARKGREY   0x2A09
#define TFT_LIGHTGREY  0x6D78

#define COL_BG      0x0042   // 거의 검정에 가까운 남색 배경
#define COL_CARD    0x10E5   // 상단 헤더의 짙은 네이비
#define COL_TEXT    TFT_WHITE
#define COL_SUB     TFT_LIGHTGREY
#define COL_WARN    TFT_RED
#define COL_CAUTION TFT_ORANGE
#define COL_WATCH   TFT_YELLOW
#define COL_INFO    TFT_CYAN // WEATHER 상태 바의 밝은 청록색
#define COL_LINE    TFT_DARKGREY

// ===================== 화면 크기 (setRotation 이후 실제 값으로 갱신) =====================
int16_t SCR_W = 240;
int16_t SCR_H = 320;

String inputLine;
bool lineOverflow = false;

// ------------------------------------------------------------------
// setup / loop
// ------------------------------------------------------------------
void setup() {
  Serial.setRxBufferSize(1024);   // begin() 전에 호출해야 적용됨
  Serial.begin(BAUD_RATE);
  inputLine.reserve(LINE_MAX_LEN + 16);

  mySPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.init(240, 320);  // ST7789 실제 패널 해상도
  tft.setRotation(3);   // 가로 320x240. 반대 방향이면 3으로 변경

  // 이 패널은 기본 ST7789 반전 설정을 그대로 쓰면
  // 검정이 흰색으로 표시되는 경우가 있어 반전을 해제합니다.
  tft.invertDisplay(false);

  SCR_W = tft.width();
  SCR_H = tft.height();
  tft.setTextWrap(false);

  showIdleScreen();
  Serial.print("LCD_SIZE|");
  Serial.print(tft.width());
  Serial.print("|");
  Serial.println(tft.height());
  Serial.println("READY|CSV_LCD_RECEIVER");
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n') {
      if (lineOverflow) {
        Serial.println("ERR|TOO_LONG");
        lineOverflow = false;
      } else {
        handleLine(inputLine);
      }
      inputLine = "";
    } else if (c != '\r') {
      if (inputLine.length() < LINE_MAX_LEN) {
        inputLine += c;
      } else {
        lineOverflow = true;   // 넘친 뒤 나머지는 줄바꿈까지 버림
      }
    }
  }
}

// ------------------------------------------------------------------
// 패킷 파싱 / 분기
// ------------------------------------------------------------------
void handleLine(const String& line) {
  if (line.length() == 0) return;

  int sep = line.indexOf('|');
  String type = (sep == -1) ? line : line.substring(0, sep);

  if (type == "W2") {
    handleWeatherPacket(line);
  } else if (type == "G2") {
    handleGraphPacket(line);
  } else if (type == "F1") {
    handleFreePacket(line);
  } else {
    Serial.println("ERR|FORMAT");
  }
}

// F1|열개수|행개수|헤더1,헤더2,...|셀1,셀2,...(행 우선 순서, 열개수x행개수개)
void handleFreePacket(const String& line) {
  String f[5];
  int n = splitFields(line, f, 5);
  if (n != 5) { Serial.println("ERR|FORMAT"); return; }

  int C = f[1].toInt();
  int R = f[2].toInt();
  if (C <= 0 || C > MAX_FREE_COLS || R <= 0 || R > MAX_FREE_ROWS) {
    Serial.println("ERR|FORMAT");
    return;
  }

  String hdrs[MAX_FREE_COLS];
  int hCount = splitByComma(f[3], hdrs, MAX_FREE_COLS);
  if (hCount != C) { Serial.println("ERR|FORMAT"); return; }

  String cells[MAX_FREE_CELLS];
  int cCount = splitByComma(f[4], cells, MAX_FREE_CELLS);
  if (cCount != C * R) { Serial.println("ERR|FORMAT"); return; }

  drawFreeTable(hdrs, cells, C, R);
  Serial.println("OK");
}

// W2|날짜|지역|날씨상태|기온|습도|강수량|풍속|체감|주의  (필드 10개)
void handleWeatherPacket(const String& line) {
  String f[10];
  int n = splitFields(line, f, 10);
  if (n != 10) {
    Serial.println("ERR|FORMAT");
    return;
  }

  String date   = f[1];
  String area   = f[2];
  String cond   = f[3];
  float  temp   = f[4].toFloat();
  float  hum    = f[5].toFloat();
  float  rain   = f[6].toFloat();
  float  wind   = f[7].toFloat();
  float  feel   = f[8].toFloat();
  String notice = f[9];

  drawWeatherCard(date, area, cond, temp, hum, rain, wind, feel, notice);
  Serial.println("OK");
}

// G2|타입|제목|X축명|개수|라벨목록|값목록  (필드 7개, 라벨/값은 콤마로 구분)
void handleGraphPacket(const String& line) {
  String f[7];
  int n = splitFields(line, f, 7);
  if (n != 7) {
    Serial.println("ERR|FORMAT");
    return;
  }

  String type   = f[1];
  String title  = f[2];
  String xTitle = f[3];
  int    count  = f[4].toInt();

  if (type != "LINE" && type != "BAR" && type != "HBAR" && type != "SUMMARY") {
    Serial.println("ERR|FORMAT");
    return;
  }

  String labels[MAX_POINTS];
  float  values[MAX_POINTS];
  int labelN = splitByComma(f[5], labels, MAX_POINTS);
  int valueN = splitFloatsByComma(f[6], values, MAX_POINTS);

  int plotN = min(count, min(labelN, valueN));
  if (plotN <= 0) {
    Serial.println("ERR|FORMAT");
    return;
  }

  drawGraph(type, title, xTitle, labels, values, plotN);
  Serial.println("OK");
}

// ------------------------------------------------------------------
// 문자열 분리 헬퍼
// ------------------------------------------------------------------
int splitFields(const String& s, String* out, int maxOut) {
  int count = 0, start = 0;
  while (count < maxOut) {
    int idx = s.indexOf('|', start);
    if (idx == -1) { out[count++] = s.substring(start); break; }
    out[count++] = s.substring(start, idx);
    start = idx + 1;
  }
  return count;
}

int splitByComma(const String& s, String* out, int maxOut) {
  int count = 0, start = 0;
  int len = s.length();
  while (count < maxOut && start <= len) {
    int idx = s.indexOf(',', start);
    if (idx == -1) { out[count++] = s.substring(start); break; }
    out[count++] = s.substring(start, idx);
    start = idx + 1;
  }
  return count;
}

int splitFloatsByComma(const String& s, float* out, int maxOut) {
  String tmp[MAX_POINTS];
  int n = splitByComma(s, tmp, maxOut);
  for (int i = 0; i < n; i++) out[i] = tmp[i].toFloat();
  return n;
}

String shortenLabel(const String& s, int maxLen) {
  if ((int)s.length() <= maxLen) return s;
  return s.substring(0, maxLen);
}

// LCD 기본 폰트는 ASCII만 지원합니다. UTF-8 한글(멀티바이트)이 들어오면
// 글자 하나당 물음표(?) 하나로 안전하게 치환합니다(바이트마다 ?가 찍히는 것 방지).
String asciiOnly(const String& s) {
  String out;
  out.reserve(s.length());
  size_t i = 0;
  while (i < s.length()) {
    uint8_t c = (uint8_t)s[i];
    if (c >= 0x20 && c <= 0x7E) { out += (char)c; i += 1; }
    else if (c >= 0xF0) { out += '?'; i += 4; }
    else if (c >= 0xE0) { out += '?'; i += 3; }
    else if (c >= 0xC0) { out += '?'; i += 2; }
    else { i += 1; }
  }
  return out;
}

// ------------------------------------------------------------------
// 대기 화면
// ------------------------------------------------------------------
void showIdleScreen() {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, SCR_W, 34, COL_CARD);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 9);
  tft.print("Weather LCD");

  tft.setTextColor(COL_SUB);
  tft.setTextSize(1);
  tft.setCursor(10, 70);
  tft.print("Waiting for data");
  tft.setCursor(10, 86);
  tft.print("(Serial 115200)");

  tft.setCursor(10, 130);
  tft.print("Format:");
  tft.setCursor(10, 150);
  tft.print("W2 | weather");
  tft.setCursor(10, 168);
  tft.print("G2 | graph");
  tft.setCursor(10, 186);
  tft.print("F1 | free");
}

// ------------------------------------------------------------------
// 일반 모드: 날씨 카드
// ------------------------------------------------------------------
void drawWeatherCard(String date, String area, String cond, float temp,
                      float hum, float rain, float wind, float feel, String notice) {
  tft.fillScreen(COL_BG);

  // ------------------------------------------------------------
  // 1. 상단 제목 바: 지역은 왼쪽, 날짜는 오른쪽
  // ------------------------------------------------------------
  const int headerH = 34;
  const int statusH = 22;

  tft.fillRect(0, 0, SCR_W, headerH, COL_CARD);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);

  String safeArea = shortenLabel(asciiOnly(area), 15);
  String safeDate = shortenLabel(asciiOnly(date), 10);

  tft.setCursor(10, 9);
  tft.print(safeArea);

  int dateW = safeDate.length() * 12;  // 기본 폰트 textSize 2 기준
  tft.setCursor(SCR_W - 10 - dateW, 9);
  tft.print(safeDate);

  // ------------------------------------------------------------
  // 2. 상태 바
  // ------------------------------------------------------------
  uint16_t noticeColor = COL_INFO;
  if (notice == "WARNING")      noticeColor = COL_WARN;
  else if (notice == "CAUTION") noticeColor = COL_CAUTION;
  else if (notice == "WATCH")   noticeColor = COL_WATCH;

  tft.fillRect(0, headerH, SCR_W, statusH, noticeColor);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, headerH + 4);
  tft.print(shortenLabel(asciiOnly(notice), 24));

  // ------------------------------------------------------------
  // 3. 좌측 데이터 영역: 원하는 시안과 동일한 2열 배치
  // ------------------------------------------------------------
  const int colL = 18;
  const int colR = 120;

  const int row1 = 70;
  const int row2 = 116;
  const int row3 = 170;

  drawMetric(colL, row1, "TEMP", String(temp, 1) + "C");
  drawMetric(colR, row1, "FEEL", String(feel, 1) + "C");

  drawMetric(colL, row2, "HUM",  String((int)hum) + "%");
  drawMetric(colR, row2, "RAIN", String(rain, 1) + "mm");

  drawMetric(colL, row3, "WIND", String(wind, 1) + "m/s");

  // ------------------------------------------------------------
  // 4. 우측 날씨 아이콘
  // ------------------------------------------------------------
  const int iconX = SCR_W - 70;
  const int iconY = 130;
  const int iconR = 34;

  drawWeatherIcon(iconX, iconY, iconR, cond);
}
void drawMetric(int x, int y, const String& label, const String& value) {
  tft.setTextColor(COL_SUB);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(label);

  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(x, y + 12);
  tft.print(value);
}

// ------------------------------------------------------------------
// 날씨 아이콘 (비트맵 없이 벡터 도형으로 그림)
// ------------------------------------------------------------------
void drawSunRays(int cx, int cy, int rIn, int rOut, uint16_t color) {
  static const float ux[8] = {0, 0.7071, 1, 0.7071, 0, -0.7071, -1, -0.7071};
  static const float uy[8] = {-1, -0.7071, 0, 0.7071, 1, 0.7071, 0, -0.7071};
  for (int i = 0; i < 8; i++) {
    int x1 = cx + (int)(ux[i] * rIn),  y1 = cy + (int)(uy[i] * rIn);
    int x2 = cx + (int)(ux[i] * rOut), y2 = cy + (int)(uy[i] * rOut);
    tft.drawLine(x1, y1, x2, y2, color);
  }
}

void drawCloudShape(int cx, int cy, int r, uint16_t color) {
  tft.fillCircle(cx - (int)(r * 0.3),  cy + (int)(r * 0.1),  (int)(r * 0.35), color);
  tft.fillCircle(cx + (int)(r * 0.15), cy - (int)(r * 0.05), (int)(r * 0.45), color);
  tft.fillCircle(cx + (int)(r * 0.5),  cy + (int)(r * 0.15), (int)(r * 0.3),  color);
  tft.fillRect(cx - (int)(r * 0.45), cy + (int)(r * 0.1), (int)(r * 1.0), (int)(r * 0.3), color);
}

void drawWeatherIcon(int cx, int cy, int r, const String& cond) {
  if (cond == "SUN") {
    tft.fillCircle(cx, cy, (int)(r * 0.5), TFT_YELLOW);
    drawSunRays(cx, cy, (int)(r * 0.65), r, TFT_YELLOW);

  } else if (cond == "HEAT") {
    tft.fillCircle(cx, cy, (int)(r * 0.55), TFT_RED);
    drawSunRays(cx, cy, (int)(r * 0.7), (int)(r * 1.05), TFT_ORANGE);

  } else if (cond == "CLOUD") {
    drawCloudShape(cx, cy, r, TFT_LIGHTGREY);

  } else if (cond == "RAIN") {
    drawCloudShape(cx, cy - (int)(r * 0.15), r, TFT_LIGHTGREY);
    for (int i = -2; i <= 2; i++) {
      int x = cx + i * (int)(r * 0.28);
      tft.drawLine(x, cy + (int)(r * 0.35), x - (int)(r * 0.12), cy + (int)(r * 0.75), TFT_BLUE);
    }

  } else if (cond == "STORM") {
    drawCloudShape(cx, cy - (int)(r * 0.15), r, TFT_DARKGREY);
    tft.fillTriangle(cx - 4, cy + (int)(r * 0.3), cx + 10, cy + (int)(r * 0.3), cx - 2, cy + (int)(r * 0.62), TFT_YELLOW);
    tft.fillTriangle(cx - 2, cy + (int)(r * 0.62), cx + 12, cy + (int)(r * 0.62), cx, cy + (int)(r * 0.95), TFT_YELLOW);

  } else if (cond == "SNOW") {
    drawCloudShape(cx, cy - (int)(r * 0.15), r, TFT_LIGHTGREY);
    for (int i = -2; i <= 2; i++) {
      int x = cx + i * (int)(r * 0.28);
      int y = cy + (int)(r * 0.55);
      tft.drawLine(x - 4, y, x + 4, y, TFT_WHITE);
      tft.drawLine(x, y - 4, x, y + 4, TFT_WHITE);
      tft.drawLine(x - 3, y - 3, x + 3, y + 3, TFT_WHITE);
      tft.drawLine(x - 3, y + 3, x + 3, y - 3, TFT_WHITE);
    }

  } else if (cond == "WIND") {
    for (int i = 0; i < 3; i++) {
      int y = cy - (int)(r * 0.4) + i * (int)(r * 0.4);
      tft.drawLine(cx - (int)(r * 0.7), y, cx + (int)(r * 0.5), y, COL_INFO);
      tft.drawLine(cx + (int)(r * 0.5), y, cx + (int)(r * 0.35), y - 6, COL_INFO);
      tft.drawLine(cx + (int)(r * 0.5), y, cx + (int)(r * 0.35), y + 6, COL_INFO);
    }

  } else {
    tft.drawCircle(cx, cy, (int)(r * 0.6), COL_SUB);
    tft.setTextColor(COL_SUB);
    tft.setTextSize(3);
    tft.setCursor(cx - 8, cy - 12);
    tft.print("?");
  }
}

// ------------------------------------------------------------------
// 그래프 모드
// ------------------------------------------------------------------
void drawGraph(const String& type, const String& title, const String& xTitle,
               String* labels, float* values, int n) {
  tft.fillScreen(COL_BG);

  tft.fillRect(0, 0, SCR_W, 30, COL_CARD);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 6);
  tft.print(title);

  tft.setTextColor(COL_SUB);
  tft.setTextSize(1);
  tft.setCursor(10, 22);
  tft.print("X:");
  tft.print(xTitle);
  tft.print("  N=");
  tft.print(n);

  float vMin = values[0], vMax = values[0];
  for (int i = 1; i < n; i++) {
    if (values[i] < vMin) vMin = values[i];
    if (values[i] > vMax) vMax = values[i];
  }
  if (vMax == vMin) { vMax += 1; vMin -= 1; }

  const int px = 12, py = 40, pw = SCR_W - 24, ph = SCR_H - py - 30;

  if (type == "HBAR") {
    drawHBar(labels, values, n, vMin, vMax, px, py, pw, ph);
  } else if (type == "LINE") {
    drawLineChart(labels, values, n, vMin, vMax, px, py, pw, ph);
  } else {
    drawBarChart(labels, values, n, vMin, vMax, px, py, pw, ph);
  }
}

void drawLineChart(String* labels, float* values, int n, float vMin, float vMax,
                    int px, int py, int pw, int ph) {
  tft.drawRect(px, py, pw, ph, COL_LINE);

  int prevX = px, prevY = py + ph;
  for (int i = 0; i < n; i++) {
    int x = px + (n <= 1 ? pw / 2 : (pw * i) / (n - 1));
    int y = py + ph - (int)(((values[i] - vMin) / (vMax - vMin)) * ph);
    if (i > 0) tft.drawLine(prevX, prevY, x, y, COL_INFO);
    tft.fillCircle(x, y, 3, COL_INFO);
    prevX = x; prevY = y;
  }
  drawAxisLabels(labels, n, px, py, pw, ph);
}

void drawBarChart(String* labels, float* values, int n, float vMin, float vMax,
                   int px, int py, int pw, int ph) {
  tft.drawRect(px, py, pw, ph, COL_LINE);

  float base = (vMin > 0) ? 0 : vMin;
  float range = vMax - base;
  if (range == 0) range = 1;

  int slot = pw / n;
  int barW = max(4, slot - 6);

  for (int i = 0; i < n; i++) {
    int barH = (int)(((values[i] - base) / range) * ph);
    barH = constrain(barH, 0, ph);
    int x = px + i * slot + (slot - barW) / 2;
    int y = py + ph - barH;
    tft.fillRect(x, y, barW, barH, COL_INFO);
  }
  drawAxisLabels(labels, n, px, py, pw, ph);
}

void drawHBar(String* labels, float* values, int n, float vMin, float vMax,
              int px, int py, int pw, int ph) {
  tft.drawRect(px, py, pw, ph, COL_LINE);

  float base = (vMin > 0) ? 0 : vMin;
  float range = vMax - base;
  if (range == 0) range = 1;

  int labelW   = 64;
  int barAreaX = px + labelW;
  int barAreaW = pw - labelW;
  int slot = ph / n;
  int barH = max(6, slot - 6);

  tft.setTextSize(1);
  for (int i = 0; i < n; i++) {
    int y = py + i * slot + (slot - barH) / 2;
    int barW = (int)(((values[i] - base) / range) * barAreaW);
    barW = constrain(barW, 0, barAreaW);

    tft.setTextColor(COL_SUB);
    tft.setCursor(px, y + 2);
    tft.print(shortenLabel(labels[i], 8));

    tft.fillRect(barAreaX, y, barW, barH, COL_INFO);
  }
}

void drawAxisLabels(String* labels, int n, int px, int py, int pw, int ph) {
  tft.setTextColor(COL_SUB);
  tft.setTextSize(1);
  tft.setCursor(px, py + ph + 5);
  tft.print(shortenLabel(labels[0], 9));

  if (n > 1) {
    String lastLbl = shortenLabel(labels[n - 1], 9);
    int textW = lastLbl.length() * 6;
    tft.setCursor(px + pw - textW, py + ph + 5);
    tft.print(lastLbl);
  }
}

// ------------------------------------------------------------------
// 자유 모드: 열/행을 그대로 표로 표시 (실전 공공데이터 실습용)
// ------------------------------------------------------------------
void drawFreeTable(String* hdrs, String* cells, int C, int R) {
  tft.fillScreen(COL_BG);

  tft.fillRect(0, 0, SCR_W, 26, COL_CARD);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 4);
  tft.print("FREE DATA");
  tft.setTextColor(COL_SUB);
  tft.setTextSize(1);
  tft.setCursor(SCR_W - 44, 9);
  tft.print(R); tft.print("x"); tft.print(C);

  int colW = SCR_W / C;
  int y0 = 30;
  int availH = SCR_H - y0;         // 표에 쓸 수 있는 세로 공간
  int rowH = availH / (R + 1);     // 헤더1 + 데이터R 행이 화면을 꽉 채우도록
  rowH = constrain(rowH, 14, 34);
  int textDy = (rowH - 8) / 2;     // 셀 안에서 글자를 세로 가운데로
  int charCap = max(3, colW / 6 - 1);

  // 헤더 행(강조 배경)
  tft.fillRect(0, y0, SCR_W, rowH, COL_LINE);
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(1);
  for (int c = 0; c < C; c++) {
    tft.setCursor(4 + c * colW, y0 + textDy);
    tft.print(shortenLabel(asciiOnly(hdrs[c]), charCap));
  }

  // 데이터 행 (한 줄 걸러 배경색으로 가독성 확보, 화면 밖으로 넘치면 중단)
  for (int r = 0; r < R; r++) {
    int y = y0 + rowH * (r + 1);
    if (y + rowH > SCR_H) break;
    if (r % 2 == 1) tft.fillRect(0, y, SCR_W, rowH, COL_CARD);
    tft.setTextColor(COL_TEXT);
    for (int c = 0; c < C; c++) {
      tft.setCursor(4 + c * colW, y + textDy);
      tft.print(shortenLabel(asciiOnly(cells[r * C + c]), charCap));
    }
  }
}