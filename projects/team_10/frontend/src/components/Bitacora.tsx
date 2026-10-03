import { useMemo, useState } from "react";
import { AlarmClock, BellRing, ChevronLeft, ChevronRight, CircleCheck, CircleX } from "lucide-react";
import type { LucideIcon } from "lucide-react";
import type { DoseEvent, Medication } from "../types";
import {
  dateKeyOf,
  dayKeyForOffset,
  dayLabelForKey,
  dayNumberForKey,
  formatTime,
} from "../dates";

interface BitacoraProps {
  events: DoseEvent[];
  medications: Medication[];
  timeZoneId: string;
}

const ACTION_UI: Record<
  DoseEvent["type"],
  { label: string; icon: LucideIcon; iconClass: string }
> = {
  taken: {
    label: "Toma confirmada",
    icon: CircleCheck,
    iconClass: "bg-emerald-100 text-emerald-700",
  },
  snoozed: {
    label: "Alarma pospuesta",
    icon: AlarmClock,
    iconClass: "bg-amber-100 text-amber-700",
  },
  missed: {
    label: "Dosis no tomada",
    icon: CircleX,
    iconClass: "bg-rose-100 text-rose-700",
  },
  alertsent: {
    label: "Alerta enviada",
    icon: BellRing,
    iconClass: "bg-sky-100 text-sky-700",
  },
};

export function Bitacora({ events, medications, timeZoneId }: BitacoraProps) {
  const [dayOffset, setDayOffset] = useState(0);

  const medicationName = useMemo(() => {
    const map = new Map<string, string>();
    for (const medication of medications) map.set(medication.id, medication.name);
    return map;
  }, [medications]);

  const dayKey = dayKeyForOffset(dayOffset, timeZoneId);
  const dayEvents = useMemo(
    () =>
      events
        .filter((event) => dateKeyOf(event.occurredAtUtc, timeZoneId) === dayKey)
        .sort((a, b) => b.occurredAtUtc.localeCompare(a.occurredAtUtc)),
    [events, dayKey, timeZoneId],
  );

  const relativeLabel =
    dayOffset === 0 ? "Hoy" : dayOffset === -1 ? "Ayer" : dayOffset === 1 ? "Mañana" : null;

  const taken = dayEvents.filter((event) => event.type === "taken").length;
  const snoozed = dayEvents.filter((event) => event.type === "snoozed").length;

  return (
    <section>
      <div className="mb-3 flex items-center justify-between gap-2">
        <h3 className="font-semibold text-slate-900">Registro del día</h3>
        <div className="flex items-center gap-1">
          <button
            type="button"
            onClick={() => setDayOffset((offset) => offset - 1)}
            aria-label="Día anterior"
            className="flex h-8 w-8 items-center justify-center rounded-full text-slate-500 transition-colors hover:bg-slate-100 hover:text-slate-800 active:scale-95"
          >
            <ChevronLeft className="h-4 w-4" aria-hidden="true" />
          </button>
          {dayOffset !== 0 && (
            <button
              type="button"
              onClick={() => setDayOffset(0)}
              className="rounded-full bg-teal-50 px-3 py-1 text-xs font-semibold text-teal-700 transition-colors hover:bg-teal-100 active:scale-95"
            >
              Hoy
            </button>
          )}
          <button
            type="button"
            onClick={() => setDayOffset((offset) => offset + 1)}
            aria-label="Día siguiente"
            className="flex h-8 w-8 items-center justify-center rounded-full text-slate-500 transition-colors hover:bg-slate-100 hover:text-slate-800 active:scale-95"
          >
            <ChevronRight className="h-4 w-4" aria-hidden="true" />
          </button>
        </div>
      </div>

      <div className="rounded-2xl border border-slate-200/80 bg-white p-5 shadow-sm">
        <div className="mb-4 flex items-baseline justify-between gap-2 border-b border-slate-100 pb-3">
          <div className="flex items-baseline gap-2">
            <p className="text-xl font-bold text-slate-900">
              {dayLabelForKey(dayKey)} {dayNumberForKey(dayKey)}
            </p>
            {relativeLabel && (
              <span className="text-sm font-semibold text-teal-600">{relativeLabel}</span>
            )}
          </div>
          <span className="text-xs text-slate-400">
            {taken > 0 && <span className="font-medium text-emerald-600">{taken} tomadas</span>}
            {taken > 0 && snoozed > 0 && " · "}
            {snoozed > 0 && (
              <span className="font-medium text-amber-600">{snoozed} pospuestas</span>
            )}
            {taken === 0 && snoozed === 0 && `${dayEvents.length} eventos`}
          </span>
        </div>

        {dayEvents.length === 0 ? (
          <div className="rounded-xl border border-dashed border-slate-300 bg-slate-50/60 px-4 py-8 text-center text-sm text-slate-500">
            Sin eventos registrados para este día.
          </div>
        ) : (
          <ol className="relative">
            {dayEvents.map((event, index) => {
              const action = ACTION_UI[event.type];
              const Icon = action.icon;
              const name = medicationName.get(event.medicationId);
              const isLast = index === dayEvents.length - 1;
              const snoozeMinutes = event.snoozeSeconds
                ? Math.round(event.snoozeSeconds / 60)
                : null;

              return (
                <li key={event.id} className="relative flex gap-3 pb-3 pl-10">
                  {!isLast && (
                    <span
                      aria-hidden="true"
                      className="absolute bottom-0 left-4 top-8 w-px bg-slate-200"
                    />
                  )}
                  <div
                    className={`absolute left-0 top-0 flex h-8 w-8 items-center justify-center rounded-full shadow-sm ${action.iconClass}`}
                  >
                    <Icon className="h-4 w-4" aria-hidden="true" />
                  </div>
                  <div className="min-w-0 flex-1 rounded-xl px-3 py-1">
                    <div className="flex items-baseline justify-between gap-2">
                      <p className="truncate text-sm font-semibold text-slate-800">
                        {name ?? "Medicamento"}
                        <span className="ml-1.5 font-normal text-slate-500">
                          · {action.label}
                        </span>
                      </p>
                      <span className="shrink-0 text-xs font-medium tabular-nums text-slate-400">
                        {formatTime(event.occurredAtUtc, timeZoneId)}
                      </span>
                    </div>
                    {snoozeMinutes !== null && (
                      <p className="mt-0.5 text-xs text-slate-400">Posponer {snoozeMinutes} min</p>
                    )}
                    {event.note && (
                      <p className="mt-0.5 text-xs text-slate-400">{event.note}</p>
                    )}
                  </div>
                </li>
              );
            })}
          </ol>
        )}
      </div>
    </section>
  );
}