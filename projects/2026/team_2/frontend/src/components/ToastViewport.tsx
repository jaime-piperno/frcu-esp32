import { AlertTriangle, CheckCircle2, Info, X, XCircle } from "lucide-react";
import { dismissToast, useToasts } from "../toast";

const ICONS = {
  success: CheckCircle2,
  info: Info,
  warning: AlertTriangle,
  error: XCircle,
};

const TONE = {
  success: "text-emerald-600",
  info: "text-sky-600",
  warning: "text-amber-600",
  error: "text-rose-600",
};

export function ToastViewport() {
  const toasts = useToasts();
  if (toasts.length === 0) return null;

  return (
    <div className="pointer-events-none fixed right-4 top-20 z-50 flex w-80 flex-col gap-2">
      {toasts.map((toast) => {
        const Icon = ICONS[toast.kind];
        return (
          <div
            key={toast.id}
            className="pointer-events-auto flex items-start gap-2 rounded-xl border border-slate-200 bg-white p-3 shadow-lg"
            role="status"
          >
            <Icon className={`mt-0.5 h-4 w-4 shrink-0 ${TONE[toast.kind]}`} aria-hidden="true" />
            <div className="min-w-0 flex-1">
              <p className="text-sm font-semibold text-slate-900">{toast.title}</p>
              {toast.message && <p className="text-xs text-slate-500">{toast.message}</p>}
            </div>
            <button
              onClick={() => dismissToast(toast.id)}
              className="rounded p-0.5 text-slate-400 hover:text-slate-600"
              aria-label="Cerrar notificación"
            >
              <X className="h-3.5 w-3.5" />
            </button>
          </div>
        );
      })}
    </div>
  );
}