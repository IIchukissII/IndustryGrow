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
  const res = await fetch(`/api/v1${path}`, {
    method,
    headers: {
      Authorization: `Bearer ${getToken()}`,
      ...(body !== undefined ? { "Content-Type": "application/json" } : {}),
    },
    body: body !== undefined ? JSON.stringify(body) : undefined,
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

export const api = {
  listInstances: (eNumber?: string) =>
    req<Instance[]>("GET", `/instances${eNumber ? `?e_number=${eNumber}` : ""}`),
  allocate: (e_number: string, version: string, quantity: number) =>
    req<AllocateResult>("POST", "/instances", { e_number, version, quantity }),
  instanceHistory: (id: string) => req<IntegrationRecord[]>("GET", `/instances/${id}/history`),

  listMachines: () => req<Machine[]>("GET", "/machines"),
  machineIntegration: (gbox: string) =>
    req<IntegrationRecord[]>("GET", `/machines/${gbox}/integration`),
  setPosition: (gbox: string, depth: string, instance_id: string) =>
    req<IntegrationRecord>("PUT", `/machines/${gbox}/positions/${depth}`, { instance_id }),
  clearPosition: (gbox: string, depth: string) =>
    req<{ ok: boolean }>("DELETE", `/machines/${gbox}/positions/${depth}`),

  listProfiles: (gbox: string) => req<Profile[]>("GET", `/machines/${gbox}/profiles`),
  calibrationExpiring: (days = 30) =>
    req<LifecycleDoc[]>("GET", `/calibration/expiring?days=${days}`),
  listStock: () => req<SPStock[]>("GET", "/sp-stock"),
};

// Display-only module labels + leaf colours (mirrors app/web/catalog.py).
// Type meaning is REGISTRY.md's; this is a UI convenience only.
export const MODULE_DISPLAY: Record<string, [string, string]> = {
  E0001: ["E0001 carrier", "#9fb2c9"],
  E0002: ["M01-CLIMATE", "#67e8f9"],
  E0003: ["M02-LIGHT", "#fcd34d"],
  E0004: ["M03-ANALYTICS", "#c4b5fd"],
  E0005: ["M04-PLANT", "#86efac"],
  E0006: ["M05-SAFETY", "#fb7185"],
};
export function moduleDisplay(eNumber: string): [string, string] {
  return MODULE_DISPLAY[eNumber] ?? [eNumber, "#9fb2c9"];
}
