// secrets.example.h — Configuración de red para el Nodo Exterior
#pragma once

// Puede conectarse a la red Wi-Fi de tu casa/facultad, o directo a la red del
// Nodo Interior:
// #define WIFI_SSID "nombre-de-tu-red"
// #define WIFI_PASS "clave-de-tu-red"

// Si se conecta directamente al Punto de Acceso generado por Confort_Interior:
#define WIFI_SSID "Confort_Interior"
#define WIFI_PASS "confort1234"

// URL destino de la API del Nodo Interior:
// - Si se conecta al AP propio del Interior:
// "http://192.168.4.1/api/nodo_remoto"
// - Si ambos están en el router local: "http://confort.local/api/nodo_remoto" o
// "http://<IP_INTERIOR>/api/nodo_remoto"
#define SERVER_URL "http://192.168.4.1/api/nodo_remoto"
