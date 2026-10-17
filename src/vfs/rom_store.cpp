#include "rom_store.h"
#include "rom_xip.h"
#include <esp_partition.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

namespace {

constexpr uint32_t MAGIC       = 0x52535433u;  // "RST3"
constexpr uint32_t VERSION     = 1;
constexpr uint32_t HEADER_AREA = ROMSTORE_ALIGN;  // first sector holds the index

struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
    RomStoreEntry entries[ROMSTORE_MAX_ENTRIES];
};
static_assert(sizeof(Header) <= ROMSTORE_ALIGN, "index must fit in one sector");

const esp_partition_t* s_part = nullptr;
char   s_label[17] = "spiffs";
Header s_hdr;

// streaming write state
bool     s_writing  = false;
uint8_t* s_buf      = nullptr;
size_t   s_bufLen   = 0;
uint32_t s_wOffset  = 0;
uint32_t s_wCap     = 0;
uint32_t s_wFlushed = 0;
char     s_wName[ROMSTORE_NAME_LEN];

inline uint32_t alignUp(uint32_t v) { return (v + ROMSTORE_ALIGN - 1) & ~(ROMSTORE_ALIGN - 1); }

bool saveHeader()
{
    if (esp_partition_erase_range(s_part, 0, ROMSTORE_ALIGN) != ESP_OK) return false;
    return esp_partition_write(s_part, 0, &s_hdr, sizeof(s_hdr)) == ESP_OK;
}

void sortEntries()
{
    for (uint32_t i = 1; i < s_hdr.count; i++) {
        RomStoreEntry tmp = s_hdr.entries[i];
        int j = (int)i - 1;
        while (j >= 0 && s_hdr.entries[j].offset > tmp.offset) {
            s_hdr.entries[j + 1] = s_hdr.entries[j];
            j--;
        }
        s_hdr.entries[j + 1] = tmp;
    }
}

void resetHeader()
{
    memset(&s_hdr, 0, sizeof(s_hdr));
    s_hdr.magic   = MAGIC;
    s_hdr.version = VERSION;
    s_hdr.count   = 0;
}

bool headerLooksValid()
{
    if (s_hdr.magic != MAGIC || s_hdr.version != VERSION || s_hdr.count > ROMSTORE_MAX_ENTRIES) return false;
    sortEntries();
    uint32_t prevEnd = HEADER_AREA;
    for (uint32_t i = 0; i < s_hdr.count; i++) {
        const RomStoreEntry& e = s_hdr.entries[i];
        if (memchr(e.name, 0, ROMSTORE_NAME_LEN) == nullptr) return false;
        if (e.size == 0 || (e.offset % ROMSTORE_ALIGN) != 0 || e.offset < prevEnd) return false;
        if ((uint64_t)e.offset + e.size > s_part->size) return false;
        prevEnd = alignUp(e.offset + e.size);
    }
    return true;
}

// Calls f(start, size) for every free block between ROMs (entries must be sorted).
template <typename F>
void forEachHole(F f)
{
    uint32_t cursor = HEADER_AREA;
    for (uint32_t i = 0; i < s_hdr.count; i++) {
        const RomStoreEntry& e = s_hdr.entries[i];
        if (e.offset > cursor) f(cursor, e.offset - cursor);
        cursor = alignUp(e.offset + e.size);
    }
    if (s_part->size > cursor) f(cursor, (uint32_t)s_part->size - cursor);
}

// need == 0 -> pick the largest block, otherwise first block that fits.
bool findHole(uint32_t need, uint32_t* off, uint32_t* cap)
{
    bool found = false;
    uint32_t bestOff = 0, bestCap = 0;
    forEachHole([&](uint32_t s, uint32_t z) {
        if (need == 0) {
            if (z > bestCap) { bestCap = z; bestOff = s; found = true; }
        } else if (!found && z >= need) {
            found = true; bestOff = s; bestCap = z;
        }
    });
    if (found) { *off = bestOff; *cap = bestCap; }
    return found;
}

bool flushBuf()
{
    if (s_bufLen == 0) return true;
    if ((uint64_t)s_wFlushed + ROMSTORE_ALIGN > s_wCap) return false;  // would overflow the free block
    const uint32_t pos = s_wOffset + s_wFlushed;
    if (esp_partition_erase_range(s_part, pos, ROMSTORE_ALIGN) != ESP_OK) return false;
    if (esp_partition_write(s_part, pos, s_buf, s_bufLen) != ESP_OK) return false;
    s_wFlushed += (uint32_t)s_bufLen;
    s_bufLen = 0;
    return true;
}

void freeWriteState()
{
    if (s_buf) { free(s_buf); s_buf = nullptr; }
    s_bufLen = 0;
    s_writing = false;
}

}  // namespace

// ---------------------------------------------------------------------------

bool romstore_init(const char* partLabel)
{
    if (partLabel == nullptr) partLabel = "spiffs";
    strncpy(s_label, partLabel, sizeof(s_label) - 1);
    s_label[sizeof(s_label) - 1] = 0;

    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, s_label);
    if (!s_part) return false;

    if (esp_partition_read(s_part, 0, &s_hdr, sizeof(s_hdr)) != ESP_OK || !headerLooksValid()) {
        // Unknown content (new device, or old single-ROM layout): start with an empty library.
        resetHeader();
        if (!saveHeader()) return false;
    }
    return true;
}

bool romstore_format()
{
    if (!s_part) return false;
    resetHeader();
    return saveHeader();
}

int romstore_count()
{
    return (int)s_hdr.count;
}

const RomStoreEntry* romstore_get(int index)
{
    if (index < 0 || index >= (int)s_hdr.count) return nullptr;
    return &s_hdr.entries[index];
}

int romstore_find(const char* name)
{
    if (!name) return -1;
    for (uint32_t i = 0; i < s_hdr.count; i++) {
        if (strncmp(s_hdr.entries[i].name, name, ROMSTORE_NAME_LEN) == 0) return (int)i;
    }
    return -1;
}

bool romstore_remove(int index)
{
    if (!s_part || index < 0 || index >= (int)s_hdr.count) return false;
    for (uint32_t i = (uint32_t)index; i + 1 < s_hdr.count; i++) {
        s_hdr.entries[i] = s_hdr.entries[i + 1];
    }
    s_hdr.count--;
    memset(&s_hdr.entries[s_hdr.count], 0, sizeof(RomStoreEntry));
    return saveHeader();
}

size_t romstore_free_bytes()
{
    if (!s_part) return 0;
    size_t total = 0;
    forEachHole([&](uint32_t, uint32_t z) { total += z; });
    return total;
}

size_t romstore_largest_hole()
{
    if (!s_part) return 0;
    size_t best = 0;
    forEachHole([&](uint32_t, uint32_t z) { if (z > best) best = z; });
    return best;
}

bool romstore_write_begin(const char* name, size_t expectedSize)
{
    if (!s_part || s_writing || !name) return false;

    romstore_sanitize_name(name, s_wName, sizeof(s_wName));

    // Same name already stored: replace it.
    int existing = romstore_find(s_wName);
    if (existing >= 0) romstore_remove(existing);

    if (s_hdr.count >= ROMSTORE_MAX_ENTRIES) return false;

    uint32_t off = 0, cap = 0;
    const uint32_t need = expectedSize ? alignUp((uint32_t)expectedSize) : 0;
    if (!findHole(need, &off, &cap)) return false;

    s_buf = (uint8_t*)malloc(ROMSTORE_ALIGN);
    if (!s_buf) return false;

    s_bufLen   = 0;
    s_wOffset  = off;
    s_wCap     = cap;
    s_wFlushed = 0;
    s_writing  = true;
    return true;
}

bool romstore_write_chunk(const uint8_t* data, size_t len)
{
    if (!s_writing) return false;
    while (len > 0) {
        size_t n = ROMSTORE_ALIGN - s_bufLen;
        if (n > len) n = len;
        memcpy(s_buf + s_bufLen, data, n);
        s_bufLen += n;
        data += n;
        len  -= n;
        if (s_bufLen == ROMSTORE_ALIGN) {
            if (!flushBuf()) { romstore_write_abort(); return false; }
        }
    }
    return true;
}

bool romstore_write_end()
{
    if (!s_writing) return false;
    if (!flushBuf() || s_wFlushed == 0) { romstore_write_abort(); return false; }

    RomStoreEntry& e = s_hdr.entries[s_hdr.count];
    memset(&e, 0, sizeof(e));
    strncpy(e.name, s_wName, ROMSTORE_NAME_LEN - 1);
    e.offset = s_wOffset;
    e.size   = s_wFlushed;
    s_hdr.count++;
    sortEntries();

    freeWriteState();
    return saveHeader();
}

void romstore_write_abort()
{
    freeWriteState();
}

bool romstore_map(int index)
{
    if (!s_part || index < 0 || index >= (int)s_hdr.count) return false;
    const RomStoreEntry& e = s_hdr.entries[index];
    xip_unmap();
    return xip_map_rom_partition_at(s_label, e.offset, e.size) == 0;
}

void romstore_sanitize_name(const char* in, char* out, size_t outLen)
{
    if (outLen == 0) return;

    // drop any directory part (0x5C is a backslash)
    const char* base = in;
    for (const char* p = in; *p; ++p) {
        if (*p == '/' || *p == 0x5C) base = p + 1;
    }

    char tmp[128];
    size_t n = 0;
    for (const char* p = base; *p && n < sizeof(tmp) - 1; ++p) {
        unsigned char c = (unsigned char)*p;
        bool ok = isalnum(c) || c == '.' || c == '-' || c == '_';
        tmp[n++] = ok ? (char)c : '_';
    }
    tmp[n] = 0;

    if (n == 0) {
        strncpy(tmp, "rom.bin", sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        n = strlen(tmp);
    }

    if (n < outLen) {
        memcpy(out, tmp, n + 1);
        return;
    }

    // Too long: shorten the stem, keep the extension (needed to detect the console).
    const char* dot = strrchr(tmp, '.');
    size_t extLen = (dot && strlen(dot) <= 8) ? strlen(dot) : 0;
    size_t stem = (outLen - 1 > extLen) ? (outLen - 1 - extLen) : 0;
    memcpy(out, tmp, stem);
    memcpy(out + stem, tmp + n - extLen, extLen);
    out[stem + extLen] = 0;
}
