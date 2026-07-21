// SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
// Typed client for the instance/integration ERP API (ADR-0022).

export interface Instance {
  instance_id: string;
  e_number: string;
  version: string;
  serial: string;
  status: string;
}

export interface IntegrationRecord {
  machine_id: string;
  depth_code: string;
  instance_id: string;
  installed_at: string | null;
  removed_at: string | null;
  removal_reason: string | null;
}

export interface Machine {
  machine_id: string;
  notes: string | null;
}

export interface LifecycleDoc {
  instance_full_id: string;
  doc_type: string;
  object_key: string;
  valid_until: string | null;
  status: string;
}

export interface Profile {
  machine_id: string;
  version_tag: string;
  created_at: string | null;
  active: boolean;
}

export interface Provisioning {
  cert_serial: string;
  public_key_fingerprint: string;
  cert_not_before: string;
  cert_not_after: string;
  pr_object_key: string;
}

export interface DocumentUpload {
  doc_type: string;
  file: File;
  valid_until?: string;
  doc_date?: string;
}

export interface SPStock {
  sp_number: string;
  quantity: number;
  location: string | null;
}

export interface AllocateResult {
  serials: string[];
  next_serial: number;
}

const TOKEN_KEY = "erp_operator_token";

export function getToken(): string {
  return localStorage.getItem(TOKEN_KEY) ?? "dev-operator-token";
}
export function setToken(token: string): void {
  localStorage.setItem(TOKEN_KEY, token);
}

class ApiError extends Error {
  constructor(
    public status: number,
    message: string,
  ) {
    super(message);
  }
}

async function req<T>(method: string, path: string, body?: unknown): Promise<T> {
  // FormData carries its own multipart boundary — let the browser set the header.
  const multipart = body instanceof FormData;
  const res = await fetch(`/api/v1${path}`, {
    method,
    headers: {
      Authorization: `Bearer ${getToken()}`,
      ...(body !== undefined && !multipart ? { "Content-Type": "application/json" } : {}),
    },
    body: body === undefined ? undefined : multipart ? body : JSON.stringify(body),
  });
  if (!res.ok) {
    let detail = res.statusText;
    try {
      detail = (await res.json()).detail ?? detail;
    } catch {
      /* keep statusText */
    }
    throw new ApiError(res.status, detail);
  }
  return res.status === 204 ? (undefined as T) : ((await res.json()) as T);
}

export interface Meta {
  operator_name: string;
  operator_uuid: string;
  role: string;
}

/** A designed assembly, per REGISTRY.md (ADR-0017 d3). */
export interface Module {
  e_number: string;
  designation: string;
  discipline: string;
  notes: string;
}

/** A purchased part, per REGISTRY.md (ADR-0019). The SKU stays in the BOM. */
export interface Part {
  sp_number: string;
  role: string;
  instance_tracked: boolean;
  notes: string;
}

export interface Catalog {
  modules: Module[];
  parts: Part[];
}

export const api = {
  meta: () => req<Meta>("GET", "/meta"),
  catalog: () => req<Catalog>("GET", "/catalog"),
  listInstances: (eNumber?: string) =>
    req<Instance[]>("GET", `/instances${eNumber ? `?e_number=${eNumber}` : ""}`),
  getInstance: (id: string) => req<Instance>("GET", `/instances/${id}`),
  allocate: (e_number: string, version: string, quantity: number) =>
    req<AllocateResult>("POST", "/instances", { e_number, version, quantity }),
  instanceHistory: (id: string) => req<IntegrationRecord[]>("GET", `/instances/${id}/history`),
  bindProvisioning: (id: string, body: Provisioning) =>
    req<{ ok: boolean }>("POST", `/instances/${id}/provisioning`, body),

  instanceDocuments: (id: string) => req<LifecycleDoc[]>("GET", `/instances/${id}/documents`),
  uploadDocument: (id: string, doc: DocumentUpload) => {
    const form = new FormData();
    form.append("doc_type", doc.doc_type);
    form.append("file", doc.file);
    if (doc.valid_until) form.append("valid_until", doc.valid_until);
    if (doc.doc_date) form.append("doc_date", doc.doc_date);
    return req<LifecycleDoc>("POST", `/instances/${id}/documents`, form);
  },

  listMachines: () => req<Machine[]>("GET", "/machines"),
  machineIntegration: (gbox: string) =>
    req<IntegrationRecord[]>("GET", `/machines/${gbox}/integration`),
  setPosition: (gbox: string, depth: string, instance_id: string) =>
    req<IntegrationRecord>("PUT", `/machines/${gbox}/positions/${depth}`, { instance_id }),
  clearPosition: (gbox: string, depth: string, reason = "removed") =>
    req<{ ok: boolean }>(
      "DELETE",
      `/machines/${gbox}/positions/${depth}?reason=${encodeURIComponent(reason)}`,
    ),

  listProfiles: (gbox: string) => req<Profile[]>("GET", `/machines/${gbox}/profiles`),
  storeProfile: (gbox: string, version_tag: string, payload: unknown, signed_hash?: string) =>
    req<Profile>("POST", `/machines/${gbox}/profiles`, { version_tag, payload, signed_hash }),
  recordActiveProfile: (gbox: string, version_tag: string) =>
    req<{ ok: boolean }>("PUT", `/machines/${gbox}/active-profile`, { version_tag }),

  calibrationExpiring: (days = 30) =>
    req<LifecycleDoc[]>("GET", `/calibration/expiring?days=${days}`),
  listStock: () => req<SPStock[]>("GET", "/sp-stock"),
  setStock: (sp_number: string, quantity: number, location: string | null) =>
    req<{ ok: boolean }>("POST", "/sp-stock", { sp_number, quantity, location }),
};

/** The instance-lifecycle document allowlist (ADR-0022 d7) — mirrors the API's. */
export const DOC_TYPES = ["QP", "QR", "CP", "CC", "PR"] as const;

// ---- the type registry, as read from REGISTRY.md (ADR-0023) ---------------
// The console carries no table of its own: what an `Exxxx` means is the
// registry's to say (ADR-0017 d3, ADR-0021 d11), fetched once at boot. An
// identifier the registry does not know renders as itself — never as a guess.

let catalogue: Catalog = { modules: [], parts: [] };

export function setCatalog(c: Catalog): void {
  catalogue = c;
}

export function moduleDesignation(eNumber: string): string {
  return catalogue.modules.find((m) => m.e_number === eNumber)?.designation ?? eNumber;
}

/** What a purchased part *is*, per the registry — vendor-free, and never the
 *  SKU or price, which stay in the BOM (ADR-0019, ADR-0021 d9). */
export function partRole(spNumber: string): string | null {
  return catalogue.parts.find((p) => p.sp_number === spNumber)?.role ?? null;
}

/** The leaf hue for a module: assigned from the design palette by E-number, so a
 *  newly registered type gets one with no change here and none in REGISTRY.md,
 *  which holds meaning and never presentation (ADR-0023 d6). */
export function moduleHue(eNumber: string): string {
  const n = Number.parseInt(eNumber.replace(/^E/, ""), 10);
  return Number.isFinite(n) ? `var(--leaf-${(n - 1) % LEAF_HUES})` : "var(--ink-2)";
}

/** How many leaf hues the stylesheet defines; the sequence cycles past it. */
const LEAF_HUES = 7;
