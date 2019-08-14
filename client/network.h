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

class UdpSocketNetwork {
 public:
  bool TryConnect();
  void Close();

  size_t Receive(void* buf, size_t size);
  bool Broadcast(void* buf, size_t size);

 private:
  int fd_ = -1;
};
