#pragma once
// The firmware version shown on the web page and in the serial log. Bump it when you change
// something worth telling apart; the build date and time are added by the compiler.
constexpr const char *kVersion = "1.4";
constexpr const char *kBuild = __DATE__ " " __TIME__;
