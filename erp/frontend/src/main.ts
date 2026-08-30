// SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
// The instance/integration ERP console — a Vite + TS SPA over the ADR-0022 API.

import DOMPurify from "dompurify";
import { marked } from "marked";

import "./styles.css";
import treeMark from "../../../img/industrygrow-logo.svg";
import {
  api,
  DOC_TYPES,
  getToken,
  moduleDesignation,
  moduleHue,
  partRole,
  rootLabel,
  setCatalog,
  setToken,
  type Instance,
  type IntegrationRecord,
  type LifecycleDoc,
  type Machine,
  type Meta,
  type StoreDoc,
} from "./api";

type View =
  | "overview"
  | "instances"
  | "instance"
  | "integration"
  | "profiles"
  | "firmware"
  | "gateway"
  | "library"
  | "stock";

const TITLES: Record<View, string> = {
  overview: "Overview",
  instances: "Instances",
  instance: "Instance",
  integration: "Integration",
  profiles: "Deployment profile",
  firmware: "Firmware",
  gateway: "Gateway channel",
  library: "Documents",
  stock: "SP stock",
};

// Views that read one cabinet at a time show the machine picker; the rest do not.
const MACHINE_SCOPED: View[] = ["overview", "integration", "profiles", "firmware", "gateway"];

const state: {
  view: View;
  machines: Machine[];
  machine: string | null;
  instance: string | null;
  meta: Meta | null;
  counts: { instances: number | null; stock: number | null };
  /** The document scope, held as what it actually is: a key prefix (ADR-0017 d15). */
  prefix: string;
  /** The same idea on the instance axis. Kept apart from the document scope:
   *  `SP0004` narrows a shelf and means nothing among serials, so carrying one
   *  filter between the two views would only ever empty the second. */
  instancePrefix: string;
} = {
  view: "overview",
  machines: [],
  machine: null,
  instance: null,
  meta: null,
  counts: { instances: null, stock: null },
  prefix: "",
  instancePrefix: "",
};

const app = document.getElementById("app")!;
const esc = (s: unknown): string =>
  String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]!);

const $ = <T extends HTMLElement>(id: string): T => document.getElementById(id) as T;
const val = (id: string): string => $<HTMLInputElement>(id).value.trim();
const day = (iso: string | null): string => (iso ? iso.slice(0, 10) : "—");

/**
 * The integration identifier, shown as the join it is.
 *
 * `GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN` is not a five-part name — it is two
 * orthogonal axes meeting at one mutable cross-reference (ADR-0017's governing
 * invariant). A run of five sibling pills says the opposite: that the parts are
 * peers of one kind. So position and identity are set as two groups with the
 * join marked between them, and each side shows its codes *decoded*.
 *
 * Decoding is what stops the worst confusion in this row: the depth and the
 * version are both six digits and mean entirely unrelated things. `020100` as a
 * position is main 02, and `020100` as a version is v2.1.0 — telling them apart
 * by which slot they sat in was work the reader should never have been doing.
 */
function integrationId(r: IntegrationRecord): string {
  const depth = r.depth_levels
    ? r.depth_levels.map((n) => String(n).padStart(2, "0")).join("·")
    : r.depth_code;
  const pos = `<span class="axis"><span class="seg pos" title="machine">${esc(r.machine_id)}</span
    ><span class="seg pos" title="depth ${esc(r.depth_code)} — main · sub · sub">${esc(depth)}</span></span>`;
  if (!r.e_number)
    return `<span class="iid">${pos}<span class="axjoin"></span><span class="seg muted">— empty —</span></span>`;
  const id = `<span class="axis"><span class="seg id" title="module">${esc(r.e_number)}</span
    ><span class="seg id" title="design version ${esc(r.version ?? "")}">${esc(r.version_label ?? r.version ?? "")}</span
    ><span class="seg id" title="serial">${esc(r.serial ?? "")}</span></span>`;
  // The join is a mark, not a separator: it says these two sides are different
  // kinds of fact, and that the pairing is re-assigned whenever the instance
  // moves (ADR-0017 d7, d13).
  return `<span class="iid">${pos}<span class="axjoin"
    title="assigned at integration; re-assigned whenever the instance moves"></span>${id}</span>`;
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
    return `<div class="empty">No cabinets on record yet. Register one with
      <code>PUT /api/v1/machines/GBOX_0001</code>, then allocate serials to fill it.</div>`;
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
      const e = r.e_number ?? "";
      const name = moduleDesignation(e), colour = moduleHue(e);
      return `<div class="slot-row pick" data-open="${i}"><span class="slot-idx">${String(i + 1).padStart(2, "0")}</span>
        <div class="slot-body">${integrationId(r)}
        <div class="slot-meta"><span class="leaf" style="background:${colour}"></span><b>${esc(name)}</b><span class="key-echo">${esc(`${r.machine_id}-${r.depth_code}-${r.instance_id}`)}</span></div></div>
        <span class="st ok">growing</span></div>
        ${historyDrawer(r.instance_id, histories[i])}`;
    })
    .join("");
  const calRows = cals
    .map((c) => {
      const [cls, label] = calStatus(c.valid_until);
      return `<div class="slot-row two pick" data-instance="${esc(c.instance_full_id)}">
        <div class="slot-body"><div class="iid"><span class="seg id">${esc(c.instance_full_id)}</span></div>
        <div class="slot-meta"><span class="ref">${esc(c.doc_type)} ·</span><button
          class="seg id key sm" data-doc="${esc(c.object_key)}"
          data-doc-instance="${esc(c.instance_full_id)}">${esc(c.object_key)}</button></div></div>
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

/**
 * The instance layer of the identity axis, grouped the way it is allocated.
 *
 * A serial is unique per module *and* version, and the allocator counts on
 * exactly that pair — `counter_id` is literally `Exxxx-VVVVVV` (ADR-0017 d1, d8).
 * So the row is that prefix and the cells are the serials issued against it:
 * the grouping is not a display convenience, it is the shape of the counter.
 *
 * Deliberately not the Documents plate. There the second dimension was the
 * document layer and a grid earned its keep; here every cell would be one
 * status column of mostly-empty width. Status rides on the chip instead, and
 * the form matches the data rather than the last view built.
 */
async function instances(): Promise<string> {
  const all = await api.listInstances();
  state.counts.instances = all.length;

  const prefix = state.instancePrefix;
  const list = prefix ? all.filter((i) => i.instance_id.startsWith(prefix)) : all;

  // module -> version -> the serials issued against that counter
  const byModule = new Map<string, Map<string, Instance[]>>();
  for (const it of list) {
    const versions = byModule.get(it.e_number) ?? new Map<string, Instance[]>();
    versions.set(it.version, [...(versions.get(it.version) ?? []), it]);
    byModule.set(it.e_number, versions);
  }

  // One continuous table, not a card per module. The grouping is load-bearing —
  // a serial only means anything against its module and version — but a bordered
  // card per group would spend more room on chrome than on records, most visibly
  // in exactly the case this catalog starts in: one serial per type.
  const groups = [...byModule.keys()]
    .sort()
    .map((e) => {
      const versions = byModule.get(e)!;
      const count = [...versions.values()].flat().length;
      const head = `<div class="prow mod"><span class="modh">
        <span class="leaf" style="background:${moduleHue(e)}"></span>
        <button class="seg id dh-root" data-instance-prefix="${esc(e)}"
          title="Narrow to ${esc(e)}">${esc(e)}</button>
        <b>${esc(moduleDesignation(e))}</b>
        <span class="ref">${count} serial${count === 1 ? "" : "s"}</span></span></div>`;

      return (
        head +
        [...versions.keys()]
          .sort((a, b) => b.localeCompare(a))
          .map((v) => {
            const here = [...versions.get(v)!].sort((a, b) => a.serial.localeCompare(b.serial));
            const chips = here
              .map(
                (it) =>
                  `<button class="seg id key chip serial${it.status === "installed" ? " live" : ""}"
                    data-instance="${esc(it.instance_id)}"
                    title="${esc(`${it.instance_id} · ${it.status}`)}">-${esc(it.serial)}</button>`,
              )
              .join("");
            return `<div class="prow"><span class="pv"><span class="pv-v">${esc(
              here[0].version_label ?? v,
            )}</span><span class="pv-k">${esc(`${e}-${v}`)}</span></span>
              <span class="pcell">${chips}</span></div>`;
          })
          .join("")
      );
    })
    .join("");

  const chips = [...new Set(all.map((i) => i.e_number))]
    .sort()
    .map(
      (e) =>
        `<button class="rchip${prefix === e ? " on" : ""}" data-instance-prefix="${esc(e)}">${esc(e)}</button>`,
    )
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
      <span class="desc">${all.length} tracked · a row is one counter, its cells the serials
      issued against it — select a serial for its documents and provisioning</span>
      <div class="right pfx"><label for="in-p">prefix</label>
        <input id="in-p" value="${esc(prefix)}" placeholder="E0002-020100" size="17"
          spellcheck="false" autocomplete="off">
        ${prefix ? `<button class="btn ghost sm" id="in-x">Clear</button>` : ""}</div></div>
      <div class="rchips">${chips}
        <span class="statkey"><span class="dot live"></span>growing
          <span class="dot"></span>in inventory</span></div>
      ${
        groups
          ? `<div class="plate flush" style="--cols:1">${groups}</div>`
          : all.length
            ? `<div class="empty">No serial starts with <code>${esc(prefix)}</code>.</div>`
            : `<div class="empty">Nothing allocated yet. Issue the first serials above.</div>`
      }</section>`;
}

async function instanceDetail(): Promise<string> {
  const id = state.instance!;
  const [inst, history, documents] = await Promise.all([
    api.getInstance(id),
    api.instanceHistory(id),
    api.instanceDocuments(id),
  ]);
  const provisioned = documents.some((d) => d.doc_type === "PR");
  const modName = moduleDesignation(inst.e_number), modColour = moduleHue(inst.e_number);
  const current = history.find((h) => !h.removed_at);

  const docRows = documents
    .map((d) => {
      const [cls, label] = d.doc_type === "CC" ? calStatus(d.valid_until) : ["ok", d.status];
      // The identifier IS the object key (ADR-0017 d15), so the key is the
      // control rather than a label beside one: there is no separate "file" to
      // point at. Read as "open this record", not "download an attachment".
      return `<div class="slot-row two"><div class="slot-body">
        <div class="iid"><span class="seg pos">${esc(d.doc_type)}</span><span class="sepx">·</span><button
          class="seg id key" data-doc="${esc(d.object_key)}"
          data-doc-instance="${esc(d.instance_full_id)}"
          title="Open ${esc(d.object_key)}">${esc(d.object_key)}</button></div>
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

    <section class="panel"><div class="ph"><h2>Dossier</h2>
      <span class="desc">identity, provisioning binding, integration history and documents, as one
      printable sheet</span></div>
      <div class="form">
        <button class="btn" id="rp-pdf">Download PDF</button>
        <span class="ref">rendered for monochrome — it is meant to be printed and filed</span>
      </div>
      <div class="result" id="rp-out"></div></section>

    <section class="panel"><div class="ph"><h2>Lifecycle documents</h2>
      <span class="desc">blob → warehouse, key → ERP · QP, QR, CP, CC and PR only</span></div>
      <div class="form">
        <div class="field"><label>Type</label><select id="dc-t">${docOpts}</select></div>
        <div class="field"><label>File</label><input type="file" id="dc-f"></div>
        <button class="btn" id="dc-go">Upload document</button>
      </div>
      <!-- Only a calibration certificate uses these: the date makes its key unique
           so a recalibration cannot overwrite its predecessor, and the expiry is
           what makes "what is due" a query. On any other type they are stored and
           never read, so asking for them invites filling in a field that does
           nothing. -->
      <div class="form" id="dc-cal" hidden>
        <div class="field"><label>Calibration date</label><input type="date" id="dc-d">
          <span class="hint">names the certificate · defaults to today</span></div>
        <div class="field"><label>Valid until</label><input type="date" id="dc-u">
          <span class="hint">what makes it show up as due</span></div>
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

  // Ordered by the position code, which sorts as the machine hierarchy does —
  // main, then sub, then sub — because that is what the six digits are.
  const rows = [...estate]
    .sort((a, b) => a.depth_code.localeCompare(b.depth_code))
    .map((r, i) => {
      const e = r.e_number ?? "";
      const name = moduleDesignation(e), colour = moduleHue(e);
      return `<div class="slot-row pick" data-open="${i}"><span class="slot-idx">${String(i + 1).padStart(2, "0")}</span>
        <div class="slot-body">${integrationId(r)}
        <div class="slot-meta"><span class="leaf" style="background:${colour}"></span><b>${esc(name)}</b><span class="ref">· growing since ${day(r.installed_at)}</span>
        <span class="key-echo">${esc(`${r.machine_id}-${r.depth_code}-${r.instance_id}`)}</span></div></div>
        <div class="slot-actions"><button class="btn ghost" data-clear="${esc(r.depth_code)}">Remove</button></div></div>
        ${historyDrawer(r.instance_id, histories[i])}`;
    })
    .join("");
  const opts = available
    .map((i) => `<option value="${esc(i.instance_id)}">${esc(i.instance_id)}</option>`)
    .join("");

  return `
    <section class="panel"><div class="ph"><h2>${esc(gbox)}</h2>
      <span class="desc">${estate.length} module${estate.length === 1 ? "" : "s"} growing · select a row for its history</span></div>
      ${
        estate.length
          ? `<div class="axhead">
              <span class="axh"><span class="sw pos"></span>where it sits
                <em>machine · depth</em></span>
              <span class="axh"><span class="sw id"></span>what it is
                <em>module · version · serial</em></span>
              <span class="axh-note">the pair is re-assigned whenever a module moves; the serial
                keeps its own history either way</span></div>`
          : ""
      }
      ${rows || `<div class="empty">This cabinet is bare. Install a module below to fill its first depth.</div>`}
    </section>

    <section class="panel"><div class="ph"><h2>Install a module</h2>
      <span class="desc">a depth that already holds a module is replaced — the outgoing one keeps its history</span></div>
      <div class="form">
        <div class="field"><label>Depth</label><input id="in-d" value="010100" size="8">
          <span class="hint">main · sub · sub, two digits each</span></div>
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
      <span class="desc">setpoints and model together, as one whole artifact. Paste the document
      exactly as signed &mdash; the signature covers these bytes, so reformatting breaks it</span></div>
      <div class="form">
        <div class="field"><label>Version tag</label><input id="pf-t" placeholder="v9" size="12"></div>
        <div class="field grow"><label>Signature</label><input id="pf-h"
          placeholder="base64 from sign_profile.py — required before it can be activated" size="44"></div>
      </div>
      <div class="form">
        <div class="field grow"><label>Profile document</label><textarea id="pf-p" rows="6">{
  "machine_id": "GBOX_0001",
  "version_tag": "v9",
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

/**
 * Firmware: which release an operator wants this cabinet's nodes running.
 *
 * The same three-homes shape the profile view uses, because firmware travels the
 * same way — published once for everyone, selected here for this cabinet, pulled
 * and applied by the gateway. The third home is the one that differs: a profile's
 * active version is at least *recorded*, while what a node is running is only
 * ever observed on the bus (ADR-0029 d15) and is excluded from this API by
 * ADR-0022 d9. So the third column here is not a value with a caveat; it is
 * empty, and the panel below says where the answer actually lives.
 *
 * The slot pair is shown on every release rather than hidden as an
 * implementation detail. It is the fact that explains the rest of the view: a
 * release is two images, a node takes the one for the slot it is not running,
 * and that is why the gateway has to identify a node before it can serve it.
 */
async function firmware(): Promise<string> {
  const gbox = state.machine;
  if (!gbox) return `<div class="empty">No cabinets on record yet.</div>`;
  const [releases, intent] = await Promise.all([
    api.listFirmwareReleases(),
    api.firmwareIntent(gbox),
  ]);

  const slots = (keys: string[]): string =>
    keys
      .map((k) => {
        const slot = k.endsWith("-slot-a.img") ? "A" : k.endsWith("-slot-b.img") ? "B" : "?";
        return `<span class="seg pos">slot ${esc(slot)}</span>`;
      })
      .join(`<span class="sepx">·</span>`);

  const rows = releases
    .map((r) => {
      const chosen = intent?.release_root === r.release_root;
      return `<div class="slot-row two"><div class="slot-body">
        <div class="iid"><span class="seg id">${esc(r.release_root)}</span>${
          r.version_label ? `<span class="sepx">·</span><span class="seg">${esc(r.version_label)}</span>` : ""
        }</div>
        <div class="slot-meta">${slots(r.artifact_keys)}<span class="ref">one image per slot</span></div></div>
        ${
          chosen
            ? `<span class="st ok">intended for ${esc(gbox)}</span>`
            : `<button class="btn ghost" data-intend="${esc(r.release_root)}">Record as intended</button>`
        }</div>`;
    })
    .join("");

  return `
    <section class="panel"><div class="ph"><h2>Where firmware comes from</h2>
      <span class="desc">three homes, one direction of travel</span></div>
      <div class="roles">
        <div class="role tpl"><span class="pin"></span>
          <div><div class="r-name">Released</div><div class="r-where">warehouse · signed once · the same artifact for every operator</div></div>
          <div class="r-ver">${esc(releases[0]?.version_label ?? "—")}<small>${releases.length} published</small></div></div>
        <div class="role erp"><span class="pin"></span>
          <div><div class="r-name">Intended here</div><div class="r-where">what an operator chose for this cabinet</div></div>
          <div class="r-ver">${esc(intent?.version_label ?? "—")}<small>${
            intent ? `selected ${day(intent.selected_at)}` : "nothing selected"
          }</small></div></div>
        <div class="role gw"><span class="pin"></span>
          <div><div class="r-name">Running on the nodes</div><div class="r-where">observed on the bus, never recorded here</div></div>
          <div class="r-ver">—<small>ask the gateway</small></div></div>
      </div>
      <div class="note"><b>The ERP does not update anything.</b> Choosing a release records what you
        want; the gateway compares that against what each node reports and performs the transfer over
        the bus. There is no flash button, by design.</div>
    </section>

    <section class="panel"><div class="ph"><h2>Choose a release</h2>
      <span class="desc">${releases.length} published for the carrier every node runs</span></div>
      ${
        rows ||
        `<div class="empty">No firmware releases in the warehouse. Build one with
          <code>firmware/tools/release.sh --key &lt;pem&gt;</code>, then publish it with
          <code>python -m app.store_sync</code>; both slot images must be there before a
          release can be selected.</div>`
      }
      <div class="result" id="fw-out"></div></section>

    <section class="panel"><div class="ph"><h2>Whether the nodes took it</h2>
      <span class="desc">not answerable here, by design</span></div>
      <div class="empty">The ERP records the release you intend, never what a node is running. A node
        reports its running image on the bus, and a version written back here would be a machine
        reporting an operational act, which this API does not accept (ADR-0022 d9).<br><br>
        To see what ${esc(gbox)}'s nodes are actually running, ask the gateway:
        <code>firmware_client.py show</code> for the release it holds, or
        <code>firmware_client.py once --dry-run</code> for what each node needs and why.</div>
    </section>`;
}

/**
 * The gateway channel: what the ERP knows about how a cabinet is reached.
 *
 * The shape of this view is set by what the ERP is allowed to own. It shows the
 * machine's certificate on record and the profile version waiting for it — both
 * ERP-owned configuration — and it states plainly that whether the gateway has
 * actually collected that profile is not knowable here. That gap is deliberate
 * (ADR-0022 d8 rev 1, d9): a pull-confirmation write would be a machine
 * reporting an operational act. Naming the gap is more honest than a green tick
 * that means "we recorded it", which is what an operator would read it as.
 */
async function gateway(): Promise<string> {
  const gbox = state.machine!;
  const ch = await api.gatewayChannel(gbox);
  const id = ch.identity;

  // Bands, not a countdown: ADR-0007 d7 makes leaves short-lived and renewed, so
  // "42 days" is unremarkable and "4 days" is a problem. The threshold is the
  // renewal margin, not an arbitrary week.
  const days = id?.expires_in_days ?? 0;
  const certState = !id
    ? { cls: "warn", label: "not provisioned" }
    : days < 0
      ? { cls: "crit", label: `expired ${Math.abs(days)}d ago` }
      : days <= 14
        ? { cls: "warn", label: `expires in ${days}d` }
        : { cls: "ok", label: `valid, ${days}d left` };

  const identityPanel = id
    ? `<div class="roles">
        <div class="role erp"><span class="pin"></span>
          <div><div class="r-name">Certificate on record</div>
            <div class="r-where">serial ${esc(id.cert_serial)} · until ${day(id.cert_not_after)}</div></div>
          <div class="r-ver"><span class="st ${certState.cls}">${esc(certState.label)}</span></div></div>
        <div class="role tpl"><span class="pin"></span>
          <div><div class="r-name">Hardware anchor</div>
            <div class="r-where">SP0004 vendor serial ${esc(id.vendor_serial)}${
              id.atecc_serial ? ` · ATECC ${esc(id.atecc_serial)}` : ""
            }</div></div>
          <div class="r-ver">—<small>survives renewal</small></div></div>
        <div class="role gw"><span class="pin"></span>
          <div><div class="r-name">Public-key fingerprint</div>
            <div class="r-where"><code>${esc(id.public_key_fingerprint.slice(0, 32))}…</code></div></div>
          <div class="r-ver">—<small>stable identity</small></div></div>
      </div>
      <div class="note">This is the certificate the ERP was <b>told about</b>, at provisioning or at
        the last re-certification. Gateway certificates are short-lived and renewed, so if renewal
        does not re-submit the binding, what is shown here ages out of step with what the cabinet
        actually presents.</div>`
    : `<div class="empty">No certificate recorded for ${esc(gbox)}. Provision the gateway
        (<code>gateway/provision_identity.py</code>), then submit its binding — until then this
        machine cannot authenticate to the ERP at all.</div>`;

  const unsigned =
    ch.unsigned_versions > 0
      ? `<div class="note"><b>${ch.unsigned_versions} stored version${
          ch.unsigned_versions === 1 ? " carries" : "s carry"
        } no signature.</b> Those cannot be recorded active and so can never reach the gateway
        — sign them with <code>signing/sign_profile.py</code>.</div>`
      : "";

  return `
    <section class="panel"><div class="ph"><h2>Identity</h2>
      <span class="desc">how ${esc(gbox)} proves it is itself</span></div>
      ${identityPanel}
    </section>

    <section class="panel"><div class="ph"><h2>What is waiting for it</h2>
      <span class="desc">the ERP end of the channel</span></div>
      <div class="roles">
        <div class="role erp"><span class="pin"></span>
          <div><div class="r-name">Recorded active</div>
            <div class="r-where">${
              ch.active_since ? `since ${day(ch.active_since)}` : "nothing recorded active"
            }</div></div>
          <div class="r-ver">${esc(ch.active_version ?? "—")}<small>${ch.stored_versions} stored</small></div></div>
      </div>
      ${unsigned}
    </section>

    <section class="panel"><div class="ph"><h2>Whether it arrived</h2>
      <span class="desc">not answerable here, by design</span></div>
      <div class="empty">The ERP does not record that a gateway pulled. The pull is a read; a
        confirmation written back would be a machine reporting an operational act, which this API
        does not accept (ADR-0022 d9).<br><br>
        To see whether ${esc(gbox)} is actually collecting its profile, ask the gateway:
        <code>journalctl -u industrygrow-profile-pull</code>, or
        <code>profile_client.py show</code> for what it currently holds.</div>
    </section>`;
}

/**
 * The reader is one `<dialog>` for the whole console, created on first use.
 *
 * A document is something an operator opens *over* their work and closes again,
 * not a panel that grows on the end of the page they were reading: at a cabinet
 * you hold the manual up, you do not scroll past the record to reach it. A modal
 * dialog is also the honest element for it — the top layer, `Escape`, and the
 * inert background come from the platform rather than from a stack of z-indexes.
 */
function readerDialog(): HTMLDialogElement {
  const found = document.querySelector<HTMLDialogElement>("#dc-reader");
  if (found) return found;

  const dlg = document.createElement("dialog");
  dlg.id = "dc-reader";
  dlg.className = "reader";
  // Clicking the backdrop is a click on the dialog itself — the sheet fills it,
  // so any hit that lands on the element and not on a child is outside the sheet.
  dlg.addEventListener("click", (e) => {
    if (e.target === dlg) dlg.close();
  });
  // Drop the document when the sheet closes, however it closed — button, backdrop,
  // or Escape. Nothing depends on this having run: every open rewrites the sheet
  // before showing it, so a stale body can never be displayed. It only keeps a
  // closed reader from holding a manual's worth of DOM.
  dlg.addEventListener("close", () => {
    // Revoke before clearing: an image or PDF body holds an object URL, and
    // dropping the element does not free the blob behind it. A reader used on a
    // few fabrication packages would otherwise retain them for the session.
    dlg.querySelectorAll<HTMLImageElement | HTMLIFrameElement>("img[src^=blob\\:], iframe[src^=blob\\:]")
      .forEach((el) => URL.revokeObjectURL(el.src));
    dlg.innerHTML = "";
  });
  document.body.append(dlg);
  return dlg;
}

/** A name an operating system can open. Instance document keys carry no suffix
 *  (`E0004-010100-000188-CC-20260415`), and a certificate saved without one is a
 *  file the operator has to explain to their own computer. */
const EXT_FOR = new Map([
  ["application/pdf", ".pdf"],
  ["text/markdown", ".md"],
  ["text/plain", ".txt"],
  ["text/csv", ".csv"],
  ["application/json", ".json"],
  ["image/png", ".png"],
  ["image/jpeg", ".jpg"],
]);

function downloadName(objectKey: string, contentType: string): string {
  const base = objectKey.split("/").pop() || objectKey;
  if (/\.[A-Za-z0-9]{1,8}$/.test(base)) return base;
  return base + (EXT_FOR.get(contentType.split(";")[0].trim().toLowerCase()) ?? "");
}

/**
 * A CSV rendered as a table.
 *
 * The store's `-L` bills of materials and `-pos` placement files are CSV, and
 * read as text they are a wall of commas — the columns are the whole point. This
 * is a deliberately small parser: it handles quoted fields and embedded commas,
 * quotes and newlines, which is what a spreadsheet export actually emits, and
 * nothing beyond that.
 *
 * A file it cannot parse falls back to plain text rather than to a wrong table,
 * because a table with the columns silently misaligned is worse than no table.
 */
function parseCsv(text: string): string[][] {
  const rows: string[][] = [];
  let row: string[] = [];
  let field = "";
  let quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"') {
        if (text[i + 1] === '"') { field += '"'; i++; } else quoted = false;
      } else field += c;
      continue;
    }
    if (c === '"') quoted = true;
    else if (c === ",") { row.push(field); field = ""; }
    else if (c === "\n") { row.push(field); rows.push(row); row = []; field = ""; }
    else if (c !== "\r") field += c;
  }
  if (field !== "" || row.length) { row.push(field); rows.push(row); }
  return rows.filter((r) => r.some((cell) => cell.trim() !== ""));
}

function csvTable(text: string): string {
  const rows = parseCsv(text);
  if (rows.length < 2) return `<pre class="rd-body plain">${esc(text)}</pre>`;
  const [head, ...body] = rows;
  const width = head.length;
  const th = head.map((h) => `<th>${esc(h)}</th>`).join("");
  const tr = body
    .map((r) => {
      // Pad or trim to the header width so a ragged row cannot shift every
      // column after it.
      const cells = Array.from({ length: width }, (_, i) => r[i] ?? "");
      return `<tr>${cells.map((c) => `<td>${esc(c)}</td>`).join("")}</tr>`;
    })
    .join("");
  return `<div class="rd-body rd-csv"><table><thead><tr>${th}</tr></thead><tbody>${tr}</tbody></table>
    <div class="rd-csv-foot">${body.length} row${body.length === 1 ? "" : "s"} \u00b7 ${width} columns</div></div>`;
}

/**
 * Open one indexed document over the console.
 *
 * The bytes come from the object store, not from the ERP: they stream through
 * our own origin (ADR-0022 rev 2 d7) and the ERP keeps none of them. Two things
 * an operator can do with a document, and they need different mechanics:
 *
 * - **Save** uses the bytes already fetched here, so it works for every document
 *   the reader can reach and needs nothing from the bucket.
 * - **Open in a tab** must be a *plain navigation*, which carries no
 *   `Authorization` header — so it uses the time-limited grant from d7, the one
 *   form of this the browser can spend on its own. If the deployment cannot mint
 *   one, the link is absent rather than present and broken.
 */
async function openDocument(instanceId: string | null, objectKey: string): Promise<void> {
  const reader = readerDialog();
  reader.innerHTML = `<div class="rd-sheet"><div class="rd-head">
    <span class="rd-key">${esc(objectKey)}</span>
    <span class="rd-note">opening…</span></div></div>`;
  if (!reader.open) reader.showModal();

  const path = api.documentContentPath(instanceId, objectKey);
  const grant = (
    instanceId ? api.documentUrl(instanceId, objectKey) : api.storeDocumentUrl(objectKey)
  )
    .then((g) => g.url)
    .catch(() => null);

  let bytes: Blob | null = null;
  let saveAs = objectKey;
  let tabUrl: string | null = null;
  let objectUrl: string | null = null;

  const head = (note = "") => {
    const tab = tabUrl
      ? `<a class="btn ghost sm" href="${esc(tabUrl)}" target="_blank" rel="noopener noreferrer">Open in a tab</a>`
      : "";
    const save = bytes ? `<button class="btn ghost sm" id="rd-save">Download</button>` : "";
    return `<div class="rd-head">
      <span class="rd-key">${esc(objectKey)}</span>
      ${note ? `<span class="rd-note">${esc(note)}</span>` : ""}
      <span class="rd-acts">${save}${tab}
        <button class="btn ghost sm" id="rd-close" aria-label="Close">Close</button></span>
    </div>`;
  };

  let body = "";
  try {
    const res = await fetch(path, { headers: { Authorization: `Bearer ${getToken()}` } });
    tabUrl = await grant;
    if (!res.ok) {
      const detail = await res.json().then((j) => j.detail).catch(() => res.statusText);
      body = `<div class="empty"><span class="err">${esc(detail)}</span></div>`;
    } else {
      const type = res.headers.get("content-type") ?? "";
      bytes = await res.blob();
      saveAs = downloadName(objectKey, type);
      const isImage = /^image\//.test(type) || /\.(png|jpe?g|gif|webp|svg)$/i.test(objectKey);
      const isPdf = /pdf/.test(type) || /\.pdf$/i.test(objectKey);
      const isCsv = /csv/.test(type) || /\.csv$/i.test(objectKey);
      const textual =
        /^text\/|json|markdown|xml|csv/.test(type) || /\.(md|markdown|txt|csv)$/i.test(objectKey);

      // Images and PDFs are shown from an object URL over the bytes already
      // fetched, not from the presigned grant: the grant is optional (a
      // deployment whose bucket cannot mint one still reads through), and a
      // viewer that only worked where it could would be the harder case to
      // explain. The URL is revoked when the dialog closes.
      if (isImage) {
        objectUrl = URL.createObjectURL(bytes);
        body = `<div class="rd-body rd-image"><img src="${objectUrl}" alt="${esc(objectKey)}" /></div>`;
      } else if (isPdf) {
        objectUrl = URL.createObjectURL(bytes);
        body = `<iframe class="rd-body rd-pdf" src="${objectUrl}" title="${esc(objectKey)}"></iframe>`;
      } else if (isCsv) {
        body = csvTable(await bytes.text());
      } else if (!textual) {
        body = `<div class="empty rd-binary"><strong>${esc(type.split(";")[0] || "binary")}</strong>
          <span>Not a previewable document. Download it, or open it in a tab to view it.</span></div>`;
      } else {
        const text = await bytes.text();
        const md = /markdown/.test(type) || /\.(md|markdown)$/i.test(objectKey);
        body = md
          ? `<article class="rd-body md">${DOMPurify.sanitize(await marked.parse(text))}</article>`
          : `<pre class="rd-body plain">${esc(text)}</pre>`;
      }
    }
  } catch (e) {
    tabUrl = await grant;
    body = `<div class="empty"><span class="err">${esc((e as Error).message)}</span></div>`;
  }

  reader.innerHTML = `<div class="rd-sheet">${head()}${body}</div>`;
  $("rd-close")?.addEventListener("click", () => reader.close());
  $("rd-save")?.addEventListener("click", () => {
    const url = URL.createObjectURL(bytes!);
    const a = Object.assign(document.createElement("a"), { href: url, download: saveAs });
    a.click();
    // Revoke on the next tick: revoking synchronously can beat the download start.
    setTimeout(() => URL.revokeObjectURL(url));
  });
}

// ---- the filing plate ------------------------------------------------------

/** Layer columns, in the order ADR-0017's code legend lists them. `F` is
 *  deliberately absent: firmware is versioned on its own stream and gets a plate
 *  of its own (d16). */
const LAYER_COLUMNS = ["S", "D", "L", "P", "M", "I"];

const size = (n: number) =>
  n < 1024 ? `${n} B` : n < 1e6 ? `${Math.round(n / 1024)} kB` : `${(n / 1e6).toFixed(1)} MB`;

// The layer letter says what a document is ABOUT; the extension says whether the
// reader can render it. A design-layer pinmap is still markdown.
const READABLE_KINDS = new Set(["manual", "document", "procedure", "instruction", "table"]);
const readable = (d: StoreDoc) =>
  READABLE_KINDS.has(d.kind) || /\.(md|markdown|txt|csv)$/i.test(d.object_key);

/** One object, shown as the part of its key that the row and column do not
 *  already carry. The key is still the control (ADR-0017 d15) — just not
 *  repeated whole in every cell. */
function docChip(d: StoreDoc, prefix: string): string {
  const tail = d.object_key.slice(prefix.length);
  return `<button class="seg id key chip${readable(d) ? "" : " bin"}"
    data-doc="${esc(d.object_key)}"
    title="${esc(`${d.object_key} · ${d.kind} · ${size(d.size_bytes)}`)}">${esc(tail)}${
      d.packaged ? `<span class="pkg" aria-hidden="true">⧉</span>` : ""
    }</button>`;
}

/**
 * One plate: versions down, document layers across.
 *
 * An empty cell is information — this version carries nothing on that layer — so
 * the columns hold steady across versions instead of collapsing per row.
 */
function plate(root: string, docs: StoreDoc[], allowed: string[]): string {
  const live = docs.filter((d) => !d.status);
  // Keys carrying no layer letter at all — the raw EDA sources. Real objects in
  // the store, so they get a column of their own rather than a guessed letter.
  const cols = [
    ...allowed.filter((letter) => live.some((d) => d.layer === letter)),
    ...(live.some((d) => !d.layer) ? [""] : []),
  ];

  const byVersion = new Map<string, StoreDoc[]>();
  for (const d of docs) {
    const v = d.version ?? "";
    byVersion.set(v, [...(byVersion.get(v) ?? []), d]);
  }

  const heading = (letter: string) =>
    letter
      ? `<b>${esc(letter)}</b> ${esc(live.find((d) => d.layer === letter)?.layer_label ?? "")}`
      : `<b>·</b> no layer letter`;

  const rows = [...byVersion.keys()]
    .sort((a, b) => b.localeCompare(a))
    .map((version) => {
      const group = byVersion.get(version)!;
      const alive = group.filter((d) => !d.status);
      const withdrawn = group.filter((d) => d.status);
      const rowPrefix = version ? `${root}-${version}` : root;

      const cells = alive.length
        ? cols
            .map((letter) => {
              const here = alive.filter((d) => (d.layer ?? "") === letter);
              // The letter rides on the cell so a narrow screen, where the
              // column header is gone, can still say which layer this is.
              return `<span class="pcell" data-layer="${esc(letter || "·")}">${
                here.length
                  ? here.map((d) => docChip(d, rowPrefix)).join("")
                  : `<span class="pnil" title="nothing filed on this layer">–</span>`
              }</span>`;
            })
            .join("")
        : "";

      // A withdrawal is scoped to the defect, not blindly to the whole version
      // (ADR-0017 d17), so a version can be part live and part archived. The
      // strip sits under whatever is still live rather than replacing it.
      const strip = withdrawn.length
        ? `<div class="pstrip"><span class="wd">withdrawn</span>${withdrawn
            .map((d) => docChip(d, rowPrefix))
            .join("")}
          <span class="ref">which status covers which files, and why — REGISTRY.md</span></div>`
        : "";

      return `<div class="prow${alive.length ? "" : " gone"}"><span class="pv"><span
        class="pv-v${version ? "" : " none"}">${esc(
          version ? (group[0].version_label ?? version) : "no version",
        )}</span><span class="pv-k">${esc(rowPrefix)}</span></span>${cells}${strip}</div>`;
    })
    .join("");

  return `<div class="plate" style="--cols:${cols.length}">
    <div class="prow head"><span class="pv">version</span>${cols
      .map((letter) => `<span class="pcell">${heading(letter)}</span>`)
      .join("")}</div>${rows}</div>`;
}

/** Everything filed under one root — the dossier a single key prefix returns
 *  (ADR-0019 d9: which part an artifact serves is the load-bearing fact, and the
 *  prefix is what carries it). */
function dossier(root: string, docs: StoreDoc[]): string {
  const { name, space } = rootLabel(root);
  const firmware = docs.filter((d) => d.layer === "F");
  const design = docs.filter((d) => d.layer !== "F");
  const purchased = root.startsWith("SP");

  return `<article class="dossier">
    <header class="dh">
      <span class="leaf" style="background:${purchased ? "var(--ink-3)" : moduleHue(root)}"></span>
      <button class="seg id dh-root" data-prefix="${esc(root)}"
        title="List everything under ${esc(root)}">${esc(root)}</button>
      <b>${esc(name ?? "not in the registry")}</b>
      <span class="ref">${esc(space)} · ${docs.length} object${docs.length === 1 ? "" : "s"}</span>
    </header>
    ${design.length ? plate(root, design, LAYER_COLUMNS) : ""}
    ${
      firmware.length
        ? `<div class="fw"><span class="fw-cap">firmware · its own version stream</span>
             <span class="ref">one codebase shared by every node, so this version tracks the code
             and not the board's design (ADR-0017 d16)</span></div>${plate(root, firmware, ["F"])}`
        : ""
    }</article>`;
}

/**
 * The documents the repository owns — manuals, pinmaps, schematics, fab packages.
 *
 * These are not ERP records: the ERP indexes none of them and owns none of them
 * (ADR-0021 d11). It serves the copy `store_sync` mirrored into the warehouse,
 * read-only, the same shape ADR-0023 gave `REGISTRY.md`. An operator at a cabinet
 * wants the bring-up manual, and the alternative to showing it here is a second
 * copy of store/ somewhere.
 *
 * The layout is the identifier, taken apart. The store is flat by decision — an
 * identifier IS the object key and filtering it is a prefix list (ADR-0017 d15)
 * — but flat is how it is *stored*, not how it is read, and the key already
 * spells out root, version, layer and slug. So a row carries the prefix its
 * objects share, a column carries the document layer, and a cell carries only
 * what is left of the key; read a row across and the pieces spell the key back.
 * That is the on-demand hierarchy synthesis d15 anticipated, done for a reader
 * instead of for a bucket listing. Grouping by "kind" instead — the shape this
 * view had — threw away the three facts the scheme works hardest to record:
 * which root a document belongs to, which version of it, and whether that
 * version is still alive.
 */
async function library(): Promise<string> {
  const all = await api.storeDocuments();
  if (!all.length)
    return `<div class="empty">Nothing mirrored into the warehouse yet. Run
      <code>python -m app.store_sync</code> to publish the repository's <code>store/</code>.</div>`;

  // One control, and it is the store's own: a prefix. Not a search box pretending
  // the flat keyspace is a database — `ListObjectsV2(Prefix=…)` is the whole
  // query the store can answer, and typing part of an identifier is how you ask.
  const prefix = state.prefix;
  const docs = prefix ? all.filter((d) => d.object_key.startsWith(prefix)) : all;

  const roots = new Map<string, StoreDoc[]>();
  for (const d of docs) roots.set(d.root ?? "", [...(roots.get(d.root ?? "") ?? []), d]);
  const unfiled = roots.get("") ?? [];

  const body = docs.length
    ? [...roots.keys()]
        .filter(Boolean)
        .sort()
        .map((root) => dossier(root, roots.get(root)!))
        .join("") +
      (unfiled.length
        ? `<article class="dossier"><header class="dh">
            <span class="leaf loose-mark"></span>
            <b>Not filed under an identifier</b>
            <span class="ref">${unfiled.length} object${unfiled.length === 1 ? "" : "s"} in
              <code>store/</code> whose name is not a key, so nothing addresses them</span></header>
            <div class="loose">${unfiled
              .map((d) => `${docChip(d, "")}<span class="ref">${esc(d.kind)}</span>`)
              .join("")}</div></article>`
        : "")
    : `<div class="empty">No object key starts with <code>${esc(prefix)}</code>.
        The store is flat, so a prefix is the whole query — shorten it to widen the list.</div>`;

  const chips = [...new Set(all.map((d) => d.root).filter(Boolean))]
    .sort()
    .map(
      (root) =>
        `<button class="rchip${prefix === root ? " on" : ""}" data-prefix="${esc(root!)}">${esc(root!)}</button>`,
    )
    .join("");

  return `
    <section class="panel"><div class="ph"><h2>What the repository holds</h2>
      <span class="desc">${all.length} objects, filed flat — laid out here along the identifier
      that names each one</span>
      <div class="right pfx"><label for="lb-p">prefix</label>
        <input id="lb-p" value="${esc(prefix)}" placeholder="E0001-000002" size="17"
          spellcheck="false" autocomplete="off">
        ${prefix ? `<button class="btn ghost sm" id="lb-x">Clear</button>` : ""}</div></div>
      <div class="rchips">${chips}</div>
      <div class="note">These belong to the repository, not to the ERP. It indexes none of them
        and owns none of them — it reads through to the copy <code>store_sync</code> published, so
        the manual you read here is the one in <code>store/</code>.</div>
      <div class="legendline">A row is the prefix its objects share; a cell is what follows it.
        Read one across and the pieces spell the key back.</div>
      <div class="lib">${body}</div>
    </section>`;
}

async function stock(): Promise<string> {
  const list = await api.listStock();
  state.counts.stock = list.length;
  const rows = list
    .map((s) => {
      const cls = s.quantity === 0 ? "crit" : s.quantity <= 2 ? "warn" : "ok";
      const label = s.quantity === 0 ? "out" : s.quantity <= 2 ? "low" : "in stock";
      // What the part is comes from the registry; where it sits is the ERP's.
      const role = partRole(s.sp_number);
      const where = s.location ?? "no location recorded";
      return `<div class="trow"><div><div class="sp-id">${esc(s.sp_number)}</div>
        <div class="sp-spec">${esc(role ?? where)}${role ? `<span class="ref"> · ${esc(where)}</span>` : ""}</div></div>
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
  firmware,
  gateway,
  library,
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
  // The object key is the control, so it carries the click (ADR-0017 d15 — the
  // identifier is the key; there is no separate file to point at).
  document.querySelectorAll<HTMLElement>("[data-doc]").forEach((key) =>
    key.addEventListener("click", (ev) => {
      ev.stopPropagation();
      void openDocument(key.dataset.docInstance ?? null, key.dataset.doc!);
    }),
  );
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
    const box = $<HTMLInputElement>("in-p");
    box.addEventListener("input", async () => {
      state.instancePrefix = box.value.trim();
      await renderBody();
      const again = $<HTMLInputElement>("in-p");
      again.focus();
      again.setSelectionRange(again.value.length, again.value.length);
    });
    $("in-x")?.addEventListener("click", () => {
      state.instancePrefix = "";
      void renderBody();
    });
    document.querySelectorAll<HTMLElement>("[data-instance-prefix]").forEach((el) =>
      el.addEventListener("click", () => {
        const want = el.dataset.instancePrefix!;
        state.instancePrefix = state.instancePrefix === want ? "" : want;
        void renderBody();
      }),
    );

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
    const calFields = () => {
      const isCal = $<HTMLSelectElement>("dc-t").value === "CC";
      $("dc-cal").hidden = !isCal;
      if (!isCal) {
        $<HTMLInputElement>("dc-d").value = "";
        $<HTMLInputElement>("dc-u").value = "";
      }
    };
    $("dc-t").addEventListener("change", calFields);
    calFields();

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
        return `<span class="rl">Installed</span>${integrationId(rec)}`;
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
        // Encoded exactly as pasted, not parsed and re-stringified: the signature
        // covers these bytes (ADR-0025 d6), so reformatting them would invalidate
        // it. Parsing happens only to reject something that is not JSON at all.
        const text = $<HTMLTextAreaElement>("pf-p").value;
        try {
          JSON.parse(text);
        } catch {
          throw new Error("the profile document is not valid JSON");
        }
        const document_b64 = btoa(
          String.fromCharCode(...new TextEncoder().encode(text)),
        );
        await api.storeProfile(state.machine!, tag, document_b64, val("pf-h") || undefined);
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

  if (state.view === "instance") {
    $("rp-pdf")?.addEventListener("click", () =>
      act("rp-out", async () => {
        // The report is a plain fetch, not a navigation: the route is token
        // authenticated and a navigation carries no Authorization header. The
        // blob is handed to a synthetic anchor so the browser saves it under the
        // filename the server chose.
        const blob = await api.instanceReport(state.instance!);
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = `${state.instance}-dossier.pdf`;
        a.click();
        setTimeout(() => URL.revokeObjectURL(url));
        return `<span class="rl">Saved</span>${esc(state.instance)}-dossier.pdf`;
      }),
    );
  }

  if (state.view === "firmware") {
    document.querySelectorAll<HTMLElement>("[data-intend]").forEach((btn) =>
      btn.addEventListener("click", () =>
        act("fw-out", async () => {
          const release = btn.dataset.intend!;
          await api.setFirmwareIntent(state.machine!, release);
          await renderBody();
          // Says what was recorded and what was not, because the gap is the part
          // an operator is most likely to fill in wrongly on their own.
          return `<span class="rl">Recorded</span>${esc(release)} is what ${esc(
            state.machine,
          )} should run. Nothing has been sent to a node — the gateway performs the update.`;
        }),
      ),
    );
  }

  if (state.view === "library") {
    // Narrowing the prefix redraws the plates under the cursor, so the caret is
    // put back where it was — an operator typing an identifier is mid-word, not
    // mid-command.
    const box = $<HTMLInputElement>("lb-p");
    box.addEventListener("input", async () => {
      state.prefix = box.value.trim();
      await renderBody();
      const again = $<HTMLInputElement>("lb-p");
      again.focus();
      again.setSelectionRange(again.value.length, again.value.length);
    });
    $("lb-x")?.addEventListener("click", () => {
      state.prefix = "";
      void renderBody();
    });
    // A root is a prefix; clicking one asks the store the same question by hand.
    document.querySelectorAll<HTMLElement>("[data-prefix]").forEach((el) =>
      el.addEventListener("click", () => {
        state.prefix = state.prefix === el.dataset.prefix ? "" : el.dataset.prefix!;
        void renderBody();
      }),
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
        ${nav("firmware", "Firmware")}
        ${nav("gateway", "Gateway channel")}
        ${nav("library", "Documents")}
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
    // The type registry, read once. Labels come from REGISTRY.md via the ERP
    // (ADR-0023); the console keeps no table of its own, so an identifier the
    // registry does not carry simply renders as itself.
    setCatalog(await api.catalog());
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
