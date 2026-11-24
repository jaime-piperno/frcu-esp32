export const BA_TIME_ZONE = "America/Argentina/Buenos_Aires";

export const DAY_NAMES = ["Lun", "Mar", "Mié", "Jue", "Vie", "Sáb", "Dom"];
export const DAY_BITS = [1, 2, 4, 8, 16, 32, 64];

function timeFmtFor(timeZone: string): Intl.DateTimeFormat {
  return new Intl.DateTimeFormat("es-AR", {
    timeZone,
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  });
}

function dateKeyFmtFor(timeZone: string): Intl.DateTimeFormat {
  return new Intl.DateTimeFormat("en-CA", {
    timeZone,
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
  });
}

function shortDateFmtFor(timeZone: string): Intl.DateTimeFormat {
  return new Intl.DateTimeFormat("es-AR", {
    timeZone,
    weekday: "short",
    day: "2-digit",
    month: "2-digit",
  });
}

function eventTimeFmtFor(timeZone: string): Intl.DateTimeFormat {
  return new Intl.DateTimeFormat("es-AR", {
    timeZone,
    weekday: "short",
    day: "2-digit",
    month: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  });
}

function dateKeyIn(iso: string, timeZone: string): string {
  const parts = dateKeyFmtFor(timeZone).formatToParts(new Date(iso));
  const map: Record<string, string> = {};
  for (const part of parts) map[part.type] = part.value;
  return `${map.year}-${map.month}-${map.day}`;
}

/** Adds calendar days to a YYYY-MM-DD key using UTC arithmetic (DST-safe). */
function addDaysKey(key: string, days: number): string {
  const [y, m, d] = key.split("-").map(Number);
  const utcKey = new Intl.DateTimeFormat("en-CA", {
    timeZone: "UTC",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
  });
  return utcKey.format(new Date(Date.UTC(y, m - 1, d + days)));
}

export function formatTime(iso: string, timeZone: string = BA_TIME_ZONE): string {
  return timeFmtFor(timeZone).format(new Date(iso));
}

export function formatLocalTime(localTime: string): string {
  const match = /^(\d{1,2}):(\d{2})/.exec(localTime);
  return match ? `${match[1].padStart(2, "0")}:${match[2]}` : localTime;
}

export function formatNextDose(scheduledUtc: string, timeZone: string = BA_TIME_ZONE): string {
  const target = dateKeyIn(scheduledUtc, timeZone);
  const now = new Date();
  const today = dateKeyIn(now.toISOString(), timeZone);
  const tomorrow = addDaysKey(today, 1);
  const day =
    target === today ? "Hoy" : target === tomorrow ? "Mañana" : shortDateFmtFor(timeZone).format(new Date(scheduledUtc));
  return `${day} ${formatTime(scheduledUtc, timeZone)}`;
}

export function formatEventTime(occurredAtUtc: string, timeZone: string = BA_TIME_ZONE): string {
  return eventTimeFmtFor(timeZone).format(new Date(occurredAtUtc));
}

export function formatDays(bitmask: number): string {
  if (bitmask === 127) return "Todos los días";
  const names = DAY_NAMES.filter((_, i) => (bitmask & DAY_BITS[i]) !== 0);
  return names.length > 0 ? names.join(" ") : "Sin días";
}

export function isSameDay(iso: string, iso2: string, timeZone: string = BA_TIME_ZONE): boolean {
  return dateKeyIn(iso, timeZone) === dateKeyIn(iso2, timeZone);
}

/** YYYY-MM-DD key for a calendar-day offset (0 = today) in the given timezone (DST-safe). */
export function dayKeyForOffset(offset: number, timeZone: string = BA_TIME_ZONE): string {
  return addDaysKey(dateKeyIn(new Date().toISOString(), timeZone), offset);
}

/** Local calendar-day key (YYYY-MM-DD) for an ISO timestamp in the given timezone. */
export function dateKeyOf(iso: string, timeZone: string = BA_TIME_ZONE): string {
  return dateKeyIn(iso, timeZone);
}

/** 0 = Lun … 6 = Dom for a YYYY-MM-DD key (matches DAY_NAMES / DAY_BITS indexing). */
function dayIndexForKey(key: string): number {
  const [y, m, d] = key.split("-").map(Number);
  const jsDay = new Date(Date.UTC(y, m - 1, d)).getUTCDay();
  return (jsDay + 6) % 7;
}

/** Schedule bitmask value (Lun=1 … Dom=64) for a YYYY-MM-DD key. */
export function dayBitForKey(key: string): number {
  return DAY_BITS[dayIndexForKey(key)];
}

/** Short weekday label for a YYYY-MM-DD key, e.g. "Vie". */
export function dayLabelForKey(key: string): string {
  return DAY_NAMES[dayIndexForKey(key)];
}

/** Day-of-month number for a YYYY-MM-DD key. */
export function dayNumberForKey(key: string): number {
  return Number(key.split("-")[2]);
}