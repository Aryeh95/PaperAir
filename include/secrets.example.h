#pragma once
// Copy this file to include/secrets.h and fill it in. secrets.h is ignored by git, so your
// passwords and keys stay on your computer.

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// Outdoor air, first choice: a PurpleAir sensor near you.
//   Read key: free from https://develop.purpleair.com (sign in, then "API Keys")
//   Sensor index: open the sensor on https://map.purpleair.com; it's the number after
//   "select=" in the address bar. The backup is used when the first has no recent reading.
// Leave the key empty to skip PurpleAir.
#define PURPLEAIR_API_KEY ""
#define PURPLEAIR_SENSOR_INDEX 0
#define PURPLEAIR_BACKUP_SENSOR_INDEX 0  // 0 = no backup

// Outdoor air, fallback: AirNow (US government monitors). Free key from
// https://docs.airnowapi.org/ . Leave the key empty to skip AirNow.
#define AIRNOW_API_KEY ""
#define AIRNOW_ZIP_CODE ""
