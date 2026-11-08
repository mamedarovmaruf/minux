#ifndef FAT_H
#define FAT_H

#include <stdint.h>

#define FAT_NAME_MAX 12

#define FAT_OK            0
#define FAT_ERR_NODISK   -1
#define FAT_ERR_NOFS     -2
#define FAT_ERR_IO       -3
#define FAT_ERR_NOTFOUND -4
#define FAT_ERR_EXISTS   -5
#define FAT_ERR_NAME     -6
#define FAT_ERR_FULL     -7
#define FAT_ERR_ISDIR    -8
#define FAT_ERR_CORRUPT  -9
#define FAT_ERR_TOOBIG   -10
#define FAT_ERR_SMALL    -11

struct fat_entry {
    char name[FAT_NAME_MAX + 1];
    uint32_t size;
    uint8_t is_dir;
};

struct fat_info {
    uint32_t disk_sectors;
    uint32_t cluster_bytes;
    uint32_t total_clusters;
    uint32_t free_clusters;
};

int fat_mount(void);
int fat_format(void);
int fat_readdir(uint32_t *cursor, struct fat_entry *out);
int fat_create(const char *name);
int fat_read(const char *name, uint32_t offset, void *buf, uint32_t len);
int fat_write(const char *name, const void *data, uint32_t len);
int fat_append(const char *name, const void *data, uint32_t len);
int fat_remove(const char *name);
int fat_get_info(struct fat_info *out);
const char *fat_strerror(int err);

#endif
