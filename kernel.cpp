cat << 'EOF' > kernel.cpp
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;
const int VGA_WIDTH = 80;
const int VGA_HEIGHT = 25;

int cursor_x = 0;
int cursor_y = 0;
uint8_t terminal_color = 0x07;

char input_buffer[256];
int input_len = 0;
int system_ram_mb = 128;

struct VirtualFile {
    char name[32];
    char content[500];
    int size;
    bool exists;
};

#define MAX_FILES 10
VirtualFile files[MAX_FILES];
bool is_russian = false;

uint32_t next_rand = 1;
int rand_num() {
    next_rand = next_rand * 1103515245 + 12345;
    return (uint32_t)(next_rand / 65536) % 32768;
}

void delay(int count) {
    volatile int i;
    for (i = 0; i < count * 50000; i++) {}
}

void clear_buffer() {
    for (int i = 0; i < 256; i++) {
        input_buffer[i] = 0;
    }
    input_len = 0;
}

void set_cursor(int x, int y) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        cursor_x = x;
        cursor_y = y;
    }
}

void draw_char(int x, int y, char c, uint8_t color) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        VGA_MEMORY[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)color << 8);
    }
}

const char kbd_en[60] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

const char kbd_ru[60] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'j', 'c', 'u', 'k', 'e', 'n', 'g', 's', 'h', 'z', 'h', 'h', '\n',
    0, 'f', 'y', 'w', 'a', 'p', 'r', 'o', 'l', 'd', 'z', 'e', 'e', 0, '\\',
    'j', 'c', 'j', 'm', 'i', 't', 'b', '.', ',', '/', 0, '*', 0, ' '
};

inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

inline void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

bool str_equal(const char* s1, const char* s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

bool str_start_with(const char* buffer, const char* prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (buffer[i] != prefix[i]) return false;
        i++;
    }
    return true;
}

void str_copy(char* dest, const char* src) {
    int i = 0;
    while (src[i] != '\0' && i < 31) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void clear_screen() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = (uint16_t)' ' | ((uint16_t)terminal_color << 8);
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

void scroll() {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = VGA_MEMORY[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (uint16_t)' ' | ((uint16_t)terminal_color << 8);
    }
    cursor_y = VGA_HEIGHT - 1;
}

void print_char(uint8_t c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 4) {
            cursor_x--;
            VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)' ' | ((uint16_t)terminal_color << 8);
        }
    } else {
        VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)c | ((uint16_t)terminal_color << 8);
        cursor_x++;
    }
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= VGA_HEIGHT) {
        scroll();
    }
}

void print_string(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) print_char(str[i]);
}

void print_int(int num) {
    char str[16];
    int i = 0;
    if (num == 0) { print_char('0'); return; }
    if (num < 0) { print_char('-'); num = -num; }
    while (num > 0 && i < 15) {
        str[i++] = (num % 10) + '0';
        num /= 10;
    }
    for (int j = i - 1; j >= 0; j--) print_char(str[j]);
}

void print_num_2digits(int num) {
    if (num < 10) print_char('0');
    print_int(num);
}

void print_prompt() {
    uint8_t old = terminal_color;
    terminal_color = 0x0E;
    print_string("OS> ");
    terminal_color = old;
}

uint8_t get_rtc_register(int reg) {
    outb(0x70, reg);
    return inb(0x71);
}

void get_time(int* h, int* m, int* s) {
    outb(0x70, 0x0A);
    while (inb(0x71) & 0x80);
    *s = get_rtc_register(0x00);
    *m = get_rtc_register(0x02);
    *h = get_rtc_register(0x04);
    uint8_t regB = get_rtc_register(0x0B);
    if (!(regB & 0x04)) {
        *s = ((*s & 0xF0) >> 4) * 10 + (*s & 0x0F);
        *m = ((*m & 0xF0) >> 4) * 10 + (*m & 0x0F);
        *h = ((*h & 0xF0) >> 4) * 10 + (*h & 0x0F);
    }
}

void get_date(int* d, int* mo, int* y) {
    *d = get_rtc_register(0x07);
    *mo = get_rtc_register(0x08);
    *y = get_rtc_register(0x09);
    uint8_t regB = get_rtc_register(0x0B);
    if (!(regB & 0x04)) {
        *d = ((*d & 0xF0) >> 4) * 10 + (*d & 0x0F);
        *mo = ((*mo & 0xF0) >> 4) * 10 + (*mo & 0x0F);
        *y = ((*y & 0xF0) >> 4) * 10 + (*y & 0x0F);
    }
}

void run_animated_time() {
    clear_screen();
    print_string("Live Clock Active. Press ANY key to return to terminal.\n\n");
    int clock_y = cursor_y + 2;
    while (true) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if (!(sc & 0x80)) {
                inb(0x60);
                break;
            }
        }
        int h, m, s, d, mo, y;
        get_time(&h, &m, &s);
        get_date(&d, &mo, &y);
        set_cursor(25, clock_y);
        terminal_color = 0x0B;
        print_string("TIME: "); print_num_2digits(h); print_string(":"); print_num_2digits(m); print_string(":"); print_num_2digits(s); print_string(" UTC ");
        set_cursor(25, clock_y + 1);
        terminal_color = 0x0A;
        print_string("DATE: "); print_num_2digits(d); print_string("."); print_num_2digits(mo); print_string(".20"); print_num_2digits(y); print_string(" ");
        terminal_color = 0x07;
        set_cursor(0, 0);
        delay(10);
    }
    clear_screen();
}

void run_calculator(const char* args) {
    int i = 0;
    while (args[i] == ' ') i++;
    int num1 = 0;
    bool neg1 = false;
    if (args[i] == '-') { neg1 = true; i++; }
    if (args[i] < '0' || args[i] > '9') { print_string("Usage: calc <n1> <op> <n2>\n"); return; }
    while (args[i] >= '0' && args[i] <= '9') { num1 = num1 * 10 + (args[i] - '0'); i++; }
    if (neg1) num1 = -num1;
    while (args[i] == ' ') i++;
    char op = args[i++];
    while (args[i] == ' ') i++;
    int num2 = 0;
    bool neg2 = false;
    if (args[i] == '-') { neg2 = true; i++; }
    while (args[i] >= '0' && args[i] <= '9') { num2 = num2 * 10 + (args[i] - '0'); i++; }
    if (neg2) num2 = -num2;
    print_string("Result: ");
    if (op == '+') print_int(num1 + num2);
    else if (op == '-') print_int(num1 - num2);
    else if (op == '*') print_int(num1 * num2);
    else if (op == '/') {
        if (num2 == 0) print_string("Error: Div by zero!");
        else {
            print_int(num1 / num2);
            if (num1 % num2 != 0) {
                print_string(" (rem: ");
                print_int(num1 % num2);
                print_string(")");
            }
        }
    }
    print_char('\n');
}

// Поиск или создание индексного дескриптора файла
int get_or_create_file(const char* filename) {
    int i = 0;
    while (filename[i] == ' ') i++;
    if (filename[i] == '\0') return -1;

    for (int idx = 0; idx < MAX_FILES; idx++) {
        if (files[idx].exists && str_equal(files[idx].name, &filename[i])) { return idx; }
    }
    for (int idx = 0; idx < MAX_FILES; idx++) {
        if (!files[idx].exists) {
            str_copy(files[idx].name, &filename[i]);
            files[idx].content[0] = '\0';
            files[idx].size = 0;
            files[idx].exists = true;
            return idx;
        }
    }
    return -1;
}

// --- КЛАССИЧЕСКИЙ ТЕКСТОВЫЙ РЕДАКТОР НАНО ---
void run_nano(const char* filename) {
    int file_idx = get_or_create_file(filename);
    if (file_idx == -1) { print_string("Error: Ramdisk full or invalid name!\n"); return; }
    
    clear_screen();
    uint8_t old_color = terminal_color;
    
    // Серая верхняя панель Nano
    terminal_color = 0x70;
    for(int x=0; x<VGA_WIDTH; x++) draw_char(x, 0, ' ', 0x70);
    set_cursor(2, 0); print_string("LumenOS GNU Nano v1.0 - File: "); print_string(files[file_idx].name);
    
    // Серая нижняя панель подсказок
    for(int x=0; x<VGA_WIDTH; x++) draw_char(x, VGA_HEIGHT-1, ' ', 0x70);
    set_cursor(2, VGA_HEIGHT - 1); print_string("[F2] Save & Exit to Terminal     [ESC] Cancel");
    
    // Стандартное черное поле ввода
    terminal_color = 0x0F;
    for(int y=1; y<VGA_HEIGHT-1; y++) {
        for(int x=0; x<VGA_WIDTH; x++) draw_char(x, y, ' ', 0x0F);
    }
    
    set_cursor(0, 1);
    print_string(files[file_idx].content);
    
    int edit_len = files[file_idx].size;
    char edit_buffer[500];
    for(int idx=0; idx<edit_len; idx++) edit_buffer[idx] = files[file_idx].content[idx];
    
    while (true) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if (!(sc & 0x80)) {
                if (sc == 0x01) break; // ESC
                if (sc == 0x3C) {      // F2 - Сохранить и выйти
                    edit_buffer[edit_len] = '\0';
for(int idx=0; idx<=edit_len; idx++) files[file_idx].content[idx] = edit_buffer[idx];
files[file_idx].size = edit_len;
break;
}
char c = is_russian ? kbd_ru[sc] : kbd_en[sc];
if (c == '\b' && edit_len > 0) {
edit_len--; cursor_x--;
if(cursor_x < 0) { cursor_x = VGA_WIDTH - 1; cursor_y--; }
VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)' ' | (0x0F << 8);
} else if (c == '\n' && edit_len < 499) {
edit_buffer[edit_len++] = '\n'; cursor_x = 0; cursor_y++;
} else if (c >= 32 && edit_len < 499) {
edit_buffer[edit_len++] = c;
VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)c | (0x0F << 8); cursor_x++;
if (cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; }
}
}
}
}
terminal_color = old_color; clear_screen();
}
// --- ПРОДВИНУТЫЙ РЕДАКТОР КОДА (CODE IDE) ---
void run_code_editor(const char* filename) {
int file_idx = get_or_create_file(filename);
if (file_idx == -1) { print_string("Error: Ramdisk full or invalid name!\n"); return; }
clear_screen();
uint8_t old_color = terminal_color;
// Синяя шапка IDE Borland Style
terminal_color = 0x1F;
for(int x=0; x<VGA_WIDTH; x++) draw_char(x, 0, ' ', 0x1F);
set_cursor(2, 0); print_string("LumenOS Source Code Editor - File: "); print_string(files[file_idx].name);
// Нижняя статус-панель
for(int x=0; x<VGA_WIDTH; x++) draw_char(x, VGA_HEIGHT-1, ' ', 0x70);
set_cursor(2, VGA_HEIGHT - 1);
terminal_color = 0x70; print_string("[F2] Save & Exit to Terminal       [ESC] Exit without Saving");
// Поле редактирования (Синий фон)
terminal_color = 0x1E;
for(int y=1; y<VGA_HEIGHT-1; y++) {
for(int x=0; x<VGA_WIDTH; x++) draw_char(x, y, ' ', 0x1E);
}
set_cursor(0, 1);
print_string(files[file_idx].content);
int edit_len = files[file_idx].size;
char edit_buffer[500];
for(int idx=0; idx<edit_len; idx++) edit_buffer[idx] = files[file_idx].content[idx];
while (true) {
if (inb(0x64) & 1) {
uint8_t sc = inb(0x60);
if (!(sc & 0x80)) {
if (sc == 0x01) break; // ESC
if (sc == 0x3C) {      // F2 - Сохранить и выйти
edit_buffer[edit_len] = '\0';
for(int idx=0; idx<=edit_len; idx++) files[file_idx].content[idx] = edit_buffer[idx];
files[file_idx].size = edit_len;
break;
}
char c = kbd_en[sc]; // Скрипты пишем только на EN
if (c == '\b' && edit_len > 0) {
edit_len--; cursor_x--;
if(cursor_x < 0) { cursor_x = VGA_WIDTH - 1; cursor_y--; }
VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)' ' | (0x1E << 8);
} else if (c == '\n' && edit_len < 499) {
edit_buffer[edit_len++] = '\n'; cursor_x = 0; cursor_y++;
} else if (c >= 32 && edit_len < 499) {
edit_buffer[edit_len++] = c;
VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)c | (0x1E << 8); cursor_x++;
if (cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; }
}
}
}
}
terminal_color = old_color; clear_screen();
}
// --- ИНТЕРПРЕТАТОР СКРИПТОВ LUMENSCRIPT ---
void run_script(const char* filename) {
int i = 0; while (filename[i] == ' ') i++;
int file_idx = -1;
for (int idx = 0; idx < MAX_FILES; idx++) {
if (files[idx].exists && str_equal(files[idx].name, &filename[i])) { file_idx = idx; break; }
}
if (file_idx == -1) { print_string("Error: Script file not found.\n"); return; }
print_string("Executing core LumenScript engine...\n");
int reg_A = 0, reg_B = 0, reg_C = 0;
char* src = files[file_idx].content;
int ptr = 0;
while (src[ptr] != '\0') {
while (src[ptr] == '\n' || src[ptr] == '\r') ptr++;
if (src[ptr] == '\0') break;
if (src[ptr] == 'p' && src[ptr+1] == 'r' && src[ptr+2] == 'i' && src[ptr+3] == 'n' && src[ptr+4] == 't' && src[ptr+5] == ' ') {
ptr += 6; if (src[ptr] == '"') ptr++;
while (src[ptr] != '"' && src[ptr] != '\n' && src[ptr] != '\0') { print_char(src[ptr]); ptr++; }
if (src[ptr] == '"') { ptr++; }
print_char('\n');
}
else if (src[ptr] == 's' && src[ptr+1] == 'e' && src[ptr+2] == 't' && src[ptr+3] == ' ') {
ptr += 4; char r = src[ptr++]; while (src[ptr] == ' ' || src[ptr] == '=') ptr++;
int val = 0; while (src[ptr] >= '0' && src[ptr] <= '9') { val = val * 10 + (src[ptr] - '0'); ptr++; }
if (r == 'A') { reg_A = val; } if (r == 'B') { reg_B = val; } if (r == 'C') { reg_C = val; }
}
else if (src[ptr] == 'a' && src[ptr+1] == 'd' && src[ptr+2] == 'd' && src[ptr+3] == ' ') {
ptr += 4; char r = src[ptr++]; while (src[ptr] == ' ') ptr++;
int val = 0; while (src[ptr] >= '0' && src[ptr] <= '9') { val = val * 10 + (src[ptr] - '0'); ptr++; }
if (r == 'A') { reg_A += val; } if (r == 'B') { reg_B += val; } if (r == 'C') { reg_C += val; }
}
else if (src[ptr] == 's' && src[ptr+1] == 'h' && src[ptr+2] == 'o' && src[ptr+3] == 'w' && src[ptr+4] == ' ') {
ptr += 5; char r = src[ptr++]; print_string("Variable "); print_char(r); print_string(" = ");
if (r == 'A') { print_int(reg_A); } if (r == 'B') { print_int(reg_B); } if (r == 'C') { print_int(reg_C); }
print_char('\n');
}
else { while (src[ptr] != '\n' && src[ptr] != '\0') ptr++; }
}
}
void run_rm(const char* filename) {
int i = 0; while (filename[i] == ' ') i++;
for (int idx = 0; idx < MAX_FILES; idx++) {
if (files[idx].exists && str_equal(files[idx].name, &filename[i])) { files[idx].exists = false; print_string("File deleted.\n"); return; }
}
print_string("Error: File not found.\n");
}
void run_rename(const char* args) {
int i = 0; while (args[i] == ' ') i++;
char old_name[32]; int old_len = 0;
while (args[i] != ' ' && args[i] != '\0' && old_len < 31) { old_name[old_len++] = args[i++]; }
old_name[old_len] = '\0';
while (args[i] == ' ') i++;
char new_name[32]; int new_len = 0;
while (args[i] != ' ' && args[i] != '\0' && new_len < 31) { new_name[new_len++] = args[i++]; }
new_name[new_len] = '\0';
if (old_len == 0 || new_len == 0) { print_string("Usage: rename  \n"); return; }
for (int idx = 0; idx < MAX_FILES; idx++) {
if (files[idx].exists && str_equal(files[idx].name, old_name)) { str_copy(files[idx].name, new_name); print_string("File renamed.\n"); return; }
}
print_string("Error: File not found.\n");
}
void run_color(const char* arg) {
int i = 0; while (arg[i] == ' ') i++;
int code = 0; if (arg[i] < '0' || arg[i] > '9') { print_string("Usage: color <0-15>\n"); return; }
while (arg[i] >= '0' && arg[i] <= '9') { code = code * 10 + (arg[i] - '0'); i++; }
if (code >= 0 && code <= 15) { terminal_color = (uint8_t)code; clear_screen(); print_string("Color changed.\n"); }
else print_string("Error: Invalid code.\n");
}
void run_matrix() {
clear_screen(); int drops[80];
for (int x = 0; x < 80; x++) { drops[x] = rand_num() % 25; }
print_string("Entering Matrix... Press ANY key to exit.\n"); delay(20); clear_screen();
while (true) {
if (inb(0x64) & 1) { uint8_t sc = inb(0x60); if (!(sc & 0x80)) { inb(0x60); break; } }
for (int x = 0; x < 80; x++) {
char rc = 33 + (rand_num() % 90); uint8_t col = (rand_num() % 4 == 0) ? 0x0A : 0x02;
draw_char(x, drops[x], rc, col); int tail = drops[x] - 12; if (tail < 0) tail += 25;
draw_char(x, tail, ' ', 0x00); drops[x]++; if (drops[x] >= 25) drops[x] = 0;
}
delay(1);
}
clear_screen();
}
void run_poweroff() {
print_string("Shutting down LumenOS system...\n");
delay(10); outw(0x604, 0x2000); outw(0xB004, 0x2000);
print_string("Poweroff failed.\n");
}
void run_reboot() {
print_string("Hardware reboot initialized...\n");
delay(10); outb(0x64, 0xFE);
print_string("Reboot failed.\n");
}
void draw_splash_screen() {
uint8_t old_color = terminal_color;
terminal_color = 0x09; clear_screen();
int y = 9; uint8_t b = 219;
for(int i=0; i<5; i++) { draw_char(10, y+i, b, 0x0F); draw_char(14, y+i, b, 0x0F); }
for(int i=1; i<4; i++) { draw_char(10+i, y+2, b, 0x0F); }
for(int i=0; i<5; i++) { draw_char(20, y+i, b, 0x0F); }
for(int i=1; i<5; i++) { draw_char(20+i, y, b, 0x0F); draw_char(20+i, y+2, b, 0x0F); draw_char(20+i, y+4, b, 0x0F); }
for(int i=0; i<5; i++) { draw_char(30, y+i, b, 0x0F); }
for(int i=1; i<5; i++) { draw_char(30+i, y+4, b, 0x0F); }
for(int i=0; i<5; i++) { draw_char(40, y+i, b, 0x0F); }
for(int i=1; i<5; i++) { draw_char(40+i, y+4, b, 0x0F); }
for(int i=0; i<5; i++) { draw_char(50, y+i, b, 0x0F); draw_char(55, y+i, b, 0x0F); }
for(int i=1; i<5; i++) { draw_char(50+i, y, b, 0x0F); draw_char(50+i, y+4, b, 0x0F); }
set_cursor(23, y + 7);
uint8_t original_color = terminal_color; terminal_color = 0x0E;
print_string("LumenOS Core booting up successfully...");
terminal_color = original_color; set_cursor(0, 0); delay(40);
terminal_color = old_color; clear_screen();
}
void print_neofetch() {
for(int i = 0; i < 8; i++) print_char('\n');
int start_y = cursor_y - 8;
draw_char(2, start_y,     219, 0x0F); draw_char(3, start_y,     219, 0x0F); draw_char(4, start_y,     187, 0x0F);
draw_char(2, start_y + 1, 219, 0x0F); draw_char(3, start_y + 1, 219, 0x0F); draw_char(4, start_y + 1, 186, 0x0F);
draw_char(2, start_y + 2, 219, 0x0F); draw_char(3, start_y + 2, 219, 0x0F); draw_char(4, start_y + 2, 186, 0x0F);
draw_char(2, start_y + 3, 219, 0x0F); draw_char(3, start_y + 3, 219, 0x0F); draw_char(4, start_y + 3, 186, 0x0F);
for(int i=0; i<7; i++) { draw_char(2+i, start_y + 4, 219, 0x0F); } draw_char(9, start_y + 4, 187, 0x0F);
draw_char(2, start_y + 5, 200, 0x0F); for(int i=0; i<6; i++) { draw_char(3+i, start_y + 5, 205, 0x0F); } draw_char(9, start_y + 5, 188, 0x0F);
set_cursor(15, start_y); uint8_t old = terminal_color; terminal_color = 0x0A; print_string("root@lumenos"); terminal_color = old;
set_cursor(15, start_y + 1); print_string("------------");
set_cursor(15, start_y + 2); print_string("OS: LumenOS v1.0.0 x86_64");
set_cursor(15, start_y + 3); print_string("Kernel: 6.12.0-lumen-core");
set_cursor(15, start_y + 4); print_string("Uptime: 3 hours, 10 mins");
set_cursor(15, start_y + 5); print_string("Layout: "); print_string(is_russian ? "RU (Transit)" : "EN");
set_cursor(15, start_y + 6); print_string("Memory: 14 MB / "); print_int(system_ram_mb); print_string(" MB");
set_cursor(0, start_y + 7);
}
void execute_command() {
print_char('\n'); input_buffer[input_len] = '\0';
if (input_len == 0) { print_prompt(); return; }
// Два независимых парсера для Nano и Code IDE
if (str_start_with(input_buffer, "nano ")) {
run_nano(input_buffer + 5);
clear_buffer(); print_prompt(); return;
}
if (str_start_with(input_buffer, "code ")) {
run_code_editor(input_buffer + 5);
clear_buffer(); print_prompt(); return;
}
if (str_start_with(input_buffer, "run ")) {
run_script(input_buffer + 4);
clear_buffer(); print_prompt(); return;
}
if (str_start_with(input_buffer, "calc ")) { run_calculator(input_buffer + 5); clear_buffer(); print_prompt(); return; }
if (str_start_with(input_buffer, "rm ")) { run_rm(input_buffer + 3); clear_buffer(); print_prompt(); return; }
if (str_start_with(input_buffer, "rename ")) { run_rename(input_buffer + 7); clear_buffer(); print_prompt(); return; }
if (str_start_with(input_buffer, "color ")) { run_color(input_buffer + 6); clear_buffer(); print_prompt(); return; }
if (str_equal(input_buffer, "matrix")) run_matrix();
else if (str_equal(input_buffer, "time")) run_animated_time();
else if (str_equal(input_buffer, "clear")) clear_screen();
else if (str_equal(input_buffer, "about")) print_neofetch();
else if (str_equal(input_buffer, "poweroff")) run_poweroff();
else if (str_equal(input_buffer, "reboot")) run_reboot();
else if (str_equal(input_buffer, "help")) {
print_string("Commands:\n  help     - list commands\n  about    - system specs (Neofetch)\n  clear    - reset screen\n  poweroff - close QEMU\n  reboot   - restart system hardware\n  ls       - show files\n  nano    - classic text editor\n  code    - developer IDE for scripts\n  run     - execute script\n  rm      - delete file\n  rename  - rename file\n  time          - animated clock\n  color <0-15>  - text color\n  matrix        - screen saver\n");
} else if (str_equal(input_buffer, "ls")) {
print_string("Files on Ramdisk:\n"); int cnt = 0;
for (int i = 0; i < MAX_FILES; i++) {
if (files[i].exists) {
print_string("  - "); print_string(files[i].name);
print_string(" ("); print_int(files[i].size); print_string(" bytes)\n"); cnt++;
}
}
if (cnt == 0) print_string("  No files found.\n");
} else {
print_string("Unknown command: "); print_string(input_buffer); print_string("\n");
}
if (cursor_y >= VGA_HEIGHT) scroll();
clear_buffer(); print_prompt();
}
extern "C" void kernel_main(struct multiboot_info* mbi) {
if (mbi->flags & 1) { system_ram_mb = (mbi->mem_upper + 1024) / 1024; }
for (int i = 0; i < MAX_FILES; i++) files[i].exists = false;
draw_splash_screen();
print_string("Welcome to LumenOS Ultimate Terminal!\n");
print_string("Type 'help' to review system commands.\n\n");
print_prompt();
clear_buffer();
while (true) {
if (inb(0x64) & 1) {
uint8_t sc = inb(0x60);
if (!(sc & 0x80)) {
if (sc == 0x38) {
is_russian = !is_russian;
print_string("\n[Layout switched to: "); print_string(is_russian ? "RU" : "EN"); print_string("]\n");
print_prompt(); clear_buffer(); continue;
}
char c = is_russian ? kbd_ru[sc] : kbd_en[sc];
if (c == '\n') execute_command();
else if (c == '\b') { if (input_len > 0) { input_len--; print_char('\b'); input_buffer[input_len] = 0; } }
else if (c >= 32) { if (input_len < 250) { input_buffer[input_len++] = c; print_char(c); } }
}
}
}
}
EOF