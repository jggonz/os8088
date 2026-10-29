/* modeless.c - WIN16 fixture (SPEC.md 124.13, docs/plans/WIN16-PLAN.md S6):
 * dialog.c's dialog made MODELESS, the way CALC 3.0 makes its Scientific
 * panel: CreateDialog, ShowWindow, a GetMessage loop that hands every message
 * to IsDialogMessage first, and the dialog's own buttons ending it with
 * DestroyWindow and PostQuitMessage. SetDlgItemInt writes a signed number
 * into the EDIT and GetDlgItemInt reads it back; what came back is shown in
 * a message box.
 */
#include <windows.h>
#include <stdio.h>
#include "dialog.h"

static char name[32];
static int value, ended;
static BOOL ok;

BOOL __export CALLBACK DlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemInt(dlg, IDC_NAME, -42, TRUE);
        CheckRadioButton(dlg, IDC_SMALL, IDC_LARGE, IDC_LARGE);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
        case IDCANCEL:
            value = GetDlgItemInt(dlg, IDC_NAME, &ok, TRUE);
            GetDlgItemText(dlg, IDC_NAME, name, sizeof name);
            ended = wp;
            DestroyWindow(dlg);
            PostQuitMessage(0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    FARPROC fp = MakeProcInstance((FARPROC)DlgProc, hInst);
    HWND dlg = CreateDialog(hInst, "W16DLG", NULL, (DLGPROC)fp);
    MSG m;
    char buf[96];

    if (!dlg)
        return 1;
    ShowWindow(dlg, SW_SHOW);
    while (GetMessage(&m, NULL, 0, 0))
        if (!IsDialogMessage(dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    FreeProcInstance(fp);
    sprintf(buf, "%s: \"%s\" reads %d (%s)", ended == IDOK ? "OK" : "Cancel",
            name, value, ok ? "a number" : "not a number");
    MessageBox(NULL, buf, "WIN16 modeless", MB_OK);
    return 0;
}
