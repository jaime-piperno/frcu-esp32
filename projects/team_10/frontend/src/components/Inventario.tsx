import { useState } from "react";
import { Pencil, Plus, Trash2, X } from "lucide-react";
import type { FormEvent } from "react";
import { createMedication, deleteMedication, syncMedication } from "../api";
import { DAY_BITS, DAY_NAMES, formatDays, formatLocalTime } from "../dates";
import { pushToast } from "../toast";
import type { Medication } from "../types";

interface InventarioProps {
  medications: Medication[];
  deviceId: string;
  onRefresh: () => void;
}

interface ScheduleDraft {
  /** Existing schedule id when editing; omitted for new schedules. */
  id?: string;
  localTime: string;
  days: boolean[];
}

interface MedicationModalProps {
  deviceId: string;
  medication?: Medication | null;
  onClose: () => void;
  onSaved: () => void;
}

function MedicationModal({ deviceId, medication, onClose, onSaved }: MedicationModalProps) {
  const editing = medication !== null && medication !== undefined;
  const [name, setName] = useState(medication?.name ?? "");
  const [dosage, setDosage] = useState(medication?.dosage ?? "");
  const [schedules, setSchedules] = useState<ScheduleDraft[]>(
    medication
      ? medication.schedules
          .filter((s) => s.isActive)
          .map((s) => ({
            id: s.id,
            localTime: s.localTime.slice(0, 5),
            days: DAY_BITS.map((bit) => (s.daysOfWeek & bit) !== 0),
          }))
      : [{ localTime: "08:00", days: DAY_BITS.map(() => true) }],
  );
  const [error, setError] = useState("");
  const [submitting, setSubmitting] = useState(false);

  function updateSchedule(index: number, patch: Partial<ScheduleDraft>) {
    setSchedules((prev) => prev.map((schedule, i) => (i === index ? { ...schedule, ...patch } : schedule)));
  }

  function toggleDay(index: number, dayIndex: number) {
    setSchedules((prev) =>
      prev.map((schedule, i) =>
        i === index
          ? { ...schedule, days: schedule.days.map((checked, j) => (j === dayIndex ? !checked : checked)) }
          : schedule,
      ),
    );
  }

  function removeSchedule(index: number) {
    setSchedules((prev) => prev.filter((_, i) => i !== index));
  }

  function addSchedule() {
    setSchedules((prev) => [...prev, { localTime: "08:00", days: DAY_BITS.map(() => true) }]);
  }

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    if (!name.trim() || !dosage.trim()) {
      setError("Ingresá nombre y dosis.");
      return;
    }
    if (schedules.length === 0) {
      setError("Agregá al menos un horario.");
      return;
    }
    if (schedules.some((s) => !s.localTime)) {
      setError("Completá la hora de cada horario.");
      return;
    }
    if (schedules.some((s) => s.days.every((checked) => !checked))) {
      setError("Cada horario necesita al menos un día.");
      return;
    }
    const hours = schedules.map((s) => s.localTime);
    if (new Set(hours).size !== hours.length) {
      setError("No podés repetir la misma hora en dos horarios.");
      return;
    }

    const payload = schedules.map((s) => ({
      id: s.id,
      localTime: `${s.localTime}:00`,
      daysOfWeek: s.days.reduce((acc, checked, i) => acc + (checked ? DAY_BITS[i] : 0), 0),
    }));

    setSubmitting(true);
    setError("");
    try {
      if (editing && medication) {
        await syncMedication(medication.id, {
          name: name.trim(),
          dosage: dosage.trim(),
          schedules: payload,
        });
        pushToast("success", "Medicamento actualizado", name.trim());
      } else {
        await createMedication({
          deviceId,
          name: name.trim(),
          dosage: dosage.trim(),
          schedules: payload.map(({ id: _id, ...schedule }) => schedule),
        });
        pushToast("success", "Medicamento agregado", name.trim());
      }
      onSaved();
      onClose();
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      pushToast("error", editing ? "No se pudo actualizar" : "No se pudo crear el medicamento");
      setSubmitting(false);
    }
  }

  return (
    <div className="fixed inset-0 z-40 flex items-center justify-center bg-slate-900/40 p-4">
      <div className="w-full max-w-lg rounded-2xl bg-white p-6 shadow-xl">
        <div className="mb-4 flex items-center justify-between">
          <h3 className="text-lg font-bold text-slate-900">
            {editing ? "Editar medicamento" : "Agregar medicamento"}
          </h3>
          <button onClick={onClose} className="rounded-lg p-1 text-slate-400 hover:text-slate-600" aria-label="Cerrar">
            <X className="h-5 w-5" />
          </button>
        </div>
        <form onSubmit={(e) => void handleSubmit(e)} className="space-y-4">
          <div className="grid grid-cols-2 gap-3">
            <label className="col-span-1 block">
              <span className="mb-1 block text-sm font-medium text-slate-700">Nombre</span>
              <input
                value={name}
                onChange={(e) => setName(e.target.value)}
                placeholder="Ej: Enalapril"
                className="w-full rounded-xl border border-slate-300 px-3 py-2 text-sm focus:border-teal-500 focus:outline-none"
              />
            </label>
            <label className="col-span-1 block">
              <span className="mb-1 block text-sm font-medium text-slate-700">Dosis</span>
              <input
                value={dosage}
                onChange={(e) => setDosage(e.target.value)}
                placeholder="Ej: 10 mg"
                className="w-full rounded-xl border border-slate-300 px-3 py-2 text-sm focus:border-teal-500 focus:outline-none"
              />
            </label>
          </div>

          <div>
            <div className="mb-1 flex items-center justify-between">
              <span className="text-sm font-medium text-slate-700">Horarios</span>
              <span className="text-xs text-slate-400">
                Podés programar varias tomas por día (ej. cada 8 horas)
              </span>
            </div>
            <div className="space-y-3">
              {schedules.map((schedule, index) => (
                <div key={index} className="rounded-xl border border-slate-200 bg-slate-50/60 p-3">
                  <div className="flex items-start gap-2">
                    <label className="block">
                      <span className="mb-1 block text-xs font-medium text-slate-500">Hora</span>
                      <input
                        type="time"
                        value={schedule.localTime}
                        onChange={(e) => updateSchedule(index, { localTime: e.target.value })}
                        className="w-28 rounded-lg border border-slate-300 px-2 py-1.5 text-sm focus:border-teal-500 focus:outline-none"
                      />
                    </label>
                    <div className="flex-1">
                      <span className="mb-1 block text-xs font-medium text-slate-500">Días</span>
                      <div className="flex flex-wrap gap-1">
                        {DAY_NAMES.map((dayName, dayIndex) => (
                          <label
                            key={dayName}
                            className={`cursor-pointer rounded-md border px-2 py-1 text-[11px] font-medium transition ${
                              schedule.days[dayIndex]
                                ? "border-teal-500 bg-teal-50 text-teal-700"
                                : "border-slate-300 text-slate-500"
                            }`}
                          >
                            <input
                              type="checkbox"
                              className="sr-only"
                              checked={schedule.days[dayIndex]}
                              onChange={() => toggleDay(index, dayIndex)}
                            />
                            {dayName}
                          </label>
                        ))}
                      </div>
                    </div>
                    <button
                      type="button"
                      onClick={() => removeSchedule(index)}
                      disabled={schedules.length <= 1}
                      className="mt-5 rounded-lg p-1 text-slate-400 transition hover:text-rose-600 disabled:cursor-not-allowed disabled:opacity-30"
                      aria-label="Quitar horario"
                    >
                      <X className="h-4 w-4" />
                    </button>
                  </div>
                </div>
              ))}
            </div>
            <button
              type="button"
              onClick={addSchedule}
              className="mt-3 inline-flex items-center gap-1 rounded-lg border border-dashed border-teal-400 px-3 py-1.5 text-xs font-semibold text-teal-700 transition hover:bg-teal-50"
            >
              <Plus className="h-3.5 w-3.5" aria-hidden="true" />
              Agregar horario
            </button>
          </div>

          {error && <p className="text-sm text-rose-600">{error}</p>}
          <button
            type="submit"
            disabled={submitting}
            className="w-full rounded-xl bg-teal-600 px-4 py-2.5 text-sm font-semibold text-white transition hover:bg-teal-700 disabled:opacity-50"
          >
            {submitting ? "Guardando…" : editing ? "Guardar cambios" : "Crear medicamento"}
          </button>
        </form>
      </div>
    </div>
  );
}

interface DeleteModalProps {
  medication: Medication;
  onClose: () => void;
  onDeleted: () => void;
}

function DeleteModal({ medication, onClose, onDeleted }: DeleteModalProps) {
  const [submitting, setSubmitting] = useState(false);

  async function handleDelete() {
    setSubmitting(true);
    try {
      await deleteMedication(medication.id);
      pushToast("success", "Medicamento eliminado", medication.name);
      onDeleted();
      onClose();
    } catch (err) {
      pushToast("error", "No se pudo eliminar", err instanceof Error ? err.message : String(err));
      setSubmitting(false);
    }
  }

  return (
    <div className="fixed inset-0 z-40 flex items-center justify-center bg-slate-900/40 p-4">
      <div className="w-full max-w-sm rounded-2xl bg-white p-6 shadow-xl">
        <div className="mb-4 flex items-center gap-2">
          <div className="flex h-10 w-10 items-center justify-center rounded-xl bg-rose-100 text-rose-600">
            <Trash2 className="h-5 w-5" aria-hidden="true" />
          </div>
          <h3 className="text-lg font-bold text-slate-900">Eliminar medicamento</h3>
        </div>
        <p className="text-sm text-slate-600">
          Vas a dar de baja <strong>{medication.name} {medication.dosage}</strong> y se eliminarán
          sus horarios de dosificación.
        </p>
        <div className="mt-5 flex gap-2">
          <button
            onClick={onClose}
            disabled={submitting}
            className="flex-1 rounded-xl border border-slate-300 px-4 py-2 text-sm font-semibold text-slate-700 transition hover:bg-slate-50"
          >
            Cancelar
          </button>
          <button
            onClick={() => void handleDelete()}
            disabled={submitting}
            className="flex-1 rounded-xl bg-rose-600 px-4 py-2 text-sm font-semibold text-white transition hover:bg-rose-700 disabled:opacity-50"
          >
            {submitting ? "Eliminando…" : "Eliminar"}
          </button>
        </div>
      </div>
    </div>
  );
}

export function Inventario({ medications, deviceId, onRefresh }: InventarioProps) {
  const [modalTarget, setModalTarget] = useState<Medication | null | "new">(null);
  const [deleteTarget, setDeleteTarget] = useState<Medication | null>(null);

  return (
    <div className="space-y-4">
      <div className="flex justify-end">
        <button
          onClick={() => setModalTarget("new")}
          className="inline-flex items-center gap-1.5 rounded-xl bg-teal-600 px-3 py-2 text-sm font-semibold text-white transition hover:bg-teal-700"
        >
          <Plus className="h-4 w-4" aria-hidden="true" />
          Agregar Medicamento
        </button>
      </div>

      {medications.length === 0 ? (
        <div className="rounded-2xl border border-dashed border-slate-300 bg-white p-8 text-center text-sm text-slate-500">
          No hay medicamentos para este dispositivo.
        </div>
      ) : (
        <ul className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
          {medications.map((medication) => {
            const activeSchedules = medication.schedules.filter((s) => s.isActive);
            return (
              <li
                key={medication.id}
                className="flex flex-col rounded-2xl border border-slate-200/80 bg-white p-4 shadow-sm"
              >
                <h4 className="font-semibold text-slate-900">{medication.name}</h4>
                <p className="text-sm text-slate-500">{medication.dosage}</p>

                <ul className="mt-3 space-y-1.5">
                  {activeSchedules.length === 0 ? (
                    <li className="text-xs text-slate-400">Sin horarios</li>
                  ) : (
                    activeSchedules.map((schedule) => (
                      <li key={schedule.id} className="flex items-center gap-2">
                        <span className="flex min-w-14 items-center justify-center rounded-md bg-teal-50 px-1.5 py-1 text-xs font-bold tabular-nums text-teal-700">
                          {formatLocalTime(schedule.localTime)}
                        </span>
                        <span className="text-xs text-slate-500">
                          {formatDays(schedule.daysOfWeek)}
                        </span>
                      </li>
                    ))
                  )}
                </ul>

                <div className="mt-4 flex gap-2">
                  <button
                    onClick={() => setModalTarget(medication)}
                    className="inline-flex items-center justify-center rounded-xl bg-slate-100 px-3 py-2 text-slate-600 transition hover:bg-slate-200"
                    aria-label={`Editar ${medication.name}`}
                    title="Editar medicamento"
                  >
                    <Pencil className="h-4 w-4" aria-hidden="true" />
                  </button>
                  <button
                    onClick={() => setDeleteTarget(medication)}
                    className="inline-flex items-center justify-center rounded-xl bg-rose-50 px-3 py-2 text-rose-600 transition hover:bg-rose-100"
                    aria-label={`Eliminar ${medication.name}`}
                    title="Eliminar medicamento"
                  >
                    <Trash2 className="h-4 w-4" aria-hidden="true" />
                  </button>
                </div>
              </li>
            );
          })}
        </ul>
      )}

      {modalTarget !== null && (
        <MedicationModal
          deviceId={deviceId}
          medication={modalTarget === "new" ? null : modalTarget}
          onClose={() => setModalTarget(null)}
          onSaved={onRefresh}
        />
      )}
      {deleteTarget && (
        <DeleteModal
          medication={deleteTarget}
          onClose={() => setDeleteTarget(null)}
          onDeleted={onRefresh}
        />
      )}
    </div>
  );
}