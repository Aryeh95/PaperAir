# PlatformIO extra script for the paperdiy-ota environment: gives espota the device's password.
# PAPERAIR_PASSWORD in the environment wins; otherwise WEB_PASSWORD from include/secrets.h.
# PAPERAIR_HOST replaces paperair.local when mDNS doesn't resolve on this computer.
import os
import re

Import("env")  # noqa: F821

password = os.environ.get("PAPERAIR_PASSWORD")
if password is None:
    password = ""
    try:
        with open(os.path.join(env.subst("$PROJECT_DIR"), "include", "secrets.h")) as f:  # noqa: F821
            m = re.search(r'#define\s+WEB_PASSWORD\s+"([^"]*)"', f.read())
            if m:
                password = m.group(1)
    except OSError:
        pass
if password:
    env.Append(UPLOAD_FLAGS=["--auth=" + password])  # noqa: F821
if os.environ.get("PAPERAIR_HOST"):
    env.Replace(UPLOAD_PORT=os.environ["PAPERAIR_HOST"])  # noqa: F821
