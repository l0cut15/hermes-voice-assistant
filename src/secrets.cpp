#include "secrets.h"
#include <M5Unified.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <string.h>

Secrets g_secrets = {};

bool secrets_load() {
    if (!LittleFS.begin(false)) {
        Serial.println("Secrets: LittleFS mount failed");
        return false;
    }
    File f = LittleFS.open("/secrets.json", "r");
    if (!f) {
        Serial.println("Secrets: /secrets.json not found — flash LittleFS first");
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("Secrets: JSON parse error: %s\n", err.c_str());
        return false;
    }
    strlcpy(g_secrets.wifi_ssid,  doc["wifi_ssid"]  | "", sizeof(g_secrets.wifi_ssid));
    strlcpy(g_secrets.wifi_pass,  doc["wifi_pass"]  | "", sizeof(g_secrets.wifi_pass));
    strlcpy(g_secrets.openai_key, doc["openai_key"] | "", sizeof(g_secrets.openai_key));
    strlcpy(g_secrets.stt_host,    doc["stt_host"]    | "", sizeof(g_secrets.stt_host));
    strlcpy(g_secrets.stt_port,    doc["stt_port"]    | "7124", sizeof(g_secrets.stt_port));
    strlcpy(g_secrets.hermes_host, doc["hermes_host"] | "", sizeof(g_secrets.hermes_host));
    strlcpy(g_secrets.hermes_port, doc["hermes_port"] | "7237", sizeof(g_secrets.hermes_port));
    strlcpy(g_secrets.hermes_key,  doc["hermes_key"]  | "", sizeof(g_secrets.hermes_key));
    strlcpy(g_secrets.tts_host,    doc["tts_host"]    | "", sizeof(g_secrets.tts_host));
    strlcpy(g_secrets.tts_port,    doc["tts_port"]    | "7235", sizeof(g_secrets.tts_port));
    strlcpy(g_secrets.tts_voice,   doc["tts_voice"]   | "af_heart", sizeof(g_secrets.tts_voice));
    Serial.printf("Secrets: loaded (ssid=%s stt=%s:%s hermes=%s:%s tts=%s:%s voice=%s)\n",
                  g_secrets.wifi_ssid,
                  g_secrets.stt_host, g_secrets.stt_port,
                  g_secrets.hermes_host, g_secrets.hermes_port,
                  g_secrets.tts_host, g_secrets.tts_port,
                  g_secrets.tts_voice);
    return true;
}
