import { HeartPulse } from "lucide-react";
import type { ConnectionStatus } from "../signalr";
import type { TabId } from "../types";

interface HeaderProps {
  activeTab: TabId;
  onTabChange: (tab: TabId) => void;
  connectionStatus: ConnectionStatus;
}

const TABS: { id: TabId; label: string }[] = [
  { id: "today", label: "Línea de Hoy" },
  { id: "inventory", label: "Medicamentos" },
  { id: "log", label: "Registro / Bitácora" },
];

const CONNECTION_UI: Record<ConnectionStatus, { label: string; dotClass: string; textClass: string }> = {
  connecting: { label: "Conectando…", dotClass: "bg-amber-500", textClass: "text-amber-700" },
  connected: { label: "En línea", dotClass: "bg-emerald-500", textClass: "text-emerald-700" },
  reconnecting: { label: "Reconectando…", dotClass: "bg-amber-500", textClass: "text-amber-700" },
  disconnected: { label: "Sin conexión", dotClass: "bg-rose-500", textClass: "text-rose-700" },
};

export function Header({ activeTab, onTabChange, connectionStatus }: HeaderProps) {
  const connection = CONNECTION_UI[connectionStatus];

  return (
    <header className="border-b border-slate-200/80 bg-white/90 backdrop-blur sticky top-0 z-30">
      <div className="mx-auto max-w-6xl px-4 sm:px-6">
        <div className="flex flex-wrap items-center gap-3 py-3">
          <div className="flex items-center gap-2.5">
            <div className="flex h-10 w-10 items-center justify-center rounded-xl bg-teal-600 text-white shadow-sm">
              <HeartPulse className="h-5 w-5" aria-hidden="true" />
            </div>
            <p className="text-base font-bold text-slate-900">CuidaMed IoT</p>
          </div>

          <nav className="order-3 w-full sm:order-2 sm:w-auto sm:flex-1 flex gap-1 rounded-xl bg-slate-100/80 p-1 overflow-x-auto" aria-label="Navegación principal">
            {TABS.map((tab) => (
              <button
                key={tab.id}
                onClick={() => onTabChange(tab.id)}
                className={`whitespace-nowrap rounded-lg px-3 py-1.5 text-sm font-medium transition-colors ${
                  activeTab === tab.id
                    ? "bg-white text-teal-700 shadow-sm"
                    : "text-slate-600 hover:text-slate-900"
                }`}
              >
                {tab.label}
              </button>
            ))}
          </nav>

          <div className="order-2 sm:order-3 flex items-center gap-2 ml-auto">
            <span
              className={`hidden md:inline-flex items-center gap-1.5 rounded-lg bg-slate-100/80 px-2.5 py-1.5 text-xs font-medium ${connection.textClass}`}
              title="Conexión en tiempo real (SignalR)"
            >
              <span className={`h-1.5 w-1.5 rounded-full ${connection.dotClass}`} aria-hidden="true" />
              {connection.label}
            </span>
          </div>
        </div>
      </div>
    </header>
  );
}