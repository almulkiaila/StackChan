# Professor Stack-chan 🤓

Stack-chan (M5Stack CoreS3 on the K151 base) turned into a **nerdy professor** you can talk to:
thick tortoiseshell glasses, bushy eyebrows, frizzy grey hair, a moustache and a red bow tie on the
screen, and a voice powered by **Gemini Live**. It answers questions like an enthusiastic teacher,
shows moods on its face (including a new *thinking* face), moves its head and dances.

Based on demos 02 (Gemini voice) and 02.1 (voice moves) of
[rhyloo/StackChan-Course](https://github.com/rhyloo/StackChan-Course).

```
robot mic --UDP--> relay (laptop) --> Gemini Live --> voice --UDP--> robot speaker
                                       └-> set_mood("thinking"), look(...), dance(...)
```

## Setup (Windows)

1. `setup.bat` — creates `.venv` with PlatformIO and the Python packages.
2. Copy `config\wifi_secrets.h.example` to `config\wifi_secrets.h` and fill in your **2.4 GHz** Wi-Fi
   (it is in `.gitignore`: never committed).
3. Get a Gemini key at https://aistudio.google.com/apikey and run once:
   `setx GEMINI_API_KEY "your-key"` (then open a new terminal).

## Run

Connect the robot with USB-C and:

```
demos\03-profesor\run.bat            :: build, flash and start the relay
demos\03-profesor\run.bat -NoFlash   :: the robot already has the firmware: only start the relay
demos\03-profesor\run.bat -FlashOnly :: only build and flash
```

Wait for `Gemini: ready` and talk. Tap the screen to mute/unmute the mic.
Extra relay options: `--voice Puck` (default `Charon`), `--push-to-talk`.

Try: "¿Qué es un agujero negro?", "Explícame la recursividad", "Pon cara de pensar", "¡Baila!",
"Mira a la izquierda".

## Where things are

| Path | What |
|---|---|
| `demos/03-profesor/firmware/src/main.cpp` | The professor face (`drawStatics`, `drawEyes`, `drawBrows`, `drawMouth`), blinking, moods |
| `demos/03-profesor/firmware/src/audio.cpp` | Mic and speaker over UDP (from demo 02) |
| `demos/03-profesor/relay/voice_moves.py` | Personality (`INSTRUCTIONS`), moods and Gemini functions |
| `demos/03-profesor/relay/main.py` | Relay entry point (model, voice) |
| `demos/02-gemini-voice/` | Original Gemini voice demo (its relay `voice.py` is reused) |
| `lib/` | Shared hardware (servos, LEDs, camera) and UDP link code |
| `docs/ROBOT.md` | Hardware reference |

Test the faces without Gemini from the serial monitor: `face thinking`, `face happy`, `face surprised`…
