#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Gemini Live straight from the robot: a TLS WebSocket to
// generativelanguage.googleapis.com, the mic as 16 kHz PCM and the voice back as 24 kHz PCM.
//
//   setup (model, voice, personality, functions) -> setupComplete
//   realtimeInput.audio (base64 PCM, every 100 ms)   -> serverContent.modelTurn audio
//                                                    -> toolCall -> toolResponse
// Gemini runs nothing itself: it asks for a function and onFunction() runs it.

// Runs a function call (on the network task) and fills `result` with what Gemini gets back.
using FunctionHandler = void (*)(const char* name, JsonObjectConst args, JsonObject result);

void geminiStart(FunctionHandler handler);   // after Wi-Fi is up: starts the network task
bool geminiReady();
const char* geminiState();                   // short text for the status bar
const char* geminiLastHeard();               // last thing the person said (transcription)
