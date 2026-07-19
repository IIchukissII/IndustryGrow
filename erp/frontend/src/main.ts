// SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
// The instance/integration ERP console — a Vite + TS SPA over the ADR-0022 API.

import "./styles.css";
import {
  api,
  getToken,
  moduleDisplay,
  setToken,
  type IntegrationRecord,
  type LifecycleDoc,
  type Machine,
} from "./api";

type View = "overview" | "instances" | "calibration" | "stock";

const state: { view: View; machines: Machine[]; machine: string | null } = {
  view: "overview",
  machines: [],
  machine: null,
};

const app = document.getElementById("app")!;
const esc = (s: unknown): string =>
  String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]!);

function segmented(machine: string, depth: string, instanceId: string | null): string {
  const pos = `<span class="seg pos">${esc(machine)}</span><span class="sepx">·</span><span class="seg pos">${esc(depth)}</span>`;
  if (!instanceId) return `${pos}<span class="sepx">·</span><span class="seg muted">— empty —</span>`;
  const [e, v, n] = instanceId.split("-");
  return `${pos}<span class="sepx">·</span><span class="seg id">${esc(e)}</span><span class="sepx">·</span><span class="seg id">${esc(v)}</span><span class="sepx">·</span><span class="seg id">${esc(n)}</span>`;
}

function calStatus(validUntil: string | null): [string, string] {
  if (!validUntil) return ["ok", "valid"];
  const days = Math.floor((new Date(validUntil).getTime() - Date.now()) / 86_400_000);
  if (days < 0) return ["crit", "expired"];
  if (days <= 30) return ["warn", `${days} days`];
  return ["ok", `${days} days`];
}

// ---- views ----------------------------------------------------------------

async function overview(): Promise<string> {
  const gbox = state.machine;
  if (!gbox) return `<div class="empty">No machines yet.</div>`;
  const [estate, cals] = await Promise.all([
    api.machineIntegration(gbox),
    api.calibrationExpiring(30).catch(() => [] as LifecycleDoc[]),
  ]);
  const rows = estate
    .map((r: IntegrationRecord, i: number) => {
      const [name, colour] = moduleDisplay(r.instance_id.split("-")[0]);
      return `<div class="row"><span class="idx">${String(i + 1).padStart(2, "0")}</span>
        <div><div class="iid">${segmented(r.machine_id, r.depth_code, r.instance_id)}</div>
        <div class="meta2"><span class="leaf" style="background:${colour}"></span><b>${esc(name)}</b><span class="ref">type → REGISTRY.md</span></div></div>
        <span class="st ok">growing</span></div>`;
    })
    .join("");
  const calRows = cals
    .map((c) => {
      const [cls, label] = calStatus(c.valid_until);
      return `<div class="row" style="grid-template-columns:1fr auto"><div><div class="iid"><span class="seg id">${esc(c.instance_full_id)}</span></div><div class="meta2"><span class="ref">${esc(c.doc_type)} · ${esc(c.object_key)}</span></div></div><span class="st ${cls}">${esc(label)}</span></div>`;
    })
    .join("");
  return `
    <section class="tiles">
      <div class="tile"><span class="tl">Installed now</span><span class="num" style="color:var(--green)">${estate.length}</span><span class="meta">on ${esc(gbox)}</span></div>
      <div class="tile"><span class="tl">Calibration ≤30d</span><span class="num" style="color:var(--amber)">${cals.length}</span><span class="meta">-CC certificates</span></div>
      <div class="tile"><span class="tl">Machine</span><span class="num" style="font-size:19px">${esc(gbox)}</span><span class="meta">grow cabinet</span></div>
      <div class="tile"><span class="tl">Store</span><span class="num" style="font-size:19px">MongoDB</span><span class="meta">+ object-store warehouse</span></div>
    </section>
    <section class="panel"><div class="ph"><h2>Integration map</h2><span class="desc">what grows where, now</span>
      <div class="right"><span>◧ position axis</span><span style="color:var(--violet)">◧ identity axis</span></div></div>
      ${rows || `<div class="empty">Nothing installed. Allocate serials, then install into positions.</div>`}</section>
    <section class="cols">
      <div class="panel"><div class="ph"><h2>Calibration &amp; docs</h2><span class="desc">-CC expiring soon → warehouse keys</span></div>
        ${calRows || `<div class="empty">No calibration certificates indexed.</div>`}</div>
      <div class="panel"><div class="ph"><h2>Deployment profile</h2><span class="desc">store &amp; record — not a deploy path</span></div>
        <div class="empty">The ERP records which version is active; the gateway <b>pulls</b> it. No deploy button, by design.</div></div>
    </section>
    <div class="foot-note">ADR-0021/0022 · <code>foundation.*</code> [F] → IndustryFlow <code>production_unit</code> at stage 11 · <code>domain.*</code> [D] stays the grow layer</div>`;
}

async function instances(): Promise<string> {
  const list = await api.listInstances();
  const rows = list
    .map((it) => {
      const [name, colour] = moduleDisplay(it.e_number);
      const cls = it.status === "installed" ? "ok" : "muted";
      return `<div class="row" style="grid-template-columns:1fr auto"><div><div class="iid"><span class="seg id">${esc(it.instance_id)}</span></div>
        <div class="meta2"><span class="leaf" style="background:${colour}"></span><b>${esc(name)}</b></div></div>
        <span class="st ${cls}">${esc(it.status)}</span></div>`;
    })
    .join("");
  return `
    <section class="panel"><div class="ph"><h2>Allocate serials</h2><span class="desc">the ERP is the serial authority — gap-free per module + version</span></div>
      <div class="form">
        <div class="field"><label>Module</label><input id="al-e" value="E0002" size="7"></div>
        <div class="field"><label>Version</label><input id="al-v" value="020100" size="8"></div>
        <div class="field"><label>Qty</label><input id="al-q" value="3" size="3"></div>
        <button class="btn" id="al-go">Allocate</button>
      </div>
      <div class="result" id="al-out"></div></section>
    <section class="panel"><div class="ph"><h2>Module instances</h2><span class="desc">${list.length} tracked</span></div>
      ${rows || `<div class="empty">No instances yet.</div>`}</section>`;
}

async function renderBody(): Promise<void> {
  const body = document.getElementById("body")!;
  body.innerHTML = `<div class="empty">loading…</div>`;
  try {
    body.innerHTML = state.view === "instances" ? await instances() : await overview();
  } catch (e) {
    body.innerHTML = `<div class="err">API error: ${esc((e as Error).message)} — check the operator token (sidebar) and that the API is running.</div>`;
    return;
  }
  if (state.view === "instances") wireAllocate();
}

function wireAllocate(): void {
  const go = document.getElementById("al-go");
  go?.addEventListener("click", async () => {
    const e = (document.getElementById("al-e") as HTMLInputElement).value.trim();
    const v = (document.getElementById("al-v") as HTMLInputElement).value.trim();
    const q = parseInt((document.getElementById("al-q") as HTMLInputElement).value, 10) || 1;
    const out = document.getElementById("al-out")!;
    try {
      const res = await api.allocate(e, v, q);
      out.innerHTML = res.serials.map((s) => `<span class="seg id">${esc(s)}</span>`).join("");
      await renderBody();
    } catch (err) {
      out.innerHTML = `<span class="err">${esc((err as Error).message)}</span>`;
    }
  });
}

// ---- shell ----------------------------------------------------------------

function render(): void {
  const nav = (v: View, label: string) =>
    `<button class="${state.view === v ? "active" : ""}" data-view="${v}">${label}</button>`;
  const opts = state.machines
    .map((m) => `<option value="${esc(m.machine_id)}" ${m.machine_id === state.machine ? "selected" : ""}>${esc(m.machine_id)}</option>`)
    .join("");
  app.innerHTML = `<div class="app">
    <aside class="side">
      <div class="brand"><span class="dot"></span><span><span class="n"><b>Industry</b><i>Grow</i></span><span class="s">instance · integration</span></span></div>
      <nav>${nav("overview", "Overview")}${nav("instances", "Instances")}</nav>
      <div class="foot"><label class="s" style="text-transform:uppercase;letter-spacing:.12em">operator token</label>
        <input id="tok" value="${esc(getToken())}"><span>meta · MongoDB &nbsp; warehouse · object store</span></div>
    </aside>
    <main>
      <div class="topbar"><h1>${state.view === "instances" ? "Instances" : "Overview"}</h1>
        <span class="ctx">${esc(state.machine ?? "")}</span>
        <select id="mach">${opts}</select></div>
      <div class="body" id="body"></div>
    </main></div>`;

  app.querySelectorAll<HTMLButtonElement>("nav button").forEach((b) =>
    b.addEventListener("click", () => {
      state.view = b.dataset.view as View;
      render();
    }),
  );
  document.getElementById("mach")?.addEventListener("change", (e) => {
    state.machine = (e.target as HTMLSelectElement).value;
    void renderBody();
  });
  document.getElementById("tok")?.addEventListener("change", (e) => {
    setToken((e.target as HTMLInputElement).value.trim());
    void renderBody();
  });
  void renderBody();
}

async function boot(): Promise<void> {
  render();
  try {
    state.machines = await api.listMachines();
    state.machine = state.machines[0]?.machine_id ?? null;
  } catch {
    /* render() already shows an error in the body */
  }
  render();
}

void boot();
