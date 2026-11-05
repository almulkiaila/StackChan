// Professor Stack-chan: a nerdy professor face (glasses, bow tie) with Gemini Live voice.
// Based on demo 02.1 of rhyloo/StackChan-Course. Demo 02's microphone and speaker over UDP
// (audio.cpp, compiled in from demo 02), without the VR viewer: the relay moves the
// head with POSE packets and changes the face with "face <mood>".
// Hands-free by default: just talk. Tap the screen to mute/unmute the mic.
// The hardware and the UDP link live in lib/ (StackChanHW, StackChanLink).
#include <M5Unified.h>
#include <WiFi.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include "link_config.h"
#include "board.h"
#include "camera.h"
#include "link.h"
#include "audio.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static M5Canvas status(&M5.Display);
static bool s_linkStarted = false;

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

// "face <mood>" from the relay. Runs on the network task: only store it (the screen
// belongs to loop()). Everything else goes to the audio commands.
static bool onCommand(const char* cmd) {
    if (!strncmp(cmd, "face ", 5)) {
        for (int i = 0; i < MOOD_COUNT; i++)
            if (!strcmp(cmd + 5, MOOD_NAMES[i])) s_wantMood = i;
        return true;
    }
    return audioCommand(cmd);
}

static void drawStatus() {
    LinkStats st;
    linkGetStats(&st);
    status.fillSprite(TFT_BLACK);
    status.setTextColor(TFT_LIGHTGREY);
    status.setCursor(4, 2);
    if (WiFi.status() != WL_CONNECTED) {
        status.setTextColor(TFT_ORANGE);
        status.printf("WiFi: connecting to %s...", WIFI_SSID);
    } else {
        status.printf("%s  stackchan-%s", WiFi.localIP().toString().c_str(), robotId());
    }
    status.setCursor(4, 16);
    if (st.relayOnline) {
        status.setTextColor(TFT_GREEN);
        status.printf("Laptop %s", st.relayIp);
    } else {
        status.setTextColor(TFT_YELLOW);
        status.print("Looking for the laptop (relay)...");
    }
    status.setTextColor(TFT_LIGHTGREY);
    status.setCursor(4, 30);
    if (audioMicOn()) status.setTextColor(TFT_RED);
    else if (audioSpeaking()) status.setTextColor(TFT_CYAN);
    const char* state = audioMuted() ? "mic muted (tap to unmute)"
                      : audioSpeaking() ? "speaking"
                      : audioMicOn() ? "listening"
                      : audioHandsFree() ? "..." : "tap to talk";
    status.printf("Bat %d%%  %s  %s", (int)M5.Power.getBatteryLevel(), state, st.lastText);
    status.pushSprite(0, 240 - 44);
}

void setup() {
    // Servo current peaks cause brief voltage dips; don't reset on them.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setBrightness(30);   // brighter heats the screen and drains the battery
    M5.Display.fillScreen(TFT_BLACK);
    initColors();
    drawStatics();
    status.setColorDepth(16);
    status.createSprite(320, 44);
    status.setTextSize(1);

    M5.Speaker.begin();
    M5.Speaker.setVolume(120);

    bool base = boardInit();
    servoInit();
    setBaseLeds(0, 0, 40);
    bool camera = cameraInit();     // the link streams video if someone asks; this demo doesn't
    Serial.printf("\n[Professor " FW_VERSION "] base:%s camera:%s\n", base ? "ok" : "NO", camera ? "ok" : "ERROR");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("stackchan");
    WiFi.setSleep(false);              // modem sleep adds 100+ ms of jitter to every packet
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    servoStartTask();
    M5.Speaker.tone(2000, 80);   // before audioStart(): nothing else uses the speaker yet
}

void loop() {
    i2cLock();   // touch, PMIC, LEDs: the audio task also uses this bus
    M5.update();
    static uint32_t lastUi = 0, lastBatt = 0;
    static wl_status_t lastWifi = WL_IDLE_STATUS;
    uint32_t now = millis();

    wl_status_t w = WiFi.status();
    if (w != lastWifi) {
        lastWifi = w;
        if (w == WL_CONNECTED) {
            Serial.printf("[WIFI] connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            setBaseLeds(0, 40, 0);
            if (!s_linkStarted) {
                linkStart();
                audioStart();
                linkSetCommandHandler(onCommand);   // after audioStart(): ours first, then the audio ones
                s_linkStarted = true;
            }
        } else {
            setBaseLeds(40, 20, 0);
        }
    }

    if (now - lastBatt >= 2000) {
        lastBatt = now;
        float busV, mA;
        if (!readBatteryCurrent(&busV, &mA)) mA = NAN;
        linkSetBattery(M5.Power.getBatteryVoltage() / 1000.0f, M5.Power.getBatteryLevel(), mA);
    }

    PendingCommands pc;
    linkTakePending(&pc);
    if (pc.brightness >= 0) M5.Display.setBrightness(constrain(pc.brightness, 0, 255));
    if (pc.wifiSleep >= 0) WiFi.setSleep(pc.wifiSleep == 1);
    if (pc.leds[0] >= 0) setBaseLeds(pc.leds[0], pc.leds[1], pc.leds[2]);
    if (now - lastUi >= 500) {
        lastUi = now;
        drawStatus();
    }
    // Tap the screen: mute/unmute (hands-free) or talk/stop (push-to-talk).
    if (s_linkStarted && M5.Touch.getCount() && M5.Touch.getDetail().wasPressed()) audioTap();

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
    // Blink now and then (not when the eyes are already closed or smiling).
    if (s_mood != HAPPY && s_mood != SLEEPY) {
        if (!blinkEnd && now >= nextBlink) { drawEyes(s_mood, true); blinkEnd = now + 130; }
        else if (blinkEnd && now >= blinkEnd) {
            drawEyes(s_mood);
            blinkEnd = 0;
            nextBlink = now + 2500 + esp_random() % 4000;
        }
    }
    int mouth = audioSpeaking() ? 2 : (audioMicOn() ? 1 : 0);
    if (mouth != lastMouth) {
        lastMouth = mouth;
        drawMouth(mouth);
    }

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line == "home") servoHome();
        else if (line.startsWith("face ")) onCommand(line.c_str());   // try the faces without the relay
    }
    i2cUnlock();
    delay(10);
}
