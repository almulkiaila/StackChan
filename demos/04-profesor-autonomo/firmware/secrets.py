# PlatformIO pre-script:
# - adds the repo's config/ folder (wifi_secrets.h) with an absolute path (relative
#   "-I../.." flags are resolved against the wrong folder by PlatformIO on Windows);
# - passes the Gemini key from the GEMINI_API_KEY environment variable as a define,
#   so it is never stored in a file of the repo.
import os
Import("env")
env.Append(CPPPATH=[os.path.abspath(os.path.join(env["PROJECT_DIR"], "..", "..", "..", "config"))])
key = os.environ.get("GEMINI_API_KEY", "")
if not key and os.name == "nt":
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as k:
            key = winreg.QueryValueEx(k, "GEMINI_API_KEY")[0]
    except OSError:
        pass
if not key:
    print("\n*** GEMINI_API_KEY is not set: the robot won't be able to talk to Gemini ***\n")
env.Append(CPPDEFINES=[("GEMINI_API_KEY", env.StringifyMacro(key))])
