#!/usr/bin/env python3
"""
dani-meglepi-server
"""

import asyncio
import json
import threading
import tkinter as tk
from tkinter import ttk, messagebox
from datetime import datetime, timedelta
from typing import Optional, Set
import sys
import weakref

try:
    import websockets
    from websockets.server import WebSocketServerProtocol
except ImportError:
    print("Hiányzó csomag: pip install websockets")
    sys.exit(1)

try:
    import ngrok as ngrok_sdk
except ImportError:
    print("Hiányzó csomag: pip install ngrok")
    sys.exit(1)

# ── Konfiguráció ─────────────────────────────────────────────────────────────
NGROK_AUTHTOKEN = "3CrfFkoxfbzOqvibRPgphslSE5t_6PMdjjxy75gX3Guqi5EYw"
SERVER_HOST     = "localhost"
SERVER_PORT     = 8765

# ── Állapot ──────────────────────────────────────────────────────────────────
g_clients: dict[str, WebSocketServerProtocol] = {}
g_clients_lock = asyncio.Lock()
g_state = "idle"
g_loop: Optional[asyncio.AbstractEventLoop] = None
g_on_change: Optional[callable] = None

# ── WebSocket szerver ─────────────────────────────────────────────────────────
async def ws_handler(ws: WebSocketServerProtocol):
    global g_state

    # Kliens ID kinyerése az útvonalból: /ws/pc-XXXXXXXX
    path = ws.request.path if hasattr(ws, 'request') else getattr(ws, 'path', '/ws/unknown')
    client_id = path.strip('/').split('/')[-1] or "unknown"

    # Regisztráció
    async with g_clients_lock:
        g_clients[client_id] = ws
    _notify()

    # Ha folyamatban van show/demo, az új kliens is kapja
    if g_state != "idle":
        try:
            await ws.send(json.dumps({"cmd": g_state}))
        except Exception:
            pass

    try:
        async for msg in ws:
            pass  # heartbeat ping-eket fogadjuk, de nem csinálunk semmit
    except Exception:
        pass
    finally:
        async with g_clients_lock:
            g_clients.pop(client_id, None)
        _notify()

def _notify():
    if g_on_change:
        g_on_change()

async def broadcast(cmd: str):
    global g_state
    g_state = cmd if cmd != "clear" else "idle"
    msg = json.dumps({"cmd": cmd})
    dead = []
    async with g_clients_lock:
        clients = dict(g_clients)
    for cid, ws in clients.items():
        try:
            await ws.send(msg)
        except Exception:
            dead.append(cid)
    async with g_clients_lock:
        for cid in dead:
            g_clients.pop(cid, None)
    _notify()

def broadcast_sync(cmd: str):
    if g_loop:
        asyncio.run_coroutine_threadsafe(broadcast(cmd), g_loop)

# ── Szerver szál ──────────────────────────────────────────────────────────────
def run_server():
    global g_loop
    g_loop = asyncio.new_event_loop()
    asyncio.set_event_loop(g_loop)

    async def main():
        async with websockets.serve(
            ws_handler,
            SERVER_HOST,
            SERVER_PORT,
            # Minden path-t elfogadunk (/ws/pc-XXXXXXXX)
            process_request=None,
        ) as server:
            await asyncio.Future()  # fut örökké

    g_loop.run_until_complete(main())

# ── Ngrok ─────────────────────────────────────────────────────────────────────
_ngrok_listener = None

def wait_for_port(host, port, timeout=15.0):
    import socket, time
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket()
            s.settimeout(1)
            s.connect((host, port))
            s.close()
            return True
        except OSError:
            time.sleep(0.2)
    return False

def start_ngrok(log_fn) -> Optional[str]:
    global _ngrok_listener
    try:
        log_fn("Uvicorn portra var (max 15s)...")
        if not wait_for_port(SERVER_HOST, SERVER_PORT):
            log_fn("HIBA: A szerver nem indult el!")
            return None
        log_fn(f"Port {SERVER_PORT}: nyitva.")

        log_fn("Ngrok authtoken beallitasa...")
        ngrok_sdk.set_auth_token(NGROK_AUTHTOKEN)

        log_fn("Ngrok tunnel inditasa...")
        _ngrok_listener = ngrok_sdk.forward(f"{SERVER_HOST}:{SERVER_PORT}")
        url = _ngrok_listener.url()
        log_fn(f"Ngrok tunnel aktiv: {url}")
        return url
    except Exception as e:
        log_fn(f"Ngrok HIBA: {e}")
        return None

# ── Tkinter GUI ───────────────────────────────────────────────────────────────
class App(tk.Tk):
    PAD = dict(padx=12, pady=6)

    def __init__(self):
        super().__init__()
        self.title("dani-meglepi vezérlo")
        self.resizable(False, False)
        self._schedule_job: Optional[str] = None

        global g_on_change
        g_on_change = self._refresh_status

        self._build_ui()
        self._refresh_status()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(200, self._start_services)

    def _start_services(self):
        threading.Thread(target=run_server, daemon=True).start()
        self._log(f"WebSocket szerver indul: ws://{SERVER_HOST}:{SERVER_PORT}")
        self.after(500, self._start_ngrok_thread)

    def _start_ngrok_thread(self):
        self._log("Ngrok inditasa...")
        def do():
            url = start_ngrok(lambda m: self.after(0, lambda msg=m: self._log(msg)))
            def update():
                if url:
                    ws_url = url.replace("https://","wss://").replace("http://","ws://")
                    self._lbl_ngrok.config(text="Ngrok: fut", fg="green")
                    self._lbl_url.config(text=f"URL: {ws_url}/ws/<id>")
                else:
                    self._lbl_ngrok.config(text="Ngrok: HIBA!", fg="red")
            self.after(0, update)
        threading.Thread(target=do, daemon=True).start()

    def _build_ui(self):
        hdr = tk.Frame(self, bg="#1a1a2e")
        hdr.pack(fill=tk.X)
        tk.Label(hdr, text="dani-meglepi szerver",
                 font=("Helvetica", 15, "bold"),
                 fg="white", bg="#1a1a2e", pady=10).pack()

        sf = tk.LabelFrame(self, text="Allapot", **self.PAD)
        sf.pack(fill=tk.X, padx=14, pady=(10,4))

        self._lbl_clients = tk.Label(sf, text="Kliensek: -",
                                     font=("Helvetica", 11))
        self._lbl_clients.grid(row=0, column=0, sticky="w", **self.PAD)

        self._lbl_state = tk.Label(sf, text="Allapot: tetlen",
                                   font=("Helvetica", 11))
        self._lbl_state.grid(row=0, column=1, sticky="w", **self.PAD)

        self._lbl_ngrok = tk.Label(sf, text="Ngrok: indul...",
                                   font=("Helvetica", 11), fg="orange")
        self._lbl_ngrok.grid(row=1, column=0, columnspan=2, sticky="w", **self.PAD)

        self._lbl_url = tk.Label(sf, text="URL: -",
                                 font=("Courier", 9), fg="gray")
        self._lbl_url.grid(row=2, column=0, columnspan=2, sticky="w", **self.PAD)

        ctrl = tk.LabelFrame(self, text="Vezérles", **self.PAD)
        ctrl.pack(fill=tk.X, padx=14, pady=4)

        btn = dict(font=("Helvetica", 12, "bold"), width=16, height=2,
                   relief=tk.FLAT, cursor="hand2", bd=0)
        tk.Button(ctrl, text="Musor inditasa",
                  bg="#27ae60", fg="white", activebackground="#2ecc71",
                  command=self._cmd_show, **btn).grid(row=0, column=0, padx=10, pady=8)
        tk.Button(ctrl, text="Demo mod",
                  bg="#e67e22", fg="white", activebackground="#f39c12",
                  command=self._cmd_demo, **btn).grid(row=0, column=1, padx=10, pady=8)
        tk.Button(ctrl, text="Leallitas",
                  bg="#c0392b", fg="white", activebackground="#e74c3c",
                  command=self._cmd_clear, **btn).grid(row=0, column=2, padx=10, pady=8)

        sched = tk.LabelFrame(self, text="Idozito", **self.PAD)
        sched.pack(fill=tk.X, padx=14, pady=4)
        tk.Label(sched, text="Inditas (ora:perc):").grid(
            row=0, column=0, sticky="w", **self.PAD)
        tf = tk.Frame(sched)
        tf.grid(row=0, column=1, sticky="w", **self.PAD)
        self._hour_var = tk.StringVar(value=datetime.now().strftime("%H"))
        self._min_var  = tk.StringVar(value="00")
        vcmd = (self.register(
            lambda v: v=="" or (v.isdigit() and len(v)<=2)), "%P")
        tk.Spinbox(tf, from_=0, to=23, width=4, format="%02.0f",
                   textvariable=self._hour_var,
                   validate="key", validatecommand=vcmd).pack(side=tk.LEFT)
        tk.Label(tf, text=":").pack(side=tk.LEFT)
        tk.Spinbox(tf, from_=0, to=59, width=4, format="%02.0f",
                   textvariable=self._min_var,
                   validate="key", validatecommand=vcmd).pack(side=tk.LEFT)
        bf = tk.Frame(sched)
        bf.grid(row=1, column=0, columnspan=2, pady=(0,6))
        tk.Button(bf, text="Idozito beallitasa",
                  command=self._set_schedule,
                  font=("Helvetica",10), relief=tk.FLAT,
                  bg="#2980b9", fg="white", cursor="hand2",
                  padx=10, pady=4).pack(side=tk.LEFT, padx=6)
        self._btn_cancel = tk.Button(bf, text="Megszakitas",
                  command=self._cancel_schedule,
                  font=("Helvetica",10), relief=tk.FLAT,
                  bg="#7f8c8d", fg="white", cursor="hand2",
                  padx=10, pady=4, state=tk.DISABLED)
        self._btn_cancel.pack(side=tk.LEFT, padx=6)
        self._lbl_sched = tk.Label(sched, text="Nincs beallitott idozito.",
                                   font=("Helvetica",9), fg="gray")
        self._lbl_sched.grid(row=2, column=0, columnspan=2,
                              sticky="w", **self.PAD)

        lf = tk.LabelFrame(self, text="Csatlakozott kliensek", **self.PAD)
        lf.pack(fill=tk.BOTH, expand=True, padx=14, pady=4)
        self._listbox = tk.Listbox(lf, height=6, font=("Courier",9),
                                   selectmode=tk.NONE, activestyle="none")
        sb = ttk.Scrollbar(lf, orient=tk.VERTICAL, command=self._listbox.yview)
        self._listbox.config(yscrollcommand=sb.set)
        self._listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)

        logf = tk.LabelFrame(self, text="Naplo", **self.PAD)
        logf.pack(fill=tk.BOTH, expand=True, padx=14, pady=(4,12))
        self._log_text = tk.Text(logf, height=8, state=tk.DISABLED,
                                 font=("Courier",8), bg="#f0f0f0")
        lsb = ttk.Scrollbar(logf, orient=tk.VERTICAL,
                             command=self._log_text.yview)
        self._log_text.config(yscrollcommand=lsb.set)
        self._log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        lsb.pack(side=tk.RIGHT, fill=tk.Y)

    def _cmd_show(self):
        broadcast_sync("show"); self._log("Musor -> osszes kliens")

    def _cmd_demo(self):
        broadcast_sync("demo"); self._log("Demo -> osszes kliens")

    def _cmd_clear(self):
        broadcast_sync("clear"); self._log("Leallitas -> osszes kliens")

    def _set_schedule(self):
        try:
            h,m = int(self._hour_var.get()), int(self._min_var.get())
        except ValueError:
            messagebox.showerror("Hiba","Ervenytelen idopont!"); return
        now = datetime.now()
        target = now.replace(hour=h, minute=m, second=0, microsecond=0)
        if target <= now: target += timedelta(days=1)
        delta_ms = int((target-now).total_seconds()*1000)
        if self._schedule_job: self.after_cancel(self._schedule_job)
        self._schedule_job = self.after(delta_ms, self._fire_schedule)
        self._btn_cancel.config(state=tk.NORMAL)
        mins = int((target-now).total_seconds()//60)
        self._lbl_sched.config(
            text=f"Idozitve: {target.strftime('%H:%M')} ({mins} perc mulva)",
            fg="#2980b9")
        self._log(f"Idozito: {target.strftime('%H:%M')}")

    def _cancel_schedule(self):
        if self._schedule_job:
            self.after_cancel(self._schedule_job)
            self._schedule_job = None
        self._btn_cancel.config(state=tk.DISABLED)
        self._lbl_sched.config(text="Nincs beallitott idozito.", fg="gray")
        self._log("Idozito torolve")

    def _fire_schedule(self):
        self._schedule_job = None
        self._btn_cancel.config(state=tk.DISABLED)
        self._lbl_sched.config(text="Idozito lejart!", fg="#27ae60")
        self._cmd_show()

    def _refresh_status(self):
        self.after(0, self._do_refresh)

    def _do_refresh(self):
        state_map = {"idle":"tetlen","show":"musor fut","demo":"demo fut"}
        self._lbl_clients.config(text=f"Kliensek: {len(g_clients)} db")
        self._lbl_state.config(
            text=f"Allapot: {state_map.get(g_state,'?')}")
        self._listbox.delete(0, tk.END)
        for cid in g_clients:
            self._listbox.insert(tk.END, f"  {cid}")

    def _log(self, msg: str):
        ts = datetime.now().strftime("%H:%M:%S")
        self._log_text.config(state=tk.NORMAL)
        self._log_text.insert(tk.END, f"[{ts}] {msg}\n")
        self._log_text.see(tk.END)
        self._log_text.config(state=tk.DISABLED)

    def _on_close(self):
        if messagebox.askokcancel("Kilepes","Biztosan leallitod?"):
            broadcast_sync("clear")
            try: ngrok_sdk.kill()
            except Exception: pass
            self.after(400, self.destroy)

if __name__ == "__main__":
    App().mainloop()
