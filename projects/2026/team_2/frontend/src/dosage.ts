import type { DoseUnit } from "./types";

export function formatDosage(amount: number, unit: DoseUnit): string {
  return `${amount} ${unit === "Milligrams" ? "mg" : amount === 1 ? "pastilla" : "pastillas"}`;
}
