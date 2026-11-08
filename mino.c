#include <stdint.h>
#include "mino.h"
#include "keyboard.h"
#include "terminal.h"
#include "fat.h"
#include "kstring.h"

#define MINO_CAP 16384
#define MINO_LINES 23
#define HIST_MAX 256
#define HIST_BYTES 4

struct edit {
    uint16_t pos;
    uint8_t len;
    uint8_t inserted;
    char bytes[HIST_BYTES];
};

static char doc[MINO_CAP];
static uint32_t size, cursor, top_line, left_col;
static struct edit history[HIST_MAX];
static uint16_t hist_count, hist_pos;
static int dirty, no_disk;
static const char *name;
static char status_msg[32];

static uint32_t line_start(uint32_t line) {
    uint32_t p = 0;
    while (line && p < size) if (doc[p++] == '\n') line--;
    return p;
}

static uint32_t line_number(uint32_t p) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < p && i < size; i++) if (doc[i] == '\n') n++;
    return n;
}

static uint32_t line_end(uint32_t p) {
    while (p < size && doc[p] != '\n') p++;
    return p;
}

static uint32_t column_at(uint32_t p) {
    uint32_t s = p;
    while (s && doc[s - 1] != '\n') s--;
    return p - s;
}

static void raw_insert(uint32_t p, const char *bytes, uint32_t n) {
    if (n > MINO_CAP - size) n = MINO_CAP - size;
    for (uint32_t i = size; i > p; i--) doc[i + n - 1] = doc[i - 1];
    for (uint32_t i = 0; i < n; i++) doc[p + i] = bytes[i];
    size += n;
}

static void raw_delete(uint32_t p, uint32_t n) {
    if (p + n > size) n = size - p;
    for (uint32_t i = p; i + n < size; i++) doc[i] = doc[i + n];
    size -= n;
}

static void record_edit(uint32_t p, const char *b, uint8_t n, uint8_t inserted) {
    if (hist_pos < hist_count) hist_count = hist_pos;
    if (hist_count == HIST_MAX) {
        for (uint16_t i = 1; i < HIST_MAX; i++) history[i - 1] = history[i];
        hist_count--;
        if (hist_pos) hist_pos--;
    }
    struct edit *e = &history[hist_count++];
    e->pos = (uint16_t)p;
    e->len = n;
    e->inserted = inserted;
    for (uint8_t i = 0; i < n; i++) e->bytes[i] = b[i];
    hist_pos = hist_count;
}

static void insert_text(const char *b, uint8_t n) {
    if (!n || size + n > MINO_CAP) return;
    raw_insert(cursor, b, n);
    record_edit(cursor, b, n, 1);
    cursor += n;
    dirty = 1;
    status_msg[0] = 0;
}

static void delete_text(uint32_t p, uint8_t n) {
    if (!n || p >= size) return;
    if (p + n > size) n = (uint8_t)(size - p);
    char b[HIST_BYTES];
    for (uint8_t i = 0; i < n; i++) b[i] = doc[p + i];
    raw_delete(p, n);
    record_edit(p, b, n, 0);
    cursor = p;
    dirty = 1;
    status_msg[0] = 0;
}

static void undo(void) {
    if (!hist_pos) return;
    struct edit *e = &history[--hist_pos];
    if (e->inserted) raw_delete(e->pos, e->len);
    else raw_insert(e->pos, e->bytes, e->len);
    cursor = e->pos;
    dirty = 1;
}

static void redo(void) {
    if (hist_pos >= hist_count) return;
    struct edit *e = &history[hist_pos++];
    if (e->inserted) raw_insert(e->pos, e->bytes, e->len);
    else raw_delete(e->pos, e->len);
    cursor = e->pos + (e->inserted ? e->len : 0);
    dirty = 1;
}

static void render(void) {
    uint32_t ln = line_number(cursor), col = column_at(cursor);
    if (ln < top_line) top_line = ln;
    if (ln >= top_line + MINO_LINES) top_line = ln - MINO_LINES + 1;
    if (col < left_col) left_col = col;
    if (col >= left_col + 80) left_col = col - 79;

    set_color(VGA_WHITE, VGA_BLUE);
    for (uint8_t x = 0; x < 80; x++) put_char_at(0, x, ' ', VGA_WHITE | (VGA_BLUE << 4));
    const char *label = name ? name : "[No Name]";
    for (uint8_t x = 0; x < 70 && label[x]; x++) put_char_at(0, x, label[x], VGA_WHITE | (VGA_BLUE << 4));
    if (dirty) put_char_at(0, 71, '*', VGA_WHITE | (VGA_BLUE << 4));

    for (uint32_t row = 0; row < MINO_LINES; row++) {
        uint32_t s = line_start(top_line + row);
        uint32_t e = line_end(s);
        for (uint32_t x = 0; x < 80; x++) {
            uint32_t index = s + left_col + x;
            char c = index < e ? doc[index] : ' ';
            put_char_at((uint8_t)(row + 1), (uint8_t)x, c, VGA_LIGHT_GREY | (VGA_BLACK << 4));
        }
    }

    set_color(VGA_LIGHT_GREY, VGA_BLACK);
    clear_line(24);
    set_cursor(24, 0);
    print_dec(ln + 1); print_char(':'); print_dec(col + 1);
    print_string("  "); print_dec(size); print_string(" B  ^S Save ^X Exit ^Z Undo ^Y Redo");
    if (status_msg[0]) { print_string("  "); print_string(status_msg); }
    uint32_t screen_row = ln - top_line + 1;
    uint32_t screen_col = col - left_col;
    set_cursor((uint8_t)screen_row, (uint8_t)screen_col);
}

static int save_file(void) {
    if (no_disk) { status_msg[0]='n';status_msg[1]='o';status_msg[2]='t';status_msg[3]=' ';status_msg[4]='s';status_msg[5]='a';status_msg[6]='v';status_msg[7]='e';status_msg[8]='d';status_msg[9]=':';status_msg[10]=' ';status_msg[11]='n';status_msg[12]='o';status_msg[13]=' ';status_msg[14]='d';status_msg[15]='i';status_msg[16]='s';status_msg[17]='k';status_msg[18]=0; return 0; }
    if (!name) { status_msg[0]='n';status_msg[1]='o';status_msg[2]=' ';status_msg[3]='f';status_msg[4]='i';status_msg[5]='l';status_msg[6]='e';status_msg[7]=0; return 0; }
    int r = fat_write(name, doc, size);
    if (r == FAT_OK) { dirty = 0; status_msg[0]='s';status_msg[1]='a';status_msg[2]='v';status_msg[3]='e';status_msg[4]='d';status_msg[5]=0; return 1; }
    const char *m = fat_strerror(r);
    uint32_t i=0; while (m[i] && i<31) { status_msg[i]=m[i]; i++; } status_msg[i]=0;
    return 0;
}

void mino_run(const char *filename) {
    size = cursor = top_line = left_col = 0;
    hist_count = hist_pos = 0; dirty = no_disk = 0; name = filename; status_msg[0] = 0;
    if (name) {
        int r = fat_read(name, 0, doc, MINO_CAP + 0);
        if (r >= 0) {
            size = (uint32_t)r;
            if (size == MINO_CAP) {
                char extra;
                r = fat_read(name, MINO_CAP, &extra, 1);
                if (r > 0) { clear_screen(); print_string("mino: file exceeds 16 KB\n"); return; }
                if (r < 0) { no_disk = (r == FAT_ERR_NODISK || r == FAT_ERR_NOFS); if (no_disk) size=0; else { clear_screen(); print_string("mino: read error\n"); return; } }
            }
        } else if (r == FAT_ERR_NOTFOUND) size = 0;
        else if (r == FAT_ERR_NODISK || r == FAT_ERR_NOFS) { size = 0; no_disk = 1; }
        else { clear_screen(); print_string("mino: "); print_string(fat_strerror(r)); print_char('\n'); return; }
    }
    clear_screen();
    for (;;) {
        render();
        char c = keyboard_getchar();
        uint32_t ln = line_number(cursor), col = column_at(cursor), s, e, target;
        if (c == 0x13) save_file();
        else if (c == 0x18) {
            if (!dirty) break;
            clear_line(24); set_cursor(24, 0); print_string("Discard changes? (y/n)");
            c = keyboard_getchar();
            if (c == 'y' || c == 'Y') break;
        } else if (c == 0x1A) undo();
        else if (c == 0x19) redo();
        else if ((uint8_t)c == KEY_LEFT) { if(cursor) cursor--; }
        else if ((uint8_t)c == KEY_RIGHT) { if(cursor<size) cursor++; }
        else if ((uint8_t)c == KEY_HOME) cursor = line_start(ln);
        else if ((uint8_t)c == KEY_END) cursor = line_end(cursor);
        else if ((uint8_t)c == KEY_UP || (uint8_t)c == KEY_DOWN) {
            if ((uint8_t)c == KEY_UP && ln == 0) continue;
            s = line_start((uint8_t)c == KEY_UP ? ln - 1 : ln + 1);
            if ((uint8_t)c == KEY_DOWN && s > size) continue;
            e = line_end(s); target = s + col; cursor = target < e ? target : e;
        } else if ((uint8_t)c == KEY_PGUP || (uint8_t)c == KEY_PGDN) {
            uint32_t dest = (uint8_t)c == KEY_PGUP ? (ln > MINO_LINES ? ln - MINO_LINES : 0) : ln + MINO_LINES;
            s = line_start(dest); e = line_end(s); cursor = s + col < e ? s + col : e;
        } else if ((uint8_t)c == KEY_DELETE) { if(cursor<size) delete_text(cursor,1); }
        else if (c == '\b') { if(cursor) delete_text(cursor-1,1); }
        else if (c == '\n') { char b='\n'; insert_text(&b,1); }
        else if (c == '\t') { char b[4]={' ',' ',' ',' '}; uint8_t n=(uint8_t)(4-(col&3)); insert_text(b,n); }
        else if (c >= 32 && c < 127) insert_text(&c,1);
    }
    clear_screen();
}
