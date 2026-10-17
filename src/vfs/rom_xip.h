#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


int xip_map_rom_partition(const char *part_name, size_t rom_size_effective);
/* Map the partition and expose only [offset, offset + rom_size) as the current ROM. */
int xip_map_rom_partition_at(const char *part_name, size_t offset, size_t rom_size);
const uint8_t* get_rom_ptr(void);
size_t get_rom_size(void);
void xip_unmap(void);

#ifdef __cplusplus
}
#endif
