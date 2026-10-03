import { useCallback, useEffect, useState } from "react";
import { fetchAdherence, fetchDevices, fetchDeviceState, fetchEvents, fetchMedications } from "./api";
import { connectAdherence, onAdherenceEvent, onStateChanged, onStatusChange } from "./signalr";
import type { ConnectionStatus } from "./signalr";
import type { AdherenceSummary, Device, DeviceState, DoseEvent, Medication, TabId } from "./types";
import { Header } from "./components/Header";
import { HeroStatusCard } from "./components/HeroStatusCard";
import { LineaDeHoy } from "./components/LineaDeHoy";
import { Inventario } from "./components/Inventario";
import { Bitacora } from "./components/Bitacora";
import { ToastViewport } from "./components/ToastViewport";
import { pushToast } from "./toast";

export default function App() {
  const [activeTab, setActiveTab] = useState<TabId>("today");

  const [devices, setDevices] = useState<Device[]>([]);
  const [devicesError, setDevicesError] = useState("");
  const [selectedDeviceId, setSelectedDeviceId] = useState<string | null>(null);

  const [connectionStatus, setConnectionStatus] = useState<ConnectionStatus>("connecting");

  const [state, setState] = useState<DeviceState | null>(null);
  const [stateStatus, setStateStatus] = useState<"idle" | "loading" | "error">("idle");
  const [stateError, setStateError] = useState("");

  const [medications, setMedications] = useState<Medication[]>([]);
  const [medicationsStatus, setMedicationsStatus] = useState<"idle" | "loading" | "error">("idle");
  const [medicationsError, setMedicationsError] = useState("");

  const [events, setEvents] = useState<DoseEvent[]>([]);
  const [eventsStatus, setEventsStatus] = useState<"idle" | "loading" | "error">("idle");

  const [adherence, setAdherence] = useState<AdherenceSummary | null>(null);

  const selectedDevice = devices.find((device) => device.id === selectedDeviceId) ?? null;
  const timeZoneId = selectedDevice?.timeZoneId ?? "America/Argentina/Buenos_Aires";

  useEffect(() => {
    const unsubscribeStatus = onStatusChange(setConnectionStatus);
    connectAdherence().catch(() => setConnectionStatus("disconnected"));
    return unsubscribeStatus;
  }, []);

  useEffect(() => {
    fetchDevices()
      .then((list) => {
        setDevices(list);
        setSelectedDeviceId((current) => current ?? list[0]?.id ?? null);
      })
      .catch((err) => setDevicesError(err instanceof Error ? err.message : String(err)));
  }, []);

  const loadState = useCallback(async () => {
    if (!selectedDeviceId) return;
    setStateStatus("loading");
    try {
      setState(await fetchDeviceState(selectedDeviceId));
      setStateStatus("idle");
    } catch (err) {
      setStateStatus("error");
      setStateError(err instanceof Error ? err.message : String(err));
    }
  }, [selectedDeviceId]);

  useEffect(() => {
    void loadState();
  }, [loadState]);

  const loadMedications = useCallback(async () => {
    if (!selectedDeviceId) return;
    setMedicationsStatus("loading");
    try {
      setMedications(await fetchMedications(selectedDeviceId));
      setMedicationsStatus("idle");
    } catch (err) {
      setMedicationsStatus("error");
      setMedicationsError(err instanceof Error ? err.message : String(err));
    }
  }, [selectedDeviceId]);

  useEffect(() => {
    void loadMedications();
  }, [loadMedications]);

  const loadEvents = useCallback(async () => {
    if (!selectedDeviceId) return;
    setEventsStatus("loading");
    try {
      const list = await fetchEvents(selectedDeviceId);
      setEvents(list);
      setEventsStatus("idle");
    } catch {
      setEventsStatus("error");
    }
  }, [selectedDeviceId]);

  useEffect(() => {
    void loadEvents();
  }, [loadEvents]);

  const loadAdherence = useCallback(async () => {
    if (!selectedDeviceId) return;
    try {
      setAdherence(await fetchAdherence(selectedDeviceId));
    } catch {
      setAdherence(null);
    }
  }, [selectedDeviceId]);

  useEffect(() => {
    void loadAdherence();
  }, [loadAdherence]);

  useEffect(() => {
    if (!selectedDeviceId) return;
    return onAdherenceEvent((payload) => {
      if (payload.deviceId !== selectedDeviceId) return;
      const ev = payload.event;
      if (!ev || typeof ev.id !== "string" || !ev.occurredAtUtc || !ev.type) return;
      setEvents((prev) => {
        const next = [ev, ...prev.filter((event) => event.id !== ev.id)];
        return next.slice(0, 200);
      });
      void loadAdherence();
    });
  }, [selectedDeviceId, loadAdherence]);

  useEffect(() => {
    if (!selectedDeviceId) return;
    return onStateChanged((payload) => {
      if (payload.deviceId === selectedDeviceId) {
        setState(payload.state);
        setStateStatus("idle");
        setStateError("");
        if (payload.state.alerts.length > 0) {
          pushToast("warning", "Alarma activa", "Hay dosis pendientes de tomar");
        }
      }
    });
  }, [selectedDeviceId]);

  return (
    <div className="min-h-screen bg-slate-50">
      <Header activeTab={activeTab} onTabChange={setActiveTab} connectionStatus={connectionStatus} />

      <div className="mx-auto max-w-6xl px-4 sm:px-6 py-6">
        <div className="space-y-4">
          {activeTab === "today" && (
            <>
              {devicesError && (
                <p className="rounded-xl bg-rose-50 px-4 py-3 text-sm text-rose-600">
                  No se pudieron cargar los dispositivos: {devicesError}
                </p>
              )}
              <HeroStatusCard
                connectionStatus={connectionStatus}
                state={state}
                adherence={adherence}
                timeZoneId={timeZoneId}
                deviceName={selectedDevice?.name}
              />
              {stateStatus === "error" && (
                <p className="rounded-xl bg-rose-50 px-3 py-2 text-xs text-rose-600">
                  No se pudo obtener el estado: {stateError}
                </p>
              )}
              {medicationsStatus === "error" && (
                <p className="rounded-xl bg-rose-50 px-3 py-2 text-xs text-rose-600">
                  No se pudieron cargar las medicaciones: {medicationsError}
                </p>
              )}
              {eventsStatus === "error" && (
                <p className="rounded-xl bg-rose-50 px-3 py-2 text-xs text-rose-600">
                  No se pudieron cargar los eventos.
                </p>
              )}
              <LineaDeHoy
                medications={medications}
                events={events}
                timeZoneId={timeZoneId}
              />
            </>
          )}
          {activeTab === "inventory" && (
            <Inventario
              medications={medications}
              deviceId={selectedDeviceId ?? ""}
              onRefresh={() => void loadMedications()}
            />
          )}
          {activeTab === "log" && (
            <Bitacora events={events} medications={medications} timeZoneId={timeZoneId} />
          )}
        </div>

        <footer className="mt-8 border-t border-slate-200 pt-4 text-center text-xs text-slate-400">
          {selectedDevice
            ? `Dispositivo: ${selectedDevice.name} · Zona horaria: ${selectedDevice.timeZoneId}`
            : "Sin dispositivo seleccionado"}
        </footer>
      </div>

      <ToastViewport />
    </div>
  );
}