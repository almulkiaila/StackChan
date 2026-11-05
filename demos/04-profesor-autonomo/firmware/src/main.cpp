// Professor Stack-chan, standalone: a nerdy professor face and a Spanish teacher that
// talks to Gemini Live by itself over Wi-Fi (gemini.cpp), with no laptop.
// Tap the screen to mute/unmute the mic. Serial: "face <mood>", "home".
#include <M5Unified.h>
#include <WiFi.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include "board.h"
#include "audio.h"
#include "gemini.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: copy config/wifi_secrets.h.example and fill it in"
#endif

// --- Professor face ---
// Frizzy grey hair on the sides, thick tortoiseshell glasses, bushy eyebrows,
// a moustache and a red bow tie. Only the eyes, brows and mouth are redrawn.
enum Mood { NEUTRAL, HAPPY, SAD, SURPRISED, ANGRY, SLEEPY, THINKING, MOOD_COUNT };
static const char* const MOOD_NAMES[MOOD_COUNT] = { "neutral", "happy", "sad", "surprised", "angry", "sleepy", "thinking" };
static volatile int s_wantMood = NEUTRAL;   // set by the network task; drawn by loop()
static int s_mood = -1;

static const int EYE_Y = 80;
static const int EYE_X[2] = { 100, 220 };
static uint16_t C_FRAME, C_LENS, C_HAIR, C_TIE, C_TIE_DOT;

static void initColors() {
    auto& d = M5.Display;
    C_FRAME = d.color565(190, 120, 50);     // tortoiseshell
    C_LENS = d.color565(8, 16, 34);         // slightly blue glass
    C_HAIR = d.color565(205, 205, 210);
    C_TIE = d.color565(200, 25, 35);
    C_TIE_DOT = d.color565(255, 220, 220);
}

// Everything that never changes: hair, glasses frame, moustache, bow tie.
static void drawStatics() {
    auto& d = M5.Display;
    d.fillRect(0, 0, 320, 196, TFT_BLACK);
    // Frizzy hair on both sides, bald on top (like every good professor).
    static const int16_t tufts[][3] = {
        {8, 30, 16}, {26, 14, 14}, {6, 58, 13}, {30, 40, 11}, {48, 8, 10}, {16, 82, 10}, {42, 24, 8},
    };
    for (auto& t : tufts) {
        d.fillCircle(t[0], t[1], t[2], C_HAIR);
        d.fillCircle(319 - t[0], t[1], t[2], C_HAIR);
    }
    // Glasses: two thick round-cornered frames, bridge and temples.
    for (int side = 0; side < 2; side++) {
        const int x = EYE_X[side];
        d.fillRoundRect(x - 41, EYE_Y - 31, 82, 62, 14, C_FRAME);
        d.fillRoundRect(x - 35, EYE_Y - 25, 70, 50, 10, C_LENS);
    }
    d.fillArc(160, EYE_Y + 4, 13, 19, 200, 340, C_FRAME);           // bridge
    d.drawWideLine(59, EYE_Y - 12, 14, EYE_Y - 20, 4, C_FRAME);     // temples
    d.drawWideLine(261, EYE_Y - 12, 306, EYE_Y - 20, 4, C_FRAME);
    // Moustache.
    d.fillEllipse(143, 130, 19, 7, C_HAIR);
    d.fillEllipse(177, 130, 19, 7, C_HAIR);
    d.fillCircle(124, 133, 5, C_HAIR);
    d.fillCircle(196, 133, 5, C_HAIR);
    // Bow tie.
    d.fillTriangle(160, 184, 128, 174, 128, 195, C_TIE);
    d.fillTriangle(160, 184, 192, 174, 192, 195, C_TIE);
    d.fillCircle(160, 184, 6, C_TIE);
    d.fillCircle(139, 180, 2, C_TIE_DOT);
    d.fillCircle(137, 189, 2, C_TIE_DOT);
    d.fillCircle(181, 180, 2, C_TIE_DOT);
    d.fillCircle(183, 189, 2, C_TIE_DOT);
}

static void drawGlint(int x) {   // reflection on the lens
    M5.Display.drawWideLine(x + 18, EYE_Y - 20, x + 28, EYE_Y - 10, 2, TFT_DARKGREY);
}

// Eyes inside the lenses. closed = blink.
static void drawEyes(int mood, bool closed = false) {
    auto& d = M5.Display;
    for (int side = 0; side < 2; side++) {
        const int x = EYE_X[side], y = EYE_Y;
        d.fillRoundRect(x - 35, y - 25, 70, 50, 10, C_LENS);
        if (closed) {
            d.fillRect(x - 16, y + 2, 32, 4, TFT_WHITE);
        } else switch (mood) {
        case HAPPY:                            // closed, smiling eyes: ^ ^
            d.fillArc(x, y + 10, 12, 18, 180, 360, TFT_WHITE);
            break;
        case SAD:
            d.fillCircle(x, y + 6, 13, TFT_WHITE);
            d.fillCircle(x, y + 10, 6, TFT_BLACK);
            break;
        case SURPRISED:
            d.fillCircle(x, y, 20, TFT_WHITE);
            d.fillCircle(x, y, 5, TFT_BLACK);
            break;
        case ANGRY:
            d.fillCircle(x, y + 6, 13, TFT_WHITE);
            d.fillCircle(x, y + 6, 6, TFT_BLACK);
            break;
        case SLEEPY:
            d.fillRect(x - 16, y + 4, 32, 6, TFT_WHITE);
            break;
        case THINKING:                         // looking up and to one side
            d.fillCircle(x, y, 15, TFT_WHITE);
            d.fillCircle(x + 7, y - 7, 7, TFT_BLACK);
            break;
        default:                               // big, magnified nerd eyes
            d.fillCircle(x, y + 1, 16, TFT_WHITE);
            d.fillCircle(x, y + 3, 7, TFT_BLACK);
        }
        drawGlint(x);
    }
}

static void drawBrows(int mood) {
    auto& d = M5.Display;
    for (int side = 0; side < 2; side++) {
        const int x = EYE_X[side];
        const int in = side ? -1 : 1;          // towards the middle of the face
        d.fillRect(x - 34, 22, 68, 26, TFT_BLACK);
        int outY = 40, inY = 40;
        switch (mood) {
        case SAD:       outY = 44; inY = 30; break;     // inner end up
        case ANGRY:     outY = 30; inY = 45; break;     // inner end down
        case SURPRISED: outY = 28; inY = 28; break;
        case HAPPY:     outY = 38; inY = 34; break;
        case SLEEPY:    outY = 45; inY = 45; break;
        case THINKING:  outY = inY = side ? 40 : 27; break;   // one brow raised
        }
        d.drawWideLine(x - 28 * in, outY, x + 24 * in, inY, 5, C_HAIR);
        d.drawWideLine(x - 22 * in, outY - 3, x + 18 * in, inY - 3, 3, C_HAIR);   // bushy
    }
}

// mouth: 0 = the mood's own, 1 = listening, 2 = speaking
static void drawMouth(int mouth) {
    auto& d = M5.Display;
    d.fillRect(112, 141, 96, 32, TFT_BLACK);
    if (mouth == 1) { d.drawCircle(160, 157, 9, TFT_GREEN); d.drawCircle(160, 157, 8, TFT_GREEN); return; }
    if (mouth == 2) { d.fillEllipse(160, 157, 18, 10, TFT_WHITE); d.fillEllipse(160, 159, 12, 5, TFT_RED); return; }
    switch (s_mood) {
    case HAPPY:     d.fillArc(160, 143, 15, 21, 20, 160, TFT_WHITE); break;    // smile
    case SAD:       d.fillArc(160, 175, 14, 19, 205, 335, TFT_WHITE); break;   // frown
    case SURPRISED: d.fillCircle(160, 157, 11, TFT_WHITE); d.fillCircle(160, 157, 6, TFT_BLACK); break;
    case ANGRY:     d.fillRect(142, 155, 36, 5, TFT_WHITE); break;
    case SLEEPY:    d.fillCircle(160, 157, 4, TFT_WHITE); break;
    case THINKING:  d.drawWideLine(146, 159, 174, 153, 2, TFT_WHITE); break;   // "hmm"
    default:        d.fillRect(140, 155, 40, 5, TFT_WHITE);
    }
}

// ---------------------------------------------------------------- moods, head, dance
// Function calls arrive on the network task: they only store what to do; loop()
// does the screen, LEDs and I2C. The servo target is safe from any task.
static const uint8_t LED = 40;   // the firmware never goes above LED_MAX_LEVEL
struct MoodStyle { uint8_t r, g, b; float tilt; bool keepTilt; };
static const MoodStyle MOOD_STYLE[MOOD_COUNT] = {
    {0, LED, 0, 0, true},              // neutral
    {LED, LED, 0, 15, false},          // happy
    {0, 0, LED, -5, false},            // sad
    {LED, LED, LED, 25, false},        // surprised
    {LED, 0, 0, 5, false},             // angry
    {LED / 4, 0, LED / 4, -5, false},  // sleepy
    {LED / 2, 0, LED, 20, false},      // thinking
};
static volatile float s_panDeg = 0, s_tiltDeg = 0;   // where we last sent the head
static volatile int s_ledR = -1, s_ledG = 0, s_ledB = 0;
static volatile uint32_t s_danceUntil = 0;
static volatile bool s_danceStop = false;
static int s_moodAfterDance = NEUTRAL;

static float wrap180(float d) { while (d > 180) d -= 360; while (d < -180) d += 360; return d; }

static void head(float panDeg, float tiltDeg) {
    s_panDeg = wrap180(panDeg);
    s_tiltDeg = constrain(tiltDeg, -7.0f, 73.0f);
    servoSetTarget(s_panDeg * DEG_TO_RAD, s_tiltDeg * DEG_TO_RAD);
}

static void leds(int r, int g, int b) { s_ledG = g; s_ledB = b; s_ledR = r; }

static void showMood(int m) {
    s_wantMood = m;
    leds(MOOD_STYLE[m].r, MOOD_STYLE[m].g, MOOD_STYLE[m].b);
    if (!MOOD_STYLE[m].keepTilt) head(s_panDeg, MOOD_STYLE[m].tilt);
}

static bool dancing() { return (int32_t)(s_danceUntil - millis()) > 0; }

static void onFunction(const char* name, JsonObjectConst args, JsonObject result) {
    if (!strcmp(name, "look") || !strcmp(name, "turn_head") || !strcmp(name, "center_head")) {
        float pan = args["pan"] | 0.0f, tilt = args["tilt"] | 0.0f;
        if (!strcmp(name, "turn_head")) { pan += s_panDeg; tilt += s_tiltDeg; }
        s_danceStop = true;
        head(pan, tilt);
        result["ok"] = true;
        result["pan"] = (int)s_panDeg;
        result["tilt"] = (int)s_tiltDeg;
    } else if (!strcmp(name, "set_mood")) {
        const char* mood = args["mood"] | "neutral";
        int m = -1;
        for (int i = 0; i < MOOD_COUNT; i++) if (!strcmp(mood, MOOD_NAMES[i])) m = i;
        if (m < 0) { result["error"] = "unknown mood"; return; }
        if (dancing()) {            // don't stop the dance: show it when the dance ends
            s_moodAfterDance = m;
            result["note"] = "still dancing; this mood shows when the dance ends";
        } else {
            showMood(m);
        }
        result["ok"] = true;
    } else if (!strcmp(name, "dance")) {
        float s = args["seconds"] | 8.0f;
        s = constrain(s, 3.0f, 30.0f);
        if (!dancing()) s_moodAfterDance = s_wantMood;
        s_danceStop = false;
        s_danceUntil = millis() + (uint32_t)(s * 1000);
        result["ok"] = true;
        result["seconds"] = (int)s;
    } else if (!strcmp(name, "stop_dance")) {
        result["was_dancing"] = dancing();
        s_danceStop = true;
        result["ok"] = true;
    } else if (!strcmp(name, "set_volume")) {
        int level = constrain((int)(args["level"] | 80), 0, 100);
        audioSetVolume(level * 255 / 100);
        result["ok"] = true;
        result["level"] = level;
    } else if (!strcmp(name, "robot_status")) {
        result["battery_percent"] = (int)M5.Power.getBatteryLevel();
        result["charging"] = M5.Power.isCharging() == m5::Power_Class::is_charging;
        result["wifi_rssi"] = WiFi.RSSI();
    } else {
        result["error"] = "unknown function";
    }
}

// One pose and one LED color per beat (0.5 s), as in demo 02.1.
static const int8_t DANCE[4][4][2] = {
    {{40, 15}, {-40, 15}, {40, 15}, {-40, 15}},     // left-right
    {{0, 40}, {0, 0}, {0, 40}, {0, 0}},             // head bob
    {{50, 30}, {15, 5}, {-15, 30}, {-50, 5}},       // sweep
    {{30, 30}, {-30, 30}, {30, 2}, {-30, 2}},       // square
};
static const uint8_t DANCE_COLORS[6][3] = {{LED, 0, 0}, {0, LED, 0}, {0, 0, LED}, {LED, 0, LED}, {LED, LED / 2, 0}, {0, LED, LED}};

static void danceTick(uint32_t now) {
    static bool was = false;
    static uint32_t nextBeat = 0;
    static int beat = 0;
    if (s_danceStop && dancing()) s_danceUntil = now;
    bool on = dancing();
    if (on && !was) { beat = 0; nextBeat = now; s_wantMood = HAPPY; }
    if (on && (int32_t)(now - nextBeat) >= 0) {
        const int8_t* p = DANCE[(beat / 4) % 4][beat % 4];
        head(p[0], p[1]);
        const uint8_t* c = DANCE_COLORS[beat % 6];
        setBaseLeds(c[0], c[1], c[2]);
        beat++;
        nextBeat = now + 500;
    }
    if (!on && was) {               // back to the front and to the mood it had
        head(0, 0);
        showMood(s_moodAfterDance);
    }
    was = on;
}

// ---------------------------------------------------------------- status bar
static M5Canvas status(&M5.Display);

static void drawStatus() {
    status.fillSprite(TFT_BLACK);
    status.setCursor(4, 2);
    if (WiFi.status() != WL_CONNECTED) {
        status.setTextColor(TFT_ORANGE);
        status.printf("WiFi: conectando a %s...", WIFI_SSID);
    } else {
        status.setTextColor(geminiReady() ? TFT_GREEN : TFT_YELLOW);
        status.printf("Gemini: %s", geminiState());
        status.setTextColor(TFT_DARKGREY);
        status.printf("   %s", WiFi.localIP().toString().c_str());
    }
    status.setCursor(4, 16);
    status.setTextColor(audioMuted() ? TFT_RED : audioSpeaking() ? TFT_CYAN : audioMicOn() ? TFT_GREEN : TFT_LIGHTGREY);
    const char* state = audioMuted() ? "micro silenciado (toca para activar)"
                      : audioSpeaking() ? "hablando"
                      : audioMicOn() ? "escuchando" : "...";
    status.printf("Bat %d%%  %s", (int)M5.Power.getBatteryLevel(), state);
    status.setCursor(4, 30);
    status.setTextColor(TFT_LIGHTGREY);
    status.print(geminiLastHeard());   // Latin text only: the built-in font has no Arabic
    status.pushSprite(0, 240 - 44);
}

// ---------------------------------------------------------------- setup / loop
static bool s_started = false;

void setup() {
    // Servo current peaks cause brief voltage dips; don't reset on them.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setBrightness(30);
    M5.Display.fillScreen(TFT_BLACK);
    initColors();
    drawStatics();
    status.setColorDepth(16);
    status.createSprite(320, 44);
    status.setTextSize(1);

    M5.Speaker.begin();
    M5.Speaker.setVolume(audioVolume());
    bool base = boardInit();
    servoInit();
    setBaseLeds(0, 0, LED);
    Serial.printf("\n[Professor standalone] base:%s\n", base ? "ok" : "NO");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("stackchan-profesor");
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    servoStartTask();
    M5.Speaker.tone(2000, 80);   // before audioStart(): nothing else uses the speaker yet
}

void loop() {
    i2cLock();   // touch, PMIC, LEDs: the audio task also uses this bus
    M5.update();
    uint32_t now = millis();
    static uint32_t lastUi = 0;
    static wl_status_t lastWifi = WL_IDLE_STATUS;

    wl_status_t w = WiFi.status();
    if (w != lastWifi) {
        lastWifi = w;
        if (w == WL_CONNECTED) {
            Serial.printf("[WIFI] connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            setBaseLeds(0, LED, 0);
            if (!s_started) {
                audioStart();
                geminiStart(onFunction);
                s_started = true;
            }
        } else {
            setBaseLeds(LED, LED / 2, 0);
        }
    }

    if (s_ledR >= 0) { setBaseLeds(s_ledR, s_ledG, s_ledB); s_ledR = -1; }
    danceTick(now);
    if (now - lastUi >= 500) { lastUi = now; drawStatus(); }
    if (s_started && M5.Touch.getCount() && M5.Touch.getDetail().wasPressed()) audioTap();

    // Face: eyes when the mood changes; mouth open while listening or speaking.
    static int lastMouth = -1;
    static uint32_t nextBlink = 3000, blinkEnd = 0;
    if (s_wantMood != s_mood) {
        s_mood = s_wantMood;
        drawEyes(s_mood);
        drawBrows(s_mood);
        lastMouth = -1;
        blinkEnd = 0;
    }
    if (s_mood != HAPPY && s_mood != SLEEPY) {
        if (!blinkEnd && now >= nextBlink) { drawEyes(s_mood, true); blinkEnd = now + 130; }
        else if (blinkEnd && now >= blinkEnd) {
            drawEyes(s_mood);
            blinkEnd = 0;
            nextBlink = now + 2500 + esp_random() % 4000;
        }
    }
    int mouth = audioSpeaking() ? 2 : (audioMicOn() ? 1 : 0);
    if (mouth != lastMouth) { lastMouth = mouth; drawMouth(mouth); }

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line == "home") servoHome();
        else if (line.startsWith("face ")) {
            for (int i = 0; i < MOOD_COUNT; i++) if (line.substring(5) == MOOD_NAMES[i]) showMood(i);
        }
    }
    i2cUnlock();
    delay(10);
}
