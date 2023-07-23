/*
 Extending the basic code of rtl_433_ESP to publish rtl_433 data to MQTT.

*/

#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <rtl_433_ESP.h>
#include "time.h"

#ifndef RF_MODULE_FREQUENCY
#  define RF_MODULE_FREQUENCY 433.92
#endif

#define JSON_MSG_BUFFER 512

// -- WIFI
#define WIFI_SSID "ssid"
#define WIFI_KEY  "key"

WiFiClient espClient;

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_KEY);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

// -- MQTT
#define MQTT_HOST "192.168.2.2"
#define MQTT_PORT 1883

// MQTT Base Topic
#define MQTT_PUB_PREFIX "rtl_433/esp32lilygo01"
#define MQTT_PATH_SIZE 100

PubSubClient client(espClient);

void reconnect_mqtt()
{
  while (!client.connected()) {
      Serial.println("Connecting to MQTT...");

      if (client.connect("ESP32Client")) {
        Serial.println("MQTT connected");
      } else {
        Serial.print("MQTT failed with state ");
        Serial.print(client.state());
      }
  }
}

// -- NTP
// Source: https://randomnerdtutorials.com/esp32-ntp-timezones-daylight-saving/
// Europe/Vienna	CET-1CEST,M3.5.0,M10.5.0/3
#define NTP_TIMEZONE_STRING "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_TIMESTRING_SIZE 20

void setTimezone(String timezone) {
  Serial.printf("Setting Timezone to %s\n", timezone.c_str());

  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  setenv("TZ", timezone.c_str(), 1);
  tzset();
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
}

void initTime(String timezone) {
  struct tm timeinfo;

  Serial.println("Setting up time");

  // First connect to NTP server, with 0 TZ offset
  configTime(0, 0, "pool.ntp.org");
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  Serial.println("Got the time from NTP");

  // Now we can set the real timezone
  setTimezone(timezone);
}

char messageBuffer[JSON_MSG_BUFFER];

rtl_433_ESP rf; // use -1 to disable transmitter

int count = 0;

void rtl_433_Callback(char* message) {
  DynamicJsonBuffer jsonBuffer2(JSON_MSG_BUFFER);
  JsonObject& RFrtl_433_ESPdata = jsonBuffer2.parseObject(message);
  logJson(RFrtl_433_ESPdata);
  count++;
}

void logJson(JsonObject& jsondata) {
  char mqtt_path[MQTT_PATH_SIZE], mqtt_local_path[MQTT_PATH_SIZE];
  char timeString[NTP_TIMESTRING_SIZE];
  struct tm timeinfo;

#if defined(ESP8266) || defined(ESP32) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1280__)
  char JSONmessageBuffer[jsondata.measureLength() + 1];
#else
  char JSONmessageBuffer[JSON_MSG_BUFFER];
#endif
  jsondata.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
#if defined(setBitrate) || defined(setFreqDev) || defined(setRxBW)
  Log.setShowLevel(false);
  Log.notice(F("."));
  Log.setShowLevel(true);
#else
  Log.notice(F("Received message : %s" CR), JSONmessageBuffer);

  if (!jsondata.containsKey("channel") || !jsondata.containsKey("id")) {
    // prefill mqtt_path with MQTT_PUB_PREFIX followed by model without channel and id as those are not provided
    snprintf(mqtt_path, MQTT_PATH_SIZE, "%s/%s", MQTT_PUB_PREFIX, jsondata["model"].as<char *>());
  } else {
    // prefill mqtt_path with MQTT_PUB_PREFIX followed by model, channel and id
    snprintf(mqtt_path, MQTT_PATH_SIZE, "%s/%s/%d/%d", MQTT_PUB_PREFIX, jsondata["model"].as<char *>(), jsondata["channel"].as<int>(), jsondata["id"].as<int>());
  }

  for (JsonObject::iterator it=jsondata.begin(); it!=jsondata.end(); ++it) {
      if (strcmp("channel", it->key) == 0 || strcmp("id", it->key) == 0 || strcmp("model", it->key) == 0) {
        continue;
      }

      // append data key to mqtt path
      snprintf(mqtt_local_path, MQTT_PATH_SIZE, "%s/%s", mqtt_path, it->key);

      // make sure mqtt is still connected and publish message
      reconnect_mqtt();
      client.publish(mqtt_local_path, it->value.as<char*>());
	}

  if(!getLocalTime(&timeinfo)){
    Log.notice("Failed to obtain time" CR);
    return;
  }

  snprintf(mqtt_local_path, MQTT_PATH_SIZE, "%s/lastupdate", mqtt_path);
  strftime(timeString, NTP_TIMESTRING_SIZE, "%Y-%m-%d %H:%M:%S", &timeinfo);

  // make sure mqtt is still connected and publish message
  reconnect_mqtt();
  client.publish(mqtt_local_path, timeString);
#endif
}


void setup() {
  Serial.begin(921600);
  delay(1000);
#ifndef LOG_LEVEL
  LOG_LEVEL_SILENT
#endif

  initWiFi();

  initTime(NTP_TIMEZONE_STRING);

  client.setServer(MQTT_HOST, MQTT_PORT);
  reconnect_mqtt();

  Log.begin(LOG_LEVEL, &Serial);
  Log.notice(F(" " CR));
  Log.notice(F("****** setup ******" CR));
  rf.initReceiver(RF_MODULE_RECEIVER_GPIO, RF_MODULE_FREQUENCY);
  rf.setCallback(rtl_433_Callback, messageBuffer, JSON_MSG_BUFFER);
  rf.enableReceiver();
  Log.notice(F("****** setup complete ******" CR));
  rf.getModuleStatus();
}

unsigned long uptime() {
  static unsigned long lastUptime = 0;
  static unsigned long uptimeAdd = 0;
  unsigned long uptime = millis() / 1000 + uptimeAdd;
  if (uptime < lastUptime) {
    uptime += 4294967;
    uptimeAdd += 4294967;
  }
  lastUptime = uptime;
  return uptime;
}

int next = uptime() + 30;

#if defined(setBitrate) || defined(setFreqDev) || defined(setRxBW)

#  ifdef setBitrate
#    define TEST    "setBitrate"
#    define STEP    2
#    define stepMin 1
#    define stepMax 300
#  elif defined(setFreqDev) // 17.24 was suggested
#    define TEST    "setFrequencyDeviation"
#    define STEP    1
#    define stepMin 5
#    define stepMax 200
#  elif defined(setRxBW)
#    define TEST "setRxBandwidth"

#    ifdef RF_SX1278
#      define STEP    5
#      define stepMin 5
#      define stepMax 250
#    else
#      define STEP    5
#      define stepMin 58
#      define stepMax 812
// #      define STEP    0.01
// #      define stepMin 202.00
// #      define stepMax 205.00
#    endif
#  endif
float step = stepMin;
#endif

void loop() {
  rf.loop();
#if defined(setBitrate) || defined(setFreqDev) || defined(setRxBW)
  char stepPrint[8];
  if (uptime() > next) {
    next = uptime() + 120; // 60 seconds
    dtostrf(step, 7, 2, stepPrint);
    Log.notice(F(CR "Finished %s: %s, count: %d" CR), TEST, stepPrint, count);
    step += STEP;
    if (step > stepMax) {
      step = stepMin;
    }
    dtostrf(step, 7, 2, stepPrint);
    Log.notice(F("Starting %s with %s" CR), TEST, stepPrint);
    count = 0;

    int16_t state = 0;
#  ifdef setBitrate
    state = rf.setBitRate(step);
    RADIOLIB_STATE(state, TEST);
#  elif defined(setFreqDev)
    state = rf.setFrequencyDeviation(step);
    RADIOLIB_STATE(state, TEST);
#  elif defined(setRxBW)
    state = rf.setRxBandwidth(step);
    if ((state) != RADIOLIB_ERR_NONE) {
      Log.notice(F(CR "Setting  %s: to %s, failed" CR), TEST, stepPrint);
      next = uptime() - 1;
    }
#  endif

    rf.receiveDirect();
    // rf.getModuleStatus();
  }
#endif
}