#pragma once
/* the RP2350's QSPI memory interface: only its timing registers are named */
typedef struct { struct { volatile uint32_t timing, rfmt, rcmd, wfmt, wcmd; } m[2]; } qmi_hw_t;
extern qmi_hw_t pmh_qmi;
#define qmi_hw (&pmh_qmi)
