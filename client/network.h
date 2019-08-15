#pragma once

#include <string>
#include <vector>

#include <stdint.h>
#include <stdlib.h>

void SetServerAddress(std::string host, int port);
void DisconnectFromServer();
void ConnectToServerIfNecessary();

void SendToServer(const uint8_t* new_data, size_t new_data_len);
std::vector<uint8_t> ReadSocketCommand();

bool TryConnectUdp();
void CloseUdp();
uint8_t* ReceiveUdp(size_t* size);
bool BroadcastUdp(void* buf, size_t size);
