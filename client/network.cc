#include "network.h"

#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static std::string server_host;
static int server_port;
static int socket_fd = -1;
static std::vector<std::vector<uint8_t>> send_buffer;

static void* get_in_addr(struct sockaddr* sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void DisconnectFromServer() {
  if (socket_fd != -1) {
    close(socket_fd);
    socket_fd = -1;
  }
}

void SetServerAddress(std::string host, int port) {
  server_host = host;
  server_port = port;
}

static void TrySendToServer() {
  while (!send_buffer.empty()) {
    std::vector<uint8_t>& buffer = send_buffer.front();
    ssize_t bytes_sent = send(socket_fd, buffer.data(), buffer.size(), 0);

    if (bytes_sent == -1) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      perror("send() failed");
      DisconnectFromServer();
      return;
    }

    buffer.erase(buffer.begin(), buffer.begin() + bytes_sent);
    if (buffer.empty())
      send_buffer.erase(send_buffer.begin());
  }
}

void SendToServer(const uint8_t* new_data, size_t new_data_len) {
  if (new_data_len)
    send_buffer.emplace_back(new_data, new_data + new_data_len);

  TrySendToServer();
}

std::vector<uint8_t> ReadSocketCommand() {
  static uint8_t tmp[1024];
  static std::vector<uint8_t> cached;

  std::vector<uint8_t> result;
  if (socket_fd == -1)
    return std::vector<uint8_t>();

  while (true) {
    // Try to send the data when we can, as we have no proper AsyncIO handling.
    TrySendToServer();
    if (socket_fd == -1)
      return std::vector<uint8_t>();

    ssize_t num_bytes = recv(socket_fd, tmp, sizeof(tmp), 0);
    if (num_bytes == -1) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      perror("recv() failed");
      DisconnectFromServer();
      break;
    }

    size_t old_cached_size = cached.size();
    cached.resize(old_cached_size + num_bytes);
    std::memcpy(cached.data() + old_cached_size, tmp, num_bytes);
  }

  while (true) {
    if (cached.size() < 2)
      return std::vector<uint8_t>();

    uint16_t data_size = (uint16_t) (cached[0] << 8) | cached[1];
    if (data_size == 0) {
      // Continue here, to make sure empty packet always means no more data.
      cached.erase(cached.begin(), cached.begin() + 2);
      continue;
    }

    size_t total_size = data_size + 2;
    if (cached.size() < total_size)
      return std::vector<uint8_t>();

    result.resize(data_size);
    std::memcpy(result.data(), cached.data() + 2, data_size);
    cached.erase(cached.begin(), cached.begin() + total_size);
    return result;
  }
}

void ReconnectToServer() {
  DisconnectFromServer();

  struct addrinfo hints;
  std::memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[20];
  std::snprintf(port_str, sizeof(port_str), "%d", server_port);

  struct addrinfo* servinfo;
  int rv = getaddrinfo(server_host.c_str(), port_str, &hints, &servinfo);
  if (rv != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return;
  }

  struct addrinfo* p;
  for (p = servinfo; p != NULL; p = p->ai_next) {
    char s[INET6_ADDRSTRLEN];
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr*)p->ai_addr), s,
              sizeof(s));
    printf("Connecting to %s\n", s);

    socket_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (socket_fd == -1) {
      perror("socket() failed");
      continue;
    }

    if (connect(socket_fd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("connect() failed");
      DisconnectFromServer();
      continue;
    }

    if (fcntl(socket_fd, F_SETFL, fcntl(socket_fd, F_GETFL) | O_NONBLOCK) < 0) {
      perror("fcntl(O_NONBLOCK) failed");
      DisconnectFromServer();
      continue;
    }

    break;
  }

  if (p) {
    fprintf(stderr, "Connected to server\n");
  } else {
    fprintf(stderr, "Failed to connect to server\n");
  }

  freeaddrinfo(servinfo);
}
