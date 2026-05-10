#!/usr/bin/env node
/**
 * dani-meglepi-server — Cloud Run
 *
 * Egy HTTP szerveren fut minden:
 *   GET  /          → Admin GUI (böngésző)
 *   GET  /ws/*      → Kliens WebSocket kapcsolat
 *   GET  /admin     → Admin WebSocket (GUI real-time frissítés)
 *   GET  /status    → JSON állapot
 *
 * Cloud Run egyetlen portot (PORT env) használ HTTPS/WSS-sel.
 * A TLS-t a Cloud Run infrastructure kezeli — nekünk plain HTTP/WS elég.
 */

"use strict";

const http = require("http");
const { WebSocketServer, WebSocket } = require("ws");

const PORT = parseInt(process.env.PORT || "8080");

// ── Állapot ──────────────────────────────────────────────────────────────────
const clients    = new Map();   // clientId → { ws, connectedAt, lastPing }
const adminSocks = new Set();   // admin GUI WS kapcsolatok
let   state      = "idle";      // idle | show | demo

// ── Segédfüggvények ──────────────────────────────────────────────────────────
function ts() {
  return new Date().toLocaleTimeString("hu-HU", { hour12: false });
}

function log(text) {
  console.log(`[${ts()}] ${text}`);
  broadcastAdmin({ type: "log", data: { text, ts: ts() } });
}

function safeSend(ws, obj) {
  if (ws.readyState === WebSocket.OPEN) {
    try { ws.send(JSON.stringify(obj)); } catch (_) {}
  }
}

function clientList() {
  return Array.from(clients.values()).map(c => ({
    id:       c.id,
    since:    c.connectedAt.toLocaleTimeString("hu-HU", { hour12: false }),
    lastPing: c.lastPing.toLocaleTimeString("hu-HU", { hour12: false }),
  }));
}

function broadcastAdmin(msg) {
  const str = JSON.stringify(msg);
  adminSocks.forEach(ws => {
    if (ws.readyState === WebSocket.OPEN) {
      try { ws.send(str); } catch (_) {}
    }
  });
}

// ── Kliens broadcast ─────────────────────────────────────────────────────────
function broadcast(cmd) {
  state = cmd === "clear" ? "idle" : cmd;
  let sent = 0;
  const dead = [];
  clients.forEach((c, id) => {
    if (c.ws.readyState === WebSocket.OPEN) {
      safeSend(c.ws, { cmd });
      sent++;
    } else {
      dead.push(id);
    }
  });
  dead.forEach(id => clients.delete(id));
  log(`Broadcast: "${cmd}" → ${sent} kliens`);
  broadcastAdmin({ type: "state",   data: { state } });
  broadcastAdmin({ type: "clients", data: clientList() });
}

// ── Admin GUI HTML ────────────────────────────────────────────────────────────
function adminHtml() {
  return `<!DOCTYPE html>
<html lang="hu">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>dani-meglepi</title>
<style>
:root{
  --bg:#0f0f1a;--surface:#1a1a2e;--surface2:#16213e;
  --green:#27ae60;--orange:#e67e22;--red:#c0392b;
  --blue:#2980b9;--text:#e0e0e0;--text2:#888;
  --border:#2a2a4a;--r:10px;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);
     font-family:'Segoe UI',system-ui,sans-serif;
     min-height:100vh;padding:16px}
h1{text-align:center;font-size:1.5rem;margin-bottom:16px;color:#fff}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px;
      max-width:960px;margin:0 auto}
.card{background:var(--surface);border-radius:var(--r);
      padding:16px;border:1px solid var(--border)}
.card h2{font-size:.75rem;text-transform:uppercase;letter-spacing:1px;
         color:var(--text2);margin-bottom:12px}
/* badges */
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.badge{padding:5px 12px;border-radius:20px;font-size:.82rem;font-weight:600}
.badge.idle{background:#2a2a3a;color:var(--text2)}
.badge.show{background:#1a3a2a;color:var(--green)}
.badge.demo{background:#3a2a1a;color:var(--orange)}
.badge.conn{background:#1a2a3a;color:#5dade2}
/* buttons */
.btn-group{display:flex;gap:8px;flex-wrap:wrap}
.btn{padding:11px 18px;border:none;border-radius:8px;
     font-size:.9rem;font-weight:700;cursor:pointer;
     transition:filter .15s;flex:1;min-width:110px}
.btn:hover{filter:brightness(1.15)}
.btn:active{filter:brightness(.9)}
.btn.show {background:var(--green); color:#fff}
.btn.demo {background:var(--orange);color:#fff}
.btn.clear{background:var(--red);   color:#fff}
.btn.sched{background:var(--blue);  color:#fff}
.btn.cancel{background:#555;color:#fff}
/* scheduler */
.sched-row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.sched-row input{background:var(--surface2);border:1px solid var(--border);
  color:var(--text);border-radius:6px;padding:7px 9px;
  width:58px;font-size:.95rem;text-align:center}
.sched-row .btn{flex:0;padding:7px 13px;font-size:.82rem;min-width:0}
#sched-st{margin-top:7px;font-size:.8rem;color:var(--text2)}
/* clients */
#clist{list-style:none}
#clist li{display:grid;grid-template-columns:1fr auto;gap:8px;
  padding:7px 10px;border-radius:6px;background:var(--surface2);
  margin-bottom:5px;font-size:.82rem;border-left:3px solid var(--green)}
#clist .cid{font-family:monospace;color:#5dade2}
#clist .ct{color:var(--text2);font-size:.72rem;align-self:center}
#clist .empty{color:var(--text2);font-style:italic;padding:8px 0}
/* log */
#log{height:200px;overflow-y:auto;font-family:monospace;font-size:.76rem;
     background:var(--surface2);border-radius:6px;padding:9px;
     border:1px solid var(--border)}
#log .e{padding:2px 0;border-bottom:1px solid #1a1a2a}
#log .t{color:var(--text2);margin-right:5px}
/* url */
.url-box{font-family:monospace;font-size:.78rem;color:#5dade2;
         word-break:break-all;margin-top:6px;
         background:var(--surface2);padding:8px;border-radius:6px}
/* dot */
.dot{width:8px;height:8px;border-radius:50%;
     display:inline-block;margin-right:5px;vertical-align:middle}
.dot.g{background:var(--green)}
.dot.r{background:var(--red)}
.dot.o{background:var(--orange);animation:pulse 1s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
/* conn indicator */
#conn-dot{margin-right:4px}
.full{grid-column:1/-1}
@media(max-width:580px){.grid{grid-template-columns:1fr}}
</style>
</head>
<body>
<h1>dani-meglepi vezérlő</h1>
<div class="grid">

<!-- Állapot -->
<div class="card">
  <h2>Állapot</h2>
  <div class="row">
    <span class="badge conn"><span id="ccnt">0</span> kliens</span>
    <span class="badge idle" id="sbadge">tétlen</span>
    <span style="font-size:.82rem">
      <span class="dot o" id="conn-dot"></span>
      <span id="conn-lbl">kapcsolódás...</span>
    </span>
  </div>
  <div class="url-box" id="srv-url">—</div>
</div>

<!-- Vezérlők -->
<div class="card">
  <h2>Vezérlés</h2>
  <div class="btn-group">
    <button class="btn show"  onclick="cmd('show')">▶️ Műsor</button>
    <button class="btn demo"  onclick="cmd('demo')">🧪 Demo</button>
    <button class="btn clear" onclick="cmd('clear')">🛑 Leállítás</button>
  </div>
</div>

<!-- Időzítő -->
<div class="card">
  <h2>Időzítő</h2>
  <div class="sched-row">
    <input type="number" id="sh" min="0" max="23" placeholder="óó">
    <span>:</span>
    <input type="number" id="sm" min="0" max="59" value="00" placeholder="pp">
    <button class="btn sched"  onclick="setSched()">⏰ Beállít</button>
    <button class="btn cancel" onclick="cancelSched()" id="cbtn" disabled>✖</button>
  </div>
  <div id="sched-st">Nincs beállított időzítő.</div>
</div>

<!-- Kliensek -->
<div class="card">
  <h2>Csatlakozott kliensek</h2>
  <ul id="clist"><li class="empty">Nincs kliens.</li></ul>
</div>

<!-- Napló -->
<div class="card full">
  <h2>Napló</h2>
  <div id="log"></div>
</div>

</div><!-- /grid -->
<script>
// Admin WebSocket
const proto = location.protocol === "https:" ? "wss:" : "ws:";
const ws = new WebSocket(proto + "//" + location.host + "/admin");
let schedTimer = null;

ws.onopen = () => {
  setConn(true);
  addLog("Admin kapcsolat: OK");
  // Lekérjük az aktuális szerver URL-t
  document.getElementById("srv-url").textContent =
    proto + "//" + location.host + "/ws/<kliens-id>";
};
ws.onclose = () => {
  setConn(false);
  addLog("Kapcsolat megszakadt — frissítsd az oldalt");
};
ws.onmessage = e => {
  const m = JSON.parse(e.data);
  if (m.type === "init") {
    updateClients(m.data.clients);
    updateState(m.data.state);
  } else if (m.type === "clients") {
    updateClients(m.data);
  } else if (m.type === "state") {
    updateState(m.data.state);
  } else if (m.type === "log") {
    addLog(m.data.text, m.data.ts);
  }
};

function cmd(c) { ws.send(JSON.stringify({ action:"cmd", cmd:c })); }

function setConn(ok) {
  const dot = document.getElementById("conn-dot");
  const lbl = document.getElementById("conn-lbl");
  dot.className = "dot " + (ok ? "g" : "r");
  lbl.textContent = ok ? "admin csatlakozva" : "kapcsolat nélkül";
}

function updateState(s) {
  const b = document.getElementById("sbadge");
  const lbl = {idle:"tétlen", show:"▶ műsor fut", demo:"🧪 demo fut"};
  b.textContent = lbl[s] || s;
  b.className = "badge " + s;
}

function updateClients(list) {
  document.getElementById("ccnt").textContent = list.length;
  const ul = document.getElementById("clist");
  if (!list.length) {
    ul.innerHTML = '<li class="empty">Nincs csatlakozott kliens.</li>';
    return;
  }
  ul.innerHTML = list.map(c =>
    '<li><span class="cid">' + esc(c.id) + '</span>' +
    '<span class="ct">csatl.: ' + esc(c.since) + '</span></li>'
  ).join("");
}

// Időzítő
function setSched() {
  const h = parseInt(document.getElementById("sh").value);
  const m = parseInt(document.getElementById("sm").value);
  if (isNaN(h)||isNaN(m)||h<0||h>23||m<0||m>59) {
    alert("Érvénytelen időpont!"); return;
  }
  const now = new Date();
  const t = new Date();
  t.setHours(h, m, 0, 0);
  if (t <= now) t.setDate(t.getDate() + 1);
  const diff = t - now;
  const mins = Math.floor(diff / 60000);
  if (schedTimer) clearTimeout(schedTimer);
  schedTimer = setTimeout(() => {
    cmd("show");
    document.getElementById("sched-st").textContent = "Lejárt — műsor elindítva!";
    document.getElementById("cbtn").disabled = true;
    schedTimer = null;
  }, diff);
  const p = n => String(n).padStart(2,"0");
  document.getElementById("sched-st").textContent =
    "Időzítve: " + p(h) + ":" + p(m) + " (" + mins + " perc múlva)";
  document.getElementById("cbtn").disabled = false;
}

function cancelSched() {
  if (schedTimer) { clearTimeout(schedTimer); schedTimer = null; }
  document.getElementById("sched-st").textContent = "Nincs beállított időzítő.";
  document.getElementById("cbtn").disabled = true;
}

// Napló
function addLog(text, t) {
  const el = document.getElementById("log");
  const d = document.createElement("div");
  d.className = "e";
  d.innerHTML = '<span class="t">' + esc(t || new Date().toLocaleTimeString("hu-HU",{hour12:false})) + '</span>' + esc(text);
  el.appendChild(d);
  el.scrollTop = el.scrollHeight;
  // Max 200 sor
  while (el.children.length > 200) el.removeChild(el.firstChild);
}

function esc(s) {
  return String(s)
    .replace(/&/g,"&amp;").replace(/</g,"&lt;")
    .replace(/>/g,"&gt;");
}

// Óra alapértelmezés
document.getElementById("sh").value =
  String(new Date().getHours()).padStart(2,"0");
</script>
</body>
</html>`;
}

// ── HTTP szerver ──────────────────────────────────────────────────────────────
const server = http.createServer((req, res) => {
  if (req.url === "/status") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({
      clients: clients.size,
      state,
      uptime: process.uptime(),
    }));
    return;
  }
  // Minden más kérés → admin GUI
  res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
  res.end(adminHtml());
});

// ── WebSocket upgrade kezelés ─────────────────────────────────────────────────
// Egyetlen WS szerver, path alapján szétválasztva
const wss = new WebSocketServer({ noServer: true });

server.on("upgrade", (req, socket, head) => {
  wss.handleUpgrade(req, socket, head, (ws) => {
    if (req.url === "/admin") {
      // Admin GUI kapcsolat
      adminSocks.add(ws);
      ws.send(JSON.stringify({
        type: "init",
        data: { clients: clientList(), state },
      }));
      ws.on("message", (data) => {
        try {
          const msg = JSON.parse(data.toString());
          if (msg.action === "cmd" && msg.cmd) broadcast(msg.cmd);
        } catch (_) {}
      });
      ws.on("close",  () => adminSocks.delete(ws));
      ws.on("error",  () => adminSocks.delete(ws));
      log("Admin GUI csatlakozott");

    } else if (req.url.startsWith("/ws/")) {
      // Kliens kapcsolat: /ws/pc-XXXXXXXX
      const id = req.url.replace("/ws/", "").trim() || "unknown";
      clients.set(id, {
        ws, id,
        connectedAt: new Date(),
        lastPing:    new Date(),
      });
      log(`Kliens csatlakozott: ${id} (összesen: ${clients.size})`);
      broadcastAdmin({ type: "clients", data: clientList() });

      // Ha megy a show/demo, az új kliens is kapja
      if (state !== "idle") safeSend(ws, { cmd: state });

      ws.on("message", (data) => {
        try {
          const msg = JSON.parse(data.toString());
          if (msg.ping && clients.has(id)) {
            clients.get(id).lastPing = new Date();
          }
        } catch (_) {}
      });

      ws.on("close", () => {
        clients.delete(id);
        log(`Kliens lecsatlakozott: ${id} (összesen: ${clients.size})`);
        broadcastAdmin({ type: "clients", data: clientList() });
      });

      ws.on("error", () => clients.delete(id));

    } else {
      socket.destroy();
    }
  });
});

// ── Indítás ───────────────────────────────────────────────────────────────────
server.listen(PORT, () => {
  log(`dani-meglepi szerver fut: port ${PORT}`);
  log(`Admin GUI: http://localhost:${PORT}`);
  log(`Kliens WS: ws://localhost:${PORT}/ws/<id>`);
});

// ── Graceful shutdown ─────────────────────────────────────────────────────────
process.on("SIGTERM", () => {
  log("SIGTERM — leállítás...");
  broadcast("clear");
  server.close(() => process.exit(0));
});

process.on("SIGINT", () => {
  broadcast("clear");
  process.exit(0);
});
