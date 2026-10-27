#pragma once

// True if the button is held for CONFIG_HOLD_MS right after power-on (LED blinks white meanwhile).
bool configButtonHeld();
// Wi-Fi setup + firmware upload web UI. Never returns; reboots on upload, request, or inactivity.
[[noreturn]] void runConfigMode();
