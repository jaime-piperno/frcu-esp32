import { useState } from "react";
import { ChevronLeft, ChevronRight } from "lucide-react";
import type { DoseEvent, Medication } from "../types";
import {
  dateKeyOf,
  dayBitForKey,
  dayKeyForOffset,
  dayLabelForKey,
  dayNumberForKey,
  formatLocalTime,
  formatTime,
} from "../dates";

interface LineaDeHoyProps {
  medications: Medication[];
  events: DoseEvent[];
  timeZoneId: string;
}

interface DoseRow {
  medication: Medication;
  scheduleId: string;
  localTime: string;
  daysOfWeek: number;
  status: "taken" | "snoozed" | "missed" | null;
}

/**
 * Derives the dose status exclusively from real backend events. A real event
 * matches a scheduled row when it belongs to the same medication, happened on
 * the given calendar day, and its scheduledUtc time equals the schedule time.
 */
function statusOf(
  row: Omit<DoseRow, "status">,
  events: DoseEvent[],
  timeZoneId: string,
  dayKey: string,
): DoseRow["status"] {
  const matching = events.filter(
    (event) =>
      event.medicationId === row.medication.id &&
      dateKeyOf(event.occurredAtUtc, timeZoneId) === dayKey &&
      formatTime(event.scheduledUtc, timeZoneId) === formatLocalTime(row.localTime),
  );
  if (matching.some((e) => e.type === "taken")) return "taken";
  if (matching.some((e) => e.type === "snoozed")) return "snoozed";
  if (matching.some((e) => e.type === "missed")) return "missed";
  return null;
}

const STATUS_UI: Record<NonNullable<DoseRow["status"]>, { label: string; className: string; dot: string }> = {
  taken: { label: "Tomada", className: "bg-emerald-50 text-emerald-700", dot: "bg-emerald-500" },
  snoozed: { label: "Pospuesta", className: "bg-amber-50 text-amber-700", dot: "bg-amber-500" },
  missed: { label: "Perdida", className: "bg-rose-50 text-rose-700", dot: "bg-rose-500" },
};

const PENDING_DOT = "bg-teal-400";

/** Builds the ordered administration rows for a single calendar day. */
function buildDay(
  medications: Medication[],
  events: DoseEvent[],
  timeZoneId: string,
  dayKey: string,
): DoseRow[] {
  const dayBit = dayBitForKey(dayKey);
  return medications
    .filter((m) => m.isActive)
    .flatMap<Omit<DoseRow, "status">>((medication) =>
      medication.schedules
        .filter((s) => s.isActive && (s.daysOfWeek & dayBit) !== 0)
        .map((schedule) => ({
          medication,
          scheduleId: schedule.id,
          localTime: schedule.localTime.slice(0, 5),
          daysOfWeek: schedule.daysOfWeek,
        })),
    )
    .sort((a, b) => a.localTime.localeCompare(b.localTime))
    .map((row) => ({ ...row, status: statusOf(row, events, timeZoneId, dayKey) }));
}

export function LineaDeHoy({ medications, events, timeZoneId }: LineaDeHoyProps) {
  const [dayOffset, setDayOffset] = useState(0);

  const dayKey = dayKeyForOffset(dayOffset, timeZoneId);
  const rows = buildDay(medications, events, timeZoneId, dayKey);

  const relativeLabel =
    dayOffset === 0 ? "Hoy" : dayOffset === -1 ? "Ayer" : dayOffset === 1 ? "Mañana" : null;

  return (
    <section>
      <div className="mb-3 flex items-center justify-between gap-2">
        <h3 className="font-semibold text-slate-900">Agenda del día</h3>
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
            {relativeLabel && <span className="text-sm font-semibold text-teal-600">{relativeLabel}</span>}
          </div>
          <span className="text-xs text-slate-400">
            {rows.length === 0
              ? "Sin administraciones"
              : `${rows.length} ${rows.length === 1 ? "administración" : "administraciones"}`}
          </span>
        </div>

        {rows.length === 0 ? (
          <div className="rounded-xl border border-dashed border-slate-300 bg-slate-50/60 px-4 py-8 text-center text-sm text-slate-500">
            Sin administraciones programadas para este día.
          </div>
        ) : (
          <ol className="relative space-y-3">
            <span
              aria-hidden="true"
              className="absolute bottom-3 left-2 top-3 w-px bg-slate-200"
            />
            {rows.map((row) => {
              const statusUi = row.status ? STATUS_UI[row.status] : null;
              return (
                <li key={row.scheduleId} className="relative flex items-start gap-3 pl-6">
                  <span
                    aria-hidden="true"
                    className={`absolute left-[3px] top-2.5 h-2.5 w-2.5 rounded-full border-2 border-white shadow-sm ${
                      statusUi ? statusUi.dot : PENDING_DOT
                    }`}
                  />
                  <div className="w-14 shrink-0 pt-1 text-right">
                    <span className="text-sm font-bold tabular-nums text-teal-700">
                      {formatLocalTime(row.localTime)}
                    </span>
                  </div>
                  <div className="min-w-0 flex-1 rounded-xl border border-slate-200/80 bg-slate-50 px-3 py-2">
                    <div className="flex flex-wrap items-center justify-between gap-2">
                      <p className="truncate text-sm font-semibold text-slate-800">
                        {row.medication.name}
                        {row.medication.dosage && (
                          <span className="ml-1.5 font-normal text-slate-500">
                            {row.medication.dosage}
                          </span>
                        )}
                      </p>
                      {statusUi && (
                        <span
                          className={`shrink-0 rounded-full px-2 py-0.5 text-[10px] font-semibold ${statusUi.className}`}
                        >
                          {statusUi.label}
                        </span>
                      )}
                    </div>
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