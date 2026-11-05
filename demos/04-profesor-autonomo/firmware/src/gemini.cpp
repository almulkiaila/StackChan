#include "gemini.h"
#include "audio.h"
#include "prompt.h"
#include "roots.h"
#include <WebSocketsClient.h>
#include <mbedtls/base64.h>

#ifndef GEMINI_API_KEY
#define GEMINI_API_KEY ""
#endif

static const char* HOST = "generativelanguage.googleapis.com";
static const char* PATH = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";
static const char* MODEL = "models/gemini-3.1-flash-live-preview";
static const char* VOICE = "Charon";
static constexpr size_t SEND_SAMPLES = 1600;   // 100 ms of mic per message

static WebSocketsClient s_ws;
static FunctionHandler s_handler = nullptr;
static volatile bool s_connected = false, s_ready = false;
static const char* s_state = "starting";
static String s_resumeHandle;                  // to carry the conversation on after a reconnect
static char s_heard[160] = "";
static String s_heardTurn, s_saidTurn;

// Fragmented frames are put back together here (PSRAM).
static uint8_t* s_frag = nullptr;
static size_t s_fragLen = 0, s_fragCap = 0;
static int16_t* s_pcm = nullptr;               // decoded audio (PSRAM)
static constexpr size_t PCM_CAP = 64 * 1024;   // bytes

bool geminiReady() { return s_ready; }
const char* geminiState() { return s_state; }
const char* geminiLastHeard() { return s_heard; }

// ---------------------------------------------------------------- setup message
static void addFn(JsonArray fns, const char* name, const char* desc) {
    JsonObject f = fns.add<JsonObject>();
    f["name"] = name;
    f["description"] = desc;
}

static JsonObject addFnParams(JsonArray fns, const char* name, const char* desc) {
    JsonObject f = fns.add<JsonObject>();
    f["name"] = name;
    f["description"] = desc;
    JsonObject p = f["parameters"].to<JsonObject>();
    p["type"] = "OBJECT";
    p["properties"].to<JsonObject>();
    return p;
}

static void numberProp(JsonObject params, const char* name, const char* desc) {
    JsonObject o = params["properties"][name].to<JsonObject>();
    o["type"] = "NUMBER";
    o["description"] = desc;
}

static void sendSetup() {
    JsonDocument doc;
    JsonObject setup = doc["setup"].to<JsonObject>();
    setup["model"] = MODEL;
    JsonObject gen = setup["generationConfig"].to<JsonObject>();
    gen["responseModalities"].add("AUDIO");
    gen["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = VOICE;
    setup["systemInstruction"]["parts"][0]["text"] = SYSTEM_PROMPT;

    JsonArray fns = setup["tools"][0]["functionDeclarations"].to<JsonArray>();
    JsonObject p = addFnParams(fns, "look", "Points the head at an absolute direction. pan from -180 to 180 "
                                            "(0 = straight ahead, 180 = behind), tilt from -7 to 73 (0 = straight ahead).");
    numberProp(p, "pan", "degrees, + to your left, - to your right");
    numberProp(p, "tilt", "degrees, + up, - down");
    p["required"].add("pan"); p["required"].add("tilt");
    p = addFnParams(fns, "turn_head", "Turns the head from where it looks now, by these degrees (for 'a bit more to the left').");
    numberProp(p, "pan", "degrees, + to your left, - to your right");
    numberProp(p, "tilt", "degrees, + up, - down");
    p["required"].add("pan"); p["required"].add("tilt");
    addFn(fns, "center_head", "Looks straight ahead (pan 0, tilt 0).");
    p = addFnParams(fns, "set_mood", "Shows a mood: face on the screen, color of the LEDs and head posture.");
    JsonObject mood = p["properties"]["mood"].to<JsonObject>();
    mood["type"] = "STRING";
    for (const char* m : {"neutral", "happy", "sad", "surprised", "angry", "sleepy", "thinking"}) mood["enum"].add(m);
    p["required"].add("mood");
    p = addFnParams(fns, "dance", "Dances: moves the head to the beat and changes the LED colors. Returns at once; "
                                  "the dance goes on for that many seconds.");
    numberProp(p, "seconds", "from 3 to 30, 8 if not said");
    addFn(fns, "stop_dance", "Stops dancing.");
    p = addFnParams(fns, "set_volume", "Changes the volume of the robot's speaker, from 0 (silent) to 100.");
    numberProp(p, "level", "0 to 100");
    p["required"].add("level");
    addFn(fns, "robot_status", "Battery level and Wi-Fi signal of the robot.");

    JsonObject vad = setup["realtimeInputConfig"]["automaticActivityDetection"].to<JsonObject>();
    vad["disabled"] = false;
    vad["startOfSpeechSensitivity"] = "START_SENSITIVITY_LOW";   // so noise does not trigger it
    vad["endOfSpeechSensitivity"] = "END_SENSITIVITY_HIGH";      // answer as soon as she stops
    vad["prefixPaddingMs"] = 200;
    vad["silenceDurationMs"] = 700;                              // a bit longer: she may pause to think
    setup["contextWindowCompression"]["slidingWindow"].to<JsonObject>();
    JsonObject resume = setup["sessionResumption"].to<JsonObject>();
    if (s_resumeHandle.length()) resume["handle"] = s_resumeHandle;
    setup["inputAudioTranscription"].to<JsonObject>();
    setup["outputAudioTranscription"].to<JsonObject>();

    String out;
    serializeJson(doc, out);
    s_ws.sendTXT(out);
    Serial.printf("[GEMINI] setup sent (%u bytes)%s\n", out.length(), s_resumeHandle.length() ? ", resuming" : "");
}

// ---------------------------------------------------------------- incoming
// Audio messages are big: decode every "data": "<base64>" by hand, without a JSON document.
static bool playAudio(char* msg, size_t len) {
    bool any = false;
    char* p = msg;
    char* end = msg + len;
    while ((p = strstr(p, "\"data\"")) != nullptr && p < end) {
        p += 6;
        while (p < end && (*p == ' ' || *p == ':' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
        if (p >= end || *p != '"') continue;
        char* start = ++p;
        while (p < end && *p != '"') p++;
        size_t b64len = p - start;
        size_t outLen = 0;
        if (mbedtls_base64_decode((unsigned char*)s_pcm, PCM_CAP, &outLen, (const unsigned char*)start, b64len) == 0) {
            if (!any) audioTurnStarted();
            audioPlay(s_pcm, outLen / 2);
            any = true;
        }
    }
    return any;
}

static void handleToolCall(JsonObjectConst call) {
    JsonDocument resp;
    JsonArray out = resp["toolResponse"]["functionResponses"].to<JsonArray>();
    for (JsonObjectConst fc : call["functionCalls"].as<JsonArrayConst>()) {
        const char* name = fc["name"] | "";
        JsonObject r = out.add<JsonObject>();
        r["id"] = fc["id"];
        r["name"] = name;
        JsonObject result = r["response"].to<JsonObject>();
        if (s_handler) s_handler(name, fc["args"].as<JsonObjectConst>(), result);
        Serial.printf("[GEMINI] function %s\n", name);
    }
    String s;
    serializeJson(resp, s);
    s_ws.sendTXT(s);
}

static void handleMessage(char* msg, size_t len) {
    msg[len] = 0;
    if (strstr(msg, "\"inlineData\"")) {
        playAudio(msg, len);
        if (strstr(msg, "\"turnComplete\"")) audioTurnDone();
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, msg, len)) {
        Serial.printf("[GEMINI] unparsed message: %.120s\n", msg);
        return;
    }
    if (doc["setupComplete"].is<JsonObjectConst>()) {
        s_ready = true;
        s_state = "listo";
        Serial.println("[GEMINI] ready");
        audioBeep(1800, 60);
        return;
    }
    if (doc["toolCall"].is<JsonObjectConst>()) {
        audioTurnStarted();
        handleToolCall(doc["toolCall"].as<JsonObjectConst>());
    }
    JsonObjectConst sc = doc["serverContent"];
    if (!sc.isNull()) {
        const char* heard = sc["inputTranscription"]["text"] | (const char*)nullptr;
        if (heard) s_heardTurn += heard;
        const char* said = sc["outputTranscription"]["text"] | (const char*)nullptr;
        if (said) { s_saidTurn += said; audioTurnStarted(); }
        if (sc["turnComplete"] | false) {
            audioTurnDone();
            if (s_heardTurn.length()) {
                Serial.printf("[YOU] %s\n", s_heardTurn.c_str());
                strlcpy(s_heard, s_heardTurn.c_str(), sizeof(s_heard));
            }
            if (s_saidTurn.length()) Serial.printf("[ROBOT] %s\n", s_saidTurn.c_str());
            s_heardTurn = "";
            s_saidTurn = "";
        }
    }
    JsonObjectConst ru = doc["sessionResumptionUpdate"];
    if (!ru.isNull() && (ru["resumable"] | false)) {
        const char* h = ru["newHandle"] | "";
        if (*h) s_resumeHandle = h;
    }
    if (!doc["goAway"].isNull()) {
        Serial.println("[GEMINI] goAway: reconnecting");
        s_ws.disconnect();
    }
}

static void onEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
    case WStype_CONNECTED:
        s_connected = true;
        s_state = "configurando";
        Serial.println("[GEMINI] connected");
        sendSetup();
        break;
    case WStype_DISCONNECTED:
        if (s_connected) Serial.println("[GEMINI] disconnected");
        s_connected = s_ready = false;
        s_state = "reconectando";
        audioReset();
        break;
    case WStype_TEXT:
    case WStype_BIN:
        // payload has room for a terminator (the library allocates length + 1)
        handleMessage((char*)payload, length);
        break;
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
        s_fragLen = 0;
        // fall through
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
        if (s_fragLen + length + 1 > s_fragCap) {
            size_t cap = (s_fragLen + length + 1) * 2;
            uint8_t* n = (uint8_t*)heap_caps_realloc(s_frag, cap, MALLOC_CAP_SPIRAM);
            if (!n) { s_fragLen = 0; break; }
            s_frag = n;
            s_fragCap = cap;
        }
        memcpy(s_frag + s_fragLen, payload, length);
        s_fragLen += length;
        if (type == WStype_FRAGMENT_FIN) handleMessage((char*)s_frag, s_fragLen);
        break;
    case WStype_ERROR:
        Serial.printf("[GEMINI] error %.*s\n", (int)length, (const char*)payload);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- outgoing mic
static void sendMic() {
    static int16_t pcm[SEND_SAMPLES];
    static char msg[64 + SEND_SAMPLES * 2 * 4 / 3 + 64];
    static const char* HEAD = "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"";
    while (s_ready && audioMicAvailable() >= SEND_SAMPLES) {
        size_t n = audioMicRead(pcm, SEND_SAMPLES);
        size_t h = strlen(HEAD), olen = 0;
        memcpy(msg, HEAD, h);
        mbedtls_base64_encode((unsigned char*)msg + h, sizeof(msg) - h - 8, &olen, (const unsigned char*)pcm, n * 2);
        memcpy(msg + h + olen, "\"}}}", 5);
        s_ws.sendTXT((uint8_t*)msg, h + olen + 4);
    }
}

static void geminiTask(void*) {
    for (;;) {
        s_ws.loop();
        sendMic();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void geminiStart(FunctionHandler handler) {
    s_handler = handler;
    s_pcm = (int16_t*)heap_caps_malloc(PCM_CAP, MALLOC_CAP_SPIRAM);
    if (!strlen(GEMINI_API_KEY)) {
        s_state = "falta la clave de Gemini";
        Serial.println("[GEMINI] no API key: build with GEMINI_API_KEY set");
        return;
    }
    s_ws.beginSslWithCA(HOST, 443, PATH, GOOGLE_ROOT_CAS, "");
    s_ws.setExtraHeaders("x-goog-api-key: " GEMINI_API_KEY);
    s_ws.onEvent(onEvent);
    s_ws.setReconnectInterval(3000);
    s_state = "conectando";
    xTaskCreatePinnedToCore(geminiTask, "gemini", 16384, nullptr, 2, nullptr, 0);
}
