#include <M5Cardputer.h>
#include <select_rom.h>
#include "cardputer/CardputerView.h"
#include "cardputer/CardputerInput.h"
#include "cardputer/SdService.h"
#include "vfs/vfs_xip.h"
#include "vfs/rom_flash_io.h"
#include "vfs/rom_xip.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "nes/run_nes.h"
#include "sms/run_sms.h"
#include "ngp/run_ngp.h"
#include "ws/run_ws.h"
#include "pce/run_pce.h"
#include "lynx/run_lynx.h"
#include "genesis/run_genesis.h"
#include "gbc/run_gbc.h"
#include "msx/run_msx.h"
#include "snes/run_snes.h"
#include "atari7800/run_a7800.h"
#include "atari2600/run_a2600.h"
#include "gx4000/run_gx4000.h"
#include "last_game.h"
#define RETRO_COMPAT_IMPLEMENTATION
#include "ngp/race/retro_compat.h"
#include "esp_task_wdt.h"
#include "share/input.h"
#include "share/emu_log_cpp.h"
#ifdef TARGET_STICKS3
#include "sticks3/sticks3_flow.h"
#endif

void setup() {
  // Set high priority for the current task (where the emulator will run)
  vTaskPrioritySet(NULL, 19);

  // Copied from Gameboy Enhanced Firmware setup
#ifdef DISABLE_WATCHDOGS
  M5.Log.printf("Disabling all WatchDogs...\n");
  esp_task_wdt_deinit(); // fully disables and removes TWDT
  disableCore0WDT(); // disable WDT
  disableCore1WDT(); // disable WDT
  esp_task_wdt_delete(NULL); // disable WDT on this therad - legacy
#endif

  auto cfg = M5.config();
  cfg.output_power = true;
  M5Cardputer.begin(cfg);
#ifdef TARGET_STICKS3
  M5Cardputer.Display.setRotation(1); // landscape 240x135 like the Cardputer (use 3 if upside down)
#endif
  CardputerInput input;
  CardputerView display;
  display.initialize();

  std::string romPath;

#ifdef TARGET_STICKS3
  // ROM library stored in flash: pick a ROM (or upload new ones over WiFi).
  // On success the ROM is already mapped through XIP.
  std::string flashRomName;
  while (!sticks3_pick_rom(flashRomName)) {
    delay(500);
  }
  romPath = "/xip/" + flashRomName; // the XIP VFS accepts any file name
  EMU_LOG("Selected ROM: %s\n", romPath.c_str());
#else
  SdService sd;

  // SD
  while (!sd.begin()) {
    display.topBar("SD CARD FOR ROMS", false, false);
    display.subMessage("No SD card found", 1000);
    display.subMessage("Insert SD card", 0);
  }

  auto romFolder = getRomFolderFromNvs(display, input, sd);
  romFolder = romFolder.empty() ? "/" : romFolder;

  if (isQuittingGame()) {
    romPath = getRomPath(sd, display, input, romFolder, true);
  } else {
    // Welcome
    display.welcome();
    input.waitPress(4000);

    // Try to get last game from NVS or select a new one
    romPath = getLastGameFromNvs(display, input, sd);
    if (romPath.empty()) {
      auto skipWelcome = romFolder == "/" ? false : true;
      romPath = getRomPath(sd, display, input, romFolder, skipWelcome);
    } else {
      romPath = "/sd" + romPath; // ensure sd prefix
    }
  }
  EMU_LOG("Selected ROM: %s\n", romPath.c_str());

  display.topBar("COPYING ROM TO FLASH", false, false);
  display.subMessage("Loading...", 0);
  
  // Find the rom partition (SPIFFS)
  const esp_partition_t* romPart = findRomPartition("spiffs");
  if (!romPart) {
    while (1) {
      display.topBar("ERROR", false, false);
      display.subMessage("No ROM partition", 0);
      delay(1500);
    }
  }

  // Copy the ROM file to the partition
  size_t romSize = 0;
  if (!copyFileToPartition(romPath.c_str(), romPart, &romSize, CardputerView::copyProgress, &display)) {    
    // Rom limit is reached for the current SPIFFS layout
    while (1) {
        display.topBar("ROM IS TOO HEAVY", false, false);
        display.subMessage("If you're using launcher",2000);
        display.subMessage("Increase SPIFFS layout", 2000);
        delay(1500);
    }
  }

  input.flushInput(10); // flush any input just in case

  // Map the ROM partition in XIP
  if (xip_map_rom_partition("spiffs", romSize) != 0) {
    while (1) {
      display.topBar("ERROR", false, false);
      display.subMessage("Map ROM failed", 0);
      delay(1500);
    }
  }
#endif

  // Register the XIP VFS
  vfs_xip_register();
  // Check the extension to choose the emulator
  auto ext = getRomType(romPath);

  // Show keymapping
  display.topBar("- + SOUND [ ] BRIGHT", false, false);
  int numButtons = (ext == ROM_TYPE_GENESIS) ? 3 : 2;
  numButtons = (ext == ROM_TYPE_SNES) ? 6 : numButtons;
  display.showKeymapping(numButtons);

  // Wait for key press or show tips
  uint32_t lastUpdate = millis();
  int state = 0;
  for (;;) {
    char key = input.readChar();
    if (key != KEY_NONE) break;

    // Show tips
    uint32_t now = millis();
    if (now - lastUpdate >= 2000) {
      lastUpdate = now;
      switch (state) {
        case 0: display.topBar("PRESS ANY KEY TO START", false, false); break;
        case 1: display.topBar("KEY \\ SCREEN MODE",       false, false); break;
        case 2: display.topBar("G0 1SEC TO QUIT GAME",     false, false); break;
        case 3: display.topBar("FN + ARROWS FOR ZOOM",     false, false); break;
        case 4: display.topBar("- + SOUND [ ] BRIGHT",     false, false); break;
      }
      state = (state + 1) % 5;
    }
    delay(1);
  }

#ifndef TARGET_STICKS3
  // Save last game to nvs (the StickS3 flow keeps its own "last ROM" entry)
  if (ext != ROM_TYPE_UNKNOWN) {
      saveLastGameToNvs(romPath);
  }
#endif

  // Prepare rom filename for emulators
  auto pos = romPath.find_last_of("/\\");
  std::string romName = (pos == std::string::npos) ? romPath : romPath.substr(pos + 1);

#ifndef TARGET_STICKS3
  // Initialize I2C M5Stack JoyV2 if any (Cardputer Grove pins, not valid on the StickS3)
  share::detectI2cPad();
#endif
  
  EMU_LOG("HEAP BEFORE EMU: %u bytes\n", esp_get_free_heap_size());
  EMU_LOG("MAX BLOCK BEFORE EMU: %u bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  // Run the emulator
  if (ext == ROM_TYPE_NES) {
      // --- NES ---
      romName = "/xip/" + romName;
      run_nes(romName.c_str());
  }
  else if (ext == ROM_TYPE_GAMEGEAR || ext == ROM_TYPE_SMS || ext == ROM_TYPE_SG1000 || ext == ROM_TYPE_COLECO) {
      // --- SMS family (SMS / GG / SG-1000 / ColecoVision) ---
      SmsConsoleMode mode = SMS_MODE_SMS;
      if (ext == ROM_TYPE_GAMEGEAR) mode = SMS_MODE_GG;
      else if (ext == ROM_TYPE_SG1000) mode = SMS_MODE_SG1000;
      else if (ext == ROM_TYPE_COLECO) mode = SMS_MODE_COLECO;
      run_sms(get_rom_ptr(), get_rom_size(), mode, romName.c_str());
  }
  else if (ext == ROM_TYPE_NGP) {
      // --- Neo Geo Pocket / Color ---
      int machine = detectNeoGeoPocketFromRom(get_rom_ptr(), get_rom_size(), romPath);
      run_ngp(get_rom_ptr(), get_rom_size(), romName.c_str(), machine);
  }
  else if (ext == ROM_TYPE_GENESIS) {
      // --- Megadrive / Genesis ---
      run_genesis(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_WS) {
      // --- WonderSwan / Color ---
      run_ws(get_rom_ptr(), get_rom_size(), romName.c_str(), detectWonderSwanFromRom(romPath));
  }
  else if (ext == ROM_TYPE_PCE) {
      // --- PC Engine / TurboGrafx-16 ---
      run_pce(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_GB) { 
      // --- Game Boy / Color ---
      run_gbc(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_LYNX) {
      // --- Lynx ---
      run_lynx(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_SNES) {
      // --- SNES / Super Famicom ---
      display.displaySnesInfo();
      input.waitPress();
      run_snes(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_MSX) {
      // --- MSX ---
      display.displayMsxInfo();
      input.waitPress();
      run_msx(get_rom_ptr(), get_rom_size(), romName.c_str(), romPath.c_str());
  }
  else if (ext == ROM_TYPE_ATARI7800) {
      // --- Atari 7800 ---
      run_a7800(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
  else if (ext == ROM_TYPE_ATARI2600) {
      // --- Atari 2600 ---
      run_a2600(get_rom_ptr(), get_rom_size(), romName.c_str());
  }
    else if (ext == ROM_TYPE_GX4000) {
      // --- Amstrad GX4000 ---
      run_gx4000(get_rom_ptr(), get_rom_size(), romName.c_str());
    }
  else {
      display.topBar("ERROR", false, false);
      display.subMessage("Unsupported ROM type", 0);
      while (1) delay(1000);
  }
}

void loop() {
  /* run_emulator is blocking */
}
