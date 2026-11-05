#pragma once
#include <Arduino.h>

// Microphone and speaker for the standalone robot (adapted from demo 02's audio.cpp):
//   microphone (16 kHz) -> mic ring -> gemini.cpp -> Gemini Live
//   Gemini's voice (24 kHz) -> audioPlay() -> speaker
// The CoreS3 shares one I2S bus between the microphones and the speaker, so it
// is half-duplex: while the robot speaks it cannot hear. Hands-free: the mic is
// open whenever the robot isn't speaking and the screen tap mutes it.

void audioStart();                         // starts the audio task (the only one that touches Mic/Speaker)
size_t audioMicRead(int16_t* dst, size_t max);   // samples recorded and not sent yet (any task)
size_t audioMicAvailable();
void audioPlay(const int16_t* pcm, size_t n);    // queue Gemini's voice (any task)
void audioTurnStarted();                   // Gemini started answering: keep the mic closed
void audioTurnDone();                      // Gemini finished its turn
void audioReset();                         // new session: drop what is queued
void audioSetVolume(int v);                // 0-255
int audioVolume();
void audioBeep(int hz, int ms);
void audioTap();                           // screen tap: mute/unmute
bool audioMicOn();
bool audioSpeaking();
bool audioMuted();
