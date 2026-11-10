/* ============================================================================
 * os8088 - apps/pagedesk/pduauth.c    THE PASSWORD PROMPT AND THE R6 QUESTION
 *                                     (PAGEDESK-SPEC 20.22.6)
 *
 * The state and the calls plat's open ladder makes (pdujob.c). The painting
 * and the keys are pdovlauth.c, an overlay tenant. The typed password lives
 * in pdu_needle (Find's buffer, free while another popover is up), so this
 * file's bss is four bytes.
 * ==========================================================================*/
/* the state, the AU_* modes and the closed hook are in pdu.h (the popover's
 * shut path in pducmd.c comes first in the translation unit) */
