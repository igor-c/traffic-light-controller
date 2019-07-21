#include "network.h"

#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define CALL_RETRY(retvar, expression) do {  \
    retvar = (expression);                   \
} while (retvar == -1 && errno == EINTR);

constexpr int64_t kMaxReconnectDelayMs = 1000;
constexpr int64_t kConnectTimeoutMs = 10000;

static std::string server_host;
static int server_port;
static int socket_fd = -1;
static bool is_connected = false;
static struct addrinfo* addrinfo_main = nullptr;
static struct addrinfo* addrinfo_cur = nullptr;
static int64_t last_connect_attempt_time = -1;
static std::vector<std::vector<uint8_t>> send_buffer;

static int64_t GetTimeMillis() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

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

  addrinfo_cur = nullptr;
  if (addrinfo_main) {
    freeaddrinfo(addrinfo_main);
    addrinfo_main = nullptr;
  }

  is_connected = false;
  send_buffer.clear();
}

void SetServerAddress(std::string host, int port) {
  server_host = host;
  server_port = port;
  DisconnectFromServer();
  ConnectToServerIfNecessary();
}

static void TrySendToServer() {
  if (!is_connected)
    return;

  while (!send_buffer.empty()) {
    std::vector<uint8_t>& buffer = send_buffer.front();
    ssize_t bytes_sent;
    CALL_RETRY(bytes_sent, send(socket_fd, buffer.data(), buffer.size(), 0));

    if (bytes_sent == -1) {
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
  ConnectToServerIfNecessary();

  // While not connected - discard all outgoing data.
  if (!is_connected)
    return;

  if (new_data_len)
    send_buffer.emplace_back(new_data, new_data + new_data_len);

  TrySendToServer();
}

std::vector<uint8_t> ReadSocketCommand() {
  static uint8_t tmp[1024];
  static std::vector<uint8_t> cached;

  ConnectToServerIfNecessary();

  std::vector<uint8_t> result;
  if (!is_connected)
    return std::vector<uint8_t>();

  while (true) {
    // Try to send the data when we can, as we have no proper AsyncIO handling.
    TrySendToServer();
    if (!is_connected)
      return std::vector<uint8_t>();

    ssize_t num_bytes;
    CALL_RETRY(num_bytes, recv(socket_fd, tmp, sizeof(tmp), 0));

    if (num_bytes == -1) {
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

static void FinishConnecting() {
  int rv;
  CALL_RETRY(rv, connect(socket_fd, addrinfo_cur->ai_addr, addrinfo_cur->ai_addrlen));

  if (rv == -1) {
    if (errno == EALREADY)
      return;

    if (errno == EINPROGRESS) {
      fprintf(stderr, "Will continue async connect to the server\n");
      return;
    }

    perror("connect() failed");
    DisconnectFromServer();
    return;
  }

  freeaddrinfo(addrinfo_main);
  addrinfo_main = nullptr;
  addrinfo_cur = nullptr;
  is_connected = true;
}

void ConnectToServerIfNecessary() {
  if (server_host == "")
    return;

  if (socket_fd != -1 && is_connected)
    return;

  int64_t cur_time = GetTimeMillis();
  if (socket_fd != -1 &&
      cur_time >= last_connect_attempt_time + kConnectTimeoutMs) {
    fprintf(stderr, "Timed out connecting to the server\n");
    DisconnectFromServer();
  }

  if (socket_fd != -1) {
    FinishConnecting();
    if (socket_fd != -1)
      return;
  }

  if (last_connect_attempt_time != -1 &&
      cur_time < last_connect_attempt_time + kMaxReconnectDelayMs) {
    return;
  }

  last_connect_attempt_time = cur_time;

  struct addrinfo hints;
  std::memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[20];
  std::snprintf(port_str, sizeof(port_str), "%d", server_port);

  int rv = getaddrinfo(server_host.c_str(), port_str, &hints, &addrinfo_main);
  if (rv != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return;
  }

  for (addrinfo_cur = addrinfo_main; addrinfo_cur != nullptr;
       addrinfo_cur = addrinfo_cur->ai_next) {
    char s[INET6_ADDRSTRLEN];
    inet_ntop(addrinfo_cur->ai_family,
              get_in_addr((struct sockaddr*)addrinfo_cur->ai_addr),
              s,
              sizeof(s));

    printf("Connecting to %s\n", s);
    socket_fd = socket(addrinfo_cur->ai_family,
                       addrinfo_cur->ai_socktype | SOCK_NONBLOCK,
                       addrinfo_cur->ai_protocol);
    if (socket_fd == -1) {
      perror("socket() failed");
      DisconnectFromServer();
      continue;
    }

    FinishConnecting();
    if (socket_fd != -1)
      break;
  }
}
