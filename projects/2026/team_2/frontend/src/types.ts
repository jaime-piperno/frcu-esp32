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
  message: string | null;
}

export type DoseUnit = "Milligrams" | "Tablets";

export interface Medication {
  id: string;
  name: string;
  dosageAmount: number;
  dosageUnit: DoseUnit;
  isActive: boolean;
  schedules: Schedule[];
}

export interface Alert {
  medicationId: string;
  scheduleId: string;
  scheduledUtc: string;
  name: string;
  dosage: string;
  dosageAmount: number;
  dosageUnit: DoseUnit;
  message: string | null;
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

export type TabId = "today" | "inventory" | "bluetooth" | "log";

export interface BluetoothDiscoveredDevice {
  address: string;
  name: string;
  rssi: number;
  audioCapable: boolean;
}

export interface BluetoothState {
  selectedAddress: string | null;
  selectedName: string | null;
  connectedAddress: string | null;
  connectionStatus: string;
  scanStatus: string;
  error: string | null;
  devices: BluetoothDiscoveredDevice[];
  updatedAtUtc: string | null;
  scanStartedAtUtc: string | null;
  connectionUpdatedAtUtc: string | null;
}
