#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "RTClib.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SparkFun_VL53L5CX_Library.h>
#include "iaq_2nd_gen-arduino.h"

// --- Wi-Fi Ayarları ---
#define WIFI_SSID "ozge"
#define WIFI_PASS "123456789"

// --- WeatherX API ---
#define WX_LAT            "41.02"
#define WX_LON            "29.10"
#define WX_FORECAST_HOST  "api.weatherxanalytics.com"
#define WX_FORECAST_KEY   "jhBgm8POXehQAsN7IPQHBLYvdbm03Q7_qCLcypZIAhc"
#define WX_NOWCAST_HOST   "nowcasting.weatherxanalytics.com"
#define WX_NOWCAST_KEY    "wx_live_72CG0M-y11_R7mnauXWAbq2KsUoJwRVqRx7erObJrVI"

// --- Pin Tanımlamaları ---
#define I2C_SDA    1
#define I2C_SCL    2
#define TOUCH_PIN  35
#define AUDIO_PIN  46
#define SHDN_PIN   45
#define ZMOD4410_ADDR 0x32   // Wire (RTC ile aynı bus, adres çakışmaz: RTC=0x68)

// --- Nesneler ---
U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R0, 17, 15, 7, 47, 48);
RTC_DS3231 rtc;
SparkFun_VL53L5CX distanceSensor;
VL53L5CX_ResultsData measurementData;

// --- ZMOD4410 Değişkenleri ---
static zmod4xxx_dev_t        dev;
static iaq_2nd_gen_handle_t  algo_handle;
static iaq_2nd_gen_results_t algo_results;
static iaq_2nd_gen_inputs_t  algo_input;
static uint8_t adc_result[ZMOD4410_ADC_DATA_LEN];
static uint8_t prod_data[ZMOD4410_PROD_DATA_LEN];
static Interface_t hal;

// Non-blocking ölçüm zamanlayıcısı
static unsigned long zmodStartTime   = 0;
static bool          zmodActive      = false;
static bool          zmodReady       = false;   // ilk ısınma tamamlandı mı

// --- LiDAR Çalışma Mantığı ---
const int UST_SINIR_MM = 2800;
const int ALT_SINIR_MM = 50;
const unsigned long GITME_ESIGI  = 3500;
const unsigned long OTURMA_TEYIT = 2000;
bool sistemMasadaSayiyorMu = false;
unsigned long sonGorulmeZamani = 0, ilkAlgilamaZamani = 0;

enum LidarState { IDLE, COUNTING, WARNING, COOLDOWN };
LidarState currentLidarState = IDLE;
unsigned long stateTimer = 0;
const unsigned long CALISMA_LIMITI = 10000;
const unsigned long UYARI_SURESI   = 2000;
bool showWarningOnScreen = false;

// --- Arayüz Değişkenleri ---
bool showHappyFace = false;
unsigned long happyFaceTimer = 0;
unsigned long lastPageSwitch = 0;
int currentPage = 0;

// --- WeatherX Değişkenleri ---
float        temperature        = 0.0f;
float        feelsLike          = 0.0f;
float        humidity           = 0.0f;
float        windSpeed          = 0.0f;
bool         forecastLoaded     = false;
unsigned long lastForecastUpdate = 0;

float        precipNow         = 0.0f;
char         precipText[20]    = "Acik";
bool         lightningNow      = false;
float        lightningKm       = -1.0f;
bool         lightningForecast = false;
int          lightningInMin    = -1;
bool         nowcastLoaded     = false;
unsigned long lastNowcastUpdate = 0;

// =============================================================================
//  WEATHERX — Forecast API
// =============================================================================
void fetchForecast() {
  if (WiFi.status() != WL_CONNECTED) return;

  char url[160];
  snprintf(url, sizeof(url),
           "https://%s/forecast?lat=%s&lon=%s&forecast_days=3",
           WX_FORECAST_HOST, WX_LAT, WX_LON);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);
  HTTPClient http;
  http.setTimeout(30000);
  http.begin(client, url);
  http.addHeader("X-Api-Key", WX_FORECAST_KEY);

  Serial.println("[WX] Forecast isteniyor...");
  int code = http.GET();
  Serial.printf("[WX] Forecast HTTP %d\n", code);

  if (code == 200) {
    String payload = http.getString();
    JsonDocument filter;
    JsonObject fh = filter["hourly"].to<JsonObject>();
    fh["temperature_2m"][0]         = true;
    fh["feels_like_temperature"][0] = true;
    fh["relative_humidity_2m"][0]   = true;
    fh["wind_speed_10m"][0]         = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload,
                                               DeserializationOption::Filter(filter));
    if (!err) {
      JsonObject h = doc["hourly"];
      temperature  = h["temperature_2m"][0]        | 0.0f;
      feelsLike    = h["feels_like_temperature"][0] | 0.0f;
      humidity     = h["relative_humidity_2m"][0]   | 0.0f;
      windSpeed    = h["wind_speed_10m"][0]         | 0.0f;
      forecastLoaded     = true;
      lastForecastUpdate = millis();
      Serial.printf("[WX] %.1fC His:%.1fC N:%.0f%% R:%.1fm/s\n",
                    temperature, feelsLike, humidity, windSpeed);
    } else {
      Serial.printf("[WX] Forecast JSON hata: %s\n", err.c_str());
    }
  } else if (code < 0) {
    Serial.printf("[WX] Forecast hata: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

// =============================================================================
//  WEATHERX — Nowcast API
// =============================================================================
void fetchNowcast() {
  if (WiFi.status() != WL_CONNECTED) return;

  char url[128];
  snprintf(url, sizeof(url),
           "https://%s/summary?lat=%s&lon=%s&hours=1",
           WX_NOWCAST_HOST, WX_LAT, WX_LON);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);
  HTTPClient http;
  http.setTimeout(30000);
  http.begin(client, url);
  http.addHeader("X-Api-Key", WX_NOWCAST_KEY);

  Serial.println("[WX] Nowcast isteniyor...");
  int code = http.GET();
  Serial.printf("[WX] Nowcast HTTP %d\n", code);

  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      JsonObject cur = doc["current"];
      if (!cur.isNull()) {
        precipNow    = cur["precipitation_mm_hr"] | 0.0f;
        lightningNow = cur["lightning_risk"]       | false;
        lightningKm  = cur["nearest_lightning_km"] | -1.0f;
        const char *intens = cur["intensity"] | "none";
        if      (strcmp(intens,"none")==0)       strncpy(precipText,"Acik",            sizeof(precipText)-1);
        else if (strcmp(intens,"drizzle")==0)    strncpy(precipText,"Cisenti",         sizeof(precipText)-1);
        else if (strcmp(intens,"light")==0)      strncpy(precipText,"Hafif yagmur",    sizeof(precipText)-1);
        else if (strcmp(intens,"moderate")==0)   strncpy(precipText,"Orta yagmur",     sizeof(precipText)-1);
        else if (strcmp(intens,"heavy")==0)      strncpy(precipText,"Siddetli yagmur", sizeof(precipText)-1);
        else if (strcmp(intens,"very_heavy")==0) strncpy(precipText,"Cok siddetli",    sizeof(precipText)-1);
        else if (strcmp(intens,"cloudburst")==0) strncpy(precipText,"Saganak",         sizeof(precipText)-1);
        else if (strstr(intens,"snow")!=NULL)    strncpy(precipText,"Kar yagisi",      sizeof(precipText)-1);
        else if (strcmp(intens,"sleet")==0)      strncpy(precipText,"Karla kar.yag.",  sizeof(precipText)-1);
        else                                     strncpy(precipText,"Yagis var",       sizeof(precipText)-1);
      }
      JsonObject fc = doc["forecast"];
      if (!fc.isNull()) {
        lightningForecast = fc["lightning_risk"] | false;
        JsonVariant lrm   = fc["lightning_risk_minutes"];
        lightningInMin    = lrm.isNull() ? -1 : lrm.as<int>();
      }
      nowcastLoaded     = true;
      lastNowcastUpdate = millis();
      Serial.printf("[WX] Yagis:%.1fmm/h (%s) Simsek:%s\n",
                    precipNow, precipText, lightningNow ? "VAR" : "yok");
    } else {
      Serial.printf("[WX] Nowcast JSON hata: %s\n", err.c_str());
    }
  }
  http.end();
}

// =============================================================================
//  ZMOD4410 — Non-blocking güncelleme (loop() içinde çağrılır)
//  WeatherX'ten gelen gerçek T ve RH kullanılır.
// =============================================================================
void zmodUpdate(unsigned long now) {
  // Ölçüm yoksa hemen başlat
  if (!zmodActive) {
    zmod4xxx_start_measurement(&dev);
    zmodStartTime = now;
    zmodActive    = true;
    return;
  }
  // 2500 ms bekle — sonra oku
  if (now - zmodStartTime < 2500) return;

  zmod4xxx_read_adc_result(&dev, adc_result);
  algo_input.adc_result      = adc_result;
  // WeatherX verisi varsa gerçek değerleri kullan, yoksa varsayılan
  algo_input.humidity_pct    = forecastLoaded ? humidity    : 50.0f;
  algo_input.temperature_degc= forecastLoaded ? temperature : 25.0f;

  int8_t ret = calc_iaq_2nd_gen(&algo_handle, &dev, NULL, &algo_input, &algo_results);
  if (ret != IAQ_2ND_GEN_STABILIZATION) {
    zmodReady = true;
    Serial.printf("[ZMOD] IAQ:%.1f TVOC:%.3f\n", algo_results.iaq, algo_results.tvoc);
  }
  zmodActive = false;   // bir sonraki çağrıda yeni ölçüm başlar
}

// --- IAQ metni (1-5 skalası) ---
const char* iaqText() {
  if (!zmodReady)               return "Isiniyor";
  if (algo_results.iaq < 1.5f)  return "Temiz";
  if (algo_results.iaq < 3.0f)  return "Orta";
  return "Tehlike";
}

// --- Sesler ---
void playR2D2() {
  for (int i = 0; i < 3; i++) { tone(AUDIO_PIN, random(1000, 3000), 100); delay(120); }
  tone(AUDIO_PIN, 3500, 200); delay(200); noTone(AUDIO_PIN);
}
void playAlertSound() {
  tone(AUDIO_PIN, 1500, 150); delay(200); tone(AUDIO_PIN, 1200, 150); noTone(AUDIO_PIN);
}

// --- Çizim ---
void drawEye(int x, int y, bool isWarning, bool isHappy) {
  int h = 38, w = 28;
  u8g2.setDrawColor(1); u8g2.drawRBox(x - w/2, y - h/2, w, h, 6);
  if (isWarning) {
    u8g2.setDrawColor(0);
    if (x < 64) u8g2.drawTriangle(x-w/2-2, y-h/2-2, x+w/2+2, y-h/2-2, x+w/2+2, y-h/2+12);
    else        u8g2.drawTriangle(x+w/2+2, y-h/2-2, x-w/2-2, y-h/2-2, x-w/2-2, y-h/2+12);
  } else if (isHappy) {
    u8g2.setDrawColor(0); u8g2.drawBox(x - w/2 - 2, y + h/2 - 8, w + 4, 10);
  }
  u8g2.setDrawColor(1);
}

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  u8g2.begin();

  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_STA);

  pinMode(TOUCH_PIN, INPUT);
  pinMode(AUDIO_PIN, OUTPUT);
  pinMode(SHDN_PIN, OUTPUT);
  digitalWrite(SHDN_PIN, LOW);

  // Tüm I2C cihazlar tek hat: SDA=1, SCL=2 (RTC 0x68, ZMOD 0x32, LiDAR 0x29)
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  rtc.begin();

  // ZMOD4410
  HAL_Init(&hal);
  dev.i2c_addr  = ZMOD4410_ADDR;
  dev.pid       = ZMOD4410_PID;
  dev.init_conf = &zmod_iaq2_sensor_cfg[INIT];
  dev.meas_conf = &zmod_iaq2_sensor_cfg[MEASUREMENT];
  dev.prod_data = prod_data;
  dev.delay_ms  = delay;
  if (zmod4xxx_init(&dev, &hal) == 0) {
    zmod4xxx_read_sensor_info(&dev);
    zmod4xxx_prepare_sensor(&dev);
    init_iaq_2nd_gen(&algo_handle);
    Serial.println("[ZMOD] Hazir — isinma 60 sn surer");
  } else {
    Serial.println("[ZMOD] Hata: sensor bulunamadi!");
  }

  // LIDAR — aynı Wire hattı
  if (distanceSensor.begin(0x29, Wire) || distanceSensor.begin(0x52, Wire)) {
    distanceSensor.setResolution(16);
    distanceSensor.setRangingFrequency(10);
    distanceSensor.startRanging();
  }

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Wi-Fi baglaniyor...");
  int dotCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(20, 30, "WiFi Baglaniyor");
    String dots = ""; dotCount = (dotCount + 1) % 4;
    for (int i = 0; i < dotCount; i++) dots += ".";
    u8g2.drawStr(20, 45, dots.c_str());
    u8g2.sendBuffer();
  }
  Serial.println("\nBaglandi!");

  fetchForecast();
  fetchNowcast();
  playR2D2();
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
  unsigned long now = millis();
  u8g2.clearBuffer();

  // --- ZMOD non-blocking ---
  zmodUpdate(now);

  // --- LiDAR Ölçüm ---
  if (distanceSensor.isDataReady()) {
    if (distanceSensor.getRangingData(&measurementData)) {
      bool goruyorMu = false;
      int merkez[] = {5, 6, 9, 10};
      for (int i : merkez) {
        if ((measurementData.target_status[i] == 5 || measurementData.target_status[i] == 9) &&
            measurementData.distance_mm[i] > ALT_SINIR_MM &&
            measurementData.distance_mm[i] < UST_SINIR_MM) {
          goruyorMu = true;
        }
      }
      if (goruyorMu) {
        if (!sistemMasadaSayiyorMu) {
          if (ilkAlgilamaZamani == 0) ilkAlgilamaZamani = now;
          if (now - ilkAlgilamaZamani >= OTURMA_TEYIT) {
            sistemMasadaSayiyorMu = true; sonGorulmeZamani = now;
          }
        } else {
          sonGorulmeZamani = now;
        }
      } else {
        ilkAlgilamaZamani = 0;
        if (sistemMasadaSayiyorMu && (now - sonGorulmeZamani >= GITME_ESIGI))
          sistemMasadaSayiyorMu = false;
      }
    }
  }

  // --- LiDAR Durum Makinesi ---
  switch (currentLidarState) {
    case IDLE:
      if (sistemMasadaSayiyorMu) { currentLidarState = COUNTING; stateTimer = now; }
      break;
    case COUNTING:
      if (!sistemMasadaSayiyorMu) currentLidarState = IDLE;
      else if (now - stateTimer >= CALISMA_LIMITI) {
        currentLidarState = WARNING; stateTimer = now; showWarningOnScreen = true;
      }
      break;
    case WARNING:
      if (now - stateTimer >= UYARI_SURESI) {
        showWarningOnScreen = false; currentLidarState = COOLDOWN; stateTimer = now;
      } else {
        static unsigned long lastSound = 0;
        if (now - lastSound > 5000) { playAlertSound(); lastSound = now; }
      }
      break;
    case COOLDOWN:
      if (now - stateTimer >= 5000) currentLidarState = IDLE;
      break;
  }

  // --- Touch Sensör ---
  static bool lastTouchState = false;
  static int clickCount = 0;
  static unsigned long lastClickTime = 0;
  bool currentTouch = digitalRead(TOUCH_PIN);
  const unsigned long clickWindow = 350;

  if (currentTouch && !lastTouchState) {
    unsigned long clickTime = millis();
    if (clickTime - lastClickTime < clickWindow) {
      showHappyFace = true; happyFaceTimer = clickTime;
      playR2D2(); clickCount = 0; lastClickTime = 0;
    } else {
      clickCount = 1; lastClickTime = clickTime;
    }
  }
  if (clickCount == 1 && (millis() - lastClickTime > clickWindow)) {
    currentPage = (currentPage + 1) % 3;
    lastPageSwitch = millis(); clickCount = 0;
  }
  lastTouchState = currentTouch;
  if (showHappyFace && (now - happyFaceTimer > 2000)) showHappyFace = false;

  // --- WeatherX Zamanlama ---
  if (now - lastForecastUpdate > 3600000UL || lastForecastUpdate == 0) fetchForecast();
  if (now - lastNowcastUpdate  >  600000UL || lastNowcastUpdate  == 0) fetchNowcast();

  // --- Ekran Çizim ---
  if (showWarningOnScreen) {
    drawEye(40, 32, true, false); drawEye(88, 32, true, false);

  } else if (showHappyFace) {
    drawEye(40, 32, false, true); drawEye(88, 32, false, true);

  } else if (currentPage == 0) {
    // Sayfa 0: Saat + Tarih
    DateTime t = rtc.now();
    char ts[10], ds[15];
    sprintf(ts, "%02d:%02d", t.hour(), t.minute());
    sprintf(ds, "%02d/%02d/%04d", t.day(), t.month(), t.year());
    u8g2.setFont(u8g2_font_logisoso28_tn); u8g2.drawStr(22, 38, ts);
    u8g2.setFont(u8g2_font_6x12_tr);       u8g2.drawStr(34, 60, ds);

  } else if (currentPage == 1) {
    // Sayfa 1: Yüz
    drawEye(40, 32, false, false); drawEye(88, 32, false, false);

  } else {
    // =========================================================================
    // Sayfa 2: Hava Durumu
    // =========================================================================

    // Başlık (inverted)
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 0, 128, 13);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_6x12_tr);
    int cw = u8g2.getStrWidth("UMRANIYE");
    u8g2.drawStr((128 - cw) / 2, 11, "UMRANIYE");
    u8g2.setDrawColor(1);

    if (forecastLoaded) {
      // Büyük sıcaklık — ortada
      char tnum[5];
      snprintf(tnum, sizeof(tnum), "%.0f", temperature);
      u8g2.setFont(u8g2_font_logisoso20_tn);
      int tnw = u8g2.getStrWidth(tnum);
      int tx  = (128 - tnw - 10) / 2;
      u8g2.drawStr(tx, 38, tnum);

      // "o" + "C" küçük — sağ üst
      u8g2.setFont(u8g2_font_6x12_tr);
      u8g2.drawStr(tx + tnw + 2, 22, "o");
      u8g2.drawStr(tx + tnw + 2, 33, "C");

      // Detay: His + Nem + Rüzgar
      char det[26];
      snprintf(det, sizeof(det), "His:%.0fC N:%.0f%% R:%.0fm/s",
               feelsLike, humidity, windSpeed);
      u8g2.drawStr(0, 50, det);
    } else {
      u8g2.setFont(u8g2_font_6x12_tr);
      u8g2.drawStr(5, 32, "Hava bekleniyor..");
    }

    // Ayırıcı
    u8g2.drawHLine(0, 52, 128);

    // Alt satır: yağış sol, IAQ sağ (şimşek varsa tam satır)
    u8g2.setFont(u8g2_font_6x12_tr);
    if (nowcastLoaded && lightningNow) {
      char lbuf[22];
      snprintf(lbuf, sizeof(lbuf), "!! SIMSEK %.0f km", lightningKm);
      u8g2.drawStr(0, 63, lbuf);
    } else if (nowcastLoaded && lightningForecast && lightningInMin > 0) {
      char lbuf[22];
      snprintf(lbuf, sizeof(lbuf), "Simsek %ddk sonra", lightningInMin);
      u8g2.drawStr(0, 63, lbuf);
    } else {
      // Yağış sol
      if (nowcastLoaded) u8g2.drawStr(0, 63, precipText);
      // IAQ sağa hizalı
      char ibuf[14];
      snprintf(ibuf, sizeof(ibuf), "I:%s", iaqText());
      u8g2.drawStr(128 - u8g2.getStrWidth(ibuf), 63, ibuf);
    }
  }

  // --- Otomatik sayfa döndürme ---
  if (now - lastPageSwitch > 8000 && !currentTouch) {
    currentPage = (currentPage + 1) % 3;
    lastPageSwitch = now;
  }

  u8g2.sendBuffer();
}