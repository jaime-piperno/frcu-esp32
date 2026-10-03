export interface Device {
  id: string;
  name: string;
  timeZoneId: string;
  isActive: boolean;
  createdAtUtc: string;
  medicationCount: number;
}

export interface Schedule {
  id: string;
  localTime: string;
  daysOfWeek: number;
  isActive: boolean;
}

export interface Medication {
  id: string;
  name: string;
  dosage: string;
  isActive: boolean;
  schedules: Schedule[];
}

export interface Alert {
  medicationId: string;
  name: string;
  dosage: string;
}

export interface NextDose {
  medicationId: string;
  name: string;
  scheduledUtc: string;
}

export interface DeviceState {
  deviceId: string;
  utcNow: string;
  timeZoneId: string;
  alerts: Alert[];
  nextDoses: NextDose[];
  snoozed: boolean;
  missed: boolean;
}

export type DoseEventType = "taken" | "snoozed" | "missed" | "alertsent";

export interface DoseEvent {
  id: string;
  deviceId: string;
  medicationId: string;
  scheduledUtc: string;
  type: DoseEventType;
  occurredAtUtc: string;
  snoozeSeconds: number | null;
  note: string | null;
}

export interface AdherenceSummary {
  date: string;
  taken: number;
  missed: number;
  snoozed: number;
  pending: number;
}

export type TabId = "today" | "inventory" | "log";