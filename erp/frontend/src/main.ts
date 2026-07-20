// SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
// The instance/integration ERP console — a Vite + TS SPA over the ADR-0022 API.

import "./styles.css";
import treeMark from "../../../img/industrygrow-logo.svg";
import {
  api,
  DOC_TYPES,
  getToken,
  moduleDisplay,
  setToken,
  type Instance,
  type IntegrationRecord,
  type LifecycleDoc,
  type Machine,
  type Meta,
} from "./api";

type View = "overview" | "instances" | "instance" | "integration" | "profiles" | "stock";

const TITLES: Record<View, string> = {
  overview: "Overview",
  instances: "Instances",
  instance: "Instance",
  integration: "Integration",
  profiles: "Deployment profile",
  stock: "SP stock",
};

// Views that read one cabinet at a time show the machine picker; the rest do not.
const MACHINE_SCOPED: View[] = ["overview", "integration", "profiles"];

const state: {
  view: View;
  machines: Machine[];
  machine: string | null;
  instance: string | null;
  meta: Meta | null;
  counts: { instances: number | null; stock: number | null };
} = {
  view: "overview",
  machines: [],
  machine: null,
  instance: null,
  meta: null,
  counts: { instances: null, stock: null },
};

const app = document.getElementById("app")!;
const esc = (s: unknown): string =>
  String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]!);

const $ = <T extends HTMLElement>(id: string): T => document.getElementById(id) as T;
const val = (id: string): string => $<HTMLInputElement>(id).value.trim();
const day = (iso: string | null): string => (iso ? iso.slice(0, 10) : "—");

/** The identifier, broken along its two axes: cyan where it grows, violet what it is. */
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

/** One instance's install/remove trail. Sits closed under a slot row, or open
 *  as a section of its own on the instance page. */
function historyDrawer(id: string, history: IntegrationRecord[], open = false): string {
  const lines = history
    .flatMap((h) => {
      const events = [
        { at: h.installed_at, out: false, text: `installed at ${h.machine_id} · ${h.depth_code}` },
      ];
      if (h.removed_at)
        events.push({
          at: h.removed_at,
          out: true,
          text: `removed from ${h.machine_id} · ${h.depth_code} — ${h.removal_reason ?? "no reason recorded"}`,
        });
      return events;
    })
    .sort((a, b) => String(b.at).localeCompare(String(a.at)))
    .map(
      (e) =>
        `<div class="h-line"><span class="h-dot ${e.out ? "out" : ""}"></span><span class="h-when">${day(e.at)}</span>${esc(e.text)}</div>`,
    )
    .join("");
  return `<div class="hist${open ? " bare" : ""}"><div class="h-in">${
    open ? "" : `<span class="h-cap">Integration history — ${esc(id)}</span>`
  }${lines || `<span class="h-line">Never installed. Its history starts at first integration.</span>`}</div></div>`;
}

/** Run an action, reporting the outcome into `outId` rather than blanking the view. */
async function act(outId: string, fn: () => Promise<string>): Promise<void> {
  const out = $(outId);
  out.innerHTML = `<span class="rl">Working…</span>`;
  try {
    out.innerHTML = await fn();
  } catch (err) {
    out.innerHTML = `<span class="rl">Did not go through</span><span class="err">${esc((err as Error).message)}</span>`;
  }
}

// ---- views ----------------------------------------------------------------

async function overview(): Promise<string> {
  const gbox = state.machine;
  if (!gbox)
    return `<div class="empty">No cabinets on record yet. Seed one, then allocate serials to fill it.</div>`;
  const [estate, cals, all, versions] = await Promise.all([
    api.machineIntegration(gbox),
    api.calibrationExpiring(30).catch(() => [] as LifecycleDoc[]),
    api.listInstances().catch(() => [] as Instance[]),
    api.listProfiles(gbox).catch(() => []),
  ]);
  const histories = await Promise.all(
    estate.map((r) => api.instanceHistory(r.instance_id).catch(() => [] as IntegrationRecord[])),
  );
  const active = versions.find((v) => v.active);

  const rows = estate
    .map((r: IntegrationRecord, i: number) => {
      const [name, colour] = moduleDisplay(r.instance_id.split("-")[0]);
      return `<div class="slot-row pick" data-open="${i}"><span class="slot-idx">${String(i + 1).padStart(2, "0")}</span>
        <div class="slot-body"><div class="iid">${segmented(r.machine_id, r.depth_code, r.instance_id)}</div>
        <div class="slot-meta"><span class="leaf" style="background:${colour}"></span><b>${esc(name)}</b><span class="ref">· type → REGISTRY.md</span></div></div>
        <span class="st ok">growing</span></div>
        ${historyDrawer(r.instance_id, histories[i])}`;
    })
    .join("");
  const calRows = cals
    .map((c) => {
      const [cls, label] = calStatus(c.valid_until);
      return `<div class="slot-row two pick" data-instance="${esc(c.instance_full_id)}">
        <div class="slot-body"><div class="iid"><span class="seg id">${esc(c.instance_full_id)}</span></div>
        <div class="slot-meta"><span class="ref">${esc(c.doc_type)} · ${esc(c.object_key)}</span></div></div>
        <span class="st ${cls}">${esc(label)}</span></div>`;
    })
    .join("");
  const inInventory = all.filter((i) => i.status !== "installed").length;

  return `
    <section class="hero">
      <span class="tree"><img src="${treeMark}" alt="The IndustryGrow tree of life"></span>
      <div class="htext">
        <h1><b>The estate, </b><i>alive</i></h1>
        <p class="lede">Every serial, binding, and integration is a leaf on the tree of life — sensor modules in the crown, actuators in the roots, the gateway burning at the trunk. This console is the record of what has <em>grown</em>, and where. The dark is <em>prima materia</em>: the ground the system rises from, not a place to hide.</p>
        <div class="chips">
          <span class="chip"><span class="d" style="background:var(--violet)"></span> meta · MongoDB</span>
          <span class="chip"><span class="d" style="background:var(--cyan)"></span> warehouse · object store</span>
          <span class="chip"><span class="d" style="background:var(--green)"></span> ADR-0021 · system of record</span>
        </div>
      </div>
    </section>

    <section class="tiles">
      <div class="tile"><span class="tl">Instances tracked</span><span class="num">${all.length}</span>
        <div class="glowbar" style="background:linear-gradient(90deg,var(--cyan-d),var(--cyan))"></div>
        <span class="meta">${inInventory} in inventory</span></div>
      <div class="tile"><span class="tl">Growing now</span><span class="num" style="color:var(--green)">${estate.length}</span>
        <div class="glowbar" style="background:linear-gradient(90deg,var(--green-d),var(--green))"></div>
        <span class="meta">installed on ${esc(gbox)}</span></div>
      <div class="tile ${cals.length ? "attn" : ""}"><span class="tl">Calibration due ≤30d</span><span class="num">${cals.length}</span>
        <span class="meta">-CC certificates in the warehouse</span></div>
      <div class="tile"><span class="tl">Profile versions</span><span class="num" style="color:var(--violet)">${versions.length}</span>
        <div class="glowbar" style="background:linear-gradient(90deg,var(--violet-d),var(--violet))"></div>
        <span class="meta">${active ? `${esc(active.version_tag)} active on gateway` : "none recorded active"}</span></div>
    </section>

    <section class="panel">
      <div class="ph"><h2>Integration map</h2>
        <span class="desc">what grows where, now — select a row for its history</span>
        <div class="right legend"><span class="a"><span class="sw pos"></span> position axis</span><span class="a"><span class="sw id"></span> identity axis</span></div></div>
      ${rows || `<div class="empty">This cabinet is bare. Allocate serials, then install them into depths.</div>`}
    </section>

    <section class="cols">
      <div class="panel"><div class="ph"><h2>Calibration &amp; docs</h2><span class="desc">-CC expiring soon → warehouse keys</span></div>
        ${calRows || `<div class="empty">No calibration certificates indexed. Upload a -CC from an instance to start the clock.</div>`}</div>
      <div class="panel"><div class="ph"><h2>Deployment profile</h2><span class="desc">store &amp; record — not a deploy path</span></div>
        <div class="empty">The ERP records which version is active; the gateway <b>pulls</b> it. No deploy button, by design.</div></div>
    </section>
    <div class="foot-note">ADR-0021/0022 · <code>foundation.*</code> [F] → IndustryFlow <code>production_unit</code> at stage 11 · <code>domain.*</code> [D] stays the grow layer</div>`;
}

async function instances(): Promise<string> {
  const list = await api.listInstances();
  state.counts.instances = list.length;
  const rows = list
    .map((it, i) => {
      const [name, colour] = moduleDisplay(it.e_number);
      return `<div class="slot-row pick" data-instance="${esc(it.instance_id)}"><span class="slot-idx">${String(i + 1).padStart(2, "0")}</span>
        <div class="slot-body"><div class="iid"><span class="seg id">${esc(it.e_number)}</span><span class="sepx">·</span><span class="seg id">${esc(it.version)}</span><span class="sepx">·</span><span class="seg id">${esc(it.serial)}</span></div>
        <div class="slot-meta"><span class="leaf" style="background:${colour}"></span><b>${esc(name)}</b></div></div>
        <span class="st ${it.status === "installed" ? "ok" : "muted"}">${esc(it.status)}</span></div>`;
    })
    .join("");
  return `
    <section class="panel"><div class="ph"><h2>Allocate serials</h2>
      <span class="desc">the serial-allocation authority · gap-free per module + version</span></div>
      <div class="form">
        <div class="field"><label>Module</label><input id="al-e" value="E0002" size="7"></div>
        <div class="field"><label>Version</label><input id="al-v" value="020100" size="8"></div>
        <div class="field"><label>Quantity</label><input id="al-q" value="3" size="3"></div>
        <button class="btn" id="al-go">Allocate serials</button>
      </div>
      <div class="result" id="al-out"></div></section>
    <section class="panel"><div class="ph"><h2>Module instances</h2>
      <span class="desc">${list.length} tracked · select one for its documents and provisioning</span></div>
      ${rows || `<div class="empty">Nothing allocated yet. Issue the first serials above.</div>`}</section>`;
}

async function instanceDetail(): Promise<string> {
  const id = state.instance!;
  const [inst, history, documents] = await Promise.all([
    api.getInstance(id),
    api.instanceHistory(id),
    api.instanceDocuments(id),
  ]);
  const provisioned = documents.some((d) => d.doc_type === "PR");
  const [modName, modColour] = moduleDisplay(inst.e_number);
  const current = history.find((h) => !h.removed_at);

  const docRows = documents
    .map((d) => {
      const [cls, label] = d.doc_type === "CC" ? calStatus(d.valid_until) : ["ok", d.status];
      return `<div class="slot-row two"><div class="slot-body">
        <div class="iid"><span class="seg pos">${esc(d.doc_type)}</span><span class="sepx">·</span><span class="seg id">${esc(d.object_key)}</span></div>
        <div class="slot-meta"><span class="ref">warehouse key · valid until ${day(d.valid_until)}</span></div></div>
        <span class="st ${cls}">${esc(label)}</span></div>`;
    })
    .join("");
  const docOpts = DOC_TYPES.map((t) => `<option value="${t}">${t}</option>`).join("");

  return `
    <section class="tiles">
      <div class="tile"><span class="tl">Module</span><span class="num sm">${esc(modName)}</span>
        <div class="glowbar" style="background:${modColour}"></div>
        <span class="meta">serial ${esc(inst.serial)} · version ${esc(inst.version)}</span></div>
      <div class="tile"><span class="tl">Status</span><span class="num sm" style="color:${inst.status === "installed" ? "var(--green)" : "var(--ink-2)"}">${esc(inst.status)}</span>
        <span class="meta">${current ? `at ${esc(current.machine_id)} · ${esc(current.depth_code)}` : "not in a cabinet"}</span></div>
      <div class="tile ${provisioned ? "" : "attn"}"><span class="tl">Provisioning</span><span class="num sm">${provisioned ? "bound" : "unbound"}</span>
        <span class="meta">${provisioned ? "-PR on record" : "no secure-element binding yet"}</span></div>
      <div class="tile"><span class="tl">Documents</span><span class="num">${documents.length}</span>
        <div class="glowbar" style="background:linear-gradient(90deg,var(--violet-d),var(--violet))"></div>
        <span class="meta">indexed in the warehouse</span></div>
    </section>

    <section class="panel"><div class="ph"><h2>Lifecycle documents</h2>
      <span class="desc">blob → warehouse, key → ERP · QP, QR, CP, CC and PR only</span></div>
      <div class="form">
        <div class="field"><label>Type</label><select id="dc-t">${docOpts}</select></div>
        <div class="field"><label>File</label><input type="file" id="dc-f"></div>
        <div class="field"><label>Document date</label><input type="date" id="dc-d"></div>
        <div class="field"><label>Valid until</label><input type="date" id="dc-u"></div>
        <button class="btn" id="dc-go">Upload document</button>
      </div>
      <div class="result" id="dc-out"></div>
      ${docRows || `<div class="empty">Nothing indexed for this instance yet.</div>`}</section>

    <section class="panel"><div class="ph"><h2>Provisioning</h2>
      <span class="desc">serial ↔ ATECC608 binding · public certificate material only</span></div>
      <div class="form">
        <div class="field"><label>Certificate serial</label><input id="pv-s" size="18"></div>
        <div class="field"><label>Public key fingerprint</label><input id="pv-f" size="24"></div>
        <div class="field"><label>Not before</label><input type="date" id="pv-b"></div>
        <div class="field"><label>Not after</label><input type="date" id="pv-a"></div>
        <div class="field"><label>-PR object key</label><input id="pv-k" size="22" value="${esc(id)}-PR"></div>
        <button class="btn" id="pv-go">Bind to element</button>
      </div>
      <div class="result" id="pv-out"></div></section>

    <section class="panel"><div class="ph"><h2>Integration history</h2>
      <span class="desc">the mutable cross-reference · depth is assigned at integration, never carried by the module</span></div>
      ${historyDrawer(id, history, true)}</section>`;
}

async function integration(): Promise<string> {
  const gbox = state.machine;
  if (!gbox) return `<div class="empty">No cabinets on record yet.</div>`;
  const [estate, all] = await Promise.all([api.machineIntegration(gbox), api.listInstances()]);
  const available = all.filter((i: Instance) => i.status !== "installed");
  const histories = await Promise.all(
    estate.map((r) => api.instanceHistory(r.instance_id).catch(() => [] as IntegrationRecord[])),
  );

  const rows = [...estate]
    .sort((a, b) => a.depth_code.localeCompare(b.depth_code))
    .map((r, i) => {
      const [name, colour] = moduleDisplay(r.instance_id.split("-")[0]);
      return `<div class="slot-row pick" data-open="${i}"><span class="slot-idx">${String(i + 1).padStart(2, "0")}</span>
        <div class="slot-body"><div class="iid">${segmented(r.machine_id, r.depth_code, r.instance_id)}</div>
        <div class="slot-meta"><span class="leaf" style="background:${colour}"></span><b>${esc(name)}</b><span class="ref">· growing since ${day(r.installed_at)}</span></div></div>
        <div class="slot-actions"><button class="btn ghost" data-clear="${esc(r.depth_code)}">Remove</button></div></div>
        ${historyDrawer(r.instance_id, histories[i])}`;
    })
    .join("");
  const opts = available
    .map((i) => `<option value="${esc(i.instance_id)}">${esc(i.instance_id)}</option>`)
    .join("");

  return `
    <section class="panel"><div class="ph"><h2>${esc(gbox)}</h2>
      <span class="desc">${estate.length} module${estate.length === 1 ? "" : "s"} growing · select a row for its history</span>
      <div class="right legend"><span class="a"><span class="sw pos"></span> position axis</span><span class="a"><span class="sw id"></span> identity axis</span></div></div>
      ${rows || `<div class="empty">This cabinet is bare. Install a module below to fill its first depth.</div>`}
    </section>

    <section class="panel"><div class="ph"><h2>Install a module</h2>
      <span class="desc">a depth that already holds a module is replaced — the outgoing one keeps its history</span></div>
      <div class="form">
        <div class="field"><label>Depth</label><input id="in-d" value="010100" size="8"></div>
        <div class="field grow"><label>Instance</label><select id="in-i">${opts || `<option value="">nothing in inventory — allocate serials first</option>`}</select></div>
        <button class="btn" id="in-go">Install</button>
      </div>
      <div class="result" id="in-out"></div></section>`;
}

async function profiles(): Promise<string> {
  const gbox = state.machine;
  if (!gbox) return `<div class="empty">No cabinets on record yet.</div>`;
  const versions = await api.listProfiles(gbox);
  const active = versions.find((v) => v.active);
  const latest = versions[0];
  const older = versions
    .slice(1, 5)
    .map((v) => v.version_tag)
    .join(" · ");

  const rows = versions
    .map(
      (p) =>
        `<div class="slot-row two"><div class="slot-body">
        <div class="iid"><span class="seg pos">${esc(p.machine_id)}</span><span class="sepx">·</span><span class="seg id">${esc(p.version_tag)}</span></div>
        <div class="slot-meta"><span class="ref">stored ${day(p.created_at)}</span></div></div>
        ${
          p.active
            ? `<span class="st ok">active on gateway</span>`
            : `<button class="btn ghost" data-activate="${esc(p.version_tag)}">Record as active</button>`
        }</div>`,
    )
    .join("");

  return `
    <section class="panel"><div class="ph"><h2>Where the profile lives</h2>
      <span class="desc">three homes, one direction of travel</span></div>
      <div class="roles">
        <div class="role tpl"><span class="pin"></span>
          <div><div class="r-name">Template</div><div class="r-where">community registry · public · the starting point</div></div>
          <div class="r-ver">—<small>upstream</small></div></div>
        <div class="role erp"><span class="pin"></span>
          <div><div class="r-name">Stored in ERP</div><div class="r-where">deployment-specific · setpoints + model, one artifact</div></div>
          <div class="r-ver">${esc(latest?.version_tag ?? "—")}<small>${esc(older || "no earlier versions")}</small></div></div>
        <div class="role gw"><span class="pin"></span>
          <div><div class="r-name">Active on gateway</div><div class="r-where">active-profile.json · the cabinet runs what it pulled</div></div>
          <div class="r-ver">${esc(active?.version_tag ?? "—")}<small>${active ? "recorded" : "nothing recorded"}</small></div></div>
      </div>
      <div class="note"><b>The ERP does not deploy.</b> Storing a version here makes it available; the gateway pulls it through its single mutation channel. Recording a version as active describes what already happened on the machine.</div>
    </section>

    <section class="panel"><div class="ph"><h2>Store a version</h2>
      <span class="desc">setpoints and model together, as one whole artifact</span></div>
      <div class="form">
        <div class="field"><label>Version tag</label><input id="pf-t" placeholder="v9" size="12"></div>
        <div class="field"><label>Signed hash</label><input id="pf-h" placeholder="optional" size="20"></div>
      </div>
      <div class="form">
        <div class="field grow"><label>Payload</label><textarea id="pf-p" rows="6">{
  "setpoints": {},
  "model": {}
}</textarea></div>
        <button class="btn" id="pf-go">Store version</button>
      </div>
      <div class="result" id="pf-out"></div></section>

    <section class="panel"><div class="ph"><h2>Versions</h2>
      <span class="desc">${versions.length} stored for ${esc(gbox)}</span></div>
      ${rows || `<div class="empty">No versions stored for this cabinet. Store one above to make it available to the gateway.</div>`}
      <div class="result" id="pa-out"></div></section>`;
}

async function stock(): Promise<string> {
  const list = await api.listStock();
  state.counts.stock = list.length;
  const rows = list
    .map((s) => {
      const cls = s.quantity === 0 ? "crit" : s.quantity <= 2 ? "warn" : "ok";
      const label = s.quantity === 0 ? "out" : s.quantity <= 2 ? "low" : "in stock";
      return `<div class="trow"><div><div class="sp-id">${esc(s.sp_number)}</div>
        <div class="sp-spec">${esc(s.location ?? "no location recorded")}</div></div>
        <span class="st ${cls}">${label}</span>
        <div class="qty">${s.quantity}<small> / on hand</small></div></div>`;
    })
    .join("");
  return `
    <section class="panel"><div class="ph"><h2>Set stock</h2>
      <span class="desc">quantity and location only · the SKU and price stay in the BOM</span></div>
      <div class="form">
        <div class="field"><label>SP number</label><input id="sp-n" placeholder="SP0001" size="9"></div>
        <div class="field"><label>Quantity</label><input id="sp-q" value="0" size="5"></div>
        <div class="field grow"><label>Location</label><input id="sp-l" placeholder="shelf, cabinet, machine…"></div>
        <button class="btn" id="sp-go">Set stock</button>
      </div>
      <div class="result" id="sp-out"></div></section>
    <section class="panel stock"><div class="ph"><h2>SP stock</h2>
      <span class="desc">${list.length} part${list.length === 1 ? "" : "s"} tracked</span></div>
      ${rows || `<div class="empty">No stock recorded. Set a quantity above to start tracking a part.</div>`}</section>`;
}

// ---- body render + wiring --------------------------------------------------

const VIEWS: Record<View, () => Promise<string>> = {
  overview,
  instances,
  instance: instanceDetail,
  integration,
  profiles,
  stock,
};

async function renderBody(): Promise<void> {
  const body = $("body");
  body.innerHTML = `<div class="empty">Reading the record…</div>`;
  try {
    body.innerHTML = await VIEWS[state.view]();
  } catch (e) {
    body.innerHTML = `<div class="panel"><div class="empty"><span class="err">The API did not answer: ${esc((e as Error).message)}</span><br>Check the operator token in the sidebar, and that the ERP is running.</div></div>`;
    return;
  }
  wire();
}

function wire(): void {
  // A row with an instance id drills into it; a row with a history drawer opens it.
  document.querySelectorAll<HTMLElement>("[data-instance]").forEach((row) =>
    row.addEventListener("click", () => {
      state.instance = row.dataset.instance!;
      state.view = "instance";
      render();
    }),
  );
  document.querySelectorAll<HTMLElement>("[data-open]").forEach((row) =>
    row.addEventListener("click", (ev) => {
      if ((ev.target as HTMLElement).closest("button")) return; // Remove is not a drill-in
      row.classList.toggle("open");
    }),
  );

  if (state.view === "instances") {
    $("al-go").addEventListener("click", () =>
      act("al-out", async () => {
        const res = await api.allocate(val("al-e"), val("al-v"), parseInt(val("al-q"), 10) || 1);
        const serials = res.serials.map((s) => `<span class="seg id">${esc(s)}</span>`).join("");
        await renderBody();
        return `<span class="rl">Issued — counter now at ${res.next_serial - 1}</span>${serials}`;
      }),
    );
  }

  if (state.view === "instance") {
    $("dc-go").addEventListener("click", () =>
      act("dc-out", async () => {
        const file = $<HTMLInputElement>("dc-f").files?.[0];
        if (!file) throw new Error("choose a file to upload");
        const doc = await api.uploadDocument(state.instance!, {
          doc_type: $<HTMLSelectElement>("dc-t").value,
          file,
          doc_date: val("dc-d") || undefined,
          valid_until: val("dc-u") || undefined,
        });
        await renderBody();
        return `<span class="rl">Uploaded — written to the warehouse, indexed here</span><span class="seg id">${esc(doc.object_key)}</span>`;
      }),
    );
    $("pv-go").addEventListener("click", () =>
      act("pv-out", async () => {
        if (!val("pv-b") || !val("pv-a")) throw new Error("both certificate dates are required");
        await api.bindProvisioning(state.instance!, {
          cert_serial: val("pv-s"),
          public_key_fingerprint: val("pv-f"),
          cert_not_before: new Date(val("pv-b")).toISOString(),
          cert_not_after: new Date(val("pv-a")).toISOString(),
          pr_object_key: val("pv-k"),
        });
        await renderBody();
        return `<span class="rl">Bound</span>${esc(state.instance)} is tied to its secure element.`;
      }),
    );
  }

  if (state.view === "integration") {
    $("in-go").addEventListener("click", () =>
      act("in-out", async () => {
        const instanceId = $<HTMLSelectElement>("in-i").value;
        if (!instanceId) throw new Error("nothing in inventory to install");
        const rec = await api.setPosition(state.machine!, val("in-d"), instanceId);
        await renderBody();
        return `<span class="rl">Installed</span>${segmented(rec.machine_id, rec.depth_code, rec.instance_id)}`;
      }),
    );
    document.querySelectorAll<HTMLElement>("[data-clear]").forEach((btn) =>
      btn.addEventListener("click", (ev) => {
        ev.stopPropagation();
        void act("in-out", async () => {
          const depth = btn.dataset.clear!;
          await api.clearPosition(state.machine!, depth);
          await renderBody();
          return `<span class="rl">Removed</span>Depth ${esc(depth)} is empty; the module keeps its history.`;
        });
      }),
    );
  }

  if (state.view === "profiles") {
    $("pf-go").addEventListener("click", () =>
      act("pf-out", async () => {
        const tag = val("pf-t");
        if (!tag) throw new Error("a version tag is required");
        let payload: unknown;
        try {
          payload = JSON.parse($<HTMLTextAreaElement>("pf-p").value);
        } catch {
          throw new Error("the payload is not valid JSON");
        }
        await api.storeProfile(state.machine!, tag, payload, val("pf-h") || undefined);
        await renderBody();
        return `<span class="rl">Stored</span>${esc(tag)} is available to the gateway. It is not deployed.`;
      }),
    );
    document.querySelectorAll<HTMLElement>("[data-activate]").forEach((btn) =>
      btn.addEventListener("click", () =>
        act("pa-out", async () => {
          const tag = btn.dataset.activate!;
          await api.recordActiveProfile(state.machine!, tag);
          await renderBody();
          return `<span class="rl">Recorded</span>${esc(tag)} is noted as the version running on ${esc(state.machine)}.`;
        }),
      ),
    );
  }

  if (state.view === "stock") {
    $("sp-go").addEventListener("click", () =>
      act("sp-out", async () => {
        const sp = val("sp-n");
        await api.setStock(sp, parseInt(val("sp-q"), 10) || 0, val("sp-l") || null);
        await renderBody();
        return `<span class="rl">Set</span>${esc(sp)} updated.`;
      }),
    );
  }
}

// ---- shell ----------------------------------------------------------------

function render(): void {
  const count = (n: number | null): string => (n === null ? "" : `<span class="n">${n}</span>`);
  const nav = (v: View, label: string, badge = "") =>
    `<button class="${state.view === v ? "active" : ""}" data-view="${v}">${label} ${badge}</button>`;
  const opts = state.machines
    .map((m) => `<option value="${esc(m.machine_id)}" ${m.machine_id === state.machine ? "selected" : ""}>${esc(m.machine_id)}</option>`)
    .join("");
  const scoped = MACHINE_SCOPED.includes(state.view);

  app.innerHTML = `<div class="app">
    <aside class="side">
      <div class="brand">
        <span class="mark"><img src="${treeMark}" alt=""></span>
        <span class="wm"><span class="n"><b>Industry</b><i>Grow</i></span><span class="s">instance · integration</span></span>
      </div>

      <div class="oper">
        <span class="lbl">Operator · single tenant</span>
        <span class="val">${esc(state.meta?.operator_name ?? "not connected")}</span>
        <span class="tag">${state.meta ? `${esc(state.meta.role)} token · ` : ""}stages 1–10 · pre-cloud record</span>
      </div>

      <nav>
        <span class="grp">Estate</span>
        ${nav("overview", "Overview")}
        ${nav("integration", "Integration", count(state.machines.length))}
        ${nav("instances", "Instances", count(state.counts.instances))}
        <span class="grp">Traceability</span>
        ${nav("profiles", "Deployment profile")}
        ${nav("stock", "SP stock", count(state.counts.stock))}
      </nav>

      <div class="foot">
        <div class="kv"><span class="d" style="background:var(--violet);box-shadow:0 0 8px var(--violet)"></span> meta <b>MongoDB</b></div>
        <div class="kv"><span class="d" style="background:var(--cyan);box-shadow:0 0 8px var(--cyan)"></span> warehouse <b>object store</b></div>
        <label for="tok">Operator token</label>
        <input id="tok" value="${esc(getToken())}">
      </div>
    </aside>

    <main>
      <div class="topbar">
        <div class="crumb"><h1>${esc(TITLES[state.view])}</h1>
          <span class="ctx">${esc(state.view === "instance" ? (state.instance ?? "") : scoped ? (state.machine ?? "") : "")}</span></div>
        <span class="spacer"></span>
        ${state.view === "instance" ? `<button class="btn ghost" id="back">← All instances</button>` : ""}
        ${scoped && opts ? `<select id="mach" aria-label="Cabinet">${opts}</select>` : ""}
      </div>
      <div class="body" id="body"></div>
    </main></div>`;

  app.querySelectorAll<HTMLButtonElement>("nav button").forEach((b) =>
    b.addEventListener("click", () => {
      state.view = b.dataset.view as View;
      render();
    }),
  );
  document.getElementById("back")?.addEventListener("click", () => {
    state.view = "instances";
    render();
  });
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
    state.meta = await api.meta();
    state.machines = await api.listMachines();
    state.machine = state.machines[0]?.machine_id ?? null;
    const [insts, sp] = await Promise.all([
      api.listInstances().catch(() => null),
      api.listStock().catch(() => null),
    ]);
    state.counts.instances = insts?.length ?? null;
    state.counts.stock = sp?.length ?? null;
  } catch {
    /* render() already reports the failure in the body */
  }
  render();
}

void boot();
