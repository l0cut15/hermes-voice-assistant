#include "llm.h"
#include "secrets.h"
#include <M5Unified.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const char* SYSTEM_PROMPT =
    "You are an enthusiastic, upbeat voice assistant built into a handheld device. "
    "Your replies are spoken aloud through a speaker — not displayed as text. "
    "Be warm, curious, and a little playful — like a knowledgeable friend who's genuinely interested. "
    "Reply in 1 to 2 short spoken sentences only. "
    "No bullet points, no lists, no markdown. "
    "No hollow filler like Sure or Great question. "
    "No preamble or reasoning steps. "
    "Use natural conversational language with contractions. "
    "If you don't know something, say so with good humour in one sentence.";

bool llm_complete(const char* transcript, char* response, size_t response_size) {
    if (!g_secrets.hermes_host[0]) {
        Serial.println("LLM: hermes_host not set in secrets.json");
        return false;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%s/v1/chat/completions",
             g_secrets.hermes_host, g_secrets.hermes_port);

    char auth_header[220];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", g_secrets.hermes_key);

    JsonDocument req;
    req["model"]      = "hermes-agent";
    req["max_tokens"] = 150;
    JsonArray msgs = req["messages"].to<JsonArray>();
    JsonObject sys = msgs.add<JsonObject>();
    sys["role"]    = "system";
    sys["content"] = SYSTEM_PROMPT;
    JsonObject usr = msgs.add<JsonObject>();
    usr["role"]    = "user";
    usr["content"] = transcript;

    String body;
    serializeJson(req, body);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(60000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", auth_header);

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("LLM: HTTP %d\n", code);
        http.end();
        return false;
    }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) {
        Serial.println("LLM: JSON parse failed");
        return false;
    }

    const char* text = doc["choices"][0]["message"]["content"] | "";
    if (!text[0]) {
        Serial.println("LLM: empty response");
        return false;
    }

    strlcpy(response, text, response_size);
    return true;
}
