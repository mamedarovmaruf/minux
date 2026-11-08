#ifndef ATA_H
#define ATA_H

#include <stdint.h>

int ata_init(void);
uint32_t ata_sector_count(void);
int ata_read(uint32_t lba, void *buf);
int ata_write(uint32_t lba, const void *buf);

#endif
