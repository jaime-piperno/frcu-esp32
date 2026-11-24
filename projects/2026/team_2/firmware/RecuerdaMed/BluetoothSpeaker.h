#pragma once

#include <Arduino.h>
#include <PubSubClient.h>

// Bluetooth Classic / A2DP Source with streamed speech from the local API.
bool bluetoothBegin();
bool bluetoothIsConnected();
bool bluetoothUseBuzzer();
bool bluetoothQueueSpeech(const String& url);
void bluetoothStopSpeech();
void bluetoothStartScan();
void bluetoothSetTarget(const char* address);
void bluetoothRequestStatusReport();
void bluetoothLoop(PubSubClient& mqtt, const String& reportTopic);
