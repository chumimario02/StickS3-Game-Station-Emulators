#ifdef TARGET_STICKS3

#include "sticks3_flow.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include "vfs/rom_store.h"

/*
 * Controls (same keys as the Cardputer, see lib_sticks3/M5CardputerShim):
 *   Up / Down : ';' / '.'   (8BitDo Micro D-pad, or StickS3 side button = down)
 *   OK        : Enter or 'l' (StickS3 front button, or Micro A)
 *   Back/Del  : 'k'        (Micro B) -> delete the highlighted ROM
 */

namespace {

constexpr int ROWS    = 7;
constexpr int ROW_H   = 16;
constexpr int TITLE_H = 18;

constexpr const char* AP_SSID = "StickS3-ROMs";
constexpr const char* AP_PASS = "stickroms";

// ----------------------------- web page -----------------------------
static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<meta name=viewport content='width=device-width,initial-scale=1'>
<title>StickS3 ROMs</title>
<style>body{font-family:sans-serif;max-width:480px;margin:20px auto;padding:0 12px}progress{width:100%}li{margin:4px 0}</style>
<h2>StickS3 ROMs</h2>
<p id=free></p>
<input type=file id=f multiple>
<button id=b>Upload</button>
<progress id=p value=0 max=100></progress>
<p id=st></p>
<ul id=l></ul>
<script>
const $=i=>document.getElementById(i);
async function refresh(){
  try{
    const r=await (await fetch('/list')).json();
    $('free').textContent='Free: '+(r.free/1024|0)+' KB (largest ROM: '+(r.hole/1024|0)+' KB)';
    $('l').innerHTML=r.roms.map(x=>'<li>'+x.name+' ('+(x.size/1024|0)+' KB)</li>').join('');
  }catch(e){}
}
function up(file){
  return new Promise(res=>{
    const x=new XMLHttpRequest();
    x.open('POST','/upload?name='+encodeURIComponent(file.name)+'&size='+file.size);
    x.upload.onprogress=e=>{if(e.lengthComputable)$('p').value=e.loaded*100/e.total};
    x.onload=()=>{$('st').textContent+=file.name+': '+x.responseText+' | ';res()};
    x.onerror=()=>{$('st').textContent+=file.name+': connection error | ';res()};
    const f=new FormData();
    f.append('rom',file,file.name);
    x.send(f);
  });
}
$('b').onclick=async()=>{
  $('st').textContent='';
  for(const f of $('f').files){await up(f)}
  $('p').value=0;
  refresh();
};
refresh();
</script>
)HTML";

// ----------------------------- upload state -----------------------------
enum UpState { UP_IDLE, UP_BUSY, UP_DONE, UP_ERR };
UpState g_state = UP_IDLE;
String g_upName;
String g_upErr;
uint32_t g_upBytes = 0;
uint32_t g_lastDraw = 0;

// ----------------------------- input helpers -----------------------------
struct Keys { bool up, down, ok, back; };

Keys readKeys()
{
    M5Cardputer.update();
    auto& kb = M5Cardputer.Keyboard;
    Keys k;
    k.up   = kb.isKeyPressed(';');
    k.down = kb.isKeyPressed('.');
    k.ok   = kb.keysState().enter || kb.isKeyPressed('l');
    k.back = kb.isKeyPressed('k');
    return k;
}

bool anyKey(const Keys& k) { return k.up || k.down || k.ok || k.back; }

void waitRelease()
{
    while (anyKey(readKeys())) delay(10);
}

struct Repeat {
    bool prev = false;
    uint32_t t0 = 0, tr = 0;
};

// true on press, then auto-repeat while held
bool fire(Repeat& r, bool now)
{
    const uint32_t t = millis();
    if (now && !r.prev) { r.prev = true; r.t0 = t; r.tr = t; return true; }
    if (now && r.prev && t - r.t0 > 400 && t - r.tr > 120) { r.tr = t; return true; }
    if (!now) r.prev = false;
    return false;
}

// ----------------------------- drawing -----------------------------
void setupText()
{
    auto& d = M5Cardputer.Display;
    d.setFont(&fonts::Font2);
    d.setTextSize(1);
}

void drawMessage(const char* l1, const char* l2)
{
    auto& d = M5Cardputer.Display;
    setupText();
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.drawString(l1, 4, 30);
    if (l2) d.drawString(l2, 4, 60);
}

void drawMenu(int sel, int top)
{
    auto& d = M5Cardputer.Display;
    const int count = romstore_count();
    const int total = count + 1;

    setupText();
    d.startWrite();
    d.fillScreen(TFT_BLACK);
    d.fillRect(0, 0, 240, TITLE_H, TFT_DARKGREEN);
    char head[48];
    snprintf(head, sizeof(head), "ROMS %d   free %uK", count, (unsigned)(romstore_free_bytes() / 1024));
    d.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    d.drawString(head, 4, 1);

    for (int i = 0; i < ROWS; i++) {
        const int idx = top + i;
        if (idx >= total) break;
        const int y = TITLE_H + 2 + i * ROW_H;
        const uint16_t bg = (idx == sel) ? TFT_NAVY : TFT_BLACK;
        d.fillRect(0, y, 240, ROW_H, bg);
        d.setTextColor(idx == sel ? TFT_YELLOW : TFT_WHITE, bg);
        char line[40];
        if (idx < count) {
            const RomStoreEntry* e = romstore_get(idx);
            snprintf(line, sizeof(line), "%.24s", e->name);
            d.drawString(line, 4, y);
            snprintf(line, sizeof(line), "%uK", (unsigned)((e->size + 1023) / 1024));
            d.drawString(line, 236 - d.textWidth(line), y);
        } else {
            d.drawString("+ Upload ROMs via WiFi", 4, y);
        }
    }
    d.endWrite();
}

bool confirmDelete(const char* name)
{
    auto& d = M5Cardputer.Display;
    setupText();
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.drawString("Delete this ROM?", 4, 20);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[40];
    snprintf(line, sizeof(line), "%.28s", name);
    d.drawString(line, 4, 46);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.drawString("A = yes     other = no", 4, 90);

    waitRelease();
    for (;;) {
        Keys k = readKeys();
        if (k.ok)  { waitRelease(); return true; }
        if (k.back || k.up || k.down) { waitRelease(); return false; }
        delay(15);
    }
}

void drawUploadStatus()
{
    auto& d = M5Cardputer.Display;
    char line[48];
    uint16_t col = TFT_WHITE;
    switch (g_state) {
        case UP_IDLE: snprintf(line, sizeof(line), "Waiting for upload..."); break;
        case UP_BUSY: snprintf(line, sizeof(line), "Receiving %uK", (unsigned)(g_upBytes / 1024)); col = TFT_CYAN; break;
        case UP_DONE: snprintf(line, sizeof(line), "Saved %.24s", g_upName.c_str()); col = TFT_GREEN; break;
        default:      snprintf(line, sizeof(line), "Error: %.28s", g_upErr.c_str()); col = TFT_RED; break;
    }
    d.fillRect(0, 96, 240, 18, TFT_BLACK);
    d.setTextColor(col, TFT_BLACK);
    d.drawString(line, 4, 96);
}

void drawUploadScreen(const IPAddress& ip)
{
    auto& d = M5Cardputer.Display;
    setupText();
    d.startWrite();
    d.fillScreen(TFT_BLACK);
    d.fillRect(0, 0, 240, TITLE_H, TFT_DARKGREEN);
    d.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    d.drawString("WIFI UPLOAD", 4, 1);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    char line[56];
    snprintf(line, sizeof(line), "WiFi: %s", AP_SSID);
    d.drawString(line, 4, 24);
    snprintf(line, sizeof(line), "Pass: %s", AP_PASS);
    d.drawString(line, 4, 42);
    snprintf(line, sizeof(line), "Open: http://%s", ip.toString().c_str());
    d.drawString(line, 4, 60);
    snprintf(line, sizeof(line), "Free: %uK", (unsigned)(romstore_free_bytes() / 1024));
    d.drawString(line, 4, 78);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.drawString("A = exit", 4, 118);
    d.endWrite();
    drawUploadStatus();
}

// ----------------------------- upload server -----------------------------
void handleUpload(WebServer& server)
{
    HTTPUpload& up = server.upload();

    if (up.status == UPLOAD_FILE_START) {
        g_upErr = "";
        g_upBytes = 0;
        g_upName = server.hasArg("name") ? server.arg("name") : up.filename;
        const size_t size = server.hasArg("size") ? (size_t)server.arg("size").toInt() : 0;
        if (romstore_write_begin(g_upName.c_str(), size)) {
            g_state = UP_BUSY;
        } else {
            g_state = UP_ERR;
            g_upErr = "no space / invalid";
        }
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (g_state == UP_BUSY) {
            if (romstore_write_chunk(up.buf, up.currentSize)) {
                g_upBytes += up.currentSize;
            } else {
                g_state = UP_ERR;
                g_upErr = "flash write failed";
            }
            if (millis() - g_lastDraw > 400) { g_lastDraw = millis(); drawUploadStatus(); }
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (g_state == UP_BUSY) {
            if (romstore_write_end()) {
                g_state = UP_DONE;
            } else {
                g_state = UP_ERR;
                g_upErr = "commit failed";
            }
        }
        drawUploadStatus();
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        romstore_write_abort();
        g_state = UP_ERR;
        g_upErr = "aborted";
        drawUploadStatus();
    }
}

void runUploadMode()
{
    g_state = UP_IDLE;
    g_upBytes = 0;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    delay(200);
    const IPAddress ip = WiFi.softAPIP();

    WebServer server(80);

    server.on("/", HTTP_GET, [&server]() {
        server.send_P(200, "text/html", PAGE);
    });

    server.on("/list", HTTP_GET, [&server]() {
        String json = R"({"free":)";
        json += String((unsigned)romstore_free_bytes());
        json += R"(,"hole":)";
        json += String((unsigned)romstore_largest_hole());
        json += R"(,"roms":[)";
        for (int i = 0; i < romstore_count(); i++) {
            const RomStoreEntry* e = romstore_get(i);
            if (i) json += ',';
            json += R"({"name":")";
            json += e->name;
            json += R"(","size":)";
            json += String((unsigned)e->size);
            json += '}';
        }
        json += "]}";
        server.send(200, "application/json", json);
    });

    server.on("/upload", HTTP_POST,
        [&server]() {
            if (g_state == UP_DONE) server.send(200, "text/plain", "OK");
            else server.send(500, "text/plain", g_upErr.length() ? g_upErr : String("error"));
        },
        [&server]() { handleUpload(server); });

    server.begin();
    drawUploadScreen(ip);

    uint32_t lastDraw = 0;
    bool prevOk = true;  // ignore the press that opened this screen
    for (;;) {
        server.handleClient();
        Keys k = readKeys();
        if (k.ok && !prevOk) break;
        prevOk = k.ok;

        const uint32_t now = millis();
        if (now - lastDraw > 500) { lastDraw = now; drawUploadStatus(); }
        delay(2);
    }

    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}

}  // namespace

// ----------------------------- public entry -----------------------------
bool sticks3_pick_rom(std::string& outName)
{
    if (!romstore_init("spiffs")) {
        drawMessage("No ROM partition", "Use partitions_5m_spiffs_8mb");
        delay(2000);
        return false;
    }

    // Preselect the last ROM that was played
    int sel = 0;
    {
        Preferences prefs;
        prefs.begin("sticks3", true);
        String last = prefs.getString("last", "");
        prefs.end();
        if (last.length()) {
            int i = romstore_find(last.c_str());
            if (i >= 0) sel = i;
        }
    }

    int top = 0;
    bool dirty = true;
    Repeat rUp, rDown;
    bool prevOk = true, prevBack = true;  // ignore keys held at boot

    for (;;) {
        Keys k = readKeys();
        const int count = romstore_count();
        const int total = count + 1;

        if (fire(rUp, k.up))     { sel = (sel + total - 1) % total; dirty = true; }
        if (fire(rDown, k.down)) { sel = (sel + 1) % total;         dirty = true; }

        const bool okEdge   = k.ok && !prevOk;
        const bool backEdge = k.back && !prevBack;
        prevOk = k.ok;
        prevBack = k.back;

        if (okEdge) {
            if (sel == count) {
                runUploadMode();
                // Restart so WiFi memory is fully released before running an emulator.
                ESP.restart();
            } else {
                const RomStoreEntry* e = romstore_get(sel);
                if (e && romstore_map(sel)) {
                    outName = e->name;
                    Preferences prefs;
                    prefs.begin("sticks3", false);
                    prefs.putString("last", e->name);
                    prefs.end();
                    return true;
                }
                drawMessage("Could not map ROM", nullptr);
                delay(1500);
                dirty = true;
            }
        }

        if (backEdge && sel < count) {
            const RomStoreEntry* e = romstore_get(sel);
            if (e && confirmDelete(e->name)) {
                romstore_remove(sel);
                if (sel >= romstore_count() + 1) sel = romstore_count();
            }
            prevBack = true;
            dirty = true;
        }

        if (dirty) {
            if (sel < top) top = sel;
            if (sel >= top + ROWS) top = sel - ROWS + 1;
            drawMenu(sel, top);
            dirty = false;
        }
        delay(15);
    }
}

#endif  // TARGET_STICKS3
