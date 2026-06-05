#pragma once

struct Secrets {
    char wifi_ssid[64];
    char wifi_pass[64];
    char openai_key[200];   // sk-proj- keys are ~164 chars
    char stt_host[64];
    char stt_port[8];
    char hermes_host[64];
    char hermes_port[8];
    char hermes_key[200];   // API_SERVER_KEY bearer token
    char tts_host[64];
    char tts_port[8];
};

extern Secrets g_secrets;

bool secrets_load();  // false if /secrets.json missing or invalid
