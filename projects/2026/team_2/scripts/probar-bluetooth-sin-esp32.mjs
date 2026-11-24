import { execFileSync } from "node:child_process";

const api = "http://localhost:8080";
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function request(path, method = "GET", body) {
  const response = await fetch(`${api}${path}`, {
    method,
    headers: body ? { "Content-Type": "application/json" } : {},
    body: body ? JSON.stringify(body) : undefined,
  });
  if (!response.ok) throw new Error(`${method} ${path}: HTTP ${response.status} ${await response.text()}`);
  return response.status === 204 || response.status === 202 ? null : response.json();
}

function mqttPublish(topic, payload) {
  execFileSync("docker", ["compose", "exec", "-T", "mqtt", "mosquitto_pub",
    "-h", "localhost", "-t", topic, "-m", JSON.stringify(payload)]);
}

async function waitFor(deviceId, predicate) {
  for (let i = 0; i < 20; i++) {
    const state = await request(`/api/devices/${deviceId}/bluetooth/`);
    if (predicate(state)) return state;
    await sleep(250);
  }
  throw new Error("El backend no recibió el reporte Bluetooth simulado.");
}

let deviceId;
try {
  deviceId = (await request("/api/devices/register", "POST", { name: "Prueba Bluetooth MQTT" })).deviceId;
  await request(`/api/devices/${deviceId}/bluetooth/scan`, "POST");
  const reportTopic = `recuerdamed/${deviceId}/bluetooth/report`;
  mqttPublish(reportTopic, {
    type: "scan", status: "complete", devices: [
      { address: "AA:BB:CC:DD:EE:01", name: "Parlante de prueba", rssi: -48, audioCapable: true },
    ],
  });
  await waitFor(deviceId, (state) => state.devices.length === 1);
  console.log("✓ El backend recibió los dispositivos descubiertos por MQTT.");

  await request(`/api/devices/${deviceId}/bluetooth/speaker`, "PUT", {
    address: "AA:BB:CC:DD:EE:01", name: "Parlante de prueba",
  });
  const retained = execFileSync("docker", ["compose", "exec", "-T", "mqtt", "mosquitto_sub",
    "-h", "localhost", "-t", `recuerdamed/${deviceId}/bluetooth/config`, "-C", "1", "-W", "5"],
  { encoding: "utf8" });
  if (JSON.parse(retained).address !== "AA:BB:CC:DD:EE:01")
    throw new Error("La configuración MQTT retenida no coincide con el parlante elegido.");
  console.log("✓ La selección quedó guardada y publicada como configuración retenida.");

  mqttPublish(reportTopic, { type: "connection", status: "connected", address: "AA:BB:CC:DD:EE:01" });
  await waitFor(deviceId, (state) => state.connectionStatus === "connected" && state.connectedAddress === "AA:BB:CC:DD:EE:01");
  console.log("✓ El dashboard puede consultar el estado de conexión reportado.");
} finally {
  if (deviceId) {
    await request(`/api/devices/${deviceId}/bluetooth/speaker`, "PUT", { address: null, name: null });
    await request(`/api/devices/${deviceId}`, "DELETE");
  }
}
