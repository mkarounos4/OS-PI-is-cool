#include "cc_runtime_symbols.h"
#include "lib/errno.h"
#include "lib/fs_syscall.h"
#include "lib/malloc.h"
#include "lib/stdio.h"
#include "lib/string.h"

#include <stdint.h>
#include <stddef.h>

extern const unsigned char __cc_runtime_elf_start[];
extern const unsigned char __cc_runtime_elf_end[];

#define TEXT_BASE UINT64_C(0x10000)
#define RODATA_BASE UINT64_C(0x20000)
#define MAX_SOURCE (64 * 1024)
#define MAX_CODE (64 * 1024)
#define MAX_RODATA (32 * 1024)
#define MAX_OUTPUT (1024 * 1024)
#define MAX_SYMBOLS 128
#define MAX_LOCALS 128
#define MAX_PATCHES 256
#define MAX_PARAMS 8

enum token_kind {
    TOK_EOF = 0,
    TOK_ID,
    TOK_NUM,
    TOK_STR,
    TOK_CHAR,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_INT,
    TOK_LONG,
    TOK_CHAR_KW,
    TOK_VOID,
    TOK_CONST,
    TOK_SIZEOF,
    TOK_EQ,
    TOK_NE,
    TOK_LE,
    TOK_GE,
    TOK_ANDAND,
    TOK_OROR,
};

struct token {
    enum token_kind kind;
    const char *start;
    int len;
    int64_t value;
    uint32_t ro_offset;
    int line;
    int col;
    int punct;
};

struct type {
    int base;
    int ptrs;
};

struct local {
    char name[32];
    struct type type;
    int offset;
    int is_array;
    int array_count;
};

struct func {
    char name[32];
    uint64_t addr;
};

struct patch {
    char name[32];
    uint32_t off;
};

struct branch_patch {
    uint32_t off;
};

static char source[MAX_SOURCE];
static uint32_t source_len;
static uint8_t code[MAX_CODE];
static uint32_t code_len;
static uint8_t rodata[MAX_RODATA];
static uint32_t rodata_len;
static uint8_t output[MAX_OUTPUT];

static const char *lex_p;
static const char *lex_end;
static int lex_line;
static int lex_col;
static struct token tok;

static struct func funcs[MAX_SYMBOLS];
static int func_count;
static struct patch call_patches[MAX_PATCHES];
static int call_patch_count;
static struct local locals[MAX_LOCALS];
static int local_count;
static int frame_size;
static int current_function_valid;
static uint32_t frame_patch_off;
static struct branch_patch return_patches[MAX_PATCHES];
static int return_patch_count;
static int had_error;

static int streq_len(const char *s, const char *t, int len) {
    int i = 0;
    while (i < len && s[i] == t[i] && t[i] != '\0') {
        i++;
    }
    return i == len && t[i] == '\0';
}

static void copy_name(char *dst, const char *src, int len) {
    int n = len;
    if (n > 31) {
        n = 31;
    }
    for (int i = 0; i < n; i++) {
        dst[i] = src[i];
    }
    dst[n] = '\0';
}

static int is_alpha_(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit_(char c) {
    return c >= '0' && c <= '9';
}

static void diag(const char *msg) {
    printf("cc:%d:%d: %s\n", tok.line, tok.col, msg);
    had_error = 1;
}

static void diag_name(const char *msg, const char *name) {
    printf("cc:%d:%d: %s %s\n", tok.line, tok.col, msg, name);
    had_error = 1;
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (i * 8));
    }
}

static void put_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (i * 8));
    }
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const uint8_t *p) {
    return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32);
}

static uint32_t align_up_u32(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}

static void emit32(uint32_t inst) {
    if (code_len + 4 > MAX_CODE) {
        diag("generated code is too large");
        return;
    }
    put_u32(code + code_len, inst);
    code_len += 4;
}

static void patch32(uint32_t off, uint32_t inst) {
    if (off + 4 <= code_len) {
        put_u32(code + off, inst);
    }
}

static void emit_mov_imm(int reg, uint64_t value) {
    emit32(0xd2800000u | (((uint32_t)(value & 0xffffu)) << 5) | (uint32_t)reg);
    emit32(0xf2a00000u | (((uint32_t)((value >> 16) & 0xffffu)) << 5) | (uint32_t)reg);
    emit32(0xf2c00000u | (((uint32_t)((value >> 32) & 0xffffu)) << 5) | (uint32_t)reg);
    emit32(0xf2e00000u | (((uint32_t)((value >> 48) & 0xffffu)) << 5) | (uint32_t)reg);
}

static void emit_add_imm(int rd, int rn, uint32_t imm) {
    if (imm > 4095) {
        diag("immediate is too large");
        imm = 0;
    }
    emit32(0x91000000u | (imm << 10) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

static void emit_sub_imm(int rd, int rn, uint32_t imm) {
    if (imm > 4095) {
        diag("immediate is too large");
        imm = 0;
    }
    emit32(0xd1000000u | (imm << 10) | ((uint32_t)rn << 5) | (uint32_t)rd);
}

static void emit_push_x0(void) {
    emit_sub_imm(31, 31, 16);
    emit32(0xf90003e0u);
}

static void emit_pop(int reg) {
    emit32(0xf94003e0u | (uint32_t)reg);
    emit_add_imm(31, 31, 16);
}

static void emit_ldr64(int rt, int rn, uint32_t off) {
    emit32(0xf9400000u | ((off / 8) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt);
}

static void emit_str64(int rt, int rn, uint32_t off) {
    emit32(0xf9000000u | ((off / 8) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt);
}

static void emit_ldrb(int rt, int rn, uint32_t off) {
    emit32(0x39400000u | (off << 10) | ((uint32_t)rn << 5) | (uint32_t)rt);
}

static void emit_strb(int rt, int rn, uint32_t off) {
    emit32(0x39000000u | (off << 10) | ((uint32_t)rn << 5) | (uint32_t)rt);
}

static void emit_cmp_x1_x0(void) {
    emit32(0xeb00003fu);
}

static void emit_cmp_x0_zero(void) {
    emit32(0xf100001fu);
}

static uint32_t emit_b_placeholder(void) {
    uint32_t off = code_len;
    emit32(0x14000000u);
    return off;
}

static uint32_t emit_bcond_placeholder(int cond) {
    uint32_t off = code_len;
    emit32(0x54000000u | (uint32_t)(cond & 0xf));
    return off;
}

static void patch_b(uint32_t off, uint64_t target) {
    int64_t diff = (int64_t)target - (int64_t)(TEXT_BASE + off);
    uint32_t imm = (uint32_t)((diff >> 2) & 0x03ffffff);
    patch32(off, 0x14000000u | imm);
}

static void patch_bl(uint32_t off, uint64_t target) {
    int64_t diff = (int64_t)target - (int64_t)(TEXT_BASE + off);
    uint32_t imm = (uint32_t)((diff >> 2) & 0x03ffffff);
    patch32(off, 0x94000000u | imm);
}

static void patch_bcond(uint32_t off, uint64_t target, int cond) {
    int64_t diff = (int64_t)target - (int64_t)(TEXT_BASE + off);
    uint32_t imm = (uint32_t)((diff >> 2) & 0x7ffff);
    patch32(off, 0x54000000u | (imm << 5) | (uint32_t)(cond & 0xf));
}

static uint32_t emit_call_placeholder(void) {
    uint32_t off = code_len;
    emit32(0x94000000u);
    return off;
}

static void emit_call_addr(uint64_t target) {
    patch_bl(emit_call_placeholder(), target);
}

static void emit_local_addr(int reg, int offset) {
    emit_sub_imm(reg, 29, (uint32_t)offset);
}

static uint32_t add_ro_bytes(const char *s, int len) {
    uint32_t off = rodata_len;
    if (rodata_len + (uint32_t)len + 1 > MAX_RODATA) {
        diag("rodata is too large");
        return 0;
    }
    for (int i = 0; i < len; i++) {
        rodata[rodata_len++] = (uint8_t)s[i];
    }
    rodata[rodata_len++] = 0;
    return off;
}

static int escape_char(char c) {
    if (c == 'n') return '\n';
    if (c == 'r') return '\r';
    if (c == 't') return '\t';
    if (c == '0') return '\0';
    return c;
}

static void next_token(void) {
    while (lex_p < lex_end) {
        char c = *lex_p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (c == '\n') {
                lex_line++;
                lex_col = 1;
            } else {
                lex_col++;
            }
            lex_p++;
            continue;
        }
        if (c == '#' && (lex_p == source || lex_p[-1] == '\n')) {
            while (lex_p < lex_end && *lex_p != '\n') {
                lex_p++;
            }
            continue;
        }
        if (c == '/' && lex_p + 1 < lex_end && lex_p[1] == '/') {
            lex_p += 2;
            while (lex_p < lex_end && *lex_p != '\n') {
                lex_p++;
            }
            continue;
        }
        if (c == '/' && lex_p + 1 < lex_end && lex_p[1] == '*') {
            lex_p += 2;
            while (lex_p + 1 < lex_end && !(lex_p[0] == '*' && lex_p[1] == '/')) {
                if (*lex_p == '\n') {
                    lex_line++;
                    lex_col = 1;
                } else {
                    lex_col++;
                }
                lex_p++;
            }
            if (lex_p + 1 < lex_end) {
                lex_p += 2;
                lex_col += 2;
            }
            continue;
        }
        break;
    }

    tok.start = lex_p;
    tok.len = 0;
    tok.value = 0;
    tok.ro_offset = 0;
    tok.line = lex_line;
    tok.col = lex_col;
    tok.punct = 0;

    if (lex_p >= lex_end) {
        tok.kind = TOK_EOF;
        return;
    }

    char c = *lex_p;
    if (is_alpha_(c)) {
        const char *start = lex_p;
        while (lex_p < lex_end && (is_alpha_(*lex_p) || is_digit_(*lex_p))) {
            lex_p++;
            lex_col++;
        }
        tok.kind = TOK_ID;
        tok.start = start;
        tok.len = (int)(lex_p - start);
        if (streq_len(start, "return", tok.len)) tok.kind = TOK_RETURN;
        else if (streq_len(start, "if", tok.len)) tok.kind = TOK_IF;
        else if (streq_len(start, "else", tok.len)) tok.kind = TOK_ELSE;
        else if (streq_len(start, "while", tok.len)) tok.kind = TOK_WHILE;
        else if (streq_len(start, "for", tok.len)) tok.kind = TOK_FOR;
        else if (streq_len(start, "int", tok.len)) tok.kind = TOK_INT;
        else if (streq_len(start, "long", tok.len)) tok.kind = TOK_LONG;
        else if (streq_len(start, "char", tok.len)) tok.kind = TOK_CHAR_KW;
        else if (streq_len(start, "void", tok.len)) tok.kind = TOK_VOID;
        else if (streq_len(start, "const", tok.len)) tok.kind = TOK_CONST;
        else if (streq_len(start, "sizeof", tok.len)) tok.kind = TOK_SIZEOF;
        return;
    }

    if (is_digit_(c)) {
        int base = 10;
        int64_t value = 0;
        if (c == '0' && lex_p + 1 < lex_end && (lex_p[1] == 'x' || lex_p[1] == 'X')) {
            base = 16;
            lex_p += 2;
            lex_col += 2;
        }
        while (lex_p < lex_end) {
            int d = -1;
            if (*lex_p >= '0' && *lex_p <= '9') d = *lex_p - '0';
            else if (*lex_p >= 'a' && *lex_p <= 'f') d = *lex_p - 'a' + 10;
            else if (*lex_p >= 'A' && *lex_p <= 'F') d = *lex_p - 'A' + 10;
            else break;
            if (d >= base) break;
            value = value * base + d;
            lex_p++;
            lex_col++;
        }
        tok.kind = TOK_NUM;
        tok.value = value;
        return;
    }

    if (c == '"') {
        char tmp[1024];
        int n = 0;
        lex_p++;
        lex_col++;
        while (lex_p < lex_end && *lex_p != '"' && n < (int)sizeof(tmp) - 1) {
            char out = *lex_p++;
            lex_col++;
            if (out == '\\' && lex_p < lex_end) {
                out = (char)escape_char(*lex_p++);
                lex_col++;
            }
            tmp[n++] = out;
        }
        if (lex_p < lex_end && *lex_p == '"') {
            lex_p++;
            lex_col++;
        } else {
            diag("unterminated string");
        }
        tok.kind = TOK_STR;
        tok.ro_offset = add_ro_bytes(tmp, n);
        return;
    }

    if (c == '\'') {
        lex_p++;
        lex_col++;
        char out = 0;
        if (lex_p < lex_end) {
            out = *lex_p++;
            lex_col++;
            if (out == '\\' && lex_p < lex_end) {
                out = (char)escape_char(*lex_p++);
                lex_col++;
            }
        }
        if (lex_p < lex_end && *lex_p == '\'') {
            lex_p++;
            lex_col++;
        } else {
            diag("unterminated character literal");
        }
        tok.kind = TOK_CHAR;
        tok.value = out;
        return;
    }

    if (lex_p + 1 < lex_end) {
        if (c == '=' && lex_p[1] == '=') { tok.kind = TOK_EQ; lex_p += 2; lex_col += 2; return; }
        if (c == '!' && lex_p[1] == '=') { tok.kind = TOK_NE; lex_p += 2; lex_col += 2; return; }
        if (c == '<' && lex_p[1] == '=') { tok.kind = TOK_LE; lex_p += 2; lex_col += 2; return; }
        if (c == '>' && lex_p[1] == '=') { tok.kind = TOK_GE; lex_p += 2; lex_col += 2; return; }
        if (c == '&' && lex_p[1] == '&') { tok.kind = TOK_ANDAND; lex_p += 2; lex_col += 2; return; }
        if (c == '|' && lex_p[1] == '|') { tok.kind = TOK_OROR; lex_p += 2; lex_col += 2; return; }
    }

    tok.kind = TOK_EOF;
    tok.punct = c;
    lex_p++;
    lex_col++;
}

static int accept_punct(int c) {
    if (tok.punct == c) {
        next_token();
        return 1;
    }
    return 0;
}

static void expect_punct(int c) {
    if (!accept_punct(c)) {
        diag("unexpected token");
    }
}

static int token_is_type_start(void) {
    return tok.kind == TOK_INT || tok.kind == TOK_LONG || tok.kind == TOK_CHAR_KW ||
           tok.kind == TOK_VOID || tok.kind == TOK_CONST ||
           (tok.kind == TOK_ID &&
            (streq_len(tok.start, "size_t", tok.len) ||
             streq_len(tok.start, "ssize_t", tok.len) ||
             streq_len(tok.start, "uint64_t", tok.len) ||
             streq_len(tok.start, "uint32_t", tok.len) ||
             streq_len(tok.start, "uint16_t", tok.len) ||
             streq_len(tok.start, "uint8_t", tok.len) ||
             streq_len(tok.start, "int32_t", tok.len) ||
             streq_len(tok.start, "pid_t", tok.len) ||
             streq_len(tok.start, "bool", tok.len)));
}

static struct type parse_type(void) {
    struct type t;
    t.base = 'i';
    t.ptrs = 0;
    while (tok.kind == TOK_CONST) {
        next_token();
    }
    if (tok.kind == TOK_CHAR_KW) {
        t.base = 'c';
        next_token();
    } else if (tok.kind == TOK_VOID) {
        t.base = 'v';
        next_token();
    } else if (tok.kind == TOK_LONG || tok.kind == TOK_INT) {
        t.base = 'i';
        next_token();
    } else if (tok.kind == TOK_ID) {
        next_token();
    } else {
        diag("expected type");
    }
    while (tok.kind == TOK_CONST) {
        next_token();
    }
    while (accept_punct('*')) {
        t.ptrs++;
        while (tok.kind == TOK_CONST) {
            next_token();
        }
    }
    return t;
}

static int type_size(struct type t) {
    if (t.ptrs > 0) return 8;
    if (t.base == 'c') return 1;
    return 8;
}

static struct type type_deref(struct type t) {
    if (t.ptrs > 0) {
        t.ptrs--;
    }
    return t;
}

static int find_local(const char *name, int len) {
    for (int i = local_count - 1; i >= 0; i--) {
        if (streq_len(name, locals[i].name, len)) {
            return i;
        }
    }
    return -1;
}

static int add_local(const char *name, int len, struct type type, int bytes, int is_array, int count) {
    if (local_count >= MAX_LOCALS) {
        diag("too many locals");
        return 0;
    }
    frame_size = align_up_u32((uint32_t)(frame_size + bytes), 8);
    copy_name(locals[local_count].name, name, len);
    locals[local_count].type = type;
    locals[local_count].offset = frame_size;
    locals[local_count].is_array = is_array;
    locals[local_count].array_count = count;
    return local_count++;
}

static int find_func(const char *name) {
    for (int i = 0; i < func_count; i++) {
        if (strcmp(funcs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_func_tok(const char *name, int len) {
    for (int i = 0; i < func_count; i++) {
        if (streq_len(name, funcs[i].name, len)) {
            return i;
        }
    }
    return -1;
}

static void define_func(const char *name, int len, uint64_t addr) {
    int idx = find_func_tok(name, len);
    if (idx < 0) {
        if (func_count >= MAX_SYMBOLS) {
            diag("too many functions");
            return;
        }
        idx = func_count++;
        copy_name(funcs[idx].name, name, len);
    }
    funcs[idx].addr = addr;
}

static uint64_t runtime_addr(const char *name, int len) {
    if (streq_len(name, "printf", len)) return CC_RUNTIME_PRINTF;
    if (streq_len(name, "puts", len)) return CC_RUNTIME_PUTS;
    if (streq_len(name, "strlen", len)) return CC_RUNTIME_STRLEN;
    if (streq_len(name, "strcmp", len)) return CC_RUNTIME_STRCMP;
    if (streq_len(name, "strtol", len)) return CC_RUNTIME_STRTOL;
    if (streq_len(name, "malloc", len)) return CC_RUNTIME_MALLOC;
    if (streq_len(name, "realloc", len)) return CC_RUNTIME_REALLOC;
    if (streq_len(name, "calloc", len)) return CC_RUNTIME_CALLOC;
    if (streq_len(name, "free", len)) return CC_RUNTIME_FREE;
    if (streq_len(name, "memcpy", len)) return CC_RUNTIME_MEMCPY;
    if (streq_len(name, "syscall0", len)) return CC_RUNTIME_SYSCALL0;
    if (streq_len(name, "syscall1", len)) return CC_RUNTIME_SYSCALL1;
    if (streq_len(name, "syscall2", len)) return CC_RUNTIME_SYSCALL2;
    if (streq_len(name, "syscall3", len)) return CC_RUNTIME_SYSCALL3;
    if (streq_len(name, "syscall4", len)) return CC_RUNTIME_SYSCALL4;
    if (streq_len(name, "syscall5", len)) return CC_RUNTIME_SYSCALL5;
    if (streq_len(name, "syscall6", len)) return CC_RUNTIME_SYSCALL6;
    if (streq_len(name, "write_console", len)) return CC_RUNTIME_WRITE_CONSOLE;
    if (streq_len(name, "putc", len)) return CC_RUNTIME_PUTC;
    if (streq_len(name, "get_ticks", len)) return CC_RUNTIME_GET_TICKS;
    if (streq_len(name, "yield", len)) return CC_RUNTIME_YIELD;
    if (streq_len(name, "current_el", len)) return CC_RUNTIME_CURRENT_EL;
    if (streq_len(name, "delay", len)) return CC_RUNTIME_DELAY;
    if (streq_len(name, "sleep", len)) return CC_RUNTIME_SLEEP;
    if (streq_len(name, "exit", len)) return CC_RUNTIME_EXIT;
    if (streq_len(name, "getpid", len)) return CC_RUNTIME_GETPID;
    if (streq_len(name, "waitpid", len)) return CC_RUNTIME_WAITPID;
    if (streq_len(name, "kill", len)) return CC_RUNTIME_KILL;
    if (streq_len(name, "touch", len)) return CC_RUNTIME_TOUCH;
    if (streq_len(name, "mv", len)) return CC_RUNTIME_MV;
    if (streq_len(name, "rm", len)) return CC_RUNTIME_RM;
    if (streq_len(name, "cat", len)) return CC_RUNTIME_CAT;
    if (streq_len(name, "cp", len)) return CC_RUNTIME_CP;
    if (streq_len(name, "ls", len)) return CC_RUNTIME_LS;
    if (streq_len(name, "fs_mkdir", len)) return CC_RUNTIME_FS_MKDIR;
    if (streq_len(name, "cd", len)) return CC_RUNTIME_CD;
    if (streq_len(name, "putstr", len)) return CC_RUNTIME_PUTSTR;
    if (streq_len(name, "puthex", len)) return CC_RUNTIME_PUTHEX;
    if (streq_len(name, "open", len)) return CC_RUNTIME_OPEN;
    if (streq_len(name, "close", len)) return CC_RUNTIME_CLOSE;
    if (streq_len(name, "lseek", len)) return CC_RUNTIME_LSEEK;
    if (streq_len(name, "read", len)) return CC_RUNTIME_READ;
    if (streq_len(name, "write", len)) return CC_RUNTIME_WRITE;
    if (streq_len(name, "fs_chmod", len)) return CC_RUNTIME_FS_CHMOD;
    if (streq_len(name, "sigprocmask", len)) return CC_RUNTIME_SIGPROCMASK;
    if (streq_len(name, "sigemptyset", len)) return CC_RUNTIME_SIGEMPTYSET;
    if (streq_len(name, "sigaddset", len)) return CC_RUNTIME_SIGADDSET;
    if (streq_len(name, "sigfillset", len)) return CC_RUNTIME_SIGFILLSET;
    if (streq_len(name, "sigsuspend", len)) return CC_RUNTIME_SIGSUSPEND;
    if (streq_len(name, "sigaction", len)) return CC_RUNTIME_SIGACTION;
    if (streq_len(name, "fork", len)) return CC_RUNTIME_FORK;
    if (streq_len(name, "dup2", len)) return CC_RUNTIME_DUP2;
    if (streq_len(name, "setpgid", len)) return CC_RUNTIME_SETPGID;
    if (streq_len(name, "getpgrp", len)) return CC_RUNTIME_GETPGRP;
    if (streq_len(name, "tcsetpgrp", len)) return CC_RUNTIME_TCSETPGRP;
    if (streq_len(name, "mount", len)) return CC_RUNTIME_MOUNT;
    if (streq_len(name, "unmount", len)) return CC_RUNTIME_UNMOUNT;
    if (streq_len(name, "pipe", len)) return CC_RUNTIME_PIPE;
    if (streq_len(name, "ps", len)) return CC_RUNTIME_PS;
    if (streq_len(name, "exec", len)) return CC_RUNTIME_EXEC;
    if (streq_len(name, "getcwd", len)) return CC_RUNTIME_GETCWD;
    if (streq_len(name, "stat", len)) return CC_RUNTIME_STAT;
    if (streq_len(name, "tty_get_mode", len)) return CC_RUNTIME_TTY_GET_MODE;
    if (streq_len(name, "tty_set_mode", len)) return CC_RUNTIME_TTY_SET_MODE;
    if (streq_len(name, "tty_get_size", len)) return CC_RUNTIME_TTY_GET_SIZE;
    if (streq_len(name, "tty_screen_enter", len)) return CC_RUNTIME_TTY_SCREEN_ENTER;
    if (streq_len(name, "tty_screen_leave", len)) return CC_RUNTIME_TTY_SCREEN_LEAVE;
    if (streq_len(name, "tty_screen_present", len)) return CC_RUNTIME_TTY_SCREEN_PRESENT;
    if (streq_len(name, "proc_change_priority", len)) return CC_RUNTIME_PROC_CHANGE_PRIORITY;
    if (streq_len(name, "createlink", len)) return CC_RUNTIME_CREATELINK;
    if (streq_len(name, "readlink", len)) return CC_RUNTIME_READLINK;
    return 0;
}

static int const_value(const char *name, int len, int64_t *out) {
    struct kv { const char *name; int value; };
    static const struct kv values[] = {
        {"NULL", 0}, {"true", 1}, {"false", 0},
        {"O_TRUNC", 4}, {"O_CREAT", 8}, {"O_APPEND", 16},
        {"O_RDONLY", 1}, {"O_WRONLY", 2}, {"O_RDWR", 3},
        {"F_SEEK_SET", 0}, {"F_SEEK_CUR", 1}, {"F_SEEK_END", 2},
        {"STDIN", 0}, {"STDOUT", 1}, {"STDERR", 2},
        {"STDIN_FILENO", 0}, {"STDOUT_FILENO", 1}, {"STDERR_FILENO", 2},
        {"WNOHANG", 1}, {"WUNTRACED", 2}, {"WCONTINUED", 4},
        {"SIGKILL", 9}, {"SIGSTOP", 10}, {"SIGCONT", 11},
        {"SIGCHLD", 12}, {"SIGTERM", 15},
    };
    for (uint32_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        if (streq_len(name, values[i].name, len)) {
            *out = values[i].value;
            return 1;
        }
    }
    return 0;
}

static struct type parse_expr(void);

static void emit_load_from_addr(struct type type) {
    if (type.ptrs == 0 && type.base == 'c') {
        emit_ldrb(0, 0, 0);
    } else {
        emit_ldr64(0, 0, 0);
    }
}

static struct type parse_primary(void) {
    struct type t = {'i', 0};
    if (tok.kind == TOK_NUM || tok.kind == TOK_CHAR) {
        emit_mov_imm(0, (uint64_t)tok.value);
        next_token();
        return t;
    }
    if (tok.kind == TOK_STR) {
        emit_mov_imm(0, RODATA_BASE + tok.ro_offset);
        t.base = 'c';
        t.ptrs = 1;
        next_token();
        return t;
    }
    if (accept_punct('(')) {
        t = parse_expr();
        expect_punct(')');
        return t;
    }
    if (tok.kind == TOK_SIZEOF) {
        next_token();
        if (accept_punct('(')) {
            struct type st = parse_type();
            expect_punct(')');
            emit_mov_imm(0, (uint64_t)type_size(st));
        } else {
            emit_mov_imm(0, 8);
        }
        return t;
    }
    if (tok.kind == TOK_ID) {
        char name[32];
        copy_name(name, tok.start, tok.len);
        int name_len = tok.len;
        const char *name_start = tok.start;
        next_token();
        if (accept_punct('(')) {
            int argc = 0;
            if (!accept_punct(')')) {
                do {
                    if (argc >= MAX_PARAMS) {
                        diag("too many call arguments");
                    }
                    parse_expr();
                    emit_push_x0();
                    argc++;
                } while (accept_punct(','));
                expect_punct(')');
            }
            for (int i = argc - 1; i >= 0; i--) {
                emit_pop(i);
            }
            uint64_t target = runtime_addr(name, name_len);
            if (target != 0) {
                emit_call_addr(target);
            } else {
                int idx = find_func(name);
                uint32_t off = emit_call_placeholder();
                if (idx >= 0 && funcs[idx].addr != 0) {
                    patch_bl(off, funcs[idx].addr);
                } else if (call_patch_count < MAX_PATCHES) {
                    copy_name(call_patches[call_patch_count].name, name, name_len);
                    call_patches[call_patch_count].off = off;
                    call_patch_count++;
                } else {
                    diag("too many call patches");
                }
            }
            return t;
        }
        int64_t cv = 0;
        if (const_value(name_start, name_len, &cv)) {
            emit_mov_imm(0, (uint64_t)cv);
            return t;
        }
        int li = find_local(name_start, name_len);
        if (li < 0) {
            diag_name("unknown identifier", name);
            emit_mov_imm(0, 0);
            return t;
        }
        t = locals[li].type;
        emit_local_addr(0, locals[li].offset);
        if (locals[li].is_array) {
            t.ptrs++;
        } else {
            emit_load_from_addr(t);
        }
        return t;
    }
    diag("expected expression");
    emit_mov_imm(0, 0);
    return t;
}

static struct type parse_unary(void) {
    if (accept_punct('&')) {
        if (tok.kind != TOK_ID) {
            diag("expected identifier after &");
            return parse_primary();
        }
        int li = find_local(tok.start, tok.len);
        struct type t = {'i', 1};
        if (li < 0) {
            diag("unknown identifier");
            emit_mov_imm(0, 0);
        } else {
            t = locals[li].type;
            t.ptrs++;
            emit_local_addr(0, locals[li].offset);
        }
        next_token();
        return t;
    }
    if (accept_punct('*')) {
        struct type t = parse_unary();
        t = type_deref(t);
        emit_load_from_addr(t);
        return t;
    }
    if (accept_punct('-')) {
        struct type t = parse_unary();
        emit32(0xcb0003e0u);
        return t;
    }
    if (accept_punct('!')) {
        struct type t = parse_unary();
        emit_cmp_x0_zero();
        emit32(0x9a9f17e0u);
        return t;
    }
    struct type t = parse_primary();
    while (accept_punct('[')) {
        emit_push_x0();
        parse_expr();
        int scale = type_size(type_deref(t));
        if (scale > 1) {
            emit_mov_imm(2, (uint64_t)scale);
            emit32(0x9b027c00u);
        }
        emit_pop(1);
        emit32(0x8b000020u);
        expect_punct(']');
        t = type_deref(t);
        emit_load_from_addr(t);
    }
    return t;
}

static struct type parse_mul(void) {
    struct type t = parse_unary();
    while (tok.punct == '*' || tok.punct == '/' || tok.punct == '%') {
        int op = tok.punct;
        next_token();
        emit_push_x0();
        parse_unary();
        emit_pop(1);
        if (op == '*') {
            emit32(0x9b007c20u);
        } else if (op == '/') {
            emit32(0x9ac00c20u);
        } else {
            emit32(0x9ac00c22u);
            emit32(0x9b008440u);
        }
        t.ptrs = 0;
    }
    return t;
}

static struct type parse_add(void) {
    struct type t = parse_mul();
    while (tok.punct == '+' || tok.punct == '-') {
        int op = tok.punct;
        next_token();
        emit_push_x0();
        parse_mul();
        emit_pop(1);
        if (op == '+') emit32(0x8b000020u);
        else emit32(0xcb000020u);
        t.ptrs = 0;
    }
    return t;
}

static void emit_cset_for_op(int op_kind, int punct) {
    emit_cmp_x1_x0();
    if (op_kind == TOK_EQ) emit32(0x9a9f17e0u);
    else if (op_kind == TOK_NE) emit32(0x9a9f07e0u);
    else if (op_kind == TOK_LE) emit32(0x9a9fc7e0u);
    else if (op_kind == TOK_GE) emit32(0x9a9fb7e0u);
    else if (punct == '<') emit32(0x9a9fa7e0u);
    else emit32(0x9a9fd7e0u);
}

static struct type parse_rel(void) {
    struct type t = parse_add();
    while (tok.kind == TOK_EQ || tok.kind == TOK_NE || tok.kind == TOK_LE ||
           tok.kind == TOK_GE || tok.punct == '<' || tok.punct == '>') {
        int kind = tok.kind;
        int punct = tok.punct;
        next_token();
        emit_push_x0();
        parse_add();
        emit_pop(1);
        emit_cset_for_op(kind, punct);
        t.base = 'i';
        t.ptrs = 0;
    }
    return t;
}

static struct type parse_expr(void) {
    if (tok.kind == TOK_ID) {
        struct token saved = tok;
        const char *saved_p = lex_p;
        int saved_line = lex_line;
        int saved_col = lex_col;
        next_token();
        int is_assign = tok.punct == '=';
        tok = saved;
        lex_p = saved_p;
        lex_line = saved_line;
        lex_col = saved_col;
        if (is_assign) {
            int li = find_local(tok.start, tok.len);
            char name[32];
            copy_name(name, tok.start, tok.len);
            if (li < 0) {
                diag_name("unknown identifier", name);
            }
            next_token();
            expect_punct('=');
            struct type t = parse_expr();
            if (li >= 0) {
                emit_local_addr(1, locals[li].offset);
                if (locals[li].type.ptrs == 0 && locals[li].type.base == 'c') {
                    emit_strb(0, 1, 0);
                } else {
                    emit_str64(0, 1, 0);
                }
            }
            return t;
        }
    }
    return parse_rel();
}

static void parse_statement(void);

static void parse_local_decl(void) {
    struct type t = parse_type();
    if (tok.kind != TOK_ID) {
        diag("expected local name");
        return;
    }
    const char *name = tok.start;
    int len = tok.len;
    next_token();
    int is_array = 0;
    int count = 1;
    int bytes = 8;
    if (accept_punct('[')) {
        is_array = 1;
        if (tok.kind == TOK_NUM) {
            count = (int)tok.value;
            next_token();
        } else {
            diag("expected array size");
        }
        expect_punct(']');
        bytes = align_up_u32((uint32_t)(type_size(t) * count), 8);
    }
    int li = add_local(name, len, t, bytes, is_array, count);
    if (accept_punct('=')) {
        parse_expr();
        if (!locals[li].is_array) {
            emit_local_addr(1, locals[li].offset);
            emit_str64(0, 1, 0);
        } else {
            diag("array initializer is not supported");
        }
    }
    expect_punct(';');
}

static void parse_block(void) {
    expect_punct('{');
    while (!had_error && tok.kind != TOK_EOF && !accept_punct('}')) {
        parse_statement();
    }
}

static void parse_statement(void) {
    if (tok.punct == '{') {
        parse_block();
        return;
    }
    if (tok.kind == TOK_RETURN) {
        next_token();
        if (!accept_punct(';')) {
            parse_expr();
            expect_punct(';');
        } else {
            emit_mov_imm(0, 0);
        }
        if (return_patch_count < MAX_PATCHES) {
            return_patches[return_patch_count++].off = emit_b_placeholder();
        } else {
            diag("too many returns");
        }
        return;
    }
    if (tok.kind == TOK_IF) {
        next_token();
        expect_punct('(');
        parse_expr();
        expect_punct(')');
        emit_cmp_x0_zero();
        uint32_t false_branch = emit_bcond_placeholder(0);
        parse_statement();
        if (tok.kind == TOK_ELSE) {
            uint32_t end_branch = emit_b_placeholder();
            patch_bcond(false_branch, TEXT_BASE + code_len, 0);
            next_token();
            parse_statement();
            patch_b(end_branch, TEXT_BASE + code_len);
        } else {
            patch_bcond(false_branch, TEXT_BASE + code_len, 0);
        }
        return;
    }
    if (tok.kind == TOK_WHILE) {
        next_token();
        uint64_t start = TEXT_BASE + code_len;
        expect_punct('(');
        parse_expr();
        expect_punct(')');
        emit_cmp_x0_zero();
        uint32_t done = emit_bcond_placeholder(0);
        parse_statement();
        patch_b(emit_b_placeholder(), start);
        patch_bcond(done, TEXT_BASE + code_len, 0);
        return;
    }
    if (tok.kind == TOK_FOR) {
        diag("for loops are not supported yet; use while");
        return;
    }
    if (token_is_type_start()) {
        parse_local_decl();
        return;
    }
    if (accept_punct(';')) {
        return;
    }
    parse_expr();
    expect_punct(';');
}

static void parse_function_body(const char *name, int name_len, struct local *params, int param_count) {
    current_function_valid = 1;
    define_func(name, name_len, TEXT_BASE + code_len);
    local_count = 0;
    frame_size = 0;
    return_patch_count = 0;

    emit32(0xa9bf7bfdu);
    emit32(0x910003fdu);
    frame_patch_off = code_len;
    emit_sub_imm(31, 31, 0);

    for (int i = 0; i < param_count; i++) {
        int li = add_local(params[i].name, (int)strlen(params[i].name),
                           params[i].type, 8, 0, 1);
        emit_local_addr(9, locals[li].offset);
        emit_str64(i, 9, 0);
    }

    parse_block();

    emit_mov_imm(0, 0);
    uint64_t epilogue = TEXT_BASE + code_len;
    for (int i = 0; i < return_patch_count; i++) {
        patch_b(return_patches[i].off, epilogue);
    }
    patch32(frame_patch_off, 0xd1000000u | ((uint32_t)frame_size << 10) | (31u << 5) | 31u);
    emit_add_imm(31, 31, (uint32_t)frame_size);
    emit32(0xa8c17bfdu);
    emit32(0xd65f03c0u);
    current_function_valid = 0;
}

static void skip_until_toplevel_end(void) {
    while (tok.kind != TOK_EOF && !accept_punct(';')) {
        if (tok.punct == '{') {
            int depth = 1;
            next_token();
            while (tok.kind != TOK_EOF && depth > 0) {
                if (tok.punct == '{') depth++;
                else if (tok.punct == '}') depth--;
                next_token();
            }
            return;
        }
        next_token();
    }
}

static void parse_toplevel(void) {
    while (!had_error && tok.kind != TOK_EOF) {
        if (!token_is_type_start()) {
            next_token();
            continue;
        }
        parse_type();
        if (tok.kind != TOK_ID) {
            skip_until_toplevel_end();
            continue;
        }
        const char *name = tok.start;
        int name_len = tok.len;
        next_token();
        if (!accept_punct('(')) {
            skip_until_toplevel_end();
            continue;
        }
        struct local params[MAX_PARAMS];
        int param_count = 0;
        if (!accept_punct(')')) {
            do {
                struct type pt = parse_type();
                if (tok.kind == TOK_ID) {
                    if (param_count >= MAX_PARAMS) {
                        diag("too many parameters");
                        break;
                    }
                    copy_name(params[param_count].name, tok.start, tok.len);
                    params[param_count].type = pt;
                    next_token();
                    if (accept_punct('[')) {
                        expect_punct(']');
                        params[param_count].type.ptrs++;
                    }
                    param_count++;
                } else if (pt.base == 'v' && pt.ptrs == 0) {
                    break;
                } else {
                    diag("expected parameter name");
                }
            } while (accept_punct(','));
            expect_punct(')');
        }
        if (accept_punct(';')) {
            continue;
        }
        if (tok.punct != '{') {
            skip_until_toplevel_end();
            continue;
        }
        parse_function_body(name, name_len, params, param_count);
    }
}

static void emit_start(uint32_t *main_call_off) {
    emit32(0xa9bf07e0u);
    emit_mov_imm(0, UINT64_C(0x400000));
    emit_mov_imm(1, UINT64_C(0x404000));
    emit_call_addr(CC_RUNTIME_MEM_INIT);
    emit32(0xa8c107e0u);
    *main_call_off = emit_call_placeholder();
    emit_mov_imm(8, 5);
    emit32(0xd4000001u);
    uint32_t spin = emit_b_placeholder();
    patch_b(spin, TEXT_BASE + spin);
}

static void resolve_calls(uint32_t main_call_off) {
    int main_idx = find_func("main");
    if (main_idx < 0 || funcs[main_idx].addr == 0) {
        diag("missing main");
        return;
    }
    patch_bl(main_call_off, funcs[main_idx].addr);
    for (int i = 0; i < call_patch_count; i++) {
        int idx = find_func(call_patches[i].name);
        if (idx < 0 || funcs[idx].addr == 0) {
            diag_name("undefined function", call_patches[i].name);
        } else {
            patch_bl(call_patches[i].off, funcs[idx].addr);
        }
    }
}

static int read_source(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print_errno("cc", path, fd);
        return fd;
    }
    source_len = 0;
    while (source_len < MAX_SOURCE - 1) {
        int n = read(fd, source + source_len, MAX_SOURCE - 1 - source_len);
        if (n < 0) {
            print_errno("cc", path, n);
            close(fd);
            return n;
        }
        if (n == 0) {
            break;
        }
        source_len += (uint32_t)n;
    }
    close(fd);
    source[source_len] = '\0';
    return 0;
}

static void write_elf_header(uint8_t *out, uint16_t phnum) {
    out[0] = 0x7f; out[1] = 'E'; out[2] = 'L'; out[3] = 'F';
    out[4] = 2;
    out[5] = 1;
    out[6] = 1;
    put_u16(out + 16, 2);
    put_u16(out + 18, 183);
    put_u32(out + 20, 1);
    put_u64(out + 24, TEXT_BASE);
    put_u64(out + 32, 64);
    put_u32(out + 48, 0);
    put_u16(out + 52, 64);
    put_u16(out + 54, 56);
    put_u16(out + 56, phnum);
}

static void write_phdr(uint8_t *p, uint32_t type, uint32_t flags, uint64_t off,
                       uint64_t vaddr, uint64_t filesz, uint64_t memsz,
                       uint64_t align) {
    put_u32(p + 0, type);
    put_u32(p + 4, flags);
    put_u64(p + 8, off);
    put_u64(p + 16, vaddr);
    put_u64(p + 24, vaddr);
    put_u64(p + 32, filesz);
    put_u64(p + 40, memsz);
    put_u64(p + 48, align);
}

static int build_output(const char *out_path) {
    const uint8_t *rt = __cc_runtime_elf_start;
    uint32_t rt_size = (uint32_t)(__cc_runtime_elf_end - __cc_runtime_elf_start);
    if (rt_size < 64 || rt[0] != 0x7f || rt[1] != 'E') {
        printf("cc: embedded runtime ELF is invalid\n");
        return -EINVAL;
    }
    uint16_t rt_phnum = get_u16(rt + 56);
    uint16_t rt_phentsize = get_u16(rt + 54);
    uint64_t rt_phoff = get_u64(rt + 32);
    int rt_loads = 0;
    for (int i = 0; i < rt_phnum; i++) {
        const uint8_t *ph = rt + rt_phoff + (uint64_t)i * rt_phentsize;
        if (get_u32(ph) == 1) {
            rt_loads++;
        }
    }

    uint16_t phnum = (uint16_t)(2 + rt_loads);
    for (uint32_t i = 0; i < MAX_OUTPUT; i++) {
        output[i] = 0;
    }
    write_elf_header(output, phnum);

    uint32_t text_off = 0x1000;
    uint32_t text_filesz = align_up_u32(code_len, 16);
    uint32_t ro_off = align_up_u32(text_off + text_filesz, 0x1000);
    uint32_t ro_filesz = rodata_len == 0 ? 1 : align_up_u32(rodata_len, 16);
    uint32_t out_pos = align_up_u32(ro_off + ro_filesz, 0x1000);

    write_phdr(output + 64, 1, 5, text_off, TEXT_BASE, text_filesz, text_filesz, 0x1000);
    write_phdr(output + 120, 1, 4, ro_off, RODATA_BASE, ro_filesz, ro_filesz, 0x1000);

    for (uint32_t i = 0; i < code_len; i++) {
        output[text_off + i] = code[i];
    }
    for (uint32_t i = 0; i < rodata_len; i++) {
        output[ro_off + i] = rodata[i];
    }

    int ph_index = 2;
    for (int i = 0; i < rt_phnum; i++) {
        const uint8_t *rph = rt + rt_phoff + (uint64_t)i * rt_phentsize;
        if (get_u32(rph) != 1) {
            continue;
        }
        uint64_t r_off = get_u64(rph + 8);
        uint64_t r_vaddr = get_u64(rph + 16);
        uint64_t r_filesz = get_u64(rph + 32);
        uint64_t r_memsz = get_u64(rph + 40);
        uint32_t r_flags = get_u32(rph + 4);
        if (r_off + r_filesz > rt_size || out_pos + r_filesz > MAX_OUTPUT) {
            printf("cc: output is too large\n");
            return -EIO;
        }
        write_phdr(output + 64 + ph_index * 56, 1, r_flags, out_pos,
                   r_vaddr, r_filesz, r_memsz, 0x1000);
        for (uint64_t j = 0; j < r_filesz; j++) {
            output[out_pos + j] = rt[r_off + j];
        }
        out_pos = align_up_u32((uint32_t)(out_pos + r_filesz), 0x1000);
        ph_index++;
    }

    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        print_errno("cc", out_path, fd);
        return fd;
    }
    uint32_t written = 0;
    while (written < out_pos) {
        int n = write(fd, (const char *)output + written, (int)(out_pos - written));
        if (n <= 0) {
            close(fd);
            return n < 0 ? n : -EIO;
        }
        written += (uint32_t)n;
    }
    int err = close(fd);
    if (err < 0) {
        return err;
    }
    err = fs_chmod((char *)out_path, "x", 2);
    if (err < 0) {
        print_errno("cc", out_path, err);
        return err;
    }
    return 0;
}

static int compile(const char *in_path, const char *out_path) {
    int err = read_source(in_path);
    if (err < 0) {
        return err;
    }
    code_len = 0;
    rodata_len = 0;
    func_count = 0;
    call_patch_count = 0;
    had_error = 0;
    current_function_valid = 0;

    uint32_t main_call_off = 0;
    emit_start(&main_call_off);

    lex_p = source;
    lex_end = source + source_len;
    lex_line = 1;
    lex_col = 1;
    next_token();
    parse_toplevel();
    resolve_calls(main_call_off);
    if (had_error) {
        return -EINVAL;
    }
    return build_output(out_path);
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output_path = "a.out";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                printf("usage: cc input.c [-o output]\n");
                return -EINVAL;
            }
            output_path = argv[++i];
        } else if (input == NULL) {
            input = argv[i];
        } else {
            printf("usage: cc input.c [-o output]\n");
            return -EINVAL;
        }
    }

    if (input == NULL) {
        printf("usage: cc input.c [-o output]\n");
        return -EINVAL;
    }
    return compile(input, output_path);
}
