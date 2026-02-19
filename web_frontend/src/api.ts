import type { Alarm, AlarmCreateInput, VacationMode } from "./types";

const BASE_URL = import.meta.env.VITE_API_BASE_URL ?? "";

async function parseResponse<T>(res: Response): Promise<T> {
  if (!res.ok) {
    const text = await res.text();
    throw new Error(text || `Request failed with ${res.status}`);
  }
  if (res.status === 204) {
    return undefined as T;
  }
  return (await res.json()) as T;
}

export async function listAlarms(): Promise<Alarm[]> {
  const res = await fetch(`${BASE_URL}/api/alarms`);
  return parseResponse<Alarm[]>(res);
}

export async function createAlarm(alarm: AlarmCreateInput): Promise<Alarm> {
  const res = await fetch(`${BASE_URL}/api/alarms`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(alarm)
  });
  return parseResponse<Alarm>(res);
}

export async function updateAlarm(alarm: Alarm): Promise<Alarm> {
  const { id, ...payload } = alarm;
  const res = await fetch(`${BASE_URL}/api/alarms/${encodeURIComponent(id)}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
  return parseResponse<Alarm>(res);
}

export async function deleteAlarm(id: string): Promise<void> {
  const res = await fetch(`${BASE_URL}/api/alarms/${encodeURIComponent(id)}`, {
    method: "DELETE"
  });
  await parseResponse<void>(res);
}

export async function getVacationMode(): Promise<VacationMode> {
  const res = await fetch(`${BASE_URL}/api/vacation-mode`);
  return parseResponse<VacationMode>(res);
}

export async function updateVacationMode(mode: VacationMode): Promise<VacationMode> {
  const res = await fetch(`${BASE_URL}/api/vacation-mode`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(mode)
  });
  return parseResponse<VacationMode>(res);
}
