#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../writer.c"
#include "../aot.c"
#include "../guest.c"

#define IMAGE_SEG  0x2000u
#define SOURCE_SEG 0x3000u

static unsigned char image_memory[65536];
static unsigned char source_memory[65536];
static unsigned char template_memory[65536];
static unsigned char written[65536];
static unsigned template_size;
static unsigned write_size;
static int claim_calls;
static int free_calls;
static int write_calls;
static int checks;
static int failures;
static struct os88_place current_place = { 7u, 1 };

static unsigned char *segment_memory(unsigned segment)
{
    if (segment == IMAGE_SEG) return image_memory;
    if (segment == SOURCE_SEG) return source_memory;
    fprintf(stderr, "guest: unexpected segment %04x\n", segment);
    exit(2);
}

int os88_peek(unsigned segment, unsigned offset)
{
    return segment_memory(segment)[offset];
}

void os88_poke(unsigned segment, unsigned offset, int value)
{
    segment_memory(segment)[offset] = (unsigned char)value;
}

unsigned os88_file_read_seg(const char *name, unsigned segment,
                            unsigned capacity)
{
    if (current_place.clus != 7u || current_place.vol != 1
        || strcmp(name, SBAOT_TEMPLATE_FILE) != 0 || capacity < template_size)
        return 0;
    memcpy(segment_memory(segment), template_memory, template_size);
    return template_size;
}

int os88_file_write_seg(const char *name, unsigned segment, unsigned count)
{
    (void)name;
    if (current_place.clus != 9u || current_place.vol != 1) return -1;
    ++write_calls;
    write_size = count;
    memcpy(written, segment_memory(segment), count);
    return 0;
}

int os88_ferr(void)
{
    return 0;
}

unsigned os88_mem_claim(unsigned kilobytes)
{
    ++claim_calls;
    return kilobytes == SBAOT_TEMPLATE_KB ? IMAGE_SEG : 0;
}

void os88_mem_free(unsigned segment)
{
    if (segment == IMAGE_SEG) ++free_calls;
}

void os88_file_here(struct os88_place *place)
{
    *place = current_place;
}

int os88_file_goto(struct os88_place *place)
{
    current_place = *place;
    return 0;
}

void os88_utoa(unsigned value, char *text)
{
    sprintf(text, "%u", value);
}

unsigned os88_strlen(const char *text)
{
    return (unsigned)strlen(text);
}

void os88_strcpy(char *destination, const char *source, unsigned capacity)
{
    unsigned i;
    if (!capacity) return;
    for (i = 0; i + 1u < capacity && source[i]; ++i)
        destination[i] = source[i];
    destination[i] = 0;
}

static void check(int value, const char *message)
{
    ++checks;
    if (!value) {
        ++failures;
        fprintf(stderr, "guest: FAIL: %s\n", message);
    }
}

static unsigned word_at(const unsigned char *memory, unsigned offset)
{
    return (unsigned)memory[offset] | (unsigned)memory[offset + 1u] << 8;
}

static void load_template(const char *path)
{
    FILE *file;
    long size;
    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "guest: cannot open template %s\n", path);
        exit(2);
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0
        || size > (long)sizeof(template_memory)
        || fseek(file, 0, SEEK_SET) != 0
        || fread(template_memory, 1, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "guest: cannot read template %s\n", path);
        fclose(file);
        exit(2);
    }
    fclose(file);
    template_size = (unsigned)size;
}

static void reset(const char *source)
{
    memset(image_memory, 0, sizeof(image_memory));
    memset(source_memory, 0, sizeof(source_memory));
    memset(written, 0, sizeof(written));
    memcpy(source_memory, source, strlen(source));
    claim_calls = free_calls = write_calls = 0;
    write_size = 0;
    current_place.clus = 9u;
    current_place.vol = 1;
}

static void test_supported_program(void)
{
    static const char source[] =
        "CLS\n"
        "PRINT \"HELLO\"\n"
        "SCREEN &H1\n"
        "COLOR 2, 0\n"
        "LOCATE 4, 5\n"
        "PSET (10, 20), 3\n"
        "LINE (1, 2)-(30, 40), 6\n"
        "END\n";
    static const unsigned address[] = {
        SBAOT_BODY, SBAOT_BODY + 12u, SBAOT_BODY + 34u,
        SBAOT_BODY + 53u, SBAOT_BODY + 76u, SBAOT_BODY + 99u,
        SBAOT_BODY + 126u, SBAOT_BODY + 161u
    };
    char error[64];
    int answer, i;
    reset(source);
    error[0] = 0;
    answer = ovl_sbg_compile(SOURCE_SEG, (unsigned)strlen(source),
                             "DEMO.O88", error, sizeof(error));
    check(answer == 0 && error[0] == 0, "supported source compiles");
    check(claim_calls == 1 && free_calls == 1, "template claim is released");
    check(write_calls == 1 && write_size == SBAOT_TEMPLATE_SIZE,
          "one exact-size package is written");
    check(memcmp(written, "O8\003\001", 4) == 0,
          "O88 v3 icon header survives compilation");
    check(word_at(written, 6) == SBAOT_TEMPLATE_ENTRY
          && word_at(written, 8) == SBAOT_TEMPLATE_SIZE
          && word_at(written, 10) == SBAOT_TEMPLATE_BSS,
          "entry, image, and BSS sizes are stamped");
    check(strcmp((char *)written + 16, "DEMO") == 0,
          "package header name drops the extension");
    check(strcmp((char *)written + SBAOT_TITLE, "DEMO") == 0,
          "window title is patched with the package name");

    check(written[SBAOT_CODE] == 0x55
          && written[SBAOT_CODE + 1u] == 0x89
          && written[SBAOT_CODE + 2u] == 0xe5,
          "dispatcher starts with 8086 frame setup");
    check(word_at(written, SBAOT_CODE + 5u) == SBAOT_PC,
          "dispatcher addresses the template PC state");
    check(word_at(written, SBAOT_CODE + 9u) == 8,
          "dispatcher bounds check uses emitted statement count");
    for (i = 0; i < 8; ++i)
        check(word_at(written, SBAOT_TABLE + (unsigned)(i * 2)) == address[i],
              "statement table contains expected body address");

    check(written[address[0]] == 0xe8, "CLS body calls its runtime helper");
    check(written[address[1]] == 0xb8
          && word_at(written, address[1] + 1u)
             == SBAOT_CODE + SBAOT_CODE_SIZE - 6u,
          "PRINT body loads the copied string address");
    check(memcmp(written + SBAOT_CODE + SBAOT_CODE_SIZE - 6u,
                 "HELLO", 6) == 0,
          "PRINT string is stored inside the reserved code slot");
    check(written[address[7]] == 0xb8
          && word_at(written, address[7] + 1u) == 1
          && written[address[7] + 3u] == 0x5d
          && written[address[7] + 4u] == 0xc3,
          "END body returns the done result");
}

static void test_diagnostic(void)
{
    static const char source[] = "CLS\nFOR I = 1 TO 10\nEND\n";
    char error[64];
    int answer;
    reset(source);
    error[0] = 0;
    answer = ovl_sbg_compile(SOURCE_SEG, (unsigned)strlen(source),
                             "BAD.O88", error, sizeof(error));
    check(answer < 0, "unsupported syntax is refused");
    check(strcmp(error, "Unsupported BASIC at line 2") == 0,
          "diagnostic identifies the source line");
    check(write_calls == 0, "failed program is not written");
    check(claim_calls == 1 && free_calls == 1,
          "failed compilation releases its template claim");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: guesttest SPEEDYCC.RT\n");
        return 2;
    }
    load_template(argv[1]);
    sb_compile_set_home();
    check(template_size == SBAOT_TEMPLATE_SIZE,
          "test template matches compiler's image size");
    test_supported_program();
    test_diagnostic();
    if (failures) {
        fprintf(stderr, "guest: %d/%d failures\n", failures, checks);
        return 1;
    }
    printf("guest: %d checks - in-OS compiler pipeline PASS\n", checks);
    return 0;
}
