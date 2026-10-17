# ROM storage in flash (StickS3, no SD card)

Status: **experimental**, not compiled or tested on hardware yet.

The StickS3 has no SD slot, so the ROM library lives in the `spiffs` partition (~5.4 MB)
of the internal flash. ROMs are read back through XIP (flash mmap), so they never need to fit in RAM.

## Flash layout (`spiffs` partition)

| Offset | Content |
|---|---|
| `0x0000` - `0x0FFF` | Index: magic + up to 64 entries (name, offset, size) |
| `0x1000` - end | ROM data, every ROM starts on a 4 KB boundary |

See `src/vfs/rom_store.h/.cpp`. Deleting a ROM only removes its index entry; the freed space is reused
(first fit) by the next upload. If the partition contains something else (old single-ROM layout, SPIFFS data),
the index is reset on first boot.

## Adding ROMs (WiFi upload)

1. Boot the StickS3 and choose **+ Upload ROMs via WiFi** in the list.
2. Connect your phone or PC to the WiFi `StickS3-ROMs` (password `stickroms`).
3. Open `http://192.168.4.1` and pick one or more ROM files.
4. Press **A** on the StickS3 to leave upload mode. The device restarts and the new ROMs show up in the list.

## Menu controls

| Action | Key |
|---|---|
| Up / Down | D-pad (`;` / `.`), StickS3 side button = down |
| Start the ROM | A (`L`), StickS3 front button |
| Delete the highlighted ROM | B (`K`), then A to confirm |

The last ROM played is preselected.

## Limits

- About 5.3 MB in total, one ROM must fit in a contiguous free block.
- ROM names are cleaned to `[A-Za-z0-9._-]` and cut to 39 characters (the extension is kept).
- Flash wears out with writes: uploads are the only thing that writes (the old "copy on every boot" is gone).

## Code

- `src/vfs/rom_store.*`: index + streaming write + XIP mapping
- `src/vfs/rom_xip.*`: new `xip_map_rom_partition_at(label, offset, size)`
- `src/sticks3/sticks3_flow.*`: ROM list, delete, WiFi upload server
- `src/main.cpp`: `#ifdef TARGET_STICKS3` replaces the SD + copy-to-flash steps
