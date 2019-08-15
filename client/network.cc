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
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

#define CALL_RETRY(retvar, expression) \
  do {                                 \
    retvar = (expression);             \
  } while (retvar == -1 && errno == EINTR);

constexpr char KMulticastAddr[] = "239.255.223.01";
constexpr int kUdpPort = 0xdf0f;
constexpr uint32_t kUdpMagic = 0xdeafbeef;

constexpr int64_t kMaxReconnectDelayMs = 1000;
constexpr int64_t kConnectTimeoutMs = 10000;

static std::string server_host;
static int server_port;
static int client_socket_fd = -1;
static bool is_connected = false;
static struct addrinfo* addrinfo_main = nullptr;
static struct addrinfo* addrinfo_cur = nullptr;
static int64_t last_connect_attempt_time = -1;
static std::vector<std::vector<uint8_t>> send_buffer;
static std::vector<uint8_t> receive_buffer;

static int udp_socket = -1;
static uint8_t udp_receive_buffer[65536];
static uint8_t udp_send_buffer[65536];

static void* get_in_addr(struct sockaddr* sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void DisconnectFromServer() {
  if (client_socket_fd != -1) {
    close(client_socket_fd);
    client_socket_fd = -1;
  }

  addrinfo_cur = nullptr;
  if (addrinfo_main) {
    freeaddrinfo(addrinfo_main);
    addrinfo_main = nullptr;
  }

  is_connected = false;
  send_buffer.clear();
  receive_buffer.clear();
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
    CALL_RETRY(bytes_sent,
               send(client_socket_fd, buffer.data(), buffer.size(), 0));

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
    CALL_RETRY(num_bytes, recv(client_socket_fd, tmp, sizeof(tmp), 0));

    if (num_bytes == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      perror("recv() failed");
      DisconnectFromServer();
      break;
    }

    size_t old_buffer_size = receive_buffer.size();
    receive_buffer.resize(old_buffer_size + num_bytes);
    std::memcpy(receive_buffer.data() + old_buffer_size, tmp, num_bytes);
  }

  while (true) {
    if (receive_buffer.size() < 2)
      return std::vector<uint8_t>();

    uint16_t data_size = (uint16_t)(receive_buffer[0] << 8) | receive_buffer[1];
    if (data_size == 0) {
      // Continue here, to make sure empty packet always means no more data.
      receive_buffer.erase(receive_buffer.begin(), receive_buffer.begin() + 2);
      continue;
    }

    size_t total_size = data_size + 2;
    if (receive_buffer.size() < total_size)
      return std::vector<uint8_t>();

    result.resize(data_size);
    std::memcpy(result.data(), receive_buffer.data() + 2, data_size);
    receive_buffer.erase(receive_buffer.begin(),
                         receive_buffer.begin() + total_size);
    return result;
  }
}

static void FinishConnecting() {
  int rv;
  CALL_RETRY(rv, connect(client_socket_fd, addrinfo_cur->ai_addr,
                         addrinfo_cur->ai_addrlen));

  if (rv == -1) {
    if (errno == EALREADY)
      return;

    if (errno == EINPROGRESS) {
      // fprintf(stderr, "Will continue async connect to the server\n");
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

  if (client_socket_fd != -1 && is_connected)
    return;

  int64_t cur_time = CurrentTimeMillis();
  if (client_socket_fd != -1 &&
      cur_time >= last_connect_attempt_time + kConnectTimeoutMs) {
    fprintf(stderr, "Timed out connecting to the server\n");
    DisconnectFromServer();
  }

  if (client_socket_fd != -1) {
    FinishConnecting();
    if (client_socket_fd != -1)
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
              get_in_addr((struct sockaddr*)addrinfo_cur->ai_addr), s,
              sizeof(s));

    printf("Connecting to %s\n", s);
    client_socket_fd = socket(addrinfo_cur->ai_family,
                              addrinfo_cur->ai_socktype | SOCK_NONBLOCK,
                              addrinfo_cur->ai_protocol);
    if (client_socket_fd == -1) {
      perror("socket() failed");
      DisconnectFromServer();
      continue;
    }

    FinishConnecting();
    if (client_socket_fd != -1)
      break;
  }
}

void CloseUdp() {
  if (udp_socket >= 0) {
    close(udp_socket);
    udp_socket = -1;
  }
}

bool TryConnectUdp() {
  if (udp_socket >= 0) {
    return true;
  }

  static uint64_t last_connect_time = 0;
  if (CurrentTimeMillis() - last_connect_time < 5000) {
    return false;
  }
  last_connect_time = CurrentTimeMillis();

  udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_socket < 0) {
    fprintf(stderr, "can't create client socket\n");
    return false;
  }

  int optval = 1;
  if (setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &optval,
                 sizeof(optval)) < 0) {
    fprintf(stderr, "can't set reuseaddr option on socket: %s\n",
            strerror(errno));
    CloseUdp();
    return false;
  }

  int flags = fcntl(udp_socket, F_GETFL) | O_NONBLOCK;
  if (fcntl(udp_socket, F_SETFL, flags) < 0) {
    fprintf(stderr, "can't set socket to nonblocking mode: %s\n",
            strerror(errno));
    CloseUdp();
    return false;
  }

  u_char loop = 0;
  if (setsockopt(udp_socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
                 sizeof(loop)) < 0) {
    fprintf(stderr, "can't disable IP_MULTICAST_LOOP %s\n", strerror(errno));
    CloseUdp();
    return false;
  }

  sockaddr_in if_addr;
  memset(&if_addr, 0, sizeof(if_addr));
  if_addr.sin_family = AF_INET;
  if_addr.sin_port = htons((unsigned short)kUdpPort);
  if_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(udp_socket, (struct sockaddr*)&if_addr, sizeof(if_addr)) < 0) {
    fprintf(stderr, "can't bind client socket to port %d: %s\n", kUdpPort,
            strerror(errno));
    CloseUdp();
    return false;
  }

  struct ip_mreq mc_group;
  mc_group.imr_multiaddr.s_addr = inet_addr(KMulticastAddr);
  mc_group.imr_interface.s_addr =
      htonl(INADDR_ANY);  // inet_addr("203.106.93.94");
  if (setsockopt(udp_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mc_group,
                 sizeof(mc_group)) < 0) {
    fprintf(stderr, "can't join multicast group %s: %s\n", KMulticastAddr,
            strerror(errno));
    CloseUdp();
    return false;
  }

  fprintf(stderr, "Connected, bound to port %d, multicast group %s\n", kUdpPort,
          KMulticastAddr);
  return true;
}

uint8_t* ReceiveUdp(size_t* size) {
  *size = 0;
  if (!TryConnectUdp())
    return nullptr;

  sockaddr_in serveraddr;
  socklen_t serverlen = sizeof(serveraddr);
  ssize_t received_size;
  CALL_RETRY(
      received_size,
      recvfrom(udp_socket, udp_receive_buffer, sizeof(udp_receive_buffer), 0,
               reinterpret_cast<sockaddr*>(&serveraddr), &serverlen));

  if (received_size < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      return nullptr;  // no data on nonblocking socket
    }
    fprintf(stderr, "recvfrom failed on %d: %s\n", udp_socket, strerror(errno));
    CloseUdp();
    return nullptr;
  }

  uint32_t* magic_ptr = reinterpret_cast<uint32_t*>(&udp_receive_buffer[0]);
  if ((size_t)received_size <= sizeof(uint32_t) || *magic_ptr != kUdpMagic) {
    fprintf(stderr, "Received unexpected UDP data\n");
    return nullptr;
  }

  *size = received_size - sizeof(uint32_t);
  return udp_receive_buffer + sizeof(uint32_t);
}

bool BroadcastUdp(void* buf, size_t size) {
  if (size > sizeof(udp_send_buffer) - sizeof(uint32_t)) {
    fprintf(stderr, "Broadcast data is too large\n");
    return false;
  }

  if (!TryConnectUdp())
    return false;

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)kUdpPort);
  addr.sin_addr.s_addr = inet_addr(KMulticastAddr);

  ssize_t size_to_send = size + sizeof(uint32_t);
  uint32_t* magic_ptr = reinterpret_cast<uint32_t*>(&udp_send_buffer[0]);
  *magic_ptr = kUdpMagic;
  memcpy(udp_send_buffer + sizeof(uint32_t), buf, size);

  ssize_t sent_size;
  CALL_RETRY(sent_size, sendto(udp_socket, udp_send_buffer, size_to_send, 0,
                               (struct sockaddr*)&addr, sizeof(addr)));

  if (sent_size != size_to_send) {
    fprintf(stderr,
            "sendto broadcast of %d bytes on port %d, socket %d, "
            "result %d: %s\n",
            size, kUdpPort, udp_socket, size_to_send, strerror(errno));
    CloseUdp();
    return false;
  }

  return true;
}
