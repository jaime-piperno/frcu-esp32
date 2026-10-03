import { useSyncExternalStore } from "react";

export interface Toast {
  id: string;
  kind: "success" | "info" | "warning" | "error";
  title: string;
  message?: string;
}

type Listener = () => void;

let toasts: Toast[] = [];
const listeners = new Set<Listener>();
let counter = 0;

function emit(): void {
  listeners.forEach((listener) => listener());
}

export function pushToast(kind: Toast["kind"], title: string, message?: string): void {
  const id = `toast-${++counter}-${Date.now()}`;
  toasts = [...toasts, { id, kind, title, message }];
  emit();
  window.setTimeout(() => {
    toasts = toasts.filter((t) => t.id !== id);
    emit();
  }, 3500);
}

export function dismissToast(id: string): void {
  toasts = toasts.filter((t) => t.id !== id);
  emit();
}

function subscribe(listener: Listener): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function useToasts(): Toast[] {
  return useSyncExternalStore(subscribe, () => toasts);
}