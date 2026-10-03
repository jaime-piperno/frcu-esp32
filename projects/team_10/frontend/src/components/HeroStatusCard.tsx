import { Bell, Clock, Users } from "lucide-react";
import type { ConnectionStatus } from "../signalr";
import type { AdherenceSummary, DeviceState } from "../types";
import { formatNextDose } from "../dates";

interface HeroStatusCardProps {
  connectionStatus: ConnectionStatus;
  state: DeviceState | null;
  adherence: AdherenceSummary | null;
  timeZoneId: string;
  deviceName?: string;
}

export function HeroStatusCard({
  connectionStatus,
  state,
  adherence,
  timeZoneId,
  deviceName,
}: HeroStatusCardProps) {
  const total = adherence
    ? adherence.taken + adherence.missed + adherence.snoozed + adherence.pending
    : 0;
  const adherencePercent =
    adherence && total > 0 ? Math.round((adherence.taken / total) * 100) : 0;
  const nextDose = state?.nextDoses?.[0] ?? null;
  const online = connectionStatus === "connected" || connectionStatus === "reconnecting";

  return (
    <section className="rounded-2xl border border-slate-200/80 bg-white p-5 shadow-sm">
      <div className="flex flex-wrap items-start justify-between gap-4">
        <div className="flex items-start gap-3">
          <div className="flex h-11 w-11 items-center justify-center rounded-xl bg-teal-50 text-teal-700">
            <Users className="h-5 w-5" aria-hidden="true" />
          </div>
          <div>
            <h2 className="text-lg font-bold text-slate-900">
              {deviceName ?? "Dispensador ESP32"}
            </h2>
            <div className="mt-1.5 flex flex-wrap items-center gap-2">
              <span
                className={`inline-flex items-center gap-1.5 rounded-full px-2.5 py-0.5 text-xs font-semibold ${
                  online ? "bg-emerald-50 text-emerald-700" : "bg-rose-50 text-rose-700"
                }`}
              >
                <span className={`h-1.5 w-1.5 rounded-full ${online ? "bg-emerald-500" : "bg-rose-500"}`} />
                ESP32 {online ? "en línea" : "sin conexión"}
              </span>
            </div>
          </div>
        </div>

        <div className="grid grid-cols-2 gap-3 text-sm">
          <div className="rounded-xl bg-slate-50 px-3 py-2">
            <p className="text-xs text-slate-500">Próxima alarma</p>
            <p className="flex items-center gap-1 font-bold text-slate-900">
              <Clock className="h-3.5 w-3.5 text-teal-600" aria-hidden="true" />
              {nextDose ? formatNextDose(nextDose.scheduledUtc, timeZoneId) : "Sin dosis"}
            </p>
          </div>
          <div className="rounded-xl bg-slate-50 px-3 py-2">
            <p className="text-xs text-slate-500">Alarma sonora</p>
            <p className="flex items-center gap-1 font-bold text-slate-900">
              <Bell className="h-3.5 w-3.5 text-teal-600" aria-hidden="true" />
              {state?.snoozed ? "Pospuesta" : "Activa"}
            </p>
          </div>
        </div>
      </div>

      <div className="mt-5 border-t border-slate-100 pt-4">
        <div className="flex items-center justify-between">
          <h3 className="text-sm font-semibold text-slate-900">Adherencia del día</h3>
          <span className="text-sm font-bold text-teal-700">
            {adherence ? `${adherence.taken}/${total} administradas` : "Sin datos"}
          </span>
        </div>
        <div className="mt-2 h-2.5 w-full overflow-hidden rounded-full bg-slate-100">
          <div
            className="h-full rounded-full bg-teal-600 transition-all"
            style={{ width: `${adherencePercent}%` }}
          />
        </div>
        <p className="mt-2 text-xs text-slate-500">
          {adherence
            ? `${adherencePercent}% de las administraciones programadas fueron confirmadas a tiempo.`
            : "Sin datos de adherencia para hoy."}
        </p>
      </div>
    </section>
  );
}