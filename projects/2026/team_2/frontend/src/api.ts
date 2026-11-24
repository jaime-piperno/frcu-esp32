import type { AdherenceSummary, BluetoothState, Device, DeviceState, DoseEvent, DoseUnit, Medication } from "./types";

export const API_BASE = "";

async function getJson<T>(url: string, headers?: Record<string, string>): Promise<T> {
  const res = await fetch(url, { headers });
  if (!res.ok) throw new Error(`GET ${url} returned ${res.status}`);
  return (await res.json()) as T;
}

export function fetchDevices(): Promise<Device[]> {
  return getJson<Device[]>(`${API_BASE}/api/devices/`);
}

export function fetchBluetoothState(deviceId: string): Promise<BluetoothState> {
  return getJson<BluetoothState>(`${API_BASE}/api/devices/${encodeURIComponent(deviceId)}/bluetooth/`);
}

export async function scanBluetooth(deviceId: string): Promise<void> {
  const response = await fetch(`${API_BASE}/api/devices/${encodeURIComponent(deviceId)}/bluetooth/scan`, { method: "POST" });
  if (!response.ok) throw new Error(`No se pudo iniciar la búsqueda: HTTP ${response.status}`);
}

export async function selectBluetoothSpeaker(deviceId: string, address: string | null, name: string | null): Promise<void> {
  const response = await fetch(`${API_BASE}/api/devices/${encodeURIComponent(deviceId)}/bluetooth/speaker`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ address, name }),
  });
  if (!response.ok) throw new Error(`No se pudo guardar el parlante: HTTP ${response.status}`);
}

export function fetchDeviceState(deviceId: string): Promise<DeviceState> {
  return getJson<DeviceState>(`${API_BASE}/api/dashboard/state/${encodeURIComponent(deviceId)}`);
}

export function fetchMedications(deviceId: string): Promise<Medication[]> {
  return getJson<Medication[]>(
    `${API_BASE}/api/medications/?deviceId=${encodeURIComponent(deviceId)}`,
  );
}

export function fetchEvents(deviceId: string): Promise<DoseEvent[]> {
  return getJson<DoseEvent[]>(
    `${API_BASE}/api/events?deviceId=${encodeURIComponent(deviceId)}`,
  );
}

export function fetchAdherence(deviceId: string): Promise<AdherenceSummary> {
  // The endpoint returns a list of daily summaries (latest first); without
  // from/to it returns the last 7 days. The dashboard wants "today".
  return getJson<AdherenceSummary[]>(
    `${API_BASE}/api/adherence?deviceId=${encodeURIComponent(deviceId)}`,
  ).then((list) => {
    if (list.length === 0) {
      return { date: new Date().toISOString().slice(0, 10), taken: 0, missed: 0, snoozed: 0, pending: 0 };
    }
    return list[list.length - 1];
  });
}

export async function deleteMedication(medicationId: string): Promise<void> {
  const res = await fetch(`${API_BASE}/api/medications/${encodeURIComponent(medicationId)}`, {
    method: "DELETE",
  });
  if (!res.ok) throw new Error(`DELETE /api/medications/ returned ${res.status}`);
}

export interface MedicationInput {
  deviceId: string;
  name: string;
  dosageAmount: number;
  dosageUnit: DoseUnit;
  schedules: { localTime: string; daysOfWeek: number; message: string | null }[];
}

export async function createMedication(input: MedicationInput): Promise<Medication> {
  const res = await fetch(`${API_BASE}/api/medications/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      deviceId: input.deviceId,
      name: input.name,
      dosageAmount: input.dosageAmount,
      dosageUnit: input.dosageUnit,
      isActive: true,
      schedules: input.schedules.map((schedule) => ({ ...schedule, isActive: true })),
    }),
  });
  if (!res.ok) throw new Error(`POST /api/medications/ returned ${res.status}`);
  return (await res.json()) as Medication;
}

export function fetchMedication(medicationId: string): Promise<Medication> {
  return getJson<Medication>(`${API_BASE}/api/medications/${encodeURIComponent(medicationId)}`);
}

export async function updateMedication(
  medicationId: string,
  input: { name: string; dosageAmount: number; dosageUnit: DoseUnit },
): Promise<void> {
  const res = await fetch(`${API_BASE}/api/medications/${encodeURIComponent(medicationId)}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(input),
  });
  if (!res.ok) throw new Error(`PUT /api/medications/ returned ${res.status}`);
}

export interface ScheduleUpdate {
  /** Existing schedule id when kept; omitted for new schedules. */
  id?: string;
  localTime: string;
  daysOfWeek: number;
  message: string | null;
}

async function postSchedule(
  medicationId: string,
  schedule: { localTime: string; daysOfWeek: number; message: string | null },
): Promise<void> {
  const res = await fetch(
    `${API_BASE}/api/medications/${encodeURIComponent(medicationId)}/schedules`,
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ...schedule, isActive: true }),
    },
  );
  if (!res.ok) throw new Error(`POST /schedules returned ${res.status}`);
}

async function putSchedule(
  medicationId: string,
  scheduleId: string,
  schedule: { localTime: string; daysOfWeek: number; message: string | null },
): Promise<void> {
  const res = await fetch(
    `${API_BASE}/api/medications/${encodeURIComponent(medicationId)}/schedules/${encodeURIComponent(scheduleId)}`,
    {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ...schedule, isActive: true }),
    },
  );
  if (!res.ok) throw new Error(`PUT /schedules returned ${res.status}`);
}

async function deleteSchedule(medicationId: string, scheduleId: string): Promise<void> {
  const res = await fetch(
    `${API_BASE}/api/medications/${encodeURIComponent(medicationId)}/schedules/${encodeURIComponent(scheduleId)}`,
    { method: "DELETE" },
  );
  if (!res.ok) throw new Error(`DELETE /schedules returned ${res.status}`);
}

/**
 * Persists a medication edit: updates name/dose and syncs the schedule list
 * against the current backend state (creates new, updates kept, removes deleted).
 */
export async function syncMedication(
  medicationId: string,
  input: { name: string; dosageAmount: number; dosageUnit: DoseUnit; schedules: ScheduleUpdate[] },
): Promise<void> {
  await updateMedication(medicationId, { name: input.name, dosageAmount: input.dosageAmount, dosageUnit: input.dosageUnit });

  const current = await fetchMedication(medicationId);
  const currentIds = current.schedules.map((schedule) => schedule.id);
  const keptIds = new Set(input.schedules.filter((s) => s.id).map((s) => s.id as string));

  for (const scheduleId of currentIds) {
    if (!keptIds.has(scheduleId)) {
      await deleteSchedule(medicationId, scheduleId);
    }
  }

  for (const schedule of input.schedules) {
    if (schedule.id) {
      await putSchedule(medicationId, schedule.id, {
        localTime: schedule.localTime,
        daysOfWeek: schedule.daysOfWeek,
        message: schedule.message,
      });
    } else {
      await postSchedule(medicationId, {
        localTime: schedule.localTime,
        daysOfWeek: schedule.daysOfWeek,
        message: schedule.message,
      });
    }
  }
}
