#include "BluetoothSpeaker.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp32-hal-alloc-bt-classic-mem.h>
#include <esp_a2dp_api.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_coexist.h>
#include <esp_gap_bt_api.h>
#include <esp_err.h>
#include <freertos/stream_buffer.h>

#include <cstdio>
#include <cstring>

namespace {
constexpr int MaxDevices = 12;
constexpr unsigned long RetryMs = 10000;
constexpr unsigned long ConnectTimeoutMs = 15000;
constexpr unsigned long SpeechRetryDelayMs = 2000;
constexpr uint8_t MaxSpeechRetries = 2;
// Keep enough internal RAM for the HTTP task and WiFi/MQTT while A2DP is up.
// 4 KB hold about 185 ms of 11.025 kHz mono PCM; smaller fallback is used
// when Bluetooth/WiFi leave little contiguous heap.
constexpr size_t SpeechBufferBytes = 4096;

struct FoundDevice {
  char address[18];
  char name[64];
  int rssi;
  bool audioCapable;
};

portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
FoundDevice found[MaxDevices];
int foundCount = 0;
char targetAddress[18] = "";
char connectedAddress[18] = "";
char connectionAddress[18] = "";
bool ready = false;
bool scanning = false;
bool scanStartedPending = false;
bool scanCompletePending = false;
bool scanFailed = false;
bool statusPending = false;
bool connecting = false;
bool connected = false;
bool disconnecting = false;
bool failed = false;
unsigned long lastConnectAttempt = 0;
unsigned long lastStatusReport = 0;
bool scanRequested = false;
StreamBufferHandle_t speechBuffer = nullptr;
size_t speechBufferCapacity = 0;
String pendingSpeechUrl;
String activeSpeechUrl;
uint8_t speechRetryCount = 0;
unsigned long speechRetryAt = 0;
volatile bool speechTaskRunning = false;
volatile bool speechTaskFinished = false;
volatile bool speechStopRequested = false;
volatile bool audioStarted = false;
volatile bool speechEverStarted = false;
volatile bool speechRequestedForAlert = false;
volatile bool speechFailed = false;
volatile bool speechTerminalFailure = false;
// An A2DP START event without PCM callbacks can leave the source profile
// connected but unusable. Reconnect the profile before retrying that alert.
volatile bool speechNeedsReconnect = false;
bool mediaStartRequested = false;
bool mediaStopRequested = false;
bool coexPreferenceSet = false;
bool mqttBufferReducedForSpeech = false;
volatile bool mediaCheckRequested = false;
volatile bool mediaReady = false;
unsigned long lastMediaCheck = 0;
unsigned long speechRequestedAt = 0;
unsigned long lastAudioDiagnostics = 0;
unsigned long audioStartedAt = 0;
unsigned long mediaStopRequestedAt = 0;
uint8_t currentMonoSample[2] = {0, 0};
uint32_t outputSampleRate = 44100;
uint32_t resamplePhase = 0;
bool monoSampleReady = false;
volatile uint32_t audioPcmBytesSent = 0;
volatile uint32_t audioUnderrunFrames = 0;
volatile uint32_t audioCallbackCalls = 0;

uint16_t read16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t read32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
    (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void speechDownloadTask(void* arg) {
  String url = *static_cast<String*>(arg);
  delete static_cast<String*>(arg);
  bool success = false;
  WiFiClient speechClient;
  HTTPClient http;
  uint32_t totalPcmBytes = 0;
  // Piper may need several seconds before the API sends the WAV headers.
  http.setTimeout(30000);
  const bool began = !speechStopRequested && http.begin(speechClient, url);
  const unsigned long requestStartedAt = millis();
  const int responseCode = began ? http.GET() : 0;
  Serial.printf("[VOZ] HTTP GET termino: codigo=%d ms=%lu heap=%u\n",
    responseCode, millis() - requestStartedAt, static_cast<unsigned int>(ESP.getFreeHeap()));
  if (began && responseCode != HTTP_CODE_OK)
    Serial.printf("[VOZ] HTTP fallo: %d (%s)\n", responseCode,
      http.errorToString(responseCode).c_str());
  if (responseCode == HTTP_CODE_OK) {
    NetworkClient* stream = http.getStreamPtr();
    uint8_t header[44];
    const size_t headerBytes = stream->readBytes(header, sizeof(header));
    if (headerBytes == sizeof(header) &&
        memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVEfmt ", 8) == 0 &&
        read16(header + 20) == 1 && read16(header + 22) == 1 &&
        read32(header + 24) == 11025 && read16(header + 34) == 16 &&
        memcmp(header + 36, "data", 4) == 0) {
      uint32_t remaining = read32(header + 40);
      totalPcmBytes = remaining;
      Serial.printf("[VOZ] WAV valido: %u bytes de PCM\n", remaining);
      uint8_t chunk[1024];
      success = true;
      while (remaining > 0 && !speechStopRequested) {
        const size_t requested = min(static_cast<uint32_t>(sizeof(chunk)), remaining);
        const size_t received = stream->readBytes(chunk, requested);
        if (received == 0) {
          Serial.printf("[VOZ] WAV incompleto: faltan %u bytes, socket=%d, disponible=%d, buffer=%u, heap=%u\n",
            remaining, stream->connected(), stream->available(),
            static_cast<unsigned int>(xStreamBufferBytesAvailable(speechBuffer)),
            static_cast<unsigned int>(ESP.getFreeHeap()));
          success = false; break;
        }
        size_t offset = 0;
        while (offset < received && !speechStopRequested) {
          offset += xStreamBufferSend(speechBuffer, chunk + offset, received - offset, pdMS_TO_TICKS(200));
        }
        remaining -= received;
      }
      if (remaining != 0) success = false;
    } else {
      Serial.println("[VOZ] Cabecera WAV incompatible");
    }
  }
  http.end();
  Serial.printf("[VOZ] Descarga terminada: ok=%d pcm=%u bytes heap=%u\n",
    success, static_cast<unsigned int>(totalPcmBytes),
    static_cast<unsigned int>(ESP.getFreeHeap()));
  if (!success && !speechStopRequested) {
    speechFailed = true;
    speechStopRequested = true;
    Serial.println("[VOZ] No se pudo descargar el audio");
  }
  speechTaskFinished = true;
  speechTaskRunning = false;
  vTaskDelete(nullptr);
}

void formatAddress(const uint8_t* address, char* output) {
  snprintf(output, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
    address[0], address[1], address[2], address[3], address[4], address[5]);
}

bool parseAddress(const char* input, esp_bd_addr_t output) {
  unsigned int bytes[6];
  if (!input || strlen(input) != 17 ||
      sscanf(input, "%2x:%2x:%2x:%2x:%2x:%2x",
        &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) return false;
  for (int i = 0; i < 6; i++) output[i] = static_cast<uint8_t>(bytes[i]);
  return true;
}

void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
  if (event == ESP_BT_GAP_DISC_RES_EVT) {
    FoundDevice item{};
    formatAddress(param->disc_res.bda, item.address);
    item.rssi = -127;
    uint32_t cod = 0;
    bool hasCod = false;
    uint8_t* eir = nullptr;

    for (int i = 0; i < param->disc_res.num_prop; i++) {
      auto* property = &param->disc_res.prop[i];
      if (property->type == ESP_BT_GAP_DEV_PROP_RSSI && property->len >= 1)
        item.rssi = *static_cast<int8_t*>(property->val);
      else if (property->type == ESP_BT_GAP_DEV_PROP_COD && property->len >= sizeof(uint32_t)) {
        cod = *static_cast<uint32_t*>(property->val);
        hasCod = true;
      } else if (property->type == ESP_BT_GAP_DEV_PROP_BDNAME && property->len > 0) {
        const size_t n = min(static_cast<size_t>(property->len), sizeof(item.name) - 1);
        memcpy(item.name, property->val, n);
        item.name[n] = '\0';
      } else if (property->type == ESP_BT_GAP_DEV_PROP_EIR) {
        eir = static_cast<uint8_t*>(property->val);
      }
    }

    if (!item.name[0] && eir) {
      uint8_t length = 0;
      uint8_t* name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &length);
      if (!name) name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &length);
      if (name && length) {
        const size_t n = min(static_cast<size_t>(length), sizeof(item.name) - 1);
        memcpy(item.name, name, n);
        item.name[n] = '\0';
      }
    }
    if (!item.name[0]) strncpy(item.name, item.address, sizeof(item.name) - 1);
    item.audioCapable = !hasCod || !esp_bt_gap_is_valid_cod(cod) ||
      esp_bt_gap_get_cod_major_dev(cod) == ESP_BT_COD_MAJOR_DEV_AV ||
      (esp_bt_gap_get_cod_srvc(cod) & (ESP_BT_COD_SRVC_RENDERING | ESP_BT_COD_SRVC_AUDIO));

    portENTER_CRITICAL(&mutex);
    if (scanning) {
      int index = 0;
      while (index < foundCount && strcmp(found[index].address, item.address) != 0) index++;
      if (index < MaxDevices) {
        found[index] = item;
        if (index == foundCount) foundCount++;
      }
    }
    portEXIT_CRITICAL(&mutex);
  } else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT &&
             param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
    portENTER_CRITICAL(&mutex);
    scanning = false;
    scanCompletePending = true;
    portEXIT_CRITICAL(&mutex);
  } else if (event == ESP_BT_GAP_CFM_REQ_EVT) {
    char address[18];
    formatAddress(param->cfm_req.bda, address);
    portENTER_CRITICAL(&mutex);
    const bool selected = strcmp(address, targetAddress) == 0;
    portEXIT_CRITICAL(&mutex);
    // The ESP32 has no display. Confirm only the speaker selected by the user.
    esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, selected);
  } else if (event == ESP_BT_GAP_PIN_REQ_EVT) {
    char address[18];
    formatAddress(param->pin_req.bda, address);
    portENTER_CRITICAL(&mutex);
    const bool selected = strcmp(address, targetAddress) == 0;
    portEXIT_CRITICAL(&mutex);
    esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
    esp_bt_gap_pin_reply(param->pin_req.bda, selected, selected ? 4 : 0, pin);
  }
}

void a2dpCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
  if (event == ESP_A2D_PROF_STATE_EVT && param->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS) {
    portENTER_CRITICAL(&mutex);
    ready = true;
    statusPending = true;
    portEXIT_CRITICAL(&mutex);
  } else if (event == ESP_A2D_CONNECTION_STATE_EVT) {
    portENTER_CRITICAL(&mutex);
    if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
      connected = true;
      connecting = false;
      disconnecting = false;
      failed = false;
      formatAddress(param->conn_stat.remote_bda, connectedAddress);
      if (speechNeedsReconnect) {
        speechNeedsReconnect = false;
        speechRetryAt = millis() + SpeechRetryDelayMs;
        Serial.println("[VOZ] A2DP reconectado; reintento de voz habilitado");
      }
    } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
      failed = connecting;
      connected = false;
      connecting = false;
      disconnecting = false;
      connectedAddress[0] = '\0';
      speechStopRequested = true;
      audioStarted = false;
      mediaReady = false;
      mediaStartRequested = false;
      mediaStopRequested = false;
      mediaCheckRequested = false;
    }
    statusPending = true;
    portEXIT_CRITICAL(&mutex);
  } else if (event == ESP_A2D_AUDIO_STATE_EVT) {
    audioStarted = param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED;
    if (audioStarted) {
      speechEverStarted = true;
      audioPcmBytesSent = 0;
      audioUnderrunFrames = 0;
      audioCallbackCalls = 0;
      audioStartedAt = millis();
      Serial.println("[VOZ] A2DP AUDIO_STATE_STARTED");
    } else {
      Serial.printf("[VOZ] A2DP AUDIO_STATE_STOPPED pcm=%u bytes underrun=%u frames\n",
        static_cast<unsigned int>(audioPcmBytesSent),
        static_cast<unsigned int>(audioUnderrunFrames));
    }
  } else if (event == ESP_A2D_AUDIO_CFG_EVT && param->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
    const uint8_t flags = param->audio_cfg.mcc.cie.sbc_info.samp_freq;
    const uint32_t rate = flags & ESP_A2D_SBC_CIE_SF_48K ? 48000 :
      flags & ESP_A2D_SBC_CIE_SF_44K ? 44100 :
      flags & ESP_A2D_SBC_CIE_SF_32K ? 32000 :
      flags & ESP_A2D_SBC_CIE_SF_16K ? 16000 : 0;
    if (rate) outputSampleRate = rate;
    Serial.printf("[BT] Codec SBC: %lu Hz\n", static_cast<unsigned long>(rate));
  } else if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT &&
             param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) {
    mediaCheckRequested = false;
    if (param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS &&
        speechRequestedForAlert && !speechStopRequested) {
      mediaReady = true;
      Serial.println("[VOZ] A2DP source listo");
    }
  } else if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT &&
             param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START) {
    if (param->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
      Serial.printf("[VOZ] A2DP START rechazado: %d\n", param->media_ctrl_stat.status);
      audioStarted = false;
      speechFailed = true;
      speechNeedsReconnect = true;
      speechStopRequested = true;
      mediaStartRequested = false;
      mediaReady = false;
    }
  } else if (event == ESP_A2D_MEDIA_CTRL_ACK_EVT &&
             param->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
    Serial.printf("[VOZ] Control A2DP rechazado: %d\n", param->media_ctrl_stat.cmd);
    speechStopRequested = true;
    audioStarted = false;
    speechFailed = true;
    if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START) mediaStartRequested = false;
  }
}

int32_t speechAudioData(uint8_t* data, int32_t length) {
  if (!data || length <= 0) return 0;
  audioCallbackCalls++;
  // Convert 11.025 kHz mono PCM to the sink's negotiated SBC rate and stereo.
  int32_t i = 0;
  for (; i + 3 < length; i += 4) {
    if (!monoSampleReady) {
      const bool gotSample = speechBuffer && !speechStopRequested &&
          xStreamBufferBytesAvailable(speechBuffer) >= 2 &&
          xStreamBufferReceive(speechBuffer, currentMonoSample, 2, 0) == 2;
      if (!gotSample) {
        currentMonoSample[0] = currentMonoSample[1] = 0;
        audioUnderrunFrames++;
      }
      monoSampleReady = true;
    }
    data[i] = data[i + 2] = currentMonoSample[0];
    data[i + 1] = data[i + 3] = currentMonoSample[1];
    audioPcmBytesSent += 4;
    resamplePhase += 11025;
    if (resamplePhase >= outputSampleRate) {
      resamplePhase -= outputSampleRate;
      monoSampleReady = false;
    }
  }
  if (i < length) memset(data + i, 0, length - i);
  return length;
}

bool initFailed() {
  portENTER_CRITICAL(&mutex);
  failed = true;
  statusPending = true;
  portEXIT_CRITICAL(&mutex);
  return false;
}

bool checkInit(const char* step, esp_err_t result) {
  if (result == ESP_OK) return true;
  Serial.printf("[BT] %s fallo: %s (0x%X), heap libre=%u\n",
    step, esp_err_to_name(result), static_cast<unsigned int>(result),
    static_cast<unsigned int>(ESP.getFreeHeap()));
  return initFailed();
}

bool publishReport(PubSubClient& mqtt, const String& topic, JsonDocument& doc) {
  char payload[3072];
  if (measureJson(doc) >= sizeof(payload)) return false;
  const size_t length = serializeJson(doc, payload, sizeof(payload));
  return mqtt.publish(topic.c_str(), reinterpret_cast<const uint8_t*>(payload), length, false);
}
}

bool bluetoothBegin() {
  esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  // A2DP and discovery use Bluetooth Classic only. Free the unused BLE stack
  // before initializing the controller so WiFi and audio streaming have RAM.
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    const esp_err_t released = esp_bt_mem_release(ESP_BT_MODE_BLE);
    Serial.printf("[BT] Memoria BLE liberada: %s\n", esp_err_to_name(released));
  }
  config.mode = ESP_BT_MODE_CLASSIC_BT;
  Serial.printf("[BT] Iniciando, heap libre=%u\n", static_cast<unsigned int>(ESP.getFreeHeap()));
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE &&
      !checkInit("esp_bt_controller_init", esp_bt_controller_init(&config))) return false;
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED &&
      !checkInit("esp_bt_controller_enable", esp_bt_controller_enable(static_cast<esp_bt_mode_t>(config.mode)))) return false;
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED &&
      !checkInit("esp_bluedroid_init", esp_bluedroid_init())) return false;
  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED &&
      !checkInit("esp_bluedroid_enable", esp_bluedroid_enable())) return false;

  esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
  if (!checkInit("esp_bt_gap_set_security_param",
      esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof(capability))) ||
      !checkInit("esp_bt_gap_register_callback", esp_bt_gap_register_callback(gapCallback)) ||
      !checkInit("esp_a2d_register_callback", esp_a2d_register_callback(a2dpCallback)) ||
      !checkInit("esp_a2d_source_init", esp_a2d_source_init()) ||
      !checkInit("esp_a2d_source_register_data_callback", esp_a2d_source_register_data_callback(speechAudioData))) return false;
  Serial.printf("[BT] A2DP Source inicializando, heap libre=%u\n",
    static_cast<unsigned int>(ESP.getFreeHeap()));
  return true;
}

bool bluetoothIsConnected() {
  portENTER_CRITICAL(&mutex);
  const bool result = connected;
  portEXIT_CRITICAL(&mutex);
  return result;
}

bool bluetoothCanPlaySpeech() {
  portENTER_CRITICAL(&mutex);
  const bool result = connected && !connecting && !disconnecting;
  portEXIT_CRITICAL(&mutex);
  return result;
}

bool bluetoothUseBuzzer() {
  return !bluetoothIsConnected() || speechTerminalFailure;
}

bool bluetoothQueueSpeech(const String& url) {
  if (!bluetoothIsConnected() || url.isEmpty()) return false;
  if (!speechBuffer) {
    speechBuffer = xStreamBufferCreate(SpeechBufferBytes, 1);
    if (speechBuffer) speechBufferCapacity = SpeechBufferBytes;
  }
  if (!speechBuffer) {
    speechBuffer = xStreamBufferCreate(SpeechBufferBytes / 2, 1);
    if (speechBuffer) speechBufferCapacity = SpeechBufferBytes / 2;
  }
  if (!speechBuffer) return false;
  pendingSpeechUrl = url;
  activeSpeechUrl = url;
  speechRetryCount = 0;
  speechRetryAt = 0;
  resamplePhase = 0;
  monoSampleReady = false;
  speechRequestedForAlert = true;
  speechFailed = false;
  speechTerminalFailure = false;
  speechNeedsReconnect = false;
  speechEverStarted = false;
  speechRequestedAt = millis();
  speechStopRequested = speechTaskRunning || mediaStartRequested || audioStarted;
  return true;
}

void bluetoothStopSpeech() {
  pendingSpeechUrl = "";
  speechRequestedForAlert = false;
  speechNeedsReconnect = false;
  speechStopRequested = speechTaskRunning || mediaStartRequested || audioStarted;
}

void bluetoothStartScan() {
  if (!ready) {
    scanRequested = true;
    return;
  }
  if (scanning) return;
  scanRequested = false;
  portENTER_CRITICAL(&mutex);
  foundCount = 0;
  scanFailed = false;
  scanning = true;
  scanStartedPending = true;
  portEXIT_CRITICAL(&mutex);
  if (esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0) != ESP_OK) {
    portENTER_CRITICAL(&mutex);
    scanning = false;
    scanFailed = true;
    scanCompletePending = true;
    portEXIT_CRITICAL(&mutex);
  }
}

void bluetoothSetTarget(const char* address) {
  if (address && *address) {
    esp_bd_addr_t parsed;
    if (!parseAddress(address, parsed)) return;
  }
  portENTER_CRITICAL(&mutex);
  snprintf(targetAddress, sizeof(targetAddress), "%s", address ? address : "");
  lastConnectAttempt = 0;
  statusPending = true;
  portEXIT_CRITICAL(&mutex);
}

void bluetoothRequestStatusReport() {
  portENTER_CRITICAL(&mutex);
  statusPending = true;
  portEXIT_CRITICAL(&mutex);
}

void bluetoothLoop(PubSubClient& mqtt, const String& reportTopic) {
  const unsigned long now = millis();
  if (scanRequested && ready) bluetoothStartScan();
  if (speechRequestedForAlert && !mqttBufferReducedForSpeech) {
    if (mqtt.setBufferSize(4096)) {
      mqttBufferReducedForSpeech = true;
    }
  } else if (!speechRequestedForAlert && mqttBufferReducedForSpeech) {
    if (mqtt.setBufferSize(8192)) mqttBufferReducedForSpeech = false;
  }
  if (audioStarted && !coexPreferenceSet) {
    const esp_err_t result = esp_coex_preference_set(ESP_COEX_PREFER_BT);
    coexPreferenceSet = result == ESP_OK;
  } else if (!audioStarted && coexPreferenceSet) {
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    coexPreferenceSet = false;
  }
  if (speechRequestedForAlert && !speechEverStarted && !speechFailed &&
      !speechTerminalFailure &&
      millis() - speechRequestedAt >= 35000) {
    speechFailed = true;
    speechStopRequested = true;
    Serial.println("[VOZ] Tiempo de inicio agotado");
  }

  if (speechRequestedForAlert && audioStarted && audioCallbackCalls == 0 &&
      !speechStopRequested && now - audioStartedAt >= 2500) {
    speechFailed = true;
    speechNeedsReconnect = true;
    speechStopRequested = true;
    Serial.println("[VOZ] A2DP inicio sin callback de PCM; recuperando canal");
  }

  if (speechBuffer && speechStopRequested && (audioStarted || mediaStartRequested) && !mediaStopRequested) {
    if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND) == ESP_OK) {
      mediaStopRequested = true;
      mediaStopRequestedAt = now;
    }
  }
  if (speechBuffer && speechStopRequested && audioStarted && mediaStopRequested &&
      !disconnecting && now - mediaStopRequestedAt >= 3000) {
    char address[18];
    portENTER_CRITICAL(&mutex);
    snprintf(address, sizeof(address), "%s", connectedAddress);
    portEXIT_CRITICAL(&mutex);
    esp_bd_addr_t parsed;
    if (parseAddress(address, parsed) && esp_a2d_source_disconnect(parsed) == ESP_OK) {
      portENTER_CRITICAL(&mutex);
      disconnecting = true;
      portEXIT_CRITICAL(&mutex);
      Serial.println("[BT] A2DP no suspendio; desconectando para recuperar el canal");
    }
  }
  // SUSPEND can report AUDIO_STATE_STOPPED even though the A2DP source stays
  // attached to a wedged sink session. Tear down that session before retrying.
  if (speechBuffer && speechNeedsReconnect && speechStopRequested &&
      !speechTaskRunning && !audioStarted && !mediaStartRequested &&
      bluetoothIsConnected() && !disconnecting) {
    char address[18];
    portENTER_CRITICAL(&mutex);
    snprintf(address, sizeof(address), "%s", connectedAddress);
    portEXIT_CRITICAL(&mutex);
    esp_bd_addr_t parsed;
    if (parseAddress(address, parsed) && esp_a2d_source_disconnect(parsed) == ESP_OK) {
      portENTER_CRITICAL(&mutex);
      disconnecting = true;
      portEXIT_CRITICAL(&mutex);
      Serial.println("[BT] Reiniciando sesión A2DP antes de reintentar la locución");
    }
  }
  if (speechBuffer && speechStopRequested && !speechTaskRunning && !audioStarted &&
      (!mediaStartRequested || mediaStopRequested)) {
    xStreamBufferReset(speechBuffer);
    if (speechFailed && speechRequestedForAlert && speechRetryCount < MaxSpeechRetries) {
      speechRetryCount++;
      speechRetryAt = speechNeedsReconnect ? 0 : millis() + SpeechRetryDelayMs;
      pendingSpeechUrl = activeSpeechUrl;
      Serial.printf("[VOZ] Reintento %u/%u %s\n", speechRetryCount, MaxSpeechRetries,
        speechNeedsReconnect ? "al restablecer A2DP" : "en 2000 ms");
    } else if (speechFailed) {
      Serial.println("[VOZ] Se agotaron los reintentos para esta alerta");
      speechTerminalFailure = true;
      speechRequestedForAlert = false;
    }
    speechStopRequested = false;
    speechTaskFinished = false;
    speechFailed = false;
    mediaStartRequested = false;
    mediaStopRequested = false;
    mediaReady = false;
    mediaCheckRequested = false;
  }
  if (speechBuffer && !speechStopRequested && !speechTaskRunning && !audioStarted &&
      pendingSpeechUrl.length() && !speechNeedsReconnect && bluetoothCanPlaySpeech() &&
      (!speechRetryAt || static_cast<long>(millis() - speechRetryAt) >= 0)) {
    xStreamBufferReset(speechBuffer);
    String* url = new String(pendingSpeechUrl);
    pendingSpeechUrl = "";
    speechRetryAt = 0;
    speechRequestedAt = millis();
    speechTaskFinished = false;
    speechTaskRunning = true;
    if (xTaskCreate(speechDownloadTask, "speech-http", 4096, url, 1, nullptr) != pdPASS) {
      speechTaskRunning = false;
      delete url;
      speechFailed = true;
      speechStopRequested = true;
      Serial.printf("[VOZ] No hay memoria para descargar audio (heap libre=%u)\n",
        static_cast<unsigned int>(ESP.getFreeHeap()));
    }
  }
  if (speechBuffer && !speechStopRequested && !mediaStartRequested && bluetoothIsConnected() &&
      (xStreamBufferBytesAvailable(speechBuffer) >= speechBufferCapacity * 3 / 4 ||
       (speechTaskFinished && xStreamBufferBytesAvailable(speechBuffer) > 0))) {
    if (mediaReady) {
      if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START) == ESP_OK) {
        mediaStartRequested = true;
        Serial.println("[VOZ] A2DP START solicitado");
      }
    } else if (!mediaCheckRequested && millis() - lastMediaCheck >= 500) {
      lastMediaCheck = millis();
      if (esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) == ESP_OK)
        mediaCheckRequested = true;
    }
  }
  if (speechBuffer && speechTaskFinished && !speechTaskRunning && mediaStartRequested &&
      xStreamBufferBytesAvailable(speechBuffer) == 0 && !speechStopRequested) {
    speechStopRequested = true;
    Serial.println("[VOZ] Reproduccion terminada");
  }

  char target[18], current[18], attempted[18];
  bool isReady, isScanning, isConnected, isConnecting, isDisconnecting;
  portENTER_CRITICAL(&mutex);
  snprintf(target, sizeof(target), "%s", targetAddress);
  snprintf(current, sizeof(current), "%s", connectedAddress);
  snprintf(attempted, sizeof(attempted), "%s", connectionAddress);
  isReady = ready;
  isScanning = scanning;
  isConnected = connected;
  isConnecting = connecting;
  isDisconnecting = disconnecting;
  portEXIT_CRITICAL(&mutex);

  if (speechRequestedForAlert && now - lastAudioDiagnostics >= 2000) {
    lastAudioDiagnostics = now;
    Serial.printf("[VOZ] Estado: started=%d task=%d/%d stop=%d buffer=%u pcm=%u callbacks=%u underrun=%u rate=%lu\n",
      audioStarted, speechTaskRunning, speechTaskFinished, speechStopRequested,
      static_cast<unsigned int>(speechBuffer ? xStreamBufferBytesAvailable(speechBuffer) : 0),
      static_cast<unsigned int>(audioPcmBytesSent),
      static_cast<unsigned int>(audioCallbackCalls),
      static_cast<unsigned int>(audioUnderrunFrames),
      static_cast<unsigned long>(outputSampleRate));
  }
  if (now - lastStatusReport >= 30000) bluetoothRequestStatusReport();
  if (isReady && isConnected && strcmp(current, target) != 0 && !isDisconnecting) {
    esp_bd_addr_t address;
    if (parseAddress(current, address) && esp_a2d_source_disconnect(address) == ESP_OK) {
      portENTER_CRITICAL(&mutex);
      disconnecting = true;
      portEXIT_CRITICAL(&mutex);
    }
  } else if (isReady && !isConnected && !isConnecting && !isScanning && target[0] &&
             (lastConnectAttempt == 0 || now - lastConnectAttempt >= RetryMs)) {
    esp_bd_addr_t address;
    lastConnectAttempt = now;
    if (parseAddress(target, address) && esp_a2d_source_connect(address) == ESP_OK) {
      portENTER_CRITICAL(&mutex);
      connecting = true;
      failed = false;
      snprintf(connectionAddress, sizeof(connectionAddress), "%s", target);
      statusPending = true;
      portEXIT_CRITICAL(&mutex);
    } else {
      portENTER_CRITICAL(&mutex);
      failed = true;
      statusPending = true;
      portEXIT_CRITICAL(&mutex);
    }
  } else if (isConnecting && now - lastConnectAttempt >= ConnectTimeoutMs) {
    portENTER_CRITICAL(&mutex);
    connecting = false;
    failed = true;
    statusPending = true;
    portEXIT_CRITICAL(&mutex);
  }

  if (!mqtt.connected()) return;

  bool sendStarted, sendComplete, sendStatus, reportConnected, reportConnecting, reportFailed, reportScanFailed;
  int count;
  FoundDevice snapshot[MaxDevices];
  char reportAddress[18];
  portENTER_CRITICAL(&mutex);
  sendStarted = scanStartedPending;
  sendComplete = scanCompletePending;
  sendStatus = statusPending;
  scanStartedPending = false;
  scanCompletePending = false;
  statusPending = false;
  reportScanFailed = scanFailed;
  count = foundCount;
  if (sendComplete) memcpy(snapshot, found, sizeof(snapshot));
  reportConnected = connected;
  reportConnecting = connecting;
  reportFailed = failed;
  snprintf(reportAddress, sizeof(reportAddress), "%s", connected ? connectedAddress : connectionAddress);
  portEXIT_CRITICAL(&mutex);

  if (sendStarted) {
    JsonDocument doc;
    doc["type"] = "scan";
    doc["status"] = "started";
    if (!publishReport(mqtt, reportTopic, doc)) {
      portENTER_CRITICAL(&mutex); scanStartedPending = true; portEXIT_CRITICAL(&mutex);
    }
  }
  if (sendComplete) {
    JsonDocument doc;
    doc["type"] = "scan";
    doc["status"] = reportScanFailed ? "error" : "complete";
    if (reportScanFailed) doc["error"] = "No se pudo iniciar la búsqueda Bluetooth.";
    JsonArray devices = doc["devices"].to<JsonArray>();
    for (int i = 0; i < count; i++) {
      JsonObject item = devices.add<JsonObject>();
      item["address"] = snapshot[i].address;
      item["name"] = snapshot[i].name;
      item["rssi"] = snapshot[i].rssi;
      item["audioCapable"] = snapshot[i].audioCapable;
    }
    if (!publishReport(mqtt, reportTopic, doc)) {
      portENTER_CRITICAL(&mutex); scanCompletePending = true; portEXIT_CRITICAL(&mutex);
    }
  }
  if (sendStatus) {
    JsonDocument doc;
    doc["type"] = "connection";
    doc["status"] = reportConnected ? "connected" : reportConnecting ? "connecting" : reportFailed ? "error" : "disconnected";
    doc["address"] = reportAddress[0] ? reportAddress : nullptr;
    if (reportFailed) doc["error"] = isReady
      ? "No se pudo conectar al parlante; se reintentará."
      : "No se pudo inicializar Bluetooth Classic/A2DP.";
    if (publishReport(mqtt, reportTopic, doc)) {
      lastStatusReport = now;
    } else {
      portENTER_CRITICAL(&mutex); statusPending = true; portEXIT_CRITICAL(&mutex);
    }
  }
}
