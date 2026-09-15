/* Guest-side standalone O88 header finalizer and far-claim writer. */

#include "os88.h"
#include "writer.h"

#define SBW_HEADER       32u
#define SBW_ICON_END     96u
#define SBW_ASSOC_SIZE   16u
#define SBW_MAX_IMAGE    0xF000u

static unsigned sbw_seg;
static unsigned sbw_cap;
static unsigned sbw_image;
static unsigned sbw_bss;
static unsigned sbw_entry;
static int sbw_flags;
static int sbw_stack;
static int sbw_have_stack;
static int sbw_have_header;
static int sbw_have_name;
static int sbw_error;
static int sbw_ferror;

static int sbw_fail(int error)
{
    sbw_error = error;
    return error;
}

static void sbw_byte(unsigned off, int value)
{
    os88_poke(sbw_seg, off, value & 255);
}

static int sbw_get(unsigned off)
{
    return os88_peek(sbw_seg, off) & 255;
}

static void sbw_word(unsigned off, unsigned value)
{
    sbw_byte(off, (int)value);
    sbw_byte(off + 1, (int)(value >> 8));
}

static unsigned sbw_get_word(unsigned off)
{
    unsigned value;
    value = (unsigned)sbw_get(off);
    value |= (unsigned)sbw_get(off + 1) << 8;
    return value;
}

static int sbw_assoc_ok(int flags, unsigned image)
{
    unsigned base;
    int count, i, j, ch;

    if (!(flags & SBW_F_ASSOC)) return 1;
    base = (flags & SBW_F_ICON) ? SBW_ICON_END : SBW_HEADER;
    if (image < base + SBW_ASSOC_SIZE) return 0;
    count = sbw_get(base);
    if (count < 0 || count > 5) return 0;
    for (i = 0; i < count; ++i) {
        for (j = 0; j < 3; ++j) {
            ch = sbw_get(base + 1 + (unsigned)(i * 3 + j));
            if (ch < 0x20 || ch > 0x7e) return 0;
            if (ch >= 'a' && ch <= 'z') return 0;
        }
        if (sbw_get(base + 1 + (unsigned)(i * 3)) == 'O'
            && sbw_get(base + 2 + (unsigned)(i * 3)) == '8'
            && sbw_get(base + 3 + (unsigned)(i * 3)) == '8') return 0;
    }
    return 1;
}

static int sbw_name_ok(void)
{
    int i, ch;

    for (i = 0; i < 16; ++i) {
        ch = sbw_get(16u + (unsigned)i);
        if (ch == 0) return i > 0;
        if (ch < 0x20 || ch > 0x7e) return 0;
    }
    return 0;
}

int sbw_bind(unsigned segment, unsigned capacity)
{
    sbw_seg = 0;
    sbw_cap = 0;
    sbw_have_header = 0;
    sbw_have_name = 0;
    sbw_have_stack = 0;
    sbw_ferror = 0;
    if (segment == 0 || capacity < SBW_HEADER)
        return sbw_fail(SBW_E_ARG);
    sbw_seg = segment;
    sbw_cap = capacity;
    sbw_error = SBW_OK;
    return SBW_OK;
}

int sbw_header(unsigned image_size, unsigned bss_size,
               unsigned entry_offset, int flags)
{
    unsigned minimum;
    int i;

    sbw_have_header = 0;
    sbw_have_name = 0;
    sbw_have_stack = 0;
    if (sbw_seg == 0) return sbw_fail(SBW_E_STATE);
    if (flags & ~(SBW_F_ICON | SBW_F_ASSOC))
        return sbw_fail(SBW_E_FLAGS);
    minimum = (flags & SBW_F_ICON) ? SBW_ICON_END : SBW_HEADER;
    if (flags & SBW_F_ASSOC) minimum += SBW_ASSOC_SIZE;
    if (image_size < minimum || image_size > sbw_cap
        || bss_size > SBW_MAX_IMAGE
        || image_size > SBW_MAX_IMAGE - bss_size)
        return sbw_fail(SBW_E_SIZE);
    if (entry_offset < minimum || entry_offset >= image_size)
        return sbw_fail(SBW_E_ENTRY);
    if (!sbw_assoc_ok(flags, image_size))
        return sbw_fail(SBW_E_ASSOC);

    sbw_byte(0, 'O');
    sbw_byte(1, '8');
    sbw_byte(2, 3);
    sbw_byte(3, flags);
    sbw_word(4, 0);
    sbw_word(6, entry_offset);
    sbw_word(8, image_size);
    sbw_word(10, bss_size);
    sbw_byte(12, 0xff);
    sbw_byte(13, 0xd5);
    sbw_byte(14, 0xcb);
    sbw_byte(15, 0);
    for (i = 0; i < 16; ++i) sbw_byte(16u + (unsigned)i, 0);

    sbw_image = image_size;
    sbw_bss = bss_size;
    sbw_entry = entry_offset;
    sbw_flags = flags;
    sbw_stack = 0;
    sbw_have_stack = 1;
    sbw_have_header = 1;
    sbw_error = SBW_OK;
    return SBW_OK;
}

int sbw_stack_class(int stack_class)
{
    sbw_have_stack = 0;
    if (!sbw_have_header) return sbw_fail(SBW_E_STATE);
    if (stack_class < 0 || stack_class > 4)
        return sbw_fail(SBW_E_STACK);
    sbw_stack = stack_class;
    sbw_byte(15, stack_class);
    sbw_have_stack = 1;
    sbw_error = SBW_OK;
    return SBW_OK;
}

int sbw_name(const char *name)
{
    int i, length, ch;

    sbw_have_name = 0;
    if (!sbw_have_header) return sbw_fail(SBW_E_STATE);
    if (name == 0) return sbw_fail(SBW_E_NAME);
    length = 0;
    while (length < 16 && name[length] != 0) {
        ch = (unsigned char)name[length];
        if (ch < 0x20 || ch > 0x7e) return sbw_fail(SBW_E_NAME);
        ++length;
    }
    if (length == 0 || length > 15 || name[length] != 0)
        return sbw_fail(SBW_E_NAME);
    for (i = 0; i < 16; ++i)
        sbw_byte(16u + (unsigned)i, i < length ? name[i] : 0);
    sbw_have_name = 1;
    sbw_error = SBW_OK;
    return SBW_OK;
}

static int sbw_final_ok(void)
{
    if (sbw_get(0) != 'O' || sbw_get(1) != '8' || sbw_get(2) != 3)
        return 0;
    if (sbw_get(3) != sbw_flags || sbw_get_word(4) != 0)
        return 0;
    if (sbw_get_word(6) != sbw_entry
        || sbw_get_word(8) != sbw_image
        || sbw_get_word(10) != sbw_bss) return 0;
    if (sbw_get(12) != 0xff || sbw_get(13) != 0xd5
        || sbw_get(14) != 0xcb || sbw_get(15) != sbw_stack) return 0;
    if (!sbw_name_ok() || !sbw_assoc_ok(sbw_flags, sbw_image)) return 0;
    return 1;
}

int sbw_write(const char *filename)
{
    if (!sbw_have_header || !sbw_have_name || !sbw_have_stack || filename == 0
        || filename[0] == 0) return sbw_fail(SBW_E_STATE);
    if (!sbw_final_ok()) return sbw_fail(SBW_E_STATE);
    if (os88_file_write_seg(filename, sbw_seg, sbw_image) != 0) {
        sbw_ferror = os88_ferr();
        return sbw_fail(SBW_E_FILE);
    }
    sbw_ferror = 0;
    sbw_error = SBW_OK;
    return SBW_OK;
}

int sbw_last_error(void)
{
    return sbw_error;
}

int sbw_file_error(void)
{
    return sbw_ferror;
}
