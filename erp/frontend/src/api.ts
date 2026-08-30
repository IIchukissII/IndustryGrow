// SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
// Typed client for the instance/integration ERP API (ADR-0022).

export interface Instance {
  instance_id: string;
  e_number: string;
  version: string;
  serial: string;
  status: string;
  /** `020100` decoded as `v2.1.0`. Null when the code is not a version code —
   *  which keeps a failed decode distinguishable from a real version. */
  version_label: string | null;
}

/**
 * An instance at a position: the one record where the two axes meet.
 *
 * Both sides arrive read into their own fields (ADR-0022 d13). That matters more
 * here than anywhere else in the API, because the depth and the version are both
 * six digits and mean unrelated things — `020100` is main 02 as a position and
 * v2.1.0 as a version, and no reader should be telling them apart by counting
 * hyphens.
 */
export interface IntegrationRecord {
  machine_id: string;
  depth_code: string;
  instance_id: string;
  installed_at: string | null;
  removed_at: string | null;
  removal_reason: string | null;
  depth_levels: number[] | null;
  depth_label: string | null;
  e_number: string | null;
  version: string | null;
  version_label: string | null;
  serial: string | null;
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

/**
 * One type-layer document, with its key already read into fields.
 *
 * The console parses no identifiers. An identifier *is* the object key
 * (ADR-0017 d15) and the API is where the grammar is spoken (ADR-0022 d13), so
 * these fields arrive parsed — a regex here would be a second implementation of
 * the scheme, free to drift from the one the store is filed by.
 *
 * `root` is null for an object in store/ that carries no identifier at all.
 */
export interface StoreDoc {
  object_key: string;
  kind: string;
  size_bytes: number;
  root: string | null;
  root_kind: "E" | "SP" | null;
  version: string | null;
  version_label: string | null;
  layer: string | null;
  layer_label: string | null;
  slug: string | null;
  status: string | null;
  packaged: boolean;
}

export interface DocumentUrl {
  object_key: string;
  url: string;
  expires_in: number;
}

export interface MachineIdentity {
  machine_id: string;
  vendor_serial: string;
  atecc_serial: string | null;
  public_key_fingerprint: string;
  cert_serial: string;
  cert_not_before: string;
  cert_not_after: string;
  expires_in_days: number;
  provisioned_at: string | null;
}

/**
 * What the ERP owns about a machine's gateway channel (ADR-0022 rev 1 d12).
 *
 * There is no `last_pulled_at` here and there will not be one: the pull is a
 * pure read and the ERP records no operational act (d8 rev 1, d9). The console
 * shows that as a gap rather than the API inventing an answer.
 */
export interface GatewayChannel {
  machine_id: string;
  identity: MachineIdentity | null;
  active_version: string | null;
  active_since: string | null;
  stored_versions: number;
  unsigned_versions: number;
}

/**
 * A firmware release the repository publishes (ADR-0029 d13).
 *
 * `artifact_keys` is the pair of slot images. A release is two artifacts because
 * the application is linked per slot, and which one a node receives depends on
 * the slot it is not already running — a choice the node makes and the gateway
 * resolves, never the operator.
 */
export interface FirmwareRelease {
  release_root: string;
  version: string | null;
  version_label: string | null;
  artifact_keys: string[];
}

/**
 * The release an operator intends for a machine (ADR-0021 d18).
 *
 * Intent, not state. There is no field for what the nodes are running and there
 * will not be one: that is observed on the bus by the gateway (ADR-0029 d15) and
 * is excluded from this API by ADR-0022 d9.
 */
export interface FirmwareIntent {
  machine_id: string;
  release_root: string;
  selected_at: string | null;
  selected_by: string | null;
  artifact_keys: string[];
  version: string | null;
  version_label: string | null;
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

// No default. A repo-wide token shipped as the fallback answers 401 on every
// deployment that set its own (which is every deployment that should), and the
// console then reads as connected-but-broken rather than as not signed in.
export function getToken(): string {
  return localStorage.getItem(TOKEN_KEY) ?? "";
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

  /**
   * A time-limited retrieval URL for one indexed document (ADR-0022 d7).
   *
   * The ERP never returns blob content, so the reader fetches from the object
   * store with this grant rather than through the API. That keeps the ERP the
   * index over the store instead of a proxy for it — and it is why reading a
   * document in the console needs a CORS rule on the bucket.
   */
  documentUrl: (instanceId: string, objectKey: string) =>
    req<DocumentUrl>(
      "GET",
      `/instances/${instanceId}/documents/${encodeURIComponent(objectKey)}/url`,
    ),
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

  /** Type-layer documents the repository owns (ADR-0022 d1, 2026-07-26). */
  storeDocuments: () => req<StoreDoc[]>("GET", "/store-documents"),
  storeDocumentUrl: (key: string) =>
    req<DocumentUrl>("GET", `/store-documents/${encodeURIComponent(key)}/url`),
  /**
   * Same-origin paths for reading a document through the ERP (ADR-0022 rev 2 d7).
   * The bytes stream from the object store and the ERP keeps none of them; going
   * through our own origin is what makes them readable without a CORS policy on
   * the bucket, which the deployment's credentials may not be able to set.
   */
  documentContentPath: (instanceId: string | null, key: string) =>
    instanceId
      ? `/api/v1/instances/${instanceId}/documents/${encodeURIComponent(key)}/content`
      : `/api/v1/store-documents/${encodeURIComponent(key)}/content`,

  gatewayChannel: (gbox: string) =>
    req<GatewayChannel>("GET", `/machines/${gbox}/gateway-channel`),

  listProfiles: (gbox: string) => req<Profile[]>("GET", `/machines/${gbox}/profiles`),
  // document_b64 carries the artifact's exact signed bytes; the console encodes
  // what the operator pastes and never reformats it, because the signature covers
  // those bytes (ADR-0025 d6).
  storeProfile: (gbox: string, version_tag: string, document_b64: string, signature?: string) =>
    req<Profile>("POST", `/machines/${gbox}/profiles`, { version_tag, document_b64, signature }),
  recordActiveProfile: (gbox: string, version_tag: string) =>
    req<{ ok: boolean }>("PUT", `/machines/${gbox}/active-profile`, { version_tag }),

  listFirmwareReleases: () => req<FirmwareRelease[]>("GET", "/firmware-releases"),
  // 404 is a real answer here, not a failure: a cabinet with no release selected
  // is the starting state, and the view says so rather than erroring.
  firmwareIntent: (gbox: string) =>
    req<FirmwareIntent>("GET", `/machines/${gbox}/firmware`).catch(() => null),
  setFirmwareIntent: (gbox: string, release_root: string) =>
    req<FirmwareIntent>("PUT", `/machines/${gbox}/firmware`, { release_root }),

  // Returns the PDF bytes. Not `req`, which decodes JSON: a report is a file,
  // and the Authorization header is why it cannot simply be a link.
  instanceReport: async (instanceId: string): Promise<Blob> => {
    const res = await fetch(`/api/v1/instances/${instanceId}/report.pdf`, {
      headers: { Authorization: `Bearer ${getToken()}` },
    });
    if (!res.ok) throw new Error(`the report could not be produced (HTTP ${res.status})`);
    return res.blob();
  },

  /** One markdown document as a PDF. Instance documents and repository documents
   *  live behind different guards, so the path differs; the caller has the same
   *  instanceId it already used to fetch the bytes. */
  documentPdf: async (instanceId: string | null, key: string): Promise<Blob> => {
    const path = instanceId
      ? `/api/v1/instances/${instanceId}/documents/${encodeURIComponent(key)}/pdf`
      : `/api/v1/store-documents/${encodeURIComponent(key)}/pdf`;
    const res = await fetch(path, { headers: { Authorization: `Bearer ${getToken()}` } });
    if (!res.ok) {
      const detail = await res.json().then((j) => j.detail).catch(() => res.statusText);
      throw new Error(String(detail));
    }
    return res.blob();
  },

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

/**
 * What an identifier root is, for either type space — designed or purchased.
 *
 * Both roots sit on the identity axis (ADR-0019 d1), so anything filed under one
 * needs the same two answers: what the thing is, and which space it belongs to.
 * A root the registry does not carry answers only the second, which is still
 * true and still useful.
 */
export function rootLabel(root: string): { name: string | null; space: string } {
  if (root.startsWith("SP"))
    return { name: partRole(root), space: "purchased part · no project version" };
  const module = catalogue.modules.find((m) => m.e_number === root);
  return { name: module?.designation ?? null, space: module?.discipline ?? "designed assembly" };
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
