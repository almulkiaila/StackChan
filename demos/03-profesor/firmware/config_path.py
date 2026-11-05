# Adds the repo's config/ folder (wifi_secrets.h) to the include path with an absolute path:
# relative "-I../.." flags are resolved against the wrong folder by PlatformIO on Windows.
import os
Import("env")
env.Append(CPPPATH=[os.path.abspath(os.path.join(env["PROJECT_DIR"], "..", "..", "..", "config"))])
