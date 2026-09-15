#include <stdio.h>
#include <string.h>

#include "../writer.c"

static unsigned char memory[65536];
static unsigned char written[65536];
static unsigned write_count;
static unsigned write_seg;
static int write_calls;
static int write_refuse;
static int fake_ferr;
static char write_name[16];
static int checks;
static int failures;

int os88_peek(unsigned segment, unsigned offset)
{
    (void)segment;
    return memory[offset];
}

void os88_poke(unsigned segment, unsigned offset, int value)
{
    (void)segment;
    memory[offset] = (unsigned char)value;
}

int os88_file_write_seg(const char *name, unsigned segment, unsigned count)
{
    ++write_calls;
    write_seg = segment;
    write_count = count;
    strcpy(write_name, name);
    memcpy(written, memory, count);
    return write_refuse ? -1 : 0;
}

int os88_ferr(void)
{
    return fake_ferr;
}

static void check(int value, const char *message)
{
    ++checks;
    if (!value) {
        ++failures;
        fprintf(stderr, "writer: FAIL: %s\n", message);
    }
}

static unsigned word_at(int off)
{
    return (unsigned)memory[off] | (unsigned)memory[off + 1] << 8;
}

static void reset(void)
{
    memset(memory, 0xa5, sizeof(memory));
    memset(written, 0, sizeof(written));
    write_count = write_seg = 0;
    write_calls = write_refuse = fake_ferr = 0;
    write_name[0] = 0;
}

static void test_plain(void)
{
    int i;
    reset();
    check(sbw_bind(0x2340, 4096) == SBW_OK, "bind claim");
    check(sbw_header(1000, 2000, 96, SBW_F_ICON) == SBW_OK,
          "stamp icon header");
    check(sbw_stack_class(2) == SBW_OK, "stack class");
    check(sbw_name("MY BASIC") == SBW_OK, "package name");
    check(memory[0] == 'O' && memory[1] == '8' && memory[2] == 3,
          "magic and version");
    check(memory[3] == SBW_F_ICON && word_at(4) == 0,
          "flags and link base");
    check(word_at(6) == 96 && word_at(8) == 1000 && word_at(10) == 2000,
          "entry/image/bss words");
    check(memory[12] == 0xff && memory[13] == 0xd5
          && memory[14] == 0xcb && memory[15] == 2,
          "dispatcher and stack");
    check(memcmp(memory + 16, "MY BASIC", 8) == 0, "name bytes");
    for (i = 24; i < 32; ++i)
        check(memory[i] == 0, "name NUL padding");
    memory[999] = 0x6c;
    memory[1000] = 0x7d;
    check(sbw_write("OUTPUT.O88") == SBW_OK, "write succeeds");
    check(write_calls == 1 && write_seg == 0x2340 && write_count == 1000,
          "segmented image-only write");
    check(strcmp(write_name, "OUTPUT.O88") == 0 && written[999] == 0x6c,
          "file name and last image byte");
    check(written[1000] == 0, "BSS and byte past image not written");
}

static void test_refusals(void)
{
    reset();
    check(sbw_bind(0, 4096) == SBW_E_ARG, "zero segment refused");
    check(sbw_bind(0x2000, 31) == SBW_E_ARG, "short claim refused");
    check(sbw_bind(0x2000, 4096) == SBW_OK, "rebind");
    check(sbw_header(4097, 0, 32, 0) == SBW_E_SIZE,
          "image beyond capacity refused");
    check(sbw_header(1000, 0xf000, 32, 0) == SBW_E_SIZE,
          "image plus BSS budget refused");
    check(sbw_header(1000, 0, 31, 0) == SBW_E_ENTRY,
          "entry inside header refused");
    check(sbw_header(1000, 0, 32, 4) == SBW_E_FLAGS,
          "parts flag refused");
    check(sbw_header(1000, 0, 32, 0) == SBW_OK, "plain header");
    check(sbw_stack_class(5) == SBW_E_STACK, "unknown stack refused");
    check(sbw_write("NO.O88") == SBW_E_STATE && write_calls == 0,
          "bad stack invalidates final image");
    check(sbw_stack_class(0) == SBW_OK, "stack corrected");
    check(sbw_name("") == SBW_E_NAME, "empty name refused");
    check(sbw_name("1234567890123456") == SBW_E_NAME,
          "sixteen-byte name refused");
    check(sbw_write("NO.O88") == SBW_E_STATE && write_calls == 0,
          "unnamed image not written");
    check(sbw_name("GOOD") == SBW_OK, "valid name after refusal");
    check(sbw_name("") == SBW_E_NAME, "bad rename refused");
    check(sbw_write("NO.O88") == SBW_E_STATE && write_calls == 0,
          "bad rename invalidates prior name");
    check(sbw_name("GOOD") == SBW_OK, "name corrected");
    memory[12] = 0;
    check(sbw_write("NO.O88") == SBW_E_STATE && write_calls == 0,
          "damaged final header refused");
}

static void test_association_and_file_error(void)
{
    reset();
    memory[96] = 1;
    memory[97] = 'B'; memory[98] = 'A'; memory[99] = 'S';
    check(sbw_bind(0x3000, 2048) == SBW_OK, "association bind");
    check(sbw_header(512, 0, 112, SBW_F_ICON | SBW_F_ASSOC) == SBW_OK,
          "association header");
    check(sbw_name("ASSOC") == SBW_OK, "association name");
    write_refuse = 1;
    fake_ferr = 6;
    check(sbw_write("ASSOC.O88") == SBW_E_FILE, "file refusal returned");
    check(sbw_last_error() == SBW_E_FILE && sbw_file_error() == 6,
          "native file error captured");

    reset();
    memory[32] = 1;
    memory[33] = 'O'; memory[34] = '8'; memory[35] = '8';
    check(sbw_bind(0x3000, 2048) == SBW_OK, "bad association bind");
    check(sbw_header(512, 0, 48, SBW_F_ASSOC) == SBW_E_ASSOC,
          "O88 association refused");
}

int main(void)
{
    test_plain();
    test_refusals();
    test_association_and_file_error();
    if (failures) {
        fprintf(stderr, "writer: %d/%d failures\n", failures, checks);
        return 1;
    }
    printf("writer: %d checks - guest O88 writer PASS\n", checks);
    return 0;
}
