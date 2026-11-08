#include "fat.h"
#include "ata.h"
#include "kstring.h"

#define SECTOR_SIZE         512
#define ENTRIES_PER_SECTOR  16

#define ENTRY_END   0x00
#define ENTRY_FREE  0xE5

#define ATTR_VOLUME  0x08
#define ATTR_DIR     0x10
#define ATTR_ARCHIVE 0x20

#define CLUSTER_EOC      0xFFFF
#define CLUSTER_BAD_MIN  0xFFF7

#define MIN_CLUSTERS 4085
#define MAX_CLUSTERS 65524

#define FORMAT_SPC          4
#define FORMAT_ROOT_ENTRIES 512
#define FORMAT_MAX_SECTORS  250000

/* there is no RTC driver yet, every file gets 2026-01-01 */
#define FIXED_DATE 0x5C21

struct bpb {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  spc;
    uint16_t reserved;
    uint8_t  nfats;
    uint16_t root_entries;
    uint16_t total16;
    uint8_t  media;
    uint16_t fat_size16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden;
    uint32_t total32;
    uint8_t  drive;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     label[11];
    char     fs_type[8];
} __attribute__((packed));

struct dir_entry {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  ctime_tenth;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cluster_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t cluster_lo;
    uint32_t size;
} __attribute__((packed));

union sector {
    uint8_t raw[SECTOR_SIZE];
    struct bpb bpb;
    struct dir_entry ent[ENTRIES_PER_SECTOR];
};

struct loc {
    uint32_t lba;
    uint32_t idx;
};

static struct {
    int mounted;
    uint32_t spc;
    uint32_t cluster_bytes;
    uint32_t fat_start;
    uint32_t nfats;
    uint32_t fat_sectors;
    uint32_t root_entries;
    uint32_t root_start;
    uint32_t root_sectors;
    uint32_t data_start;
    uint32_t clusters;
    uint32_t hint;
} fs;

static union sector dirbuf;
static union sector databuf;
static union sector fatbuf;
static uint32_t fatbuf_lba = 0xFFFFFFFF;

static int not_ready(void) {
    if (fs.mounted) {
        return 0;
    }
    return ata_sector_count() ? FAT_ERR_NOFS : FAT_ERR_NODISK;
}

static int cluster_ok(uint32_t c) {
    return c >= 2 && c < fs.clusters + 2;
}

static uint32_t cluster_lba(uint32_t c) {
    return fs.data_start + (c - 2) * fs.spc;
}

static int load_fat(uint32_t lba) {
    if (fatbuf_lba == lba) {
        return 0;
    }
    if (ata_read(lba, fatbuf.raw) < 0) {
        fatbuf_lba = 0xFFFFFFFF;
        return -1;
    }
    fatbuf_lba = lba;
    return 0;
}

static int fat_get(uint32_t c, uint32_t *out) {
    uint32_t off = c * 2;
    if (load_fat(fs.fat_start + off / SECTOR_SIZE) < 0) {
        return -1;
    }
    uint32_t i = off % SECTOR_SIZE;
    *out = (uint32_t)fatbuf.raw[i] | ((uint32_t)fatbuf.raw[i + 1] << 8);
    return 0;
}

static int fat_set(uint32_t c, uint32_t value) {
    uint32_t off = c * 2;
    uint32_t lba = fs.fat_start + off / SECTOR_SIZE;
    if (load_fat(lba) < 0) {
        return -1;
    }
    uint32_t i = off % SECTOR_SIZE;
    fatbuf.raw[i] = (uint8_t)(value & 0xFF);
    fatbuf.raw[i + 1] = (uint8_t)((value >> 8) & 0xFF);
    for (uint32_t k = 0; k < fs.nfats; k++) {
        if (ata_write(lba + k * fs.fat_sectors, fatbuf.raw) < 0) {
            fatbuf_lba = 0xFFFFFFFF;
            return -1;
        }
    }
    return 0;
}

static int alloc_cluster(uint32_t *out) {
    for (uint32_t n = 0; n < fs.clusters; n++) {
        uint32_t c = 2 + (fs.hint - 2 + n) % fs.clusters;
        uint32_t v;
        if (fat_get(c, &v) < 0) {
            return FAT_ERR_IO;
        }
        if (v == 0) {
            if (fat_set(c, CLUSTER_EOC) < 0) {
                return FAT_ERR_IO;
            }
            fs.hint = c;
            *out = c;
            return FAT_OK;
        }
    }
    return FAT_ERR_FULL;
}

static int free_chain(uint32_t c) {
    uint32_t guard = fs.clusters;
    while (cluster_ok(c) && guard--) {
        uint32_t next;
        if (fat_get(c, &next) < 0 || fat_set(c, 0) < 0) {
            return -1;
        }
        if (c < fs.hint) {
            fs.hint = c;
        }
        c = next;
    }
    return 0;
}

static int write_clusters(const uint8_t *data, uint32_t len, uint32_t *first_out) {
    *first_out = 0;
    if (len == 0) {
        return FAT_OK;
    }

    uint32_t need = (len + fs.cluster_bytes - 1) / fs.cluster_bytes;
    uint32_t first = 0;
    uint32_t prev = 0;
    uint32_t done = 0;

    for (uint32_t i = 0; i < need; i++) {
        uint32_t c;
        int r = alloc_cluster(&c);
        if (r < 0) {
            free_chain(first);
            return r;
        }
        if (prev) {
            if (fat_set(prev, c) < 0) {
                fat_set(c, 0);
                free_chain(first);
                return FAT_ERR_IO;
            }
        } else {
            first = c;
        }

        for (uint32_t s = 0; s < fs.spc && done < len; s++) {
            uint32_t remaining = len - done;
            uint32_t lba = cluster_lba(c) + s;
            int wr;
            if (remaining >= SECTOR_SIZE) {
                wr = ata_write(lba, data + done);
                done += SECTOR_SIZE;
            } else {
                kmemset(databuf.raw, 0, SECTOR_SIZE);
                kmemcpy(databuf.raw, data + done, remaining);
                wr = ata_write(lba, databuf.raw);
                done += remaining;
            }
            if (wr < 0) {
                free_chain(first);
                return FAT_ERR_IO;
            }
        }
        prev = c;
    }

    *first_out = first;
    return FAT_OK;
}

static int write_at(uint32_t cluster, uint32_t offset, const uint8_t *data, uint32_t len) {
    while (len) {
        uint32_t lba = cluster_lba(cluster) + offset / SECTOR_SIZE;
        uint32_t so = offset % SECTOR_SIZE;
        uint32_t n = SECTOR_SIZE - so;
        if (n > len) {
            n = len;
        }
        if (n == SECTOR_SIZE) {
            if (ata_write(lba, data) < 0) {
                return FAT_ERR_IO;
            }
        } else {
            if (ata_read(lba, databuf.raw) < 0) {
                return FAT_ERR_IO;
            }
            kmemcpy(databuf.raw + so, data, n);
            if (ata_write(lba, databuf.raw) < 0) {
                return FAT_ERR_IO;
            }
        }
        data += n;
        offset += n;
        len -= n;
    }
    return FAT_OK;
}

static int valid_char(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return 1;
    }
    const char *extra = "!#$%&'()-@^_`{}~";
    for (; *extra; extra++) {
        if (*extra == c) {
            return 1;
        }
    }
    return 0;
}

static int to_83(const char *name, uint8_t out[11]) {
    uint32_t base = 0;
    uint32_t ext = 0;
    int in_ext = 0;

    kmemset(out, ' ', 11);
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (c == '.') {
            if (in_ext || base == 0) {
                return -1;
            }
            in_ext = 1;
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (!valid_char(c)) {
            return -1;
        }
        if (in_ext) {
            if (ext >= 3) {
                return -1;
            }
            out[8 + ext++] = (uint8_t)c;
        } else {
            if (base >= 8) {
                return -1;
            }
            out[base++] = (uint8_t)c;
        }
    }
    return base ? 0 : -1;
}

static void from_83(const uint8_t *n, char *out) {
    int len = 0;
    int base = 8;
    while (base > 0 && n[base - 1] == ' ') {
        base--;
    }
    for (int i = 0; i < base; i++) {
        out[len++] = (char)n[i];
    }
    int ext = 3;
    while (ext > 0 && n[8 + ext - 1] == ' ') {
        ext--;
    }
    if (ext > 0) {
        out[len++] = '.';
        for (int i = 0; i < ext; i++) {
            out[len++] = (char)n[8 + i];
        }
    }
    out[len] = 0;
}

static int name_eq(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 11; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* returns 1 if found (dirbuf then holds the entry's sector), 0 if not found,
   negative on I/O error; the first free slot is reported through free_slot */
static int scan_root(const uint8_t *n83, struct loc *hit, struct loc *free_slot, int *has_free) {
    *has_free = 0;
    for (uint32_t s = 0; s < fs.root_sectors; s++) {
        uint32_t lba = fs.root_start + s;
        if (ata_read(lba, dirbuf.raw) < 0) {
            return FAT_ERR_IO;
        }
        for (uint32_t i = 0; i < ENTRIES_PER_SECTOR; i++) {
            struct dir_entry *e = &dirbuf.ent[i];
            uint8_t first = e->name[0];

            if (first == ENTRY_END) {
                if (!*has_free) {
                    free_slot->lba = lba;
                    free_slot->idx = i;
                    *has_free = 1;
                }
                return 0;
            }
            if (first == ENTRY_FREE) {
                if (!*has_free) {
                    free_slot->lba = lba;
                    free_slot->idx = i;
                    *has_free = 1;
                }
                continue;
            }
            /* volume label and long-name entries both have this bit set */
            if (e->attr & ATTR_VOLUME) {
                continue;
            }
            if (name_eq(e->name, n83)) {
                hit->lba = lba;
                hit->idx = i;
                return 1;
            }
        }
    }
    return 0;
}

static void init_entry(struct dir_entry *e, const uint8_t *n83, uint32_t start, uint32_t size) {
    kmemset(e, 0, sizeof(*e));
    kmemcpy(e->name, n83, 11);
    e->attr = ATTR_ARCHIVE;
    e->cdate = FIXED_DATE;
    e->adate = FIXED_DATE;
    e->mdate = FIXED_DATE;
    e->cluster_lo = (uint16_t)start;
    e->size = size;
}

int fat_mount(void) {
    fs.mounted = 0;
    fatbuf_lba = 0xFFFFFFFF;

    uint32_t disk = ata_sector_count();
    if (disk == 0) {
        return FAT_ERR_NODISK;
    }
    if (ata_read(0, databuf.raw) < 0) {
        return FAT_ERR_IO;
    }

    const struct bpb *b = &databuf.bpb;
    if (databuf.raw[510] != 0x55 || databuf.raw[511] != 0xAA) {
        return FAT_ERR_NOFS;
    }
    if (b->bytes_per_sector != SECTOR_SIZE || b->spc == 0 || (b->spc & (b->spc - 1))) {
        return FAT_ERR_NOFS;
    }
    if (b->reserved == 0 || b->nfats == 0 || b->root_entries == 0 || b->fat_size16 == 0) {
        return FAT_ERR_NOFS;
    }

    uint32_t total = b->total16 ? b->total16 : b->total32;
    if (total == 0 || total > disk) {
        return FAT_ERR_NOFS;
    }

    uint32_t root_sectors = ((uint32_t)b->root_entries * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint32_t data_start = b->reserved + (uint32_t)b->nfats * b->fat_size16 + root_sectors;
    if (total <= data_start) {
        return FAT_ERR_NOFS;
    }

    uint32_t clusters = (total - data_start) / b->spc;
    /* the cluster count, not a label, is what makes a volume FAT16 */
    if (clusters < MIN_CLUSTERS || clusters > MAX_CLUSTERS) {
        return FAT_ERR_NOFS;
    }

    fs.spc = b->spc;
    fs.cluster_bytes = (uint32_t)b->spc * SECTOR_SIZE;
    fs.fat_start = b->reserved;
    fs.nfats = b->nfats;
    fs.fat_sectors = b->fat_size16;
    fs.root_entries = b->root_entries;
    fs.root_start = fs.fat_start + fs.nfats * fs.fat_sectors;
    fs.root_sectors = root_sectors;
    fs.data_start = data_start;
    fs.clusters = clusters;
    fs.hint = 2;
    fs.mounted = 1;
    return FAT_OK;
}

int fat_format(void) {
    uint32_t total = ata_sector_count();
    if (total == 0) {
        return FAT_ERR_NODISK;
    }
    if (total > FORMAT_MAX_SECTORS) {
        total = FORMAT_MAX_SECTORS;
    }

    uint32_t root_sectors = FORMAT_ROOT_ENTRIES * 32 / SECTOR_SIZE;
    uint32_t fat_sectors = (((total / FORMAT_SPC) + 2) * 2 + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint32_t data_start = 1 + 2 * fat_sectors + root_sectors;
    if (total <= data_start || (total - data_start) / FORMAT_SPC < MIN_CLUSTERS) {
        return FAT_ERR_SMALL;
    }

    fs.mounted = 0;
    fatbuf_lba = 0xFFFFFFFF;

    kmemset(databuf.raw, 0, SECTOR_SIZE);
    struct bpb *b = &databuf.bpb;
    b->jmp[0] = 0xEB;
    b->jmp[1] = 0x3C;
    b->jmp[2] = 0x90;
    kmemcpy(b->oem, "MINUX   ", 8);
    b->bytes_per_sector = SECTOR_SIZE;
    b->spc = FORMAT_SPC;
    b->reserved = 1;
    b->nfats = 2;
    b->root_entries = FORMAT_ROOT_ENTRIES;
    b->total16 = total < 65536 ? (uint16_t)total : 0;
    b->media = 0xF8;
    b->fat_size16 = (uint16_t)fat_sectors;
    b->sectors_per_track = 32;
    b->heads = 2;
    b->hidden = 0;
    b->total32 = total < 65536 ? 0 : total;
    b->drive = 0x80;
    b->boot_sig = 0x29;
    b->volume_id = 0x4D494E58;
    kmemcpy(b->label, "MINUX      ", 11);
    kmemcpy(b->fs_type, "FAT16   ", 8);
    databuf.raw[510] = 0x55;
    databuf.raw[511] = 0xAA;
    if (ata_write(0, databuf.raw) < 0) {
        return FAT_ERR_IO;
    }

    kmemset(databuf.raw, 0, SECTOR_SIZE);
    for (uint32_t k = 0; k < 2; k++) {
        for (uint32_t s = 0; s < fat_sectors; s++) {
            kmemset(databuf.raw, 0, SECTOR_SIZE);
            if (s == 0) {
                databuf.raw[0] = 0xF8;
                databuf.raw[1] = 0xFF;
                databuf.raw[2] = 0xFF;
                databuf.raw[3] = 0xFF;
            }
            if (ata_write(1 + k * fat_sectors + s, databuf.raw) < 0) {
                return FAT_ERR_IO;
            }
        }
    }

    kmemset(databuf.raw, 0, SECTOR_SIZE);
    for (uint32_t s = 0; s < root_sectors; s++) {
        if (ata_write(1 + 2 * fat_sectors + s, databuf.raw) < 0) {
            return FAT_ERR_IO;
        }
    }

    return fat_mount();
}

int fat_readdir(uint32_t *cursor, struct fat_entry *out) {
    int r = not_ready();
    if (r) {
        return r;
    }

    uint32_t i = *cursor;
    uint32_t loaded = 0xFFFFFFFF;

    while (i < fs.root_entries) {
        uint32_t s = i / ENTRIES_PER_SECTOR;
        if (s != loaded) {
            if (ata_read(fs.root_start + s, dirbuf.raw) < 0) {
                return FAT_ERR_IO;
            }
            loaded = s;
        }
        struct dir_entry *e = &dirbuf.ent[i % ENTRIES_PER_SECTOR];
        i++;

        if (e->name[0] == ENTRY_END) {
            *cursor = fs.root_entries;
            return 0;
        }
        if (e->name[0] == ENTRY_FREE || (e->attr & ATTR_VOLUME)) {
            continue;
        }

        from_83(e->name, out->name);
        out->size = e->size;
        out->is_dir = (e->attr & ATTR_DIR) ? 1 : 0;
        *cursor = i;
        return 1;
    }

    *cursor = i;
    return 0;
}

int fat_create(const char *name) {
    int r = not_ready();
    if (r) {
        return r;
    }

    uint8_t n83[11];
    if (to_83(name, n83) < 0) {
        return FAT_ERR_NAME;
    }

    struct loc hit, slot;
    int has_free;
    r = scan_root(n83, &hit, &slot, &has_free);
    if (r < 0) {
        return r;
    }
    if (r == 1) {
        return FAT_ERR_EXISTS;
    }
    if (!has_free) {
        return FAT_ERR_FULL;
    }

    if (ata_read(slot.lba, dirbuf.raw) < 0) {
        return FAT_ERR_IO;
    }
    init_entry(&dirbuf.ent[slot.idx], n83, 0, 0);
    if (ata_write(slot.lba, dirbuf.raw) < 0) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

int fat_read(const char *name, uint32_t offset, void *buf, uint32_t len) {
    int r = not_ready();
    if (r) {
        return r;
    }

    uint8_t n83[11];
    if (to_83(name, n83) < 0) {
        return FAT_ERR_NAME;
    }

    struct loc hit, slot;
    int has_free;
    r = scan_root(n83, &hit, &slot, &has_free);
    if (r < 0) {
        return r;
    }
    if (r == 0) {
        return FAT_ERR_NOTFOUND;
    }

    const struct dir_entry *e = &dirbuf.ent[hit.idx];
    if (e->attr & ATTR_DIR) {
        return FAT_ERR_ISDIR;
    }
    uint32_t size = e->size;
    uint32_t c = e->cluster_lo;

    if (offset >= size) {
        return 0;
    }
    if (len > size - offset) {
        len = size - offset;
    }

    uint32_t skip = offset / fs.cluster_bytes;
    uint32_t pos = offset % fs.cluster_bytes;
    for (uint32_t i = 0; i < skip; i++) {
        if (!cluster_ok(c) || fat_get(c, &c) < 0) {
            return FAT_ERR_CORRUPT;
        }
    }

    uint8_t *out = buf;
    uint32_t done = 0;
    while (done < len) {
        if (!cluster_ok(c)) {
            return FAT_ERR_CORRUPT;
        }
        while (pos < fs.cluster_bytes && done < len) {
            uint32_t so = pos % SECTOR_SIZE;
            uint32_t n = SECTOR_SIZE - so;
            if (n > len - done) {
                n = len - done;
            }
            if (ata_read(cluster_lba(c) + pos / SECTOR_SIZE, databuf.raw) < 0) {
                return FAT_ERR_IO;
            }
            kmemcpy(out + done, databuf.raw + so, n);
            done += n;
            pos += n;
        }
        pos = 0;
        if (done < len && fat_get(c, &c) < 0) {
            return FAT_ERR_IO;
        }
    }
    return (int)done;
}

int fat_write(const char *name, const void *data, uint32_t len) {
    int r = not_ready();
    if (r) {
        return r;
    }

    uint8_t n83[11];
    if (to_83(name, n83) < 0) {
        return FAT_ERR_NAME;
    }

    struct loc hit, slot;
    int has_free;
    r = scan_root(n83, &hit, &slot, &has_free);
    if (r < 0) {
        return r;
    }

    struct loc where;
    if (r == 1) {
        if (dirbuf.ent[hit.idx].attr & ATTR_DIR) {
            return FAT_ERR_ISDIR;
        }
        where = hit;
    } else {
        if (!has_free) {
            return FAT_ERR_FULL;
        }
        where = slot;
    }

    uint32_t first;
    r = write_clusters(data, len, &first);
    if (r < 0) {
        return r;
    }

    if (ata_read(where.lba, dirbuf.raw) < 0) {
        free_chain(first);
        return FAT_ERR_IO;
    }
    struct dir_entry *e = &dirbuf.ent[where.idx];
    uint32_t old = 0;
    if (e->name[0] != ENTRY_END && e->name[0] != ENTRY_FREE && name_eq(e->name, n83)) {
        old = e->cluster_lo;
        e->cluster_lo = (uint16_t)first;
        e->size = len;
        e->mdate = FIXED_DATE;
    } else {
        init_entry(e, n83, first, len);
    }
    if (ata_write(where.lba, dirbuf.raw) < 0) {
        free_chain(first);
        return FAT_ERR_IO;
    }
    if (old) {
        free_chain(old);
    }
    return FAT_OK;
}

int fat_append(const char *name, const void *data, uint32_t len) {
    int r = not_ready();
    if (r) {
        return r;
    }

    uint8_t n83[11];
    if (to_83(name, n83) < 0) {
        return FAT_ERR_NAME;
    }

    struct loc hit, slot;
    int has_free;
    r = scan_root(n83, &hit, &slot, &has_free);
    if (r < 0) {
        return r;
    }
    if (r == 0) {
        return fat_write(name, data, len);
    }

    const struct dir_entry *e = &dirbuf.ent[hit.idx];
    if (e->attr & ATTR_DIR) {
        return FAT_ERR_ISDIR;
    }
    uint32_t size = e->size;
    uint32_t start = e->cluster_lo;

    if (len == 0) {
        return FAT_OK;
    }
    if (size + len < size) {
        return FAT_ERR_TOOBIG;
    }
    if (size == 0 || start == 0) {
        return fat_write(name, data, len);
    }

    if (!cluster_ok(start)) {
        return FAT_ERR_CORRUPT;
    }
    uint32_t last = start;
    for (uint32_t guard = fs.clusters; guard; guard--) {
        uint32_t next;
        if (fat_get(last, &next) < 0) {
            return FAT_ERR_IO;
        }
        if (!cluster_ok(next)) {
            break;
        }
        last = next;
    }

    const uint8_t *src = data;
    uint32_t used = size % fs.cluster_bytes;
    if (used == 0) {
        used = fs.cluster_bytes;
    }
    uint32_t space = fs.cluster_bytes - used;
    uint32_t fill = len < space ? len : space;

    if (fill) {
        r = write_at(last, used, src, fill);
        if (r < 0) {
            return r;
        }
    }

    uint32_t rest = len - fill;
    if (rest) {
        uint32_t first;
        r = write_clusters(src + fill, rest, &first);
        if (r < 0) {
            return r;
        }
        if (fat_set(last, first) < 0) {
            free_chain(first);
            return FAT_ERR_IO;
        }
    }

    if (ata_read(hit.lba, dirbuf.raw) < 0) {
        return FAT_ERR_IO;
    }
    dirbuf.ent[hit.idx].size = size + len;
    dirbuf.ent[hit.idx].mdate = FIXED_DATE;
    if (ata_write(hit.lba, dirbuf.raw) < 0) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

int fat_remove(const char *name) {
    int r = not_ready();
    if (r) {
        return r;
    }

    uint8_t n83[11];
    if (to_83(name, n83) < 0) {
        return FAT_ERR_NAME;
    }

    struct loc hit, slot;
    int has_free;
    r = scan_root(n83, &hit, &slot, &has_free);
    if (r < 0) {
        return r;
    }
    if (r == 0) {
        return FAT_ERR_NOTFOUND;
    }

    struct dir_entry *e = &dirbuf.ent[hit.idx];
    if (e->attr & ATTR_DIR) {
        return FAT_ERR_ISDIR;
    }
    uint32_t start = e->cluster_lo;
    e->name[0] = ENTRY_FREE;
    if (ata_write(hit.lba, dirbuf.raw) < 0) {
        return FAT_ERR_IO;
    }
    if (free_chain(start) < 0) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

int fat_get_info(struct fat_info *out) {
    int r = not_ready();
    if (r) {
        return r;
    }

    uint32_t free_count = 0;
    for (uint32_t c = 2; c < fs.clusters + 2; c++) {
        uint32_t v;
        if (fat_get(c, &v) < 0) {
            return FAT_ERR_IO;
        }
        if (v == 0) {
            free_count++;
        }
    }

    out->disk_sectors = ata_sector_count();
    out->cluster_bytes = fs.cluster_bytes;
    out->total_clusters = fs.clusters;
    out->free_clusters = free_count;
    return FAT_OK;
}

const char *fat_strerror(int err) {
    switch (err) {
        case FAT_ERR_NODISK:   return "no disk found";
        case FAT_ERR_NOFS:     return "no FAT16 filesystem, run 'format yes'";
        case FAT_ERR_IO:       return "disk I/O error";
        case FAT_ERR_NOTFOUND: return "no such file";
        case FAT_ERR_EXISTS:   return "file already exists";
        case FAT_ERR_NAME:     return "invalid name (8.3 format: up to 8 + 3 characters)";
        case FAT_ERR_FULL:     return "disk or directory is full";
        case FAT_ERR_ISDIR:    return "is a directory";
        case FAT_ERR_CORRUPT:  return "filesystem is corrupted";
        case FAT_ERR_TOOBIG:   return "file too large";
        case FAT_ERR_SMALL:    return "disk too small for FAT16";
        default:               return "unknown error";
    }
}
