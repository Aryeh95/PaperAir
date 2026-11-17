#include "ota.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

#include <memory>

namespace {

bool pending = false;  // this firmware still has to prove itself
bool served = false;   // the web page has answered a request since boot
bool started = false;
String current_pw;
std::unique_ptr<ArduinoOTAClass> ota;
std::function<void()> on_restart;

void check_pending() {
  static bool checked = false;
  if (checked) return;
  checked = true;
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    pending = true;
    Serial.printf("Firmware: new, running from %s, not confirmed yet\n", running->label);
  }
}

void configure_ota(const String &password) {
  if (ota) {
    ota->end();
    ota.reset();
  }
  current_pw = password;
  ota = std::make_unique<ArduinoOTAClass>();
  ota->setHostname("paperair");
  ota->setMdnsEnabled(false);  // the web server already runs mDNS; just add/update the service
  if (password.length()) ota->setPassword(password.c_str());
  else Serial.println("OTA: no web password set, so uploads need no password either");
  MDNS.enableArduino(3232, password.length() > 0);
  ota->onStart([] { Serial.println("OTA: update starting"); });
  ota->onEnd([] {
    if (on_restart) on_restart();
    Serial.println("OTA: done, restarting");
  });
  ota->onProgress([](unsigned int done, unsigned int total) {
    esp_task_wdt_reset();
    static unsigned int last = 100;
    const unsigned int pct = total ? done * 100 / total : 0;
    if (pct / 10 != last / 10) Serial.printf("OTA: %u%%\n", pct);
    last = pct;
  });
  ota->onError([](ota_error_t e) { Serial.printf("OTA: error %d\n", (int) e); });
  ota->begin();
}

}  // namespace

// Without this the Arduino core marks new firmware good the moment it boots (before setup()), so a
// version that crashes or hangs during startup would never roll back. With it, the firmware stays
// on trial until ota_loop() confirms it; any restart before then (a crash, or a watchdog reset
// after a hang) boots the previous version again.
extern "C" bool verifyRollbackLater() { return true; }

void ota_begin(const String &password, std::function<void()> before_restart) {
  check_pending();
  if (before_restart) on_restart = before_restart;
  if (started) return;
  started = true;
  configure_ota(password);
  Serial.println("OTA: ready on port 3232 (pio run -e paperdiy-ota -t upload)");
}

void ota_set_password(const String &password) {
  if (!started || password == current_pw) return;
  configure_ota(password);
  Serial.println("OTA: updated password");
}

void ota_loop() {
  check_pending();
  if (ota) ota->handle();
  // Confirm the new firmware once it has answered a web request and run for half a minute, or
  // after five minutes regardless (it's running; the web page may just not have been opened)
  if (pending && ((served && millis() > 30000UL) || millis() > 5UL * 60UL * 1000UL)) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      pending = false;
      Serial.println("Firmware: confirmed, the previous one stays available in the other slot");
    }
  }
}

void ota_web_served() { served = true; }
bool ota_pending() { return pending; }

bool ota_can_roll_back() {
  const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
  if (!other) return false;
  esp_app_desc_t desc;
  if (esp_ota_get_partition_description(other, &desc) != ESP_OK) return false;  // nothing there
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(other, &state) == ESP_OK && (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED)) return false;
  return true;
}

void ota_roll_back() {
  const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
  if (!other || esp_ota_set_boot_partition(other) != ESP_OK) return;
  if (on_restart) on_restart();
  Serial.printf("Firmware: booting the previous one from %s\n", other->label);
  delay(200);
  ESP.restart();
}
