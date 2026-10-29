/* dialog.c - WIN16 fixture (SPEC.md 124, docs/plans/WIN16-PLAN.md S6): a
 * modal dialog from a resource with STATIC, EDIT, a group box, two radio
 * buttons, a check box, a LISTBOX and two push buttons, driven the way
 * WinMine's Preferences and Calculator's Statistics Box drive theirs:
 * MakeProcInstance, DialogBox, WM_INITDIALOG, SetDlgItemText,
 * CheckRadioButton, SendDlgItemMessage(LB_ADDSTRING), GetDlgItemText,
 * IsDlgButtonChecked, EndDialog. What came back is shown in a message box.
 */
#include <windows.h>
#include <stdio.h>
#include "dialog.h"

static char name[32] = "os8088";
static int large, sound, pick;

BOOL __export CALLBACK DlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(dlg, IDC_NAME, name);
        CheckRadioButton(dlg, IDC_SMALL, IDC_LARGE, IDC_SMALL);
        SendDlgItemMessage(dlg, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"Beginner");
        SendDlgItemMessage(dlg, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"Intermediate");
        SendDlgItemMessage(dlg, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"Expert");
        SendDlgItemMessage(dlg, IDC_LIST, LB_SETCURSEL, 1, 0);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_SOUND:
            CheckDlgButton(dlg, IDC_SOUND, !IsDlgButtonChecked(dlg, IDC_SOUND));
            return TRUE;
        case IDC_SMALL: case IDC_LARGE:
            CheckRadioButton(dlg, IDC_SMALL, IDC_LARGE, wp);
            return TRUE;
        case IDOK:
            GetDlgItemText(dlg, IDC_NAME, name, sizeof name);
            large = IsDlgButtonChecked(dlg, IDC_LARGE);
            sound = IsDlgButtonChecked(dlg, IDC_SOUND);
            pick = (int)SendDlgItemMessage(dlg, IDC_LIST, LB_GETCURSEL, 0, 0);
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    FARPROC fp = MakeProcInstance((FARPROC)DlgProc, hInst);
    int r = DialogBox(hInst, "W16DLG", NULL, (DLGPROC)fp);
    char buf[96];

    FreeProcInstance(fp);
    sprintf(buf, "%s: name %s, %s, sound %s, list %d",
            r == IDOK ? "OK" : "Cancel", name, large ? "large" : "small",
            sound ? "on" : "off", pick);
    MessageBox(NULL, buf, "WIN16 dialog", MB_OK);
    return 0;
}
