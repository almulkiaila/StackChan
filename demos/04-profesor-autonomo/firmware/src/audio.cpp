// Adapted from demos/02-gemini-voice/firmware/src/audio.cpp: same mic/speaker handling,
// but the audio goes to and comes from gemini.cpp instead of the UDP link.
#include "audio.h"
#include "board.h"
#include <M5Unified.h>

static constexpr uint32_t MIC_RATE = 16000;
static constexpr size_t MIC_SAMPLES = 320;          // 20 ms per record() call
static constexpr size_t MIC_RING = MIC_RATE * 2;    // 2 s of mic audio waiting to be sent
static constexpr uint32_t OUT_RATE = 24000;
static constexpr size_t OUT_BLOCK = 2400;           // 100 ms per playRaw call
static constexpr size_t OUT_RING = OUT_RATE * 30;   // 30 s of speech in PSRAM
static constexpr uint32_t OUT_IDLE_MS = 600;        // no audio for this long = the answer is over
static constexpr uint32_t TURN_TIMEOUT_MS = 15000;  // a turn with no audio for 15 s is over anyway
static constexpr uint32_t REOPEN_MIC_MS = 300;      // after speaking, wait so the mic doesn't catch the echo

static volatile int s_volume = 220;                 // 0-255. Loud + LEDs + Wi-Fi can brown out the robot
static volatile bool s_volumeChanged = false;
static volatile bool s_muted = false;
static volatile bool s_micOn = false;
static volatile bool s_turn = false;                // Gemini is answering (until turnComplete)
static volatile uint32_t s_turnMs = 0;

static int16_t* s_out = nullptr;                    // speaker ring (PSRAM)
static volatile size_t s_outHead = 0, s_outTail = 0;
static volatile uint32_t s_lastOutMs = 0;
static volatile bool s_speaking = false;
static volatile uint32_t s_spokeUntilMs = 0;

static int16_t* s_mic = nullptr;                    // mic ring (PSRAM)
static volatile size_t s_micHead = 0, s_micTail = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static volatile int s_beepHz = 0, s_beepMs = 0;

static size_t outBuffered() { return (s_outHead + OUT_RING - s_outTail) % OUT_RING; }
static bool answerArriving() { return millis() - s_lastOutMs < OUT_IDLE_MS; }

static bool wantMic() {
    if (s_turn && millis() - s_turnMs > TURN_TIMEOUT_MS) s_turn = false;
    if (s_muted || s_turn || s_speaking || outBuffered() > 0 || answerArriving()) return false;
    return (int32_t)(millis() - s_spokeUntilMs) > (int32_t)REOPEN_MIC_MS;
}

static void micPush(const int16_t* p, size_t n) {
    portENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < n; i++) {
        size_t next = (s_micHead + 1) % MIC_RING;
        if (next == s_micTail) s_micTail = (s_micTail + 1) % MIC_RING;   // full: drop the oldest
        s_mic[s_micHead] = p[i];
        s_micHead = next;
    }
    portEXIT_CRITICAL(&s_mux);
}

size_t audioMicAvailable() {
    portENTER_CRITICAL(&s_mux);
    size_t n = (s_micHead + MIC_RING - s_micTail) % MIC_RING;
    portEXIT_CRITICAL(&s_mux);
    return n;
}

size_t audioMicRead(int16_t* dst, size_t max) {
    portENTER_CRITICAL(&s_mux);
    size_t n = 0;
    while (n < max && s_micTail != s_micHead) {
        dst[n++] = s_mic[s_micTail];
        s_micTail = (s_micTail + 1) % MIC_RING;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

void audioPlay(const int16_t* pcm, size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t next = (s_outHead + 1) % OUT_RING;
        if (next == s_outTail) break;               // full: drop the rest
        s_out[s_outHead] = pcm[i];
        s_outHead = next;
    }
    s_lastOutMs = millis();
    s_turnMs = millis();
}

void audioTurnStarted() { s_turn = true; s_turnMs = millis(); }
void audioTurnDone() { s_turn = false; }
void audioReset() {
    s_turn = false;
    s_outTail = s_outHead;
    portENTER_CRITICAL(&s_mux);
    s_micTail = s_micHead;
    portEXIT_CRITICAL(&s_mux);
}

static void audioTask(void*) {
    static int16_t mic[3][MIC_SAMPLES];
    static int16_t out[3][OUT_BLOCK];
    int micIdx = 0, queued = 0, outIdx = 0;
    for (;;) {
        bool listen = wantMic() && !s_beepHz;
        if (listen != s_micOn) i2cLock();   // switching reconfigures the codec and the amp over I2C
        if (listen && !s_micOn) {
            M5.Speaker.stop();
            M5.Speaker.end();
            M5.Mic.begin();
            s_micOn = true;
            micIdx = queued = 0;
            i2cUnlock();
        } else if (!listen && s_micOn) {
            M5.Mic.end();
            M5.Speaker.begin();
            M5.Speaker.setVolume(s_volume);
            s_micOn = false;
            i2cUnlock();
        }

        if (s_micOn) {
            // record() queues the buffer and returns; with two in flight, the one
            // queued two calls ago is complete. It paces this loop at 20 ms.
            M5.Mic.record(mic[micIdx], MIC_SAMPLES, MIC_RATE);
            int ready = (micIdx + 1) % 3;
            micIdx = (micIdx + 1) % 3;
            if (++queued >= 3) micPush(mic[ready], MIC_SAMPLES);
            continue;
        }

        if (s_beepHz) {
            M5.Speaker.tone(s_beepHz, s_beepMs);
            s_spokeUntilMs = millis() + s_beepMs;
            s_beepHz = 0;
        }
        if (s_volumeChanged) {
            s_volumeChanged = false;
            I2CGuard lock;
            M5.Speaker.setVolume(s_volume);
        }

        size_t avail = outBuffered();
        if (!s_speaking && (avail >= OUT_RATE / 5 || (avail > 0 && !answerArriving()))) s_speaking = true;   // 200 ms cushion
        if (s_speaking && avail > 0 && M5.Speaker.isPlaying(0) < 2) {
            size_t n = avail < OUT_BLOCK ? avail : OUT_BLOCK;
            for (size_t i = 0; i < n; i++) { out[outIdx][i] = s_out[s_outTail]; s_outTail = (s_outTail + 1) % OUT_RING; }
            M5.Speaker.playRaw(out[outIdx], n, OUT_RATE, false, 1, 0);
            outIdx = (outIdx + 1) % 3;
        } else if (s_speaking && avail == 0 && !answerArriving() && !M5.Speaker.isPlaying(0)) {
            s_speaking = false;
            s_spokeUntilMs = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void audioStart() {
    s_out = (int16_t*)heap_caps_malloc(OUT_RING * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    s_mic = (int16_t*)heap_caps_malloc(MIC_RING * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    M5.Speaker.setVolume(s_volume);
    xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 1);
}

void audioSetVolume(int v) { s_volume = constrain(v, 0, 255); s_volumeChanged = true; }
int audioVolume() { return s_volume; }
void audioBeep(int hz, int ms) { s_beepMs = ms; s_beepHz = hz; }
void audioTap() { s_muted = !s_muted; }
bool audioMicOn() { return s_micOn; }
bool audioSpeaking() { return s_speaking; }
bool audioMuted() { return s_muted; }
