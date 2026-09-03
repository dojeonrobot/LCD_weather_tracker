/*
  csv_lcd_receiver.ino
  ------------------------------------------------------------------
  csv_lcd_sender.html 에서 Web Serial로 보내는 패킷을 받아
  ESP32 + ST7789 TFT에 표시하는 스케치입니다.
  그래프 화면에는 X축 이름·Y축 이름과 각 점/막대의 실제 값을 표시합니다.

    W2|날짜|지역|날씨상태|기온|습도|강수량|풍속|체감|주의   → 일반 모드(날씨 카드)
    G2|타입|제목|X축명|개수|라벨목록|값목록|Y최소|Y최대   → 그래프 모드
       ※ Y최소·Y최대는 선택 항목입니다. 기존 7필드 G2도 지원합니다.
    I1                                                     → 대기 화면 다시 표시
    F1|열개수|행개수|헤더목록|셀목록(행 우선)              → 자유 모드(실전 데이터 표, 최대 5열x10행)

  응답: OK / ERR|FORMAT / ERR|TOO_LONG

  배선은 prac4.ino와 완전히 동일합니다 (변경 없음):
    TFT_MOSI 23 / TFT_MISO 미사용 / TFT_SCLK 14
    TFT_CS   33 / TFT_DC   13 / TFT_RST  25
    기본 SPI 객체 + Adafruit_ST7789 사용

  필요 라이브러리: Adafruit GFX Library, Adafruit ST7735 and ST7789 Library
  ------------------------------------------------------------------
*/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

// Arduino IDE에서 반드시 ESP32 계열 보드를 선택해야 합니다.
#if !defined(ARDUINO_ARCH_ESP32)
  #error "Select an ESP32 board in Tools > Board."
#endif

// ===================== 배선 =====================
#define TFT_MOSI 23
#define TFT_MISO -1   // ST7789 화면 출력에는 MISO가 필요하지 않음
#define TFT_SCLK 14
#define TFT_CS   33
#define TFT_DC   13
#define TFT_RST  25

// VSPI 상수를 사용하지 않아 보드 코어별 명칭 차이를 피합니다.
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

// ===================== 시리얼 프로토콜 설정 (csv_lcd_sender.html과 동일) =====================
#define BAUD_RATE    115200
#define LINE_MAX_LEN 2048    // 50개 그래프 포인트가 포함된 긴 패킷까지 수신
#define MAX_POINTS   50      // 수신 안전 한도. 웹 화면은 3~50개 표시
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
#define COL_GRID    0x18E3
#define COL_MAX     TFT_ORANGE

// ===================== 화면 크기 (setRotation 이후 실제 값으로 갱신) =====================
int16_t SCR_W = 240;
int16_t SCR_H = 320;

String inputLine;
bool lineOverflow = false;

// ------------------------------------------------------------------
// setup / loop
// ------------------------------------------------------------------
void setup() {
  Serial.setRxBufferSize(4096);   // 50개 그래프 포인트 패킷 수신용, begin() 전에 호출
  Serial.begin(BAUD_RATE);
  inputLine.reserve(LINE_MAX_LEN + 16);

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
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

  if (type == "I1") {
    showIdleScreen();
    Serial.println("READY|CSV_LCD_RECEIVER");
  } else if (type == "W2") {
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

// G2|타입|제목|X축명|개수|라벨목록|값목록[|Y최소|Y최대]
// 기존 7필드 G2와 Y축 범위를 추가한 9필드 G2를 모두 지원합니다.
void handleGraphPacket(const String& line) {
  String f[9];
  int fieldCount = splitFields(line, f, 9);

  if (fieldCount != 7 && fieldCount != 9) {
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

  if (count <= 0 || count > MAX_POINTS) {
    Serial.println("ERR|FORMAT");
    return;
  }

  String labels[MAX_POINTS];
  float  values[MAX_POINTS];
  int labelN = splitByComma(f[5], labels, MAX_POINTS);
  int valueN = splitFloatsByComma(f[6], values, MAX_POINTS);

  if (labelN < count || valueN < count) {
    Serial.println("ERR|FORMAT");
    return;
  }

  bool useFixedAxis = false;
  float fixedMin = 0;
  float fixedMax = 0;

  if (fieldCount == 9) {
    fixedMin = f[7].toFloat();
    fixedMax = f[8].toFloat();

    float rawMin = values[0];
    float rawMax = values[0];
    for (int i = 1; i < count; i++) {
      if (values[i] < rawMin) rawMin = values[i];
      if (values[i] > rawMax) rawMax = values[i];
    }

    useFixedAxis = fixedMin < fixedMax && fixedMin <= rawMin && fixedMax >= rawMax;
  }

  drawGraph(type, title, xTitle, labels, values, count, useFixedAxis, fixedMin, fixedMax);
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

  tft.setTextColor(COL_WATCH, COL_BG);
  tft.setCursor(10, 104);
  tft.print("FW: XY-VALUE-V4");

  tft.setTextColor(COL_SUB, COL_BG);
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
String graphUnitFromTitle(const String& title) {
  String upper = title;
  upper.toUpperCase();

  if (upper.indexOf("TEMP") >= 0) return "C";
  if (upper.indexOf("HUM") >= 0)  return "%";
  if (upper.indexOf("RAIN") >= 0) return "mm";
  if (upper.indexOf("WIND") >= 0) return "m/s";
  return "";
}

String graphMetricAxisLabel(const String& title) {
  String upper = title;
  upper.toUpperCase();

  // LCD 폭에 맞춘 짧은 Y축 이름입니다.
  if (upper.indexOf("FEEL") >= 0) return "FEEL(C)";
  if (upper.indexOf("TEMP") >= 0) return "TEMP(C)";
  if (upper.indexOf("HUM") >= 0)  return "HUM(%)";
  if (upper.indexOf("RAIN") >= 0) return "RAIN(mm)";
  if (upper.indexOf("WIND") >= 0) return "WIND(m/s)";

  return shortenLabel(asciiOnly(title), 12);
}

String formatGraphNumber(float value) {
  if (fabs(value) >= 1000.0f) return String(value, 0);

  float nearest = roundf(value);
  if (fabs(value - nearest) < 0.05f) return String((int)nearest);

  return String(value, 1);
}

int centeredTextX(const String& text, int centerX) {
  int width = text.length() * 6;
  return constrain(centerX - width / 2, 0, max(0, SCR_W - width));
}

void drawCenteredText1(const String& text, int centerX, int y, uint16_t color) {
  tft.setTextSize(1);
  tft.setTextColor(color, COL_BG);
  tft.setCursor(centeredTextX(text, centerX), y);
  tft.print(text);
}

void drawRightText1(const String& text, int rightX, int y, uint16_t color) {
  int width = text.length() * 6;
  int x = max(0, rightX - width);

  tft.setTextSize(1);
  tft.setTextColor(color, COL_BG);
  tft.setCursor(x, y);
  tft.print(text);
}

bool isShortDateLabel(const String& label) {
  return label.length() >= 5 && label.charAt(2) == '-';
}

String dateMonthPart(const String& label) {
  if (!isShortDateLabel(label)) return "";
  return label.substring(0, 2);
}

String dateDayPart(const String& label) {
  if (!isShortDateLabel(label)) return shortenLabel(label, 3);
  return label.substring(3, 5);
}

int maxValueIndex(float* values, int n) {
  int result = 0;
  for (int i = 1; i < n; i++) {
    if (values[i] > values[result]) result = i;
  }
  return result;
}

int valueToY(float value, float vMin, float vMax, int py, int ph) {
  float ratio = (value - vMin) / (vMax - vMin);
  ratio = constrain(ratio, 0.0f, 1.0f);
  return py + ph - (int)roundf(ratio * ph);
}

void drawYAxisGrid(float vMin, float vMax, int px, int py, int pw, int ph) {
  const int tickCount = 5;

  for (int i = 0; i < tickCount; i++) {
    float ratio = (float)i / (tickCount - 1);
    float value = vMax - (vMax - vMin) * ratio;
    int y = py + (int)roundf(ph * ratio);

    tft.drawFastHLine(px, y, pw, COL_GRID);
    drawRightText1(formatGraphNumber(value), px - 4, y - 3, COL_SUB);
  }

  tft.drawRect(px, py, pw, ph, COL_LINE);
}

void drawXAxisLabels(String* labels, int n, int px, int py, int pw, int ph, bool barMode) {
  const int firstLineY = py + ph + 5;
  const int secondLineY = firstLineY + 10;
  const bool compactDates = n > 8;

  String previousMonth = "";
  // 플롯 폭 273px, 날짜는 2글자(01)로 찍히므로 15개까지는 모두 표시됩니다.
  int labelStep = n <= 15 ? 1 : (int)ceil((float)n / 8.0f);

  for (int i = 0; i < n; i++) {
    if (n > 15 && i % labelStep != 0) continue;

    int x;
    if (barMode) {
      int slot = max(1, pw / n);
      x = px + i * slot + slot / 2;
    } else {
      x = px + (n <= 1 ? pw / 2 : (pw * i) / (n - 1));
    }

    String label = shortenLabel(asciiOnly(labels[i]), 9);

    if (compactDates && isShortDateLabel(label)) {
      String month = dateMonthPart(label);
      String day = dateDayPart(label);

      drawCenteredText1(day, x, firstLineY, COL_SUB);

      if (i == 0 || month != previousMonth) {
        drawCenteredText1("M" + month, x, secondLineY, COL_SUB);
      }

      previousMonth = month;
    } else {
      drawCenteredText1(label, x, firstLineY, COL_SUB);
    }
  }
}

bool shouldDrawPointValue(int index, int n, int maxIndex) {
  // 12개 이하는 모든 점의 값을 표시합니다.
  if (n <= 12) return true;

  // 데이터가 많을 때도 첫 값·마지막 값·최댓값은 반드시 표시합니다.
  if (index == 0 || index == n - 1 || index == maxIndex) return true;

  // 13~50개에서는 화면 겹침을 막기 위해 약 8개 간격으로 대표값만 표시합니다.
  int step = max(2, (int)ceil((float)n / 8.0f));
  return index % step == 0;
}

void drawPointValue(float value, int centerX, int pointY,
                    int py, int ph, bool highlighted) {
  String text = formatGraphNumber(value);

  // 기본적으로 모든 수치를 점/막대 위에 표시합니다.
  int y = pointY - 12;

  // 그래프 위쪽 경계를 벗어날 때만 점 아래로 내립니다.
  if (y < py + 2) {
    y = pointY + 6;
  }

  // 그래프 내부 안전 영역을 벗어나지 않도록 제한합니다.
  y = constrain(y, py + 1, py + ph - 8);

  drawCenteredText1(text, centerX, y, highlighted ? COL_MAX : COL_TEXT);
}

void calculateGraphAxis(const String& type, float* values, int n,
                        bool useFixedAxis, float fixedMin, float fixedMax,
                        float& vMin, float& vMax) {
  float rawMin = values[0];
  float rawMax = values[0];

  for (int i = 1; i < n; i++) {
    if (values[i] < rawMin) rawMin = values[i];
    if (values[i] > rawMax) rawMax = values[i];
  }

  if (useFixedAxis && fixedMin < fixedMax && fixedMin <= rawMin && fixedMax >= rawMax) {
    vMin = fixedMin;
    vMax = fixedMax;
    return;
  }

  if (type == "LINE") {
    float span = rawMax - rawMin;
    if (span == 0) span = max(1.0f, (float)fabs(rawMax) * 0.1f);
    float padding = max(0.5f, span * 0.15f);

    vMin = rawMin - padding;
    vMax = rawMax + padding;
  } else {
    vMin = min(0.0f, rawMin);
    vMax = max(0.0f, rawMax);

    float span = vMax - vMin;
    if (span == 0) span = 1;

    if (vMax > 0) vMax += span * 0.10f;
    if (vMin < 0) vMin -= span * 0.10f;
  }

  if (vMax == vMin) {
    vMax += 1;
    vMin -= 1;
  }
}

void drawAxisFooter(const String& xAxisLabel) {
  // LCD 하단 가장자리는 패널별로 일부 잘릴 수 있으므로 22px 높이의
  // 전용 표시 영역을 만들고 글자를 화면 안쪽에 배치합니다.
  const int footerY = SCR_H - 22;

  tft.fillRect(0, footerY, SCR_W, 22, COL_CARD);
  tft.drawFastHLine(0, footerY, SCR_W, COL_LINE);

  String text = "X AXIS: " + shortenLabel(asciiOnly(xAxisLabel), 12);
  int textWidth = text.length() * 6;
  int textX = max(4, (SCR_W - textWidth) / 2);

  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_CARD);
  tft.setCursor(textX, footerY + 7);
  tft.print(text);
}

void drawGraph(const String& type, const String& title, const String& xTitle,
               String* labels, float* values, int n,
               bool useFixedAxis, float fixedMin, float fixedMax) {
  tft.fillScreen(COL_BG);

  String metricLabel = graphMetricAxisLabel(title);
  String dateAxisLabel = shortenLabel(asciiOnly(xTitle), 10);

  // 가로 막대그래프는 축의 역할이 반대입니다.
  String yAxisLabel = (type == "HBAR") ? dateAxisLabel : metricLabel;
  String xAxisLabel = (type == "HBAR") ? metricLabel : dateAxisLabel;

  // 1) 상단 제목 바
  tft.fillRect(0, 0, SCR_W, 30, COL_CARD);
  tft.setTextColor(COL_TEXT, COL_CARD);
  tft.setTextSize(2);
  tft.setCursor(8, 7);
  tft.print(shortenLabel(metricLabel, 13));
  tft.print(" GRAPH");

  // 새 펌웨어가 실제로 올라갔는지 확인할 수 있는 버전 표식
  tft.setTextSize(1);
  tft.setTextColor(COL_WATCH, COL_CARD);
  tft.setCursor(SCR_W - 28, 10);
  tft.print("V4");

  // 2) Y축 이름 전용 줄
  // 기존 y=34 가장자리 출력보다 안쪽으로 옮기고 밝은 색으로 표시합니다.
  tft.fillRect(0, 30, SCR_W, 20, COL_BG);
  tft.setTextSize(1);
  tft.setTextColor(COL_WATCH, COL_BG);
  tft.setCursor(5, 36);
  tft.print("Y AXIS: ");
  tft.print(shortenLabel(yAxisLabel, 12));

  // 우측에는 선택 구간만 짧게 표시
  String rangeText = shortenLabel(asciiOnly(labels[0]), 5);
  rangeText += "~";
  rangeText += shortenLabel(asciiOnly(labels[n - 1]), 5);
  int rangeWidth = rangeText.length() * 6;
  tft.setTextColor(COL_SUB, COL_BG);
  tft.setCursor(max(4, SCR_W - 5 - rangeWidth), 36);
  tft.print(rangeText);

  float vMin, vMax;
  calculateGraphAxis(type, values, n, useFixedAxis, fixedMin, fixedMax, vMin, vMax);

  const int footerY = SCR_H - 22;

  if (type == "HBAR") {
    const int px = 8;
    const int py = 54;
    const int pw = SCR_W - 16;
    const int ph = max(70, footerY - py - 5);

    drawHBar(labels, values, n, vMin, vMax, px, py, pw, ph);
    drawAxisFooter(xAxisLabel);
    return;
  }

  // 3) 그래프 영역
  // 왼쪽은 Y축 숫자, 아래는 날짜 눈금과 X축 이름을 위한 안전 여백입니다.
  const int px = 39;
  const int py = 54;
  const int pw = SCR_W - px - 8;
  const int ph = max(70, footerY - py - 31);

  drawYAxisGrid(vMin, vMax, px, py, pw, ph);

  if (type == "LINE") {
    drawLineChart(labels, values, n, vMin, vMax, px, py, pw, ph);
  } else {
    drawBarChart(labels, values, n, vMin, vMax, px, py, pw, ph);
  }

  // 4) X축 이름 전용 하단 바
  drawAxisFooter(xAxisLabel);
}

void drawLineChart(String* labels, float* values, int n, float vMin, float vMax,
                   int px, int py, int pw, int ph) {
  int maxIndex = maxValueIndex(values, n);

  int previousX = px;
  int previousY = valueToY(values[0], vMin, vMax, py, ph);

  for (int i = 1; i < n; i++) {
    int x = px + (n <= 1 ? pw / 2 : (pw * i) / (n - 1));
    int y = valueToY(values[i], vMin, vMax, py, ph);

    tft.drawLine(previousX, previousY, x, y, COL_INFO);

    previousX = x;
    previousY = y;
  }

  // 선을 먼저 그린 뒤 점과 수치 레이블을 올려 가독성을 확보합니다.
  int pointRadius = n > 24 ? 1 : (n > 12 ? 2 : 3);
  int maxPointRadius = n > 24 ? 3 : 4;

  for (int i = 0; i < n; i++) {
    int x = px + (n <= 1 ? pw / 2 : (pw * i) / (n - 1));
    int y = valueToY(values[i], vMin, vMax, py, ph);
    bool highlighted = i == maxIndex;

    tft.fillCircle(
      x,
      y,
      highlighted ? maxPointRadius : pointRadius,
      highlighted ? COL_MAX : COL_INFO
    );

    if (shouldDrawPointValue(i, n, maxIndex)) {
      drawPointValue(values[i], x, y, py, ph, highlighted);
    }
  }

  drawXAxisLabels(labels, n, px, py, pw, ph, false);
}

void drawBarChart(String* labels, float* values, int n, float vMin, float vMax,
                  int px, int py, int pw, int ph) {
  int maxIndex = maxValueIndex(values, n);
  int slot = max(1, pw / n);
  int barWidth = max(3, min(22, slot - 5));

  float baselineValue;
  if (vMin <= 0 && vMax >= 0) baselineValue = 0;
  else if (vMin > 0) baselineValue = vMin;
  else baselineValue = vMax;

  int baselineY = valueToY(baselineValue, vMin, vMax, py, ph);
  tft.drawFastHLine(px, baselineY, pw, COL_LINE);

  for (int i = 0; i < n; i++) {
    int centerX = px + i * slot + slot / 2;
    int valueY = valueToY(values[i], vMin, vMax, py, ph);
    int topY = min(valueY, baselineY);
    int height = max(1, abs(valueY - baselineY));
    int x = centerX - barWidth / 2;
    bool highlighted = i == maxIndex;

    tft.fillRect(x, topY, barWidth, height, highlighted ? COL_MAX : COL_INFO);

    if (shouldDrawPointValue(i, n, maxIndex)) {
      drawPointValue(values[i], centerX, valueY, py, ph, highlighted);
    }
  }

  drawXAxisLabels(labels, n, px, py, pw, ph, true);
}

void drawHBar(String* labels, float* values, int n, float vMin, float vMax,
              int px, int py, int pw, int ph) {
  int visibleCount = min(n, 10);
  int maxIndex = maxValueIndex(values, visibleCount);

  int labelWidth = 54;
  int valueWidth = 34;
  int barX = px + labelWidth;
  int barWidthMax = pw - labelWidth - valueWidth;
  int slot = max(1, ph / visibleCount);
  int barHeight = max(4, slot - 5);

  float base = min(0.0f, vMin);
  float range = vMax - base;
  if (range == 0) range = 1;

  tft.setTextSize(1);

  for (int i = 0; i < visibleCount; i++) {
    int y = py + i * slot + max(0, (slot - barHeight) / 2);
    int width = (int)roundf(((values[i] - base) / range) * barWidthMax);
    width = constrain(width, 0, barWidthMax);
    bool highlighted = i == maxIndex;

    tft.setTextColor(COL_SUB, COL_BG);
    tft.setCursor(px, y + 1);
    tft.print(shortenLabel(asciiOnly(labels[i]), 8));

    tft.fillRect(barX, y, width, barHeight, highlighted ? COL_MAX : COL_INFO);

    String valueText = formatGraphNumber(values[i]);
    tft.setTextColor(highlighted ? COL_MAX : COL_TEXT, COL_BG);
    tft.setCursor(barX + barWidthMax + 3, y + 1);
    tft.print(valueText);
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
