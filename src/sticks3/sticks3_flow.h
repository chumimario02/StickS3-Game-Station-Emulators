#pragma once
#include <string>

// StickS3 only: the ROM library lives in flash (no SD card).
// Shows the ROM list and the WiFi upload screen. When the user picks a ROM it is mapped
// through XIP (get_rom_ptr() / get_rom_size() are valid) and its file name is returned.
bool sticks3_pick_rom(std::string& outName);
