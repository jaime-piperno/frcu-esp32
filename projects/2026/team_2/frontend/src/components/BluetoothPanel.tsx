import { useCallback, useEffect, useState } from "react";
import { Bluetooth, RefreshCw, Speaker, Unplug } from "lucide-react";
import { fetchBluetoothState, scanBluetooth, selectBluetoothSpeaker } from "../api";
import type { BluetoothState } from "../types";

export function BluetoothPanel({ deviceId }: { deviceId: string }) {
  const [state, setState] = useState<BluetoothState | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  const refresh = useCallback(async () => {
    try {
      const next = await fetchBluetoothState(deviceId);
      setState(next);
      setError("");
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    }
  }, [deviceId]);

  useEffect(() => {
    setState(null);
    void refresh();
    const interval = window.setInterval(() => void refresh(), 2000);
    return () => window.clearInterval(interval);
  }, [refresh]);

  async function scan() {
    setBusy(true);
    setError("");
    try {
      await scanBluetooth(deviceId);
      await refresh();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(false);
    }
  }

  async function select(address: string | null, name: string | null) {
    setBusy(true);
    setError("");
    try {
      await selectBluetoothSpeaker(deviceId, address, name);
      await refresh();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(false);
    }
  }

  const scanTimedOut = state?.scanStatus === "scanning" && state.scanStartedAtUtc
    && Date.now() - new Date(state.scanStartedAtUtc).getTime() > 20_000;
  const reportFresh = state?.connectionUpdatedAtUtc && Date.now() - new Date(state.connectionUpdatedAtUtc).getTime() < 60_000;
  const connected = reportFresh && state?.connectionStatus === "connected"
    && state.connectedAddress === state.selectedAddress;

  return (
    <section className="space-y-4">
      <div className="rounded-2xl border border-slate-200 bg-white p-5 shadow-sm">
        <div className="flex items-start gap-3">
          <div className="rounded-xl bg-teal-50 p-3 text-teal-700"><Speaker className="h-6 w-6" /></div>
          <div>
            <h2 className="text-lg font-bold text-slate-900">Parlante Bluetooth</h2>
            <p className="text-sm text-slate-500">Seleccioná un parlante cercano al ESP32. La elección se conserva al reiniciar el dispositivo.</p>
          </div>
        </div>
        <div className="mt-4 rounded-xl bg-slate-50 p-4 text-sm">
          <p className="font-semibold text-slate-800">{state?.selectedName ?? "Sin parlante seleccionado"}</p>
          {state?.selectedAddress && <p className="mt-1 font-mono text-xs text-slate-500">{state.selectedAddress}</p>}
          <p className={`mt-2 font-medium ${connected ? "text-emerald-700" : "text-amber-700"}`}>
            {connected ? "Conectado" : state?.selectedAddress ? "Guardado; esperando conexión del ESP32" : "Elegí un dispositivo de la lista"}
          </p>
          {state?.error && <p className="mt-1 text-rose-700">{state.error}</p>}
          {state?.selectedAddress && (
            <button type="button" disabled={busy} onClick={() => void select(null, null)}
              className="mt-3 inline-flex items-center gap-1 rounded-lg border border-slate-300 px-3 py-1.5 text-xs font-semibold text-slate-700 hover:bg-white disabled:opacity-50">
              <Unplug className="h-3.5 w-3.5" /> Desvincular
            </button>
          )}
        </div>
      </div>

      <div className="rounded-2xl border border-slate-200 bg-white p-5 shadow-sm">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div>
            <h3 className="font-semibold text-slate-900">Dispositivos cercanos</h3>
            <p className="text-xs text-slate-500">Poné el parlante en modo de emparejamiento antes de buscar.</p>
          </div>
          <button type="button" disabled={busy || state?.scanStatus === "scanning" && !scanTimedOut}
            onClick={() => void scan()}
            className="inline-flex items-center gap-2 rounded-xl bg-teal-600 px-3 py-2 text-sm font-semibold text-white hover:bg-teal-700 disabled:opacity-50">
            <RefreshCw className="h-4 w-4" /> {state?.scanStatus === "scanning" && !scanTimedOut ? "Buscando…" : "Buscar dispositivos"}
          </button>
        </div>
        {error && <p className="mt-3 text-sm text-rose-700">{error}</p>}
        {scanTimedOut && <p className="mt-3 text-sm text-amber-700">El ESP32 no respondió a la búsqueda. Verificá que esté conectado a WiFi y MQTT.</p>}
        {state?.devices.length ? (
          <ul className="mt-4 divide-y divide-slate-100">
            {state.devices.map((device) => (
              <li key={device.address} className="flex items-center justify-between gap-3 py-3">
                <div className="flex min-w-0 items-center gap-3">
                  <Bluetooth className="h-5 w-5 shrink-0 text-teal-600" />
                  <div className="min-w-0">
                    <p className="truncate text-sm font-semibold text-slate-800">{device.name || "Sin nombre"}</p>
                    <p className="font-mono text-xs text-slate-500">{device.address} · señal {device.rssi} dBm</p>
                  </div>
                </div>
                <button type="button" disabled={busy || !device.audioCapable}
                  onClick={() => void select(device.address, device.name || device.address)}
                  className="rounded-lg border border-teal-300 px-3 py-1.5 text-xs font-semibold text-teal-700 hover:bg-teal-50 disabled:opacity-40">
                  {state.selectedAddress === device.address ? "Seleccionado" : device.audioCapable ? "Conectar" : "Sin audio"}
                </button>
              </li>
            ))}
          </ul>
        ) : (
          <p className="mt-4 rounded-xl border border-dashed border-slate-300 p-6 text-center text-sm text-slate-500">
            {state?.scanStatus === "scanning" ? "Buscando dispositivos…" : "Todavía no hay resultados de búsqueda."}
          </p>
        )}
      </div>
    </section>
  );
}
