#ifndef SPEEDYBASIC_COMPILER_VM_H
#define SPEEDYBASIC_COMPILER_VM_H

/* Complete-parser fallback template.  The compiler embeds its overlay and the
 * original BASIC text as eager package parts, so the resulting O88 has no
 * runtime sidecars.  hosttest/vmabi.py guards the generated offsets. */
#define SBVM_TEMPLATE_FILE "SPEEDYVM.RT"
#define SBVM_OVERLAY_FILE  "SPEEDYVM.OVL"
#define SBVM_TEMPLATE_KB   60u
#define SBVM_TEMPLATE_SIZE 49670u
#define SBVM_TEMPLATE_BSS  11768u
#define SBVM_OVERLAY_SIZE  16527u
#define SBVM_PARTS         0xbf89u
#define SBVM_MARKER        0xc041u
#define SBVM_TITLE         0xc038u

#define SBVM_PART_ROW0     (SBVM_PARTS + 10u)
#define SBVM_PART_ROW1     (SBVM_PART_ROW0 + 8u)

#endif
