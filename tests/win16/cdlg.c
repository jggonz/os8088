/* cdlg.c - WIN16 fixture (SPEC.md 124.24.4, docs/plans/WIN16-ANY-PLAN.md S19):
 * ChooseFont and ChooseColor, then the font the first chose, made, selected
 * and asked its name.
 *
 * ChooseFont is asked to begin at "Helv", which WIN16 maps to the dialog
 * face, and ChooseColor at pure red. tests/win16cdlg.py presses OK in each
 * and reads the message box at the end: each call's answer, the face and
 * size the font dialog wrote, the colour, and what GetTextFace and
 * GetTextMetrics say once the font is selected into the window's DC.
 */
#include <windows.h>
#include <commdlg.h>
#include <string.h>

static char res[256];

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    static CHOOSEFONT cf;
    static CHOOSECOLOR cc;
    static LOGFONT lf;
    static COLORREF cust[16];
    static TEXTMETRIC tm;
    static char face[LF_FACESIZE];
    BOOL okf, okc;
    HFONT font, old;
    HWND wnd;
    HDC dc;

    lstrcpy(lf.lfFaceName, "Helv");
    cf.lStructSize = sizeof(cf);
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
    okf = ChooseFont(&cf);

    cc.lStructSize = sizeof(cc);
    cc.rgbResult = RGB(255, 0, 0);
    cc.lpCustColors = cust;
    cc.Flags = CC_RGBINIT;
    okc = ChooseColor(&cc);

    font = CreateFontIndirect(&lf);     /* a window's DC: the screen's is
                                         * read, never drawn on */
    wnd = CreateWindow("STATIC", "CDLG", WS_OVERLAPPED, 0, 0, 100, 50,
                       NULL, NULL, inst, NULL);
    dc = GetDC(wnd);
    old = SelectObject(dc, font);
    GetTextFace(dc, sizeof(face), face);
    GetTextMetrics(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(wnd, dc);
    DestroyWindow(wnd);
    DeleteObject(font);

    wsprintf(res, "font %d %s %d; colour %d %06lX; face %s %d",
             okf, (LPSTR)lf.lfFaceName, cf.iPointSize, okc,
             (DWORD)cc.rgbResult, (LPSTR)face, tm.tmHeight);
    MessageBox(NULL, res, "CDLG", MB_OK);
    return 0;
}
