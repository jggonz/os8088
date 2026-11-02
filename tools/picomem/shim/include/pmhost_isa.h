/* pmhost_isa.h - the PicoMEM's bus, as calls instead of a PIO loop.
 *
 * picomem.cpp includes this in place of pm_isa_rp2040.h when PMHOST is set
 * (patches/host/). On the iron, core 1 sits in a hand-written loop that
 * takes each ISA cycle off a PIO state machine, looks the address up in
 * MEM_Index_T or IO_Index_T and calls the device. Here the emulator hands
 * over each cycle through pm_io_read & co (pmhost.cpp), and these functions
 * are that loop's body: the same tables, the same device calls, in the same
 * order, transcribed from pm_isa_rp2040.h. Only the PIO plumbing is gone.
 *
 * GPL-2.0-or-later, like the firmware it is compiled into.
 */
#pragma once

// pm_isa_rp2040.h's two PIO hand-offs, as flags.
static inline void pm_do_ior(void) { ISA_WasRead = true; }

// On the iron, multicore_launch_core1 starts the bus loop. The bus is the
// emulator's here, so core 1 has nothing to do.
void main_core1(void) {}

// RAM_Offset_Table holds pointers BIASED by the PC address they serve -
// &PMBIOS[-ROM_BIOS] and the like - so that table[idx][pc_address] is the
// byte. Several of those biases are an unsigned 32-bit negation, which wraps
// on a 32-bit Pico and comes out exactly 2^32 too high on a 64-bit host.
// pmh_cell undoes the wrap the way the Pico's pointer width does, and checks
// the result lands in one of the arrays a table entry can name.
static inline bool pmh_in(uintptr_t p, const volatile void *base, size_t n)
{
    return p >= (uintptr_t)base && p < (uintptr_t)base + n;
}
static inline bool pmh_known(uintptr_t p)
{
    return pmh_in(p, PMBIOS, 16 * 1024) || pmh_in(p, PM_DP_RAM, sizeof PM_DP_RAM)
        || pmh_in(p, PM_Memory, sizeof PM_Memory)
#if MUX_V2
        || pmh_in(p, pmh_psram2, PMH_PSRAM2_SIZE)
#endif
        ;
}
static volatile uint8_t *pmh_cell(uint32_t idx, uint32_t a)
{
    uintptr_t p = (uintptr_t)RAM_Offset_Table[idx] + a;
    if (pmh_known(p)) return (volatile uint8_t *)p;
    p -= (uintptr_t)1 << 32;
    if (pmh_known(p)) return (volatile uint8_t *)p;
    return NULL;
}

#if MUX_V2
// THE 2.x (pm_isa_rp2350_m2_v15.h). Its PSRAM is mapped into the RP2350's
// address space, so the loop reads every index - RAM, ROM, disk buffer,
// PSRAM and EMS - through RAM_Offset_Table, and an EMS page register write
// moves its table entry (dev_memory.cpp). It writes the Pico's RAM below
// MI_W_NOWS_END and the PSRAM from 16 up, and nothing in between (the ROMs).
extern "C" bool pmh_bus_memr(uint32_t ISA_Addr, uint8_t *Data)
{
    uint32_t idx = GetMEMIndex(ISA_Addr & 0xFFFFF);   // the table is kept bus-shaped: A8-A11 inverted
    if (idx == MEM_I_NULL) return false;
    volatile uint8_t *c = pmh_cell(idx, ISA_Addr);
    if (!c) return false;
    *Data = *c;
    return true;
}

extern "C" bool pmh_bus_memw(uint32_t ISA_Addr, uint8_t Data)
{
    uint32_t idx = GetMEMIndex(ISA_Addr & 0xFFFFF);
    if (idx == MEM_I_NULL) return false;
    if (idx < MI_W_NOWS_END || idx >= 16) {
        volatile uint8_t *c = pmh_cell(idx, ISA_Addr);
        if (c) *c = Data;
        return c != NULL;
    }
    return true;                                      // a ROM: decoded, and ignored
}
#else
extern "C" bool pmh_bus_memr(uint32_t ISA_Addr, uint8_t *Data)
{
    uint32_t idx = GetMEMIndex(ISA_Addr & 0xFFFFF);   // the table is kept bus-shaped: A16-A19 inverted
    if ((idx > 0) && (idx < MI_R_WS_END)) {          // the Pico's RAM, ROM, disk buffer
        volatile uint8_t *c = pmh_cell(idx, ISA_Addr);
        if (!c) return false;
        *Data = *c;
        return true;
    }
    if (idx == MEM_I_NULL) return false;
    if (idx == MEM_EMS)                               // EMS: the frame's page base
        ISA_Addr = EMS_Base[(ISA_Addr >> 14) & 0x03] + (ISA_Addr & 0x3FFF);
#if USE_SPI_PSRAM
    *Data = psram_read8(&psram_spi, ISA_Addr);        // PSRAM-backed RAM or EMS
    return true;
#else
    return false;
#endif
}

extern "C" bool pmh_bus_memw(uint32_t ISA_Addr, uint8_t Data)
{
    uint32_t idx = GetMEMIndex(ISA_Addr & 0xFFFFF);   // the table is kept bus-shaped: A16-A19 inverted
    if ((idx > 0) && (idx < MI_W_NOWS_END)) {         // the Pico's RAM and disk buffer
        volatile uint8_t *c = pmh_cell(idx, ISA_Addr);
        if (c) *c = Data;
        return c != NULL;
    }
    if (idx == MEM_I_NULL) return false;
    if (idx < MEM_I_PSRAM) return true;               // a ROM: decoded, and ignored
    if (idx == MEM_EMS)
        ISA_Addr = EMS_Base[(ISA_Addr >> 14) & 0x03u] + (ISA_Addr & 0x3FFFu);
#if USE_SPI_PSRAM
    psram_write8(&psram_spi, ISA_Addr, Data);
#endif
    return true;
}
#endif // MUX_V2

// The memory index the card would decode at this address (0 = none).
extern "C" uint8_t pmh_bus_memidx(uint32_t ISA_Addr)
{
    return GetMEMIndex(ISA_Addr & 0xFFFFF);
}

// OS88_PMHOST_NOSNOOP=1: the card stops hearing the PC's 8237 writes - a
// break-on-purpose for the rows (tests/pmbox.py), never a model of anything.
static int pmh_nosnoop = -1;

extern "C" bool pmh_bus_iow(uint32_t ISA_IO_Addr, uint8_t ISAIOW_Data)
{
    uint32_t IO_Device = GetPortType(ISA_IO_Addr & 0x3FF);
    if (pmh_nosnoop < 0) {
        const char *e = getenv("OS88_PMHOST_NOSNOOP");
        pmh_nosnoop = e && *e && *e != '0';
    }
    if (pmh_nosnoop && IO_Device == DEV_DMA) return false;
    switch (IO_Device) {
    case DEV_PM:    dev_pmio_iow(ISA_IO_Addr, ISAIOW_Data); break;
    case DEV_LTEMS: dev_ems_iow(ISA_IO_Addr, ISAIOW_Data); break;
#if PM_WIFI
#if USE_NE2000
    case DEV_NE2000:
        dev_ne2000_iow((ISA_IO_Addr - PM_Config->ne2000Port) & 0x1F, ISAIOW_Data);
        break;
#endif
#endif
#if DEV_POST_Enable
    case DEV_POST:  dev_post_iow(ISA_IO_Addr, ISAIOW_Data); break;
#endif
#if USE_USBHOST
    case DEV_JOY:   dev_joystick_iow(ISA_IO_Addr, ISAIOW_Data); break;
#endif
#if USE_AUDIO
    case DEV_ADLIB: dev_adlib_iow(ISA_IO_Addr, ISAIOW_Data); break;
    case DEV_CMS:   dev_cms_iow(ISA_IO_Addr, ISAIOW_Data); break;
    case DEV_TANDY: dev_tdy_iow(ISA_IO_Addr, ISAIOW_Data); break;
    case DEV_MMB:   dev_mmb_iow(ISA_IO_Addr, ISAIOW_Data); break;
    case DEV_CVX:   dev_cvx_iow(ISA_IO_Addr, ISAIOW_Data); break;
#if USE_SBDSP
    case DEV_SBDSP: dev_sbdsp_iow(ISA_IO_Addr, ISAIOW_Data); break;
    case DEV_DMA:   dev_dma_iow(ISA_IO_Addr, ISAIOW_Data); break;
#endif
#endif
#if USE_RTC
    case DEV_RTC:   dev_rtc_iow(ISA_IO_Addr, ISAIOW_Data); break;
#endif
    default: return false;
    }
    return true;
}

extern "C" bool pmh_bus_ior(uint32_t ISA_IO_Addr, uint8_t *Data)
{
    uint8_t ISA_Data = 0xFF;
    uint32_t IO_Device = GetPortType(ISA_IO_Addr & 0x3FF);
    ISA_WasRead = false;
    switch (IO_Device) {
    case DEV_PM:    dev_pmio_ior(ISA_IO_Addr, &ISA_Data); pm_do_ior(); break;
    case DEV_LTEMS: dev_ems_ior(ISA_IO_Addr, &ISA_Data); pm_do_ior(); break;
#if PM_WIFI
#if USE_NE2000
    case DEV_NE2000: dev_ne2000_ior(ISA_IO_Addr, &ISA_Data); pm_do_ior(); break;
#endif
#endif
#if USE_USBHOST
    case DEV_JOY:   dev_joystick_ior(ISA_IO_Addr, &ISA_Data); pm_do_ior(); break;
#endif
#if USE_AUDIO
    case DEV_ADLIB: if (dev_adlib_ior(ISA_IO_Addr, &ISA_Data)) pm_do_ior(); break;
    case DEV_CMS:   if (dev_cms_ior(ISA_IO_Addr, &ISA_Data)) pm_do_ior(); break;
    case DEV_MMB:   if (dev_mmb_ior(ISA_IO_Addr, &ISA_Data)) pm_do_ior(); break;
    case DEV_CVX:   if (dev_cvx_ior(ISA_IO_Addr, &ISA_Data)) pm_do_ior(); break;
#if USE_SBDSP
    case DEV_SBDSP: if (dev_sbdsp_ior(ISA_IO_Addr, &ISA_Data)) pm_do_ior(); break;
#endif
#endif
#if USE_RTC
    case DEV_RTC:   dev_rtc_ior(ISA_IO_Addr, &ISA_Data); pm_do_ior(); break;
#endif
    default: break;
    }
    *Data = ISA_Data;
    return ISA_WasRead;
}

// The device type decoding this I/O block (port >> 3), 0 = none.
extern "C" uint8_t pmh_bus_portdev(uint32_t block)
{
    return GetPortType((block << 3) & 0x3FF);
}
