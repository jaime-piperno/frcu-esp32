import { execFileSync } from "node:child_process";
import { createInterface } from "node:readline/promises";

const api = "http://localhost:8080";
const zone = "America/Argentina/Buenos_Aires";
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const demo = process.argv.includes("--demo");
const terminal = demo ? createInterface({ input: process.stdin, output: process.stdout }) : null;
const pause = async (message) => {
  if (terminal) await terminal.question(`${message} Presioná Enter para continuar... `);
};

async function request(path, options = {}) {
  const response = await fetch(`${api}${path}`, {
    ...options,
    headers: { "Content-Type": "application/json", ...options.headers },
  });
  if (!response.ok) throw new Error(`${options.method ?? "GET"} ${path}: HTTP ${response.status} ${await response.text()}`);
  return response.status === 204 ? null : response.json();
}

async function waitForState(deviceId, predicate, label) {
  for (let attempt = 0; attempt < 30; attempt++) {
    const state = await request(`/api/dashboard/state/${deviceId}`);
    if (predicate(state)) return state;
    await sleep(500);
  }
  throw new Error(`No llegó el estado esperado: ${label}`);
}

function publish(deviceId, payload) {
  execFileSync("docker", [
    "compose", "exec", "-T", "mqtt", "mosquitto_pub",
    "-h", "localhost", "-t", `recuerdamed/${deviceId}/events`,
    "-m", JSON.stringify(payload),
  ], { stdio: "pipe" });
}

async function waitForMqttAlert(deviceId, medicationId) {
  const deadline = Date.now() + 45_000;
  while (Date.now() < deadline) {
    try {
      const retained = execFileSync("docker", [
        "compose", "exec", "-T", "mqtt", "mosquitto_sub", "-h", "localhost",
        "-t", `recuerdamed/${deviceId}/state`, "-C", "1", "-W", "2",
      ], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
      if (JSON.parse(retained).alerts.some((alert) => alert.medicationId === medicationId)) return;
    } catch (error) {
      if (error.status !== 27) throw error;
    }
    await sleep(1_000);
  }
  throw new Error("La alerta apareció en la API, pero no llegó a MQTT en 45 s. Revisá: docker compose logs api mqtt");
}

function localSchedule() {
  const due = new Date(Date.now() - 60_000);
  const parts = Object.fromEntries(new Intl.DateTimeFormat("en-US", {
    timeZone: zone, weekday: "short", hour: "2-digit", minute: "2-digit", hourCycle: "h23",
  }).formatToParts(due).filter((part) => part.type !== "literal").map((part) => [part.type, part.value]));
  const days = { Mon: 1, Tue: 2, Wed: 4, Thu: 8, Fri: 16, Sat: 32, Sun: 64 };
  return { localTime: `${parts.hour}:${parts.minute}:00`, daysOfWeek: days[parts.weekday], isActive: true };
}

let deviceId;
try {
  const device = await request("/api/devices/register", {
    method: "POST", body: JSON.stringify({ name: "Simulador ESP32" }),
  });
  deviceId = device.deviceId;
  console.log(`Dispositivo de prueba: ${deviceId}`);

  const medication = await request("/api/medications/", {
    method: "POST",
    body: JSON.stringify({ deviceId, name: "Dosis de prueba", dosageAmount: 1, dosageUnit: "Tablets", isActive: true, schedules: [localSchedule()] }),
  });
  console.log(`Medicamento de prueba: ${medication.id}`);

  await waitForMqttAlert(deviceId, medication.id);
  console.log("✓ La API generó la alerta.");
  console.log("✓ MQTT entregó la alerta retenida.");
  if (demo) {
    console.log(`Abrí http://localhost:5173/?device=${deviceId} para ver la alarma activa.`);
    await pause("Revisá la alarma en el dashboard.");
  }

  publish(deviceId, { type: "snoozed", medicationId: medication.id, snoozeSeconds: demo ? 300 : 10 });
  await waitForState(deviceId, (state) => state.snoozed, "posposición");
  console.log("✓ El botón simulado pospuso la dosis.");
  if (demo) await pause("Revisá el estado 'Pospuesta' en el dashboard.");

  publish(deviceId, { type: "taken", medicationId: medication.id });
  await waitForState(deviceId, (state) => !state.snoozed && !state.alerts.some((alert) => alert.medicationId === medication.id), "toma confirmada");
  const events = await request(`/api/events?deviceId=${deviceId}`);
  if (!events.some((event) => event.type === "taken") || !events.some((event) => event.type === "snoozed")) {
    throw new Error("Faltan eventos de toma o posposición en la base.");
  }
  console.log("✓ La confirmación quedó registrada en la base de datos.");
  if (demo) await pause("Revisá la toma confirmada en la agenda y la bitácora.");
} finally {
  terminal?.close();
  if (deviceId) {
    await request(`/api/devices/${deviceId}`, { method: "DELETE" });
    console.log("Dispositivo de prueba retirado del dashboard.");
  }
}
