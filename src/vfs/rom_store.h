#pragma once
/*
 * ROM library stored in the flash partition (default: "spiffs", ~5.4 MB).
 *
 * Layout of the partition:
 *   [0x0000 .. 0x0FFF]  index (magic + list of entries)
 *   [0x1000 ..      ]  ROM data, every ROM starts on a 4 KB boundary
 *
 * ROMs are written as a stream (e.g. from a WiFi upload) and read back through XIP
 * (flash mmap), so they never need to fit in RAM.
 */
#include <stdint.h>
#include <stddef.h>

#define ROMSTORE_MAX_ENTRIES 64
#define ROMSTORE_NAME_LEN    40
#define ROMSTORE_ALIGN       4096u

struct RomStoreEntry {
    char     name[ROMSTORE_NAME_LEN];
    uint32_t offset;  // from the start of the partition, 4 KB aligned
    uint32_t size;    // bytes
};

bool   romstore_init(const char* partLabel = "spiffs");
bool   romstore_format();                       // forget every ROM
int    romstore_count();
const RomStoreEntry* romstore_get(int index);
int    romstore_find(const char* name);         // -1 if missing
bool   romstore_remove(int index);
size_t romstore_free_bytes();                   // sum of all free blocks
size_t romstore_largest_hole();                 // biggest ROM that can be stored now

// Streaming write. expectedSize = 0 -> use the largest free block.
bool   romstore_write_begin(const char* name, size_t expectedSize);
bool   romstore_write_chunk(const uint8_t* data, size_t len);
bool   romstore_write_end();
void   romstore_write_abort();

// Map ROM #index through XIP: get_rom_ptr() / get_rom_size() become valid.
bool   romstore_map(int index);

// Keep only [A-Za-z0-9._-], drop any path, cut long names but keep the extension.
void   romstore_sanitize_name(const char* in, char* out, size_t outLen);
