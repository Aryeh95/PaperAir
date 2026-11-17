# PlatformIO upload script for paperdiy-ota: streams firmware.bin to http://<host>/update.
# Uses HTTP POST rather than espota so uploads work across IoT VLANs and host firewalls.
# PAPERAIR_PASSWORD in the environment wins; otherwise WEB_PASSWORD from include/secrets.h.
# PAPERAIR_HOST replaces paperair.local when mDNS doesn't resolve on this computer.
import http.client
import json
import os
import re
import sys

Import("env")  # noqa: F821


def read_password(project_dir):
    pw = os.environ.get("PAPERAIR_PASSWORD")
    if pw is not None:
        return pw
    try:
        with open(os.path.join(project_dir, "include", "secrets.h"), encoding="utf-8") as f:
            m = re.search(r'#define\s+WEB_PASSWORD\s+"([^"]*)"', f.read())
            if m:
                return m.group(1)
    except OSError:
        pass
    return ""


def on_upload(source, target, env):
    firmware = str(source[0])
    host = os.environ.get("PAPERAIR_HOST") or env.subst("$UPLOAD_PORT") or "paperair.local"
    password = read_password(env.subst("$PROJECT_DIR"))
    size = os.path.getsize(firmware)

    boundary = "----PaperAirFirmwareBoundary"
    head = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode("ascii")
    tail = f"\r\n--{boundary}--\r\n".encode("ascii")
    total = len(head) + size + len(tail)

    print(f"Uploading {size // 1024} KB to http://{host}/update ...")
    conn = http.client.HTTPConnection(host, 80, timeout=60)
    try:
        conn.putrequest("POST", "/update")
        conn.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
        conn.putheader("Content-Length", str(total))
        if password:
            conn.putheader("Authorization", f"Bearer {password}")
        conn.endheaders()

        conn.send(head)
        sent = 0
        last_pct = -1
        with open(firmware, "rb") as f:
            while True:
                chunk = f.read(16384)
                if not chunk:
                    break
                conn.send(chunk)
                sent += len(chunk)
                pct = sent * 100 // size
                if pct // 10 != last_pct // 10:
                    print(f"  {pct}%")
                    sys.stdout.flush()
                    last_pct = pct
        conn.send(tail)

        resp = conn.getresponse()
        body = resp.read().decode("utf-8", "replace")
        if resp.status != 200:
            err = body
            try:
                err = json.loads(body).get("error", body)
            except ValueError:
                pass
            sys.stderr.write(f"OTA failed (HTTP {resp.status}): {err}\n")
            return 1
        print("Uploaded. PaperAir is restarting into the new firmware.")
        return 0
    except OSError as e:
        sys.stderr.write(f"OTA connection to {host} failed: {e}\n")
        return 1
    finally:
        conn.close()


env.Replace(UPLOADCMD=on_upload)  # noqa: F821
