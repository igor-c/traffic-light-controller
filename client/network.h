#pragma once

#include <string>
#include <vector>

#include <stdint.h>
#include <stdlib.h>

void SetServerAddress(std::string host, int port);
void DisconnectFromServer();
void ReconnectToServer();

void SendToServer(const uint8_t* new_data, size_t new_data_len);
std::vector<uint8_t> ReadSocketCommand();
