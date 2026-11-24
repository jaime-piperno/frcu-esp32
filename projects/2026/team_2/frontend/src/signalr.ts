import { HubConnection, HubConnectionBuilder } from "@microsoft/signalr";
import { API_BASE } from "./api";
import type { DeviceState, DoseEvent } from "./types";

export interface AdherencePayload {
  deviceId: string;
  event: DoseEvent;
}

export interface StateChangedPayload {
  deviceId: string;
  state: DeviceState;
}

export type ConnectionStatus = "connecting" | "connected" | "reconnecting" | "disconnected";

type StatusListener = (status: ConnectionStatus) => void;
type EventListener = (payload: AdherencePayload) => void;
type StateListener = (payload: StateChangedPayload) => void;

let connection: HubConnection | null = null;
const statusListeners = new Set<StatusListener>();
const eventListeners = new Set<EventListener>();
const stateListeners = new Set<StateListener>();

function notifyStatus(status: ConnectionStatus): void {
  statusListeners.forEach((listener) => listener(status));
}

export function onStatusChange(listener: StatusListener): () => void {
  statusListeners.add(listener);
  return () => statusListeners.delete(listener);
}

export function onAdherenceEvent(listener: EventListener): () => void {
  eventListeners.add(listener);
  return () => eventListeners.delete(listener);
}

export function onStateChanged(listener: StateListener): () => void {
  stateListeners.add(listener);
  return () => stateListeners.delete(listener);
}

export function connectAdherence(): Promise<void> {
  if (connection) return connection.start().then(() => notifyStatus("connected"));

  connection = new HubConnectionBuilder()
    .withUrl(`${API_BASE}/hubs/adherence`)
    .withAutomaticReconnect()
    .build();

  connection.on("AdherenceEvent", (payload: AdherencePayload) => {
    eventListeners.forEach((listener) => listener(payload));
  });
  connection.on("StateChanged", (payload: StateChangedPayload) => {
    stateListeners.forEach((listener) => listener(payload));
  });
  connection.onreconnecting(() => notifyStatus("reconnecting"));
  connection.onreconnected(() => notifyStatus("connected"));
  connection.onclose(() => notifyStatus("disconnected"));

  return connection.start().then(() => notifyStatus("connected"));
}