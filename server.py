#!/usr/bin/env python3
"""
dani-meglepi-server
"""

import asyncio
import json
import subprocess
import threading
import tkinter as tk
from tkinter import ttk, messagebox
from datetime import datetime, timedelta
from typing import Optional
import sys

# ── Függőség-ellenőrzés ──────────────────────────────────────────────────────
try:
    import uvicorn
    from fastapi import FastAPI, WebSocket, WebSocketDisconnect
    from fastapi.responses import JSONResponse
except ImportError:
    print("Hiányzó csomagok! Futtasd: pip install fastapi uvicorn")
    sys.exit(1)

# ── Konfiguráció ─────────────────────────────────────────────────────────────
NGROK_AUTHTOKEN = "3CrfFkoxfbzOqvibRPgphslSE5t_6PMdjjxy75gX3Guqi5EYw"
NGROK_DOMAIN    = "turbulent-renter-liking.ngrok-free.app"
SERVER_HOST     = "0.0.0.0"
SERVER_PORT     = 8765

# ── WebSocket kapcsolatkezelő ────────────────────────────────────────────────
class ConnectionManager:
    def __init__(self):
        self.active: dict[str, WebSocket] = {}
        self.lock = asyncio.Lock()
        self.current_state = "idle"
        self.on_change: Optional[callable] = None

    async def connect(self, ws: WebSocket, client_id: str):
        await ws.accept()
        async with self.lock:
            self.active[client_id] = ws
        self._notify()
        if self.current_state != "idle":
            try:
                await ws.send_text(json.dumps({"cmd": self.current_state}))
            except Exception:
                pass

    async def disconnect(self, client_id: str):
        async with self.lock:
            self.active.pop(client_id, None)
        self._notify()

    async def broadcast(self, cmd: str):
        self.current_state = cmd if cmd != "clear" else "idle"
        msg = json.dumps({"cmd": cmd})
        dead = []
        async with self.lock:
            for cid, ws in self.active.items():
                try:
                    await ws.send_text(msg)
                except Exception:
                    dead.append(cid)
            for cid in dead:
                self.active.pop(cid, None)
        self._notify()

    def client_count(self) -> int:
        return len(self.active)

    def _notify(self):
        if self.on_change:
            self.on_change()

manager = ConnectionManager()

# ── FastAPI alkalmazás ───────────────────────────────────────────────────────
app = FastAPI(title="dani-meglepi-server")

@app.websocket("/ws/{client_id}")
async def websocket_endpoint(ws: WebSocket, client_id: str):
    await manager.connect(ws, client_id)
    try:
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        await manager.disconnect(client_id)

@app.get("/status")
async def status():
    return JSONResponse({"clients": manager.client_count(),
                         "state": manager.current_state})

# ── Broadcast segédfüggvény ──────────────────────────────────────────────────
def broadcast_sync(cmd: str):
    if _loop:
        asyncio.run_coroutine_threadsafe(manager.broadcast(cmd), _loop)

# ── Ngrok indítása ───────────────────────────────────────────────────────────
def start_ngrok() -> Optional[subprocess.Popen]:
    try:
        proc = subprocess.Popen(
            ["ngrok", "http",
             f"--authtoken={NGROK_AUTHTOKEN}",
             f"--domain={NGROK_DOMAIN}",
             str(SERVER_PORT)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return proc
    except FileNotFoundError:
        return None

# ── Uvicorn szerver szálban ──────────────────────────────────────────────────
_loop: Optional[asyncio.AbstractEventLoop] = None

def run_server():
    global _loop
    _loop = asyncio.new_event_loop()
    asyncio.set_event_loop(_loop)
    config = uvicorn.Config(app, host=SERVER_HOST, port=SERVER_PORT,
                            loop="none", log_level="warning")
    server = uvicorn.Server(config)
    _loop.run_until_complete(server.serve())

# ── Tkinter GUI ──────────────────────────────────────────────────────────────
class App(tk.Tk):
    PAD = dict(padx=12, pady=6)

    def __init__(self, ngrok_proc: Optional[subprocess.Popen]):
        super().__init__()
        self.ngrok_proc = ngrok_proc
        self.title("dani-meglepi vezérlő")
        self.resizable(False, False)
        self._schedule_job: Optional[str] = None
        self._schedule_target: Optional[datetime] = None
        manager.on_change = self._refresh_status
        self._build_ui()
        self._refresh_status()
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self):
        # Fejléc
        hdr = tk.Frame(self, bg="#1a1a2e")
        hdr.pack(fill=tk.X)
        tk.Label(hdr, text="dani-meglepi szerver", font=("Helvetica", 15, "bold"),
                 fg="white", bg="#1a1a2e", pady=10).pack()

        # Allapot
        status_frame = tk.LabelFrame(self, text="Allapot", **self.PAD)
        status_frame.pack(fill=tk.X, padx=14, pady=(10, 4))

        self._lbl_clients = tk.Label(status_frame, text="Kliensek: -",
                                     font=("Helvetica", 11))
        self._lbl_clients.grid(row=0, column=0, sticky="w", **self.PAD)

        self._lbl_state = tk.Label(status_frame, text="Allapot: tetlen",
                                   font=("Helvetica", 11))
        self._lbl_state.grid(row=0, column=1, sticky="w", **self.PAD)

        ngrok_text = "Ngrok: fut" if self.ngrok_proc else "Ngrok: nem talalta (lasd konzol)"
        self._lbl_ngrok = tk.Label(status_frame, text=ngrok_text, font=("Helvetica", 11))
        self._lbl_ngrok.grid(row=1, column=0, columnspan=2, sticky="w", **self.PAD)

        self._lbl_url = tk.Label(status_frame,
                                 text=f"URL: wss://{NGROK_DOMAIN}/ws/<id>",
                                 font=("Courier", 9), fg="gray")
        self._lbl_url.grid(row=2, column=0, columnspan=2, sticky="w", **self.PAD)

        # Vezerlok
        ctrl = tk.LabelFrame(self, text="Vezérles", **self.PAD)
        ctrl.pack(fill=tk.X, padx=14, pady=4)

        btn_cfg = dict(font=("Helvetica", 12, "bold"), width=16, height=2,
                       relief=tk.FLAT, cursor="hand2", bd=0)

        tk.Button(ctrl, text="Musor inditasa", bg="#27ae60", fg="white",
                  activebackground="#2ecc71",
                  command=self._cmd_show, **btn_cfg).grid(row=0, column=0, padx=10, pady=8)

        tk.Button(ctrl, text="Demo mod", bg="#e67e22", fg="white",
                  activebackground="#f39c12",
                  command=self._cmd_demo, **btn_cfg).grid(row=0, column=1, padx=10, pady=8)

        tk.Button(ctrl, text="Leallitas", bg="#c0392b", fg="white",
                  activebackground="#e74c3c",
                  command=self._cmd_clear, **btn_cfg).grid(row=0, column=2, padx=10, pady=8)

        # Idozito
        sched = tk.LabelFrame(self, text="Idozito (musor automatikus inditas)", **self.PAD)
        sched.pack(fill=tk.X, padx=14, pady=4)

        tk.Label(sched, text="Inditas (ora:perc):").grid(row=0, column=0, sticky="w", **self.PAD)

        time_frame = tk.Frame(sched)
        time_frame.grid(row=0, column=1, sticky="w", **self.PAD)

        self._hour_var = tk.StringVar(value=datetime.now().strftime("%H"))
        self._min_var  = tk.StringVar(value="00")

        vcmd = (self.register(self._validate_time_part), "%P")
        tk.Spinbox(time_frame, from_=0, to=23, width=4, format="%02.0f",
                   textvariable=self._hour_var, validate="key",
                   validatecommand=vcmd).pack(side=tk.LEFT)
        tk.Label(time_frame, text=":").pack(side=tk.LEFT)
        tk.Spinbox(time_frame, from_=0, to=59, width=4, format="%02.0f",
                   textvariable=self._min_var, validate="key",
                   validatecommand=vcmd).pack(side=tk.LEFT)

        btn_frame = tk.Frame(sched)
        btn_frame.grid(row=1, column=0, columnspan=2, pady=(0, 6))

        self._btn_sched = tk.Button(btn_frame, text="Idozito beallitasa",
                                    command=self._set_schedule,
                                    font=("Helvetica", 10), relief=tk.FLAT,
                                    bg="#2980b9", fg="white", cursor="hand2",
                                    padx=10, pady=4)
        self._btn_sched.pack(side=tk.LEFT, padx=6)

        self._btn_cancel_sched = tk.Button(btn_frame, text="Megszakitas",
                                           command=self._cancel_schedule,
                                           font=("Helvetica", 10), relief=tk.FLAT,
                                           bg="#7f8c8d", fg="white", cursor="hand2",
                                           padx=10, pady=4, state=tk.DISABLED)
        self._btn_cancel_sched.pack(side=tk.LEFT, padx=6)

        self._lbl_sched = tk.Label(sched, text="Nincs beallitott idozito.",
                                   font=("Helvetica", 9), fg="gray")
        self._lbl_sched.grid(row=2, column=0, columnspan=2, sticky="w", **self.PAD)

        # Kliensek lista
        lst_frame = tk.LabelFrame(self, text="Csatlakozott kliensek", **self.PAD)
        lst_frame.pack(fill=tk.BOTH, expand=True, padx=14, pady=4)

        self._listbox = tk.Listbox(lst_frame, height=8, font=("Courier", 9),
                                   selectmode=tk.NONE, activestyle="none")
        scrollbar = ttk.Scrollbar(lst_frame, orient=tk.VERTICAL,
                                  command=self._listbox.yview)
        self._listbox.config(yscrollcommand=scrollbar.set)
        self._listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        # Naplo
        log_frame = tk.LabelFrame(self, text="Naplo", **self.PAD)
        log_frame.pack(fill=tk.BOTH, expand=True, padx=14, pady=(4, 12))

        self._log = tk.Text(log_frame, height=5, state=tk.DISABLED,
                            font=("Courier", 8), bg="#f0f0f0")
        log_scroll = ttk.Scrollbar(log_frame, orient=tk.VERTICAL,
                                   command=self._log.yview)
        self._log.config(yscrollcommand=log_scroll.set)
        self._log.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        log_scroll.pack(side=tk.RIGHT, fill=tk.Y)

    def _cmd_show(self):
        broadcast_sync("show")
        self._log_entry("Musor elinditva -> osszes kliens")

    def _cmd_demo(self):
        broadcast_sync("demo")
        self._log_entry("Demo mod elinditva -> osszes kliens")

    def _cmd_clear(self):
        broadcast_sync("clear")
        self._log_entry("Leallitas -> osszes kliens")

    def _validate_time_part(self, value: str) -> bool:
        return value == "" or (value.isdigit() and len(value) <= 2)

    def _set_schedule(self):
        try:
            h = int(self._hour_var.get())
            m = int(self._min_var.get())
        except ValueError:
            messagebox.showerror("Hiba", "Ervenytelen idopont!")
            return

        now = datetime.now()
        target = now.replace(hour=h, minute=m, second=0, microsecond=0)
        if target <= now:
            target += timedelta(days=1)

        self._schedule_target = target
        delta_ms = int((target - now).total_seconds() * 1000)

        if self._schedule_job:
            self.after_cancel(self._schedule_job)

        self._schedule_job = self.after(delta_ms, self._fire_schedule)
        self._btn_cancel_sched.config(state=tk.NORMAL)
        mins_left = int((target - now).total_seconds() // 60)
        self._lbl_sched.config(
            text=f"Idozitve: {target.strftime('%H:%M')} ({mins_left} perc mulva)",
            fg="#2980b9"
        )
        self._log_entry(f"Idozito beallitva: {target.strftime('%H:%M')}")

    def _cancel_schedule(self):
        if self._schedule_job:
            self.after_cancel(self._schedule_job)
            self._schedule_job = None
        self._schedule_target = None
        self._btn_cancel_sched.config(state=tk.DISABLED)
        self._lbl_sched.config(text="Nincs beallitott idozito.", fg="gray")
        self._log_entry("Idozito torolve")

    def _fire_schedule(self):
        self._schedule_job = None
        self._btn_cancel_sched.config(state=tk.DISABLED)
        self._lbl_sched.config(text="Idozito lejart — musor elinditva!", fg="#27ae60")
        self._cmd_show()

    def _refresh_status(self):
        self.after(0, self._do_refresh)

    def _do_refresh(self):
        count = manager.client_count()
        state_map = {"idle": "tetlen", "show": "musor fut", "demo": "demo fut"}
        state_hu = state_map.get(manager.current_state, manager.current_state)
        self._lbl_clients.config(text=f"Kliensek: {count} db")
        self._lbl_state.config(text=f"Allapot: {state_hu}")
        self._listbox.delete(0, tk.END)
        for cid in manager.active:
            self._listbox.insert(tk.END, f"  {cid}")

    def _log_entry(self, msg: str):
        ts = datetime.now().strftime("%H:%M:%S")
        self._log.config(state=tk.NORMAL)
        self._log.insert(tk.END, f"[{ts}] {msg}\n")
        self._log.see(tk.END)
        self._log.config(state=tk.DISABLED)

    def _on_close(self):
        if messagebox.askokcancel("Kilepes", "Biztosan leallitod a szervert?\nMinden kliens lekapcsol."):
            broadcast_sync("clear")
            if self.ngrok_proc:
                self.ngrok_proc.terminate()
            self.after(400, self.destroy)

# ── Belépési pont ────────────────────────────────────────────────────────────
def main():
    print("Ngrok inditasa...")
    ngrok_proc = start_ngrok()
    if ngrok_proc:
        print(f"Ngrok fut (PID {ngrok_proc.pid})")
        print(f"  Cim: wss://{NGROK_DOMAIN}/ws/<kliens-id>")
    else:
        print("FIGYELEM: Az 'ngrok' parancs nem talalhato.")
        print("  Telepites: https://ngrok.com/download")

    server_thread = threading.Thread(target=run_server, daemon=True)
    server_thread.start()
    print(f"WebSocket szerver fut: ws://{SERVER_HOST}:{SERVER_PORT}")

    gui = App(ngrok_proc)
    gui.mainloop()

if __name__ == "__main__":
    main()
