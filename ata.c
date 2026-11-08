#include "ata.h"
#include "io.h"

#define ATA_DATA    0x1F0
#define ATA_COUNT   0x1F2
#define ATA_LBA0    0x1F3
#define ATA_LBA1    0x1F4
#define ATA_LBA2    0x1F5
#define ATA_DRIVE   0x1F6
#define ATA_STATUS  0x1F7
#define ATA_CMD     0x1F7
#define ATA_CTRL    0x3F6

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_DF  0x20
#define ST_BSY 0x80

#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

#define TIMEOUT 1000000

static uint32_t sectors = 0;

/* four reads of the alternate status register give the drive its 400ns */
static void delay(void) {
    for (int i = 0; i < 4; i++) {
        (void)inb(ATA_CTRL);
    }
}

static int wait_idle(void) {
    for (int i = 0; i < TIMEOUT; i++) {
        if (!(inb(ATA_STATUS) & ST_BSY)) {
            return 0;
        }
    }
    return -1;
}

static int wait_drq(void) {
    for (int i = 0; i < TIMEOUT; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (st & ST_BSY) {
            continue;
        }
        if (st & (ST_ERR | ST_DF)) {
            return -1;
        }
        if (st & ST_DRQ) {
            return 0;
        }
    }
    return -1;
}

static int check_status(void) {
    return (inb(ATA_STATUS) & (ST_ERR | ST_DF)) ? -1 : 0;
}

static void issue(uint32_t lba, uint8_t cmd) {
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_COUNT, 1);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, cmd);
    delay();
}

int ata_init(void) {
    uint16_t id[256];

    sectors = 0;

    /* nIEN: the driver polls, the drive must not raise IRQ14 */
    outb(ATA_CTRL, 0x02);
    outb(ATA_DRIVE, 0xA0);
    delay();

    if (inb(ATA_STATUS) == 0xFF) {
        return -1;
    }

    outb(ATA_COUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_CMD, CMD_IDENTIFY);
    delay();

    if (inb(ATA_STATUS) == 0) {
        return -1;
    }
    if (wait_idle() < 0) {
        return -1;
    }

    /* non-zero here means ATAPI or SATA signature, not a plain ATA disk */
    if (inb(ATA_LBA1) != 0 || inb(ATA_LBA2) != 0) {
        return -1;
    }
    if (wait_drq() < 0) {
        return -1;
    }

    insw(ATA_DATA, id, 256);

    if (!(id[49] & (1 << 9))) {
        return -1;
    }

    uint32_t count = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (count == 0) {
        return -1;
    }

    sectors = count;
    return 0;
}

uint32_t ata_sector_count(void) {
    return sectors;
}

int ata_read(uint32_t lba, void *buf) {
    if (lba >= sectors) {
        return -1;
    }
    if (wait_idle() < 0) {
        return -1;
    }
    issue(lba, CMD_READ);
    if (wait_drq() < 0) {
        return -1;
    }
    insw(ATA_DATA, buf, 256);
    return check_status();
}

int ata_write(uint32_t lba, const void *buf) {
    if (lba >= sectors) {
        return -1;
    }
    if (wait_idle() < 0) {
        return -1;
    }
    issue(lba, CMD_WRITE);
    if (wait_drq() < 0) {
        return -1;
    }
    outsw(ATA_DATA, buf, 256);
    delay();
    if (wait_idle() < 0 || check_status() < 0) {
        return -1;
    }
    outb(ATA_CMD, CMD_FLUSH);
    delay();
    if (wait_idle() < 0) {
        return -1;
    }
    return check_status();
}
