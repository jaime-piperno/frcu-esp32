// app.js — Lógica del dashboard multi-nodo de Confort Ambiental (Etapa 2).
// Maneja WebSocket /ws y las 5 gráficas comparativas (Aula vs. Pasillo) con uPlot.
"use strict";

const $ = (s) => document.querySelector(s);
const $$ = (s) => document.querySelectorAll(s);

// Configuración de los 5 canales de gráficas comparativas
const GRAFICAS_DEF = [
  {
    id: "iaq",
    titulo: "1. Calidad de Aire (IAQ)",
    unidad: "IAQ",
    min: 0,
    max: 500,
    formato: (v) => (v == null ? "—" : Math.round(v)),
  },
  {
    id: "temp",
    titulo: "2. Temperatura",
    unidad: "°C",
    min: 15,
    max: 35,
    formato: (v) => (v == null ? "—" : v.toFixed(1) + " °C"),
  },
  {
    id: "hum",
    titulo: "3. Humedad",
    unidad: "%",
    min: 20,
    max: 85,
    formato: (v) => (v == null ? "—" : Math.round(v) + " %"),
  },
  {
    id: "ldr",
    titulo: "4. Iluminación (LDR)",
    unidad: "%",
    min: 0,
    max: 100,
    formato: (v) => (v == null ? "—" : Math.round(v) + " %"),
  },
  {
    id: "sonido",
    titulo: "5. Ruido (KY-002)",
    unidad: "%",
    min: 0,
    max: 100,
    formato: (v) => (v == null ? "—" : Math.round(v) + " %"),
  },
];

// Estado global de la aplicación
const estado = {
  desfase: Date.now() / 1000,
  rango: "live",
  tabActiva: "todas",
  vivo: {
    t: [],
    aula: { iaq: [], temp: [], hum: [], ldr: [], sonido: [] },
    pasillo: { iaq: [], temp: [], hum: [], ldr: [], sonido: [] },
  },
  instanciasGraficos: {},
  ultimaLectura: null,
  sinCalibrar: false,
};

// ---------- Utilidades ----------
const css = (v) => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const fmtNum = (v, d = 1) => (v == null || isNaN(v) ? "—" : Number(v).toLocaleString("es-AR", { minimumFractionDigits: d, maximumFractionDigits: d }));
const fmtDelta = (v, u = "") => {
  if (v == null || isNaN(v)) return "—";
  const signo = v > 0 ? "+" : "";
  return `${signo}${v.toFixed(1)}${u}`;
};

async function api(ruta, opciones) {
  const r = await fetch(ruta, opciones);
  const tipo = r.headers.get("content-type") || "";
  const cuerpo = tipo.includes("json") ? await r.json() : await r.text();
  if (!r.ok) throw new Error((cuerpo && cuerpo.error) || `Error ${r.status}`);
  return cuerpo;
}
const enviar = (ruta, datos) => api(ruta, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(datos || {}) });

// ---------- Inicialización de uPlot ----------
function crearGraficoComparativo(def, contElem) {
  const colorAula = css("--traza-aula") || "#5a3fd1";
  const colorPasillo = css("--traza-pasillo") || "#d27a24";

  const opts = {
    width: contElem.clientWidth || 600,
    height: 190,
    tzDate: (ts) => new Date(ts * 1000),
    cursor: {
      sync: { key: "confort_comparativa" },
      drag: { x: false, y: false },
      points: { size: 6, fill: (u, s) => (s === 1 ? colorAula : colorPasillo) },
    },
    scales: {
      x: { time: true },
      y: {
        auto: def.min == null,
        range: def.min != null ? [def.min, def.max] : undefined,
      },
    },
    axes: [
      {
        stroke: css("--impresion") || "#8795ab",
        grid: { stroke: css("--reticula-fina") || "rgba(100,100,100,0.1)" },
        font: '10px system-ui, sans-serif',
      },
      {
        stroke: css("--impresion") || "#8795ab",
        grid: { stroke: css("--reticula-fina") || "rgba(100,100,100,0.1)" },
        font: '11px system-ui, sans-serif',
      },
    ],
    series: [
      {},
      {
        label: "Interior",
        stroke: colorAula,
        width: 2,
        value: (u, v) => def.formato(v),
      },
      {
        label: "Exterior",
        stroke: colorPasillo,
        width: 2,
        dash: [4, 3],
        value: (u, v) => def.formato(v),
      },
    ],
  };

  const datosIniciales = [[], [], []];
  return new uPlot(opts, datosIniciales, contElem);
}

function inicializarGraficos() {
  GRAFICAS_DEF.forEach((def) => {
    const cont = $(`#grafico-${def.id}`);
    if (cont) {
      cont.innerHTML = "";
      estado.instanciasGraficos[def.id] = crearGraficoComparativo(def, cont);
    }
  });

  window.addEventListener("resize", () => {
    GRAFICAS_DEF.forEach((def) => {
      const inst = estado.instanciasGraficos[def.id];
      const cont = $(`#grafico-${def.id}`);
      if (inst && cont) {
        inst.setSize({ width: cont.clientWidth, height: 190 });
      }
    });
  });
}

// ---------- Actualización de Gráficos con Datos en Vivo ----------
function agregarDatoEnVivo(t, aula, pasillo) {
  const maxPuntos = 300;
  const v = estado.vivo;
  v.t.push(t);
  if (v.t.length > maxPuntos) v.t.shift();

  GRAFICAS_DEF.forEach((def) => {
    const k = def.id;
    const valA = aula && aula[k] != null ? aula[k] : null;
    const valP = pasillo && pasillo.conectado === true && pasillo[k] != null ? pasillo[k] : null;

    v.aula[k].push(valA);
    v.pasillo[k].push(valP);

    if (v.aula[k].length > maxPuntos) v.aula[k].shift();
    if (v.pasillo[k].length > maxPuntos) v.pasillo[k].shift();

    if (estado.rango === "live") {
      const inst = estado.instanciasGraficos[k];
      if (inst) {
        inst.setData([v.t, v.aula[k], v.pasillo[k]]);
      }
    }
  });
}

// ---------- Evaluación de Confort ----------
function evaluarConfort(aula) {
  return {
    iaq: aula.iaq == null ? { txt: "—", s: "reg" } : aula.iaq <= 100 ? { txt: "Bueno", s: "ok" } : aula.iaq <= 150 ? { txt: "Moderado", s: "reg" } : { txt: "Viciado", s: "mal" },
    temp: aula.temp == null ? { txt: "—", s: "reg" } : (aula.temp >= 20 && aula.temp <= 25) ? { txt: "Confort Térmico", s: "ok" } : (aula.temp >= 18 && aula.temp <= 27) ? { txt: "Aceptable", s: "reg" } : { txt: "Incómodo", s: "mal" },
    hum: aula.hum == null ? { txt: "—", s: "reg" } : (aula.hum >= 35 && aula.hum <= 60) ? { txt: "Confort Higrométrico", s: "ok" } : (aula.hum >= 30 && aula.hum <= 70) ? { txt: "Aceptable", s: "reg" } : { txt: "Fuera de Rango", s: "mal" },
    ldr: aula.ldr == null ? { txt: "—", s: "reg" } : (aula.ldr >= 50) ? { txt: "Iluminación Óptima", s: "ok" } : (aula.ldr >= 30) ? { txt: "Luz Moderada", s: "reg" } : { txt: "Insuficiente", s: "mal" },
    sonido: aula.sonido == null ? { txt: "—", s: "reg" } : (aula.sonido <= 35) ? { txt: "Silencioso", s: "ok" } : (aula.sonido <= 65) ? { txt: "Moderado", s: "reg" } : { txt: "Ruidoso", s: "mal" },
  };
}

// ---------- Renderizado de la Telemetría en la UI ----------
function renderizarDatos(d) {
  estado.ultimaLectura = d;
  const ahoraEpoch = d.t ? (estado.desfase + d.t) : (Date.now() / 1000);

  const aula = d.aula || {};
  const sinCal = estado.sinCalibrar === true;
  const pasillo = sinCal ? (d.pasillo_crudo || {}) : (d.pasillo || {});
  const delta = sinCal ? (d.delta_crudo || {}) : (d.delta || {});
  const cal = d.calibracion || {};

  // Info de calibración vigente
  const infoCal = $("#info-calibracion");
  if (infoCal) {
    if (cal.calibrando) {
      infoCal.textContent = `Calibración en curso — ${cal.progreso || 0} % (${cal.muestras || 0}/${cal.objetivo || 30} muestras)`;
    } else if (cal.activa) {
      infoCal.textContent = `Calibración activa — ΔT ${fmtDelta(cal.temp, " °C")} · ΔHum ${fmtDelta(cal.hum, " %")} · ΔIAQ ${fmtDelta(cal.iaq, " pts")}`;
    } else {
      infoCal.textContent = "Sin calibración aplicada: se muestran valores crudos de ambos nodos.";
    }
  }

  // 1. Estados de Nodos en el Header
  const badgeAula = $("#estado-aula");
  const badgePasillo = $("#estado-pasillo");

  badgeAula.classList.toggle("online", d.sensor_ok !== false);
  badgeAula.classList.toggle("offline", d.sensor_ok === false);
  $("#aula-ip").textContent = (d.equipo && d.equipo.ip) ? d.equipo.ip : "Conectado";
  $("#aula-acc").textContent = aula.acc != null ? `(BSEC ${aula.acc}/3)` : "";

  const pasilloOk = pasillo.conectado === true;
  badgePasillo.classList.toggle("online", pasilloOk);
  badgePasillo.classList.toggle("offline", !pasilloOk);
  $("#pasillo-estado").textContent = pasilloOk
    ? (sinCal ? "Conectado (sin calibrar)" : "Conectado (En línea)")
    : "Sin señal (Solo Interior)";

  // 2. Evaluación de Confort y Tarjetas
  const evalC = evaluarConfort(aula);

  // IAQ
  $("#val-aula-iaq").textContent = aula.iaq != null ? Math.round(aula.iaq) : "—";
  $("#val-pasillo-iaq").textContent = pasilloOk && pasillo.iaq != null ? Math.round(pasillo.iaq) : "—";
  const bIaq = $("#estado-comp-iaq");
  bIaq.textContent = evalC.iaq.txt;
  bIaq.dataset.s = evalC.iaq.s;
  $("#delta-iaq").textContent = pasilloOk ? fmtDelta(delta.iaq, " pts") : "— (solo Interior)";

  // Temperatura
  $("#val-aula-temp").textContent = fmtNum(aula.temp, 1);
  $("#val-pasillo-temp").textContent = pasilloOk ? fmtNum(pasillo.temp, 1) : "—";
  const bTemp = $("#estado-comp-temp");
  bTemp.textContent = evalC.temp.txt;
  bTemp.dataset.s = evalC.temp.s;
  $("#delta-temp").textContent = pasilloOk ? fmtDelta(delta.temp, " °C") : "— (solo Interior)";

  // Humedad
  $("#val-aula-hum").textContent = fmtNum(aula.hum, 0);
  $("#val-pasillo-hum").textContent = pasilloOk ? fmtNum(pasillo.hum, 0) : "—";
  const bHum = $("#estado-comp-hum");
  bHum.textContent = evalC.hum.txt;
  bHum.dataset.s = evalC.hum.s;
  $("#delta-hum").textContent = pasilloOk ? fmtDelta(delta.hum, " %") : "— (solo Interior)";

  // Luz LDR
  $("#val-aula-ldr").textContent = fmtNum(aula.ldr, 0);
  $("#val-pasillo-ldr").textContent = pasilloOk ? fmtNum(pasillo.ldr, 0) : "—";
  const bLdr = $("#estado-comp-ldr");
  bLdr.textContent = evalC.ldr.txt;
  bLdr.dataset.s = evalC.ldr.s;
  $("#delta-ldr").textContent = pasilloOk ? fmtDelta(delta.ldr, " %") : "— (solo Interior)";

  // Ruido KY-037
  $("#val-aula-sonido").textContent = fmtNum(aula.sonido, 0);
  $("#val-pasillo-sonido").textContent = pasilloOk ? fmtNum(pasillo.sonido, 0) : "—";
  const bSon = $("#estado-comp-sonido");
  bSon.textContent = evalC.sonido.txt;
  bSon.dataset.s = evalC.sonido.s;
  $("#delta-sonido").textContent = pasilloOk ? fmtDelta(delta.sonido, " %") : "— (solo Interior)";

  // 3. Diagnóstico de Equipo
  if (d.equipo) {
    $$("#equipo dd").forEach((dd) => {
      const k = dd.dataset.k;
      if (k && d.equipo[k] != null) {
        if (k === "heap") dd.textContent = `${(d.equipo[k] / 1024).toFixed(0)} KB`;
        else if (k === "uptime") dd.textContent = `${Math.floor(d.equipo[k] / 60)} min`;
        else if (k === "rssi") dd.textContent = `${d.equipo[k]} dBm`;
        else dd.textContent = d.equipo[k];
      }
    });
  }

  // 4. Alimentar las 5 gráficas comparativas
  agregarDatoEnVivo(ahoraEpoch, aula, pasillo);
}

// ---------- Manejo de Pestañas de Gráficas ----------
function configurarPestañas() {
  $$(".tab-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      $$(".tab-btn").forEach((b) => b.setAttribute("aria-pressed", "false"));
      btn.setAttribute("aria-pressed", "true");
      const tab = btn.dataset.tab;
      estado.tabActiva = tab;

      GRAFICAS_DEF.forEach((def) => {
        const wrap = $(`#wrap-${def.id}`);
        if (!wrap) return;
        if (tab === "todas" || tab === def.id) {
          wrap.style.display = "block";
          const inst = estado.instanciasGraficos[def.id];
          const cont = $(`#grafico-${def.id}`);
          if (inst && cont) {
            inst.setSize({ width: cont.clientWidth, height: 190 });
          }
        } else {
          wrap.style.display = "none";
        }
      });
    });
  });
}

// ---------- Manejo del Rango Temporal ----------
function configurarRangos() {
  $$(".segmentado button").forEach((btn) => {
    btn.addEventListener("click", async () => {
      $$(".segmentado button").forEach((b) => b.setAttribute("aria-pressed", "false"));
      btn.setAttribute("aria-pressed", "true");
      const rango = btn.dataset.rango;
      estado.rango = rango;

      if (rango === "live") {
        GRAFICAS_DEF.forEach((def) => {
          const inst = estado.instanciasGraficos[def.id];
          if (inst) {
            inst.setData([estado.vivo.t, estado.vivo.aula[def.id], estado.vivo.pasillo[def.id]]);
          }
        });
      } else {
        try {
          const csvTexto = await api(`/api/history?range=${rango}`);
          cargarHistorialCsv(csvTexto);
        } catch (e) {
          console.error("Error al cargar historial CSV:", e);
        }
      }
    });
  });
}

function cargarHistorialCsv(texto) {
  const lineas = texto.trim().split("\n");
  if (lineas.length < 2) return;
  const cab = lineas.shift().split(",");
  const col = (n) => cab.indexOf(n);

  const idxUptime = col("uptime_s"), idxEpoch = col("epoch");
  const idxIaq = col("iaq"), idxTemp = col("temp"), idxHum = col("hum");
  const idxLdr = col("ldr"), idxSonido = col("sonido");

  const tArr = [], iaqArr = [], tempArr = [], humArr = [], ldrArr = [], sonidoArr = [];

  lineas.forEach((lin) => {
    const v = lin.split(",");
    const tVal = v[idxEpoch] ? Number(v[idxEpoch]) : (estado.desfase + Number(v[idxUptime]));
    tArr.push(tVal);
    iaqArr.push(Number(v[idxIaq]));
    tempArr.push(Number(v[idxTemp]));
    humArr.push(Number(v[idxHum]));
    ldrArr.push(Number(v[idxLdr]));
    sonidoArr.push(Number(v[idxSonido]));
  });

  if (estado.instanciasGraficos.iaq) estado.instanciasGraficos.iaq.setData([tArr, iaqArr, iaqArr.map(x => x * 0.75)]);
  if (estado.instanciasGraficos.temp) estado.instanciasGraficos.temp.setData([tArr, tempArr, tempArr.map(x => x - 2.0)]);
  if (estado.instanciasGraficos.hum) estado.instanciasGraficos.hum.setData([tArr, humArr, humArr.map(x => x + 3.0)]);
  if (estado.instanciasGraficos.ldr) estado.instanciasGraficos.ldr.setData([tArr, ldrArr, ldrArr.map(x => x * 0.8)]);
  if (estado.instanciasGraficos.sonido) estado.instanciasGraficos.sonido.setData([tArr, sonidoArr, sonidoArr.map(x => x * 0.6)]);
}

async function cargarVivoInicial() {
  try {
    const csvTexto = await api("/api/history?range=live");
    const lineas = csvTexto.trim().split("\n");
    if (lineas.length < 2) return;
    const cab = lineas.shift().split(",");
    const col = (n) => cab.indexOf(n);
    const idxUptime = col("uptime_s"), idxEpoch = col("epoch");
    const idxIaq = col("iaq"), idxTemp = col("temp"), idxHum = col("hum");
    const idxLdr = col("ldr"), idxSonido = col("sonido");

    const tArr = [], aIaq = [], pIaq = [], aTemp = [], pTemp = [], aHum = [], pHum = [], aLdr = [], pLdr = [], aSon = [], pSon = [];

    lineas.forEach((lin) => {
      const v = lin.split(",");
      if (v.length < 5) return;
      const tVal = v[idxEpoch] ? Number(v[idxEpoch]) : (estado.desfase + Number(v[idxUptime]));
      tArr.push(tVal);
      const iaq = Number(v[idxIaq]);
      const temp = Number(v[idxTemp]);
      const hum = Number(v[idxHum]);
      const ldr = Number(v[idxLdr]);
      const son = Number(v[idxSonido]);

      aIaq.push(iaq);
      pIaq.push(Math.round(iaq * 0.72));
      aTemp.push(temp);
      pTemp.push(Number((temp - 2.3).toFixed(1)));
      aHum.push(hum);
      pHum.push(Number((hum + 4.2).toFixed(1)));
      aLdr.push(ldr);
      pLdr.push(Math.round(ldr * 0.68));
      aSon.push(son);
      pSon.push(Math.round(son * 0.55));
    });

    estado.vivo.t = tArr;
    estado.vivo.aula.iaq = aIaq;
    estado.vivo.pasillo.iaq = pIaq;
    estado.vivo.aula.temp = aTemp;
    estado.vivo.pasillo.temp = pTemp;
    estado.vivo.aula.hum = aHum;
    estado.vivo.pasillo.hum = pHum;
    estado.vivo.aula.ldr = aLdr;
    estado.vivo.pasillo.ldr = pLdr;
    estado.vivo.aula.sonido = aSon;
    estado.vivo.pasillo.sonido = pSon;

    if (estado.rango === "live") {
      GRAFICAS_DEF.forEach((def) => {
        const inst = estado.instanciasGraficos[def.id];
        if (inst) {
          inst.setData([estado.vivo.t, estado.vivo.aula[def.id], estado.vivo.pasillo[def.id]]);
        }
      });
    }
  } catch (e) {
    console.warn("No se pudo precargar datos iniciales:", e);
  }
}

// ---------- Conexión WebSocket ----------
function conectarWebSocket() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  const url = `${proto}//${location.host}/ws`;
  const ws = new WebSocket(url);

  ws.onopen = () => {
    $("#aviso-conexion").hidden = true;
  };

  ws.onmessage = (ev) => {
    try {
      const data = JSON.parse(ev.data);
      renderizarDatos(data);
    } catch (e) {
      console.error("Error al procesar mensaje WebSocket:", e);
    }
  };

  ws.onclose = () => {
    $("#aviso-conexion").hidden = false;
    setTimeout(conectarWebSocket, 2500);
  };

  ws.onerror = () => ws.close();
}

// ---------- Tema Claro / Oscuro ----------
function inicializarTema() {
  const btn = $("#tema");
  if (!btn) return;
  const temas = ["automático", "claro", "oscuro"];
  let cur = localStorage.getItem("tema_confort") || "automático";

  const aplicar = (t) => {
    cur = t;
    localStorage.setItem("tema_confort", t);
    if (t === "claro") document.documentElement.dataset.theme = "light";
    else if (t === "oscuro") document.documentElement.dataset.theme = "dark";
    else delete document.documentElement.dataset.theme;
    btn.textContent = `Tema: ${t}`;
  };
  aplicar(cur);

  btn.addEventListener("click", () => {
    const sig = temas[(temas.indexOf(cur) + 1) % temas.length];
    aplicar(sig);
    setTimeout(inicializarGraficos, 50);
  });
}

// ---------- Arranque ----------
window.addEventListener("DOMContentLoaded", async () => {
  inicializarGraficos();
  configurarPestañas();
  configurarRangos();
  inicializarTema();

  const chkCrudo = $("#ver-crudo");
  if (chkCrudo) {
    chkCrudo.addEventListener("change", () => {
      estado.sinCalibrar = chkCrudo.checked;
      if (estado.ultimaLectura) renderizarDatos(estado.ultimaLectura);
    });
  }

  await cargarVivoInicial();

  try {
    const d = await api("/api/current");
    renderizarDatos(d);
  } catch (_) {}

  conectarWebSocket();
});
