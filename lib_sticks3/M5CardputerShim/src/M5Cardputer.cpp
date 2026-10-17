#include "M5Cardputer.h"

m5::M5_CARDPUTER M5Cardputer;

// Virtual zoom keys (8BitDo Micro L / R in Keyboard mode)
static constexpr uint8_t ZOOM_OUT_KEY = 'q';  // L -> FN + ','
static constexpr uint8_t ZOOM_IN_KEY  = 'w';  // R -> FN + '/'

// ---------------------------------------------------------------------------
// M5_CARDPUTER
// ---------------------------------------------------------------------------
void m5::M5_CARDPUTER::begin(bool enableKeyboard)
{
    auto cfg = M5.config();
    begin(cfg, enableKeyboard);
}

void m5::M5_CARDPUTER::begin(m5::M5Unified::config_t cfg, bool enableKeyboard)
{
    _enableKeyboard = enableKeyboard;
    M5.begin(cfg);
    Keyboard.begin();
}

void m5::M5_CARDPUTER::update(void)
{
    M5.update();
    if (_enableKeyboard) {
        Keyboard.setButtons(BtnA.isPressed(), BtnB.isPressed());
        Keyboard.updateKeysState();
    }
}

// ---------------------------------------------------------------------------
// Keyboard_Class
// ---------------------------------------------------------------------------
void Keyboard_Class::setButtons(bool a, bool b)
{
    _btnA = a;
    _btnB = b;
}

static char hidToChar(uint8_t u)
{
    if (u >= 0x04 && u <= 0x1D) return (char)('a' + (u - 0x04));
    if (u >= 0x1E && u <= 0x26) return (char)('1' + (u - 0x1E));
    switch (u) {
        case 0x27: return '0';
        case 0x2C: return ' ';
        case 0x2D: return '-';
        case 0x2E: return '=';
        case 0x2F: return '[';
        case 0x30: return ']';
        case 0x31: return '\\';
        case 0x33: return ';';
        case 0x34: return '\'';
        case 0x35: return '`';
        case 0x36: return ',';
        case 0x37: return '.';
        case 0x38: return '/';
        // Arrow keys -> Cardputer arrow characters
        case 0x4F: return '/';  // right
        case 0x50: return ',';  // left
        case 0x51: return '.';  // down
        case 0x52: return ';';  // up
        default:   return 0;
    }
}

void Keyboard_Class::injectHidKey(uint8_t usage, bool pressed)
{
    switch (usage) {
        case 0x28: _bleEnter = pressed; return;  // Enter
        case 0x2A: _bleDel   = pressed; return;  // Backspace
        case 0x2B: _bleTab   = pressed; return;  // Tab
        default: break;
    }
    char c = hidToChar(usage);
    if (c != 0) {
        _ble[(uint8_t)c] = pressed;
    }
}

bool Keyboard_Class::isKeyPressed(char c)
{
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    const uint8_t i = (uint8_t)c;
    if (i >= 128) return false;

    if (_ble[i]) return true;

    // Zoom keys: L ('q') acts as FN + ',' and R ('w') acts as FN + '/'
    if (c == ',' && _ble[ZOOM_OUT_KEY]) return true;
    if (c == '/' && _ble[ZOOM_IN_KEY])  return true;

    if (_btnA && c == 'l') return true;  // front button = A
    if (_btnB && c == '.') return true;  // side button  = down / next
    return false;
}

void Keyboard_Class::updateKeysState()
{
    _state.reset();
    _count = 0;
    bool changed = false;

    for (uint8_t i = 0x20; i < 0x7F; i++) {
        if (i >= 'A' && i <= 'Z') continue;  // letters are stored lowercase
        const bool cur = isKeyPressed((char)i);
        if (cur) {
            _state.word.push_back((char)i);
            _count++;
        }
        if (cur != _prev[i]) {
            changed = true;
            _prev[i] = cur;
        }
    }

    _state.enter = _bleEnter || _btnA;
    _state.del   = _bleDel;
    _state.tab   = _bleTab;
    _state.space = isKeyPressed(' ');
    // FN is "pressed" while a zoom key (L / R) is held
    _state.fn    = _ble[ZOOM_OUT_KEY] || _ble[ZOOM_IN_KEY];

    if (_state.enter != _prevEnter) { changed = true; _prevEnter = _state.enter; }
    if (_state.del   != _prevDel)   { changed = true; _prevDel   = _state.del; }
    if (_state.tab   != _prevTab)   { changed = true; _prevTab   = _state.tab; }

    if (changed) _changed = true;
}

bool Keyboard_Class::isChange()
{
    const bool c = _changed;
    _changed = false;
    return c;
}

uint8_t Keyboard_Class::isPressed()
{
    return _count;
}
