/*
 * M5Cardputer API shim for M5Stack StickS3.
 *
 * Goal: let the existing emulator code (which calls M5Cardputer.Keyboard.isKeyPressed(),
 * M5Cardputer.Display, M5Cardputer.Speaker, ...) compile unchanged on the StickS3.
 *
 * Input sources:
 *  - StickS3 hardware buttons (BtnA / BtnB)
 *  - A BLE HID keyboard (e.g. 8BitDo Micro in Keyboard mode) through
 *    Keyboard_Class::injectHidKey(), to be called by a BLE HID client.
 *
 * Key mapping is kept identical to the Cardputer so no emulator needs changes:
 *   D-pad  : ';' up, '.' down, ',' left, '/' right   (arrow keys are mapped to these)
 *   A / B  : 'l' (or 'j') / 'k'
 *   START  : '1'      SELECT : '2'
 * In 8BitDo Ultimate Software (Keyboard mode) map the Micro buttons to those keys.
 *
 * StickS3 hardware buttons:
 *   BtnA (front) : acts as 'l' (A button) and ENTER. Long press 1s = quit game (checkCommonInput).
 *   BtnB (side)  : acts as '.' (down / next item in menus).
 */
#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <cstdint>
#include <vector>

#ifndef KEY_ENTER
#define KEY_LEFT_CTRL  0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT   0x82
#define KEY_FN         0xff
#define KEY_OPT        0x00
#define KEY_BACKSPACE  0x2a
#define KEY_TAB        0x2b
#define KEY_ENTER      0x28
#endif

struct Point2D_t {
    int x = 0;
    int y = 0;
};

class Keyboard_Class {
public:
    struct KeysState {
        bool tab          = false;
        bool fn           = false;
        bool shift        = false;
        bool ctrl         = false;
        bool opt          = false;
        bool alt          = false;
        bool del          = false;
        bool enter        = false;
        bool space        = false;
        uint8_t modifiers = 0;

        std::vector<char> word;
        std::vector<uint8_t> hid_keys;
        std::vector<uint8_t> modifier_keys;

        void reset()
        {
            tab = fn = shift = ctrl = opt = alt = del = enter = space = false;
            modifiers = 0;
            word.clear();
            hid_keys.clear();
            modifier_keys.clear();
        }
    };

    void begin() {}

    void updateKeysState();
    bool isChange();                 // true once after the set of pressed keys changed
    uint8_t isPressed();             // number of pressed keys
    bool isKeyPressed(char c);

    inline KeysState& keysState() { return _state; }
    inline const std::vector<Point2D_t>& keyList()
    {
        static const std::vector<Point2D_t> empty_list;
        return empty_list;
    }
    inline bool capslocked() { return false; }
    inline void setCapsLocked(bool) {}

    // --- StickS3 specific ---
    // Called every update with the state of the hardware buttons.
    void setButtons(bool a, bool b);
    // Called by the BLE HID client: HID usage id (keyboard page) + pressed/released.
    void injectHidKey(uint8_t usage, bool pressed);

private:
    volatile bool _ble[128] = {};
    volatile bool _bleEnter = false;
    volatile bool _bleDel   = false;
    volatile bool _bleTab   = false;
    bool _btnA = false;
    bool _btnB = false;

    bool _prev[128] = {};
    bool _prevEnter = false;
    bool _prevDel   = false;
    bool _prevTab   = false;
    bool _changed   = false;
    uint8_t _count  = 0;
    KeysState _state;
};

namespace m5 {

class M5_CARDPUTER {
public:
    void begin(bool enableKeyboard = true);
    void begin(m5::M5Unified::config_t cfg, bool enableKeyboard = true);

    M5GFX &Display         = M5.Display;
    M5GFX &Lcd             = Display;
    Power_Class &Power     = M5.Power;
    Speaker_Class &Speaker = M5.Speaker;
    Mic_Class &Mic         = M5.Mic;
    Button_Class &BtnA     = M5.getButton(0);
    Button_Class &BtnB     = M5.getButton(1);

    Keyboard_Class Keyboard;

    I2C_Class &In_I2C = m5::In_I2C;
    I2C_Class &Ex_I2C = m5::Ex_I2C;

    void update(void);

private:
    bool _enableKeyboard = true;
};

}  // namespace m5

extern m5::M5_CARDPUTER M5Cardputer;
