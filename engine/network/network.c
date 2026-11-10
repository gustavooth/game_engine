#include "network.h"
#include "core/logger.h"
#include "core/memo.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct NetEventQueue
{
  EgnNetEvent events[EGN_NET_EVENT_QUEUE];
  u32 head;
  u32 tail;
  u32 count;
} NetEventQueue;

typedef struct NetPeer
{
  b8 used;
  i32 fd;
  u32 id;
  struct sockaddr_storage address;
  socklen_t address_length;
  u8 read_buffer[EGN_NET_MESSAGE_MAX + 4096];
  u32 read_length;
} NetPeer;

struct EgnNetServer
{
  i32 socket;
  b8 udp;
  u16 port;
  NetPeer peers[EGN_NET_MAX_CLIENTS];
  NetEventQueue events;
};

struct EgnNetClient
{
  i32 socket;
  b8 udp;
  b8 connected;
  u16 port;
  u8 read_buffer[EGN_NET_MESSAGE_MAX + 4096];
  u32 read_length;
  NetEventQueue events;
  struct sockaddr_storage server_address;
  socklen_t server_address_length;
};

struct EgnNetHost
{
  EgnNetServer *server;
  EgnNetClient *client;
  b8 udp;
  u16 port;
};

static void event_push(NetEventQueue *queue, i32 type, u32 client_id,
                       const void *data, u32 size)
{
  if (!queue)
    return;

  if (queue->count == EGN_NET_EVENT_QUEUE)
  {
    queue->head = (queue->head + 1) % EGN_NET_EVENT_QUEUE;
    queue->count--;
  }

  EgnNetEvent *event = &queue->events[queue->tail];
  MEMOZERO(event, sizeof(EgnNetEvent));
  event->type = type;
  event->client_id = client_id;
  if (data && size > 0)
  {
    if (size > EGN_NET_MESSAGE_MAX)
      size = EGN_NET_MESSAGE_MAX;
    MEMOCPY(event->data, data, size);
  }
  event->size = size;

  queue->tail = (queue->tail + 1) % EGN_NET_EVENT_QUEUE;
  queue->count++;
}

static b8 event_pop(NetEventQueue *queue, EgnNetEvent *out_event)
{
  if (!queue || !out_event || queue->count == 0)
    return false;

  *out_event = queue->events[queue->head];
  queue->head = (queue->head + 1) % EGN_NET_EVENT_QUEUE;
  queue->count--;
  return true;
}

static b8 set_nonblocking(i32 fd)
{
  i32 flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static b8 send_all(i32 fd, const u8 *data, u64 size)
{
  u64 sent = 0;
  while (sent < size)
  {
    ssize_t result = send(fd, data + sent, (size_t)(size - sent), MSG_NOSIGNAL);
    if (result < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (result == 0)
      return false;
    sent += (u64)result;
  }
  return true;
}

static b8 tcp_send_message(i32 fd, const void *data, u32 size)
{
  u8 header[4];
  u32 network_size = htonl(size);
  MEMOCPY(header, &network_size, 4);
  if (!send_all(fd, header, 4))
    return false;
  return size == 0 || send_all(fd, (const u8 *)data, size);
}

static void parse_tcp_stream(NetPeer *peer, NetEventQueue *events)
{
  for (;;)
  {
    if (peer->read_length < 4)
      return;

    u32 network_size = 0;
    MEMOCPY(&network_size, peer->read_buffer, 4);
    u32 message_size = ntohl(network_size);
    if (message_size > EGN_NET_MESSAGE_MAX * 64)
    {
      peer->read_length = 0;
      return;
    }

    u32 frame_size = 4 + message_size;
    if (peer->read_length < frame_size)
      return;

    event_push(events, EGN_NET_EVENT_MESSAGE, peer->id,
               peer->read_buffer + 4, message_size);

    memmove(peer->read_buffer, peer->read_buffer + frame_size,
             peer->read_length - frame_size);
    peer->read_length -= frame_size;
  }
}

static void tcp_peer_receive(NetPeer *peer, NetEventQueue *events)
{
  u8 buffer[4096];
  for (;;)
  {
    ssize_t received = recv(peer->fd, buffer, sizeof(buffer), 0);
    if (received == 0)
    {
      event_push(events, EGN_NET_EVENT_DISCONNECTED, peer->id, 0, 0);
      close(peer->fd);
      MEMOZERO(peer, sizeof(NetPeer));
      return;
    }

    if (received < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      event_push(events, EGN_NET_EVENT_DISCONNECTED, peer->id, 0, 0);
      close(peer->fd);
      MEMOZERO(peer, sizeof(NetPeer));
      return;
    }

    if (peer->read_length + (u32)received > sizeof(peer->read_buffer))
      peer->read_length = 0;
    MEMOCPY(peer->read_buffer + peer->read_length, buffer, (u64)received);
    peer->read_length += (u32)received;
    parse_tcp_stream(peer, events);
  }
}

static b8 resolve_ipv4(const char *host, u16 port, struct sockaddr_in *out_address)
{
  MEMOZERO(out_address, sizeof(struct sockaddr_in));
  out_address->sin_family = AF_INET;
  out_address->sin_port = htons(port);

  if (!host || host[0] == '\0')
    host = "127.0.0.1";

  if (inet_pton(AF_INET, host, &out_address->sin_addr) == 1)
    return true;

  struct addrinfo hints;
  struct addrinfo *result = 0;
  MEMOZERO(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, 0, &hints, &result) != 0 || !result)
    return false;

  out_address->sin_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr;
  freeaddrinfo(result);
  return true;
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

EgnNetServer *egn_net_server_create(u16 port, b8 udp)
{
  EgnNetServer *server = MEMOZALLOC(sizeof(EgnNetServer), MEMO_SCENE);
  if (!server)
    return 0;

  server->udp = udp;
  server->port = port;
  server->socket = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
  if (server->socket < 0)
  {
    EGNERROR("NETWORK failed to create server socket");
    MEMOFREE(server);
    return 0;
  }

  i32 reuse = 1;
  setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in address;
  MEMOZERO(&address, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);

  if (bind(server->socket, (struct sockaddr *)&address, sizeof(address)) < 0)
  {
    EGNERROR("NETWORK failed to bind %s server to port %u",
             udp ? "UDP" : "TCP", (u32)port);
    close(server->socket);
    MEMOFREE(server);
    return 0;
  }

  if (!udp && listen(server->socket, EGN_NET_MAX_CLIENTS) < 0)
  {
    EGNERROR("NETWORK failed to listen on port %u", (u32)port);
    close(server->socket);
    MEMOFREE(server);
    return 0;
  }

  set_nonblocking(server->socket);
  EGNINFO("NETWORK %s server listening on port %u", udp ? "UDP" : "TCP", (u32)port);
  return server;
}

void egn_net_server_destroy(EgnNetServer *server)
{
  if (!server)
    return;

  for (u32 i = 0; i < EGN_NET_MAX_CLIENTS; i++)
  {
    if (server->peers[i].used)
      close(server->peers[i].fd);
  }

  if (server->socket >= 0)
    close(server->socket);
  MEMOFREE(server);
}

static NetPeer *server_peer_by_id(EgnNetServer *server, u32 id)
{
  for (u32 i = 0; i < EGN_NET_MAX_CLIENTS; i++)
  {
    if (server->peers[i].used && server->peers[i].id == id)
      return &server->peers[i];
  }
  return 0;
}

static NetPeer *server_udp_peer(EgnNetServer *server, struct sockaddr_storage *address,
                                socklen_t address_length)
{
  for (u32 i = 0; i < EGN_NET_MAX_CLIENTS; i++)
  {
    NetPeer *peer = &server->peers[i];
    if (!peer->used)
      continue;
    if (peer->address_length == address_length &&
        memcmp(&peer->address, address, address_length) == 0)
      return peer;
  }

  for (u32 i = 0; i < EGN_NET_MAX_CLIENTS; i++)
  {
    NetPeer *peer = &server->peers[i];
    if (peer->used)
      continue;

    peer->used = true;
    peer->fd = server->socket;
    peer->id = i + 1;
    peer->address = *address;
    peer->address_length = address_length;
    event_push(&server->events, EGN_NET_EVENT_CONNECTED, peer->id, 0, 0);
    return peer;
  }
  return 0;
}

void egn_net_server_poll(EgnNetServer *server)
{
  if (!server)
    return;

  if (server->udp)
  {
    for (;;)
    {
      u8 buffer[EGN_NET_MESSAGE_MAX * 2];
      struct sockaddr_storage address;
      socklen_t address_length = sizeof(address);
      ssize_t received = recvfrom(server->socket, buffer, sizeof(buffer), 0,
                                  (struct sockaddr *)&address, &address_length);
      if (received < 0)
      {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        break;
      }

      NetPeer *peer = server_udp_peer(server, &address, address_length);
      if (peer)
        event_push(&server->events, EGN_NET_EVENT_MESSAGE, peer->id, buffer, (u32)received);
    }
    return;
  }

  for (;;)
  {
    struct sockaddr_storage address;
    socklen_t address_length = sizeof(address);
    i32 client_socket = accept(server->socket, (struct sockaddr *)&address, &address_length);
    if (client_socket < 0)
      break;

    set_nonblocking(client_socket);

    NetPeer *slot = 0;
    for (u32 i = 0; i < EGN_NET_MAX_CLIENTS; i++)
    {
      if (!server->peers[i].used)
      {
        slot = &server->peers[i];
        slot->id = i + 1;
        break;
      }
    }

    if (!slot)
    {
      close(client_socket);
      continue;
    }

    slot->used = true;
    slot->fd = client_socket;
    slot->address = address;
    slot->address_length = address_length;
    slot->read_length = 0;
    event_push(&server->events, EGN_NET_EVENT_CONNECTED, slot->id, 0, 0);
  }

  for (u32 i = 0; i < EGN_NET_MAX_CLIENTS; i++)
  {
    if (server->peers[i].used)
      tcp_peer_receive(&server->peers[i], &server->events);
  }
}

b8 egn_net_server_send(EgnNetServer *server, u32 client_id, const void *data, u32 size)
{
  if (!server)
    return false;

  NetPeer *peer = server_peer_by_id(server, client_id);
  if (!peer)
    return false;

  if (server->udp)
    return sendto(server->socket, data, size, MSG_NOSIGNAL,
                  (struct sockaddr *)&peer->address, peer->address_length) >= 0;

  return tcp_send_message(peer->fd, data, size);
}

b8 egn_net_server_broadcast(EgnNetServer *server, const void *data, u32 size)
{
  if (!server)
    return false;

  b8 any = false;
  for (u32 i = 0; i < EGN_NET_MAX_CLIENTS; i++)
  {
    if (!server->peers[i].used)
      continue;
    if (egn_net_server_send(server, server->peers[i].id, data, size))
      any = true;
  }
  return any;
}

b8 egn_net_server_poll_event(EgnNetServer *server, EgnNetEvent *out_event)
{
  return server && event_pop(&server->events, out_event);
}

u32 egn_net_server_client_count(EgnNetServer *server)
{
  if (!server)
    return 0;
  u32 count = 0;
  for (u32 i = 0; i < EGN_NET_MAX_CLIENTS; i++)
    count += server->peers[i].used ? 1 : 0;
  return count;
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

EgnNetClient *egn_net_client_create(const char *host, u16 port, b8 udp)
{
  EgnNetClient *client = MEMOZALLOC(sizeof(EgnNetClient), MEMO_SCENE);
  if (!client)
    return 0;

  struct sockaddr_in address;
  if (!resolve_ipv4(host, port, &address))
  {
    EGNERROR("NETWORK could not resolve '%s'", host ? host : "127.0.0.1");
    MEMOFREE(client);
    return 0;
  }
  MEMOCPY(&client->server_address, &address, sizeof(address));
  client->server_address_length = sizeof(address);

  client->socket = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
  if (client->socket < 0)
  {
    MEMOFREE(client);
    return 0;
  }

  if (connect(client->socket, (struct sockaddr *)&address, sizeof(address)) < 0)
  {
    EGNERROR("NETWORK failed to connect to %s:%u", host ? host : "127.0.0.1", (u32)port);
    close(client->socket);
    MEMOFREE(client);
    return 0;
  }

  client->udp = udp;
  client->port = port;
  client->connected = true;
  set_nonblocking(client->socket);
  event_push(&client->events, EGN_NET_EVENT_CONNECTED, 0, 0, 0);
  EGNINFO("NETWORK %s client connected to %s:%u", udp ? "UDP" : "TCP",
          host ? host : "127.0.0.1", (u32)port);
  return client;
}

void egn_net_client_destroy(EgnNetClient *client)
{
  if (!client)
    return;
  if (client->socket >= 0)
    close(client->socket);
  MEMOFREE(client);
}

void egn_net_client_poll(EgnNetClient *client)
{
  if (!client || !client->connected)
    return;

  if (client->udp)
  {
    for (;;)
    {
      u8 buffer[EGN_NET_MESSAGE_MAX * 2];
      ssize_t received = recv(client->socket, buffer, sizeof(buffer), 0);
      if (received < 0)
        break;
      event_push(&client->events, EGN_NET_EVENT_MESSAGE, 0, buffer, (u32)received);
    }
    return;
  }

  u8 buffer[4096];
  for (;;)
  {
    ssize_t received = recv(client->socket, buffer, sizeof(buffer), 0);
    if (received == 0)
    {
      client->connected = false;
      event_push(&client->events, EGN_NET_EVENT_DISCONNECTED, 0, 0, 0);
      return;
    }
    if (received < 0)
    {
      if (errno != EAGAIN && errno != EWOULDBLOCK)
      {
        client->connected = false;
        event_push(&client->events, EGN_NET_EVENT_DISCONNECTED, 0, 0, 0);
      }
      return;
    }

    if (client->read_length + (u32)received > sizeof(client->read_buffer))
      client->read_length = 0;
    MEMOCPY(client->read_buffer + client->read_length, buffer, (u64)received);
    client->read_length += (u32)received;

    for (;;)
    {
      if (client->read_length < 4)
        break;
      u32 network_size = 0;
      MEMOCPY(&network_size, client->read_buffer, 4);
      u32 message_size = ntohl(network_size);
      if (message_size > EGN_NET_MESSAGE_MAX * 64)
      {
        client->read_length = 0;
        break;
      }
      u32 frame_size = 4 + message_size;
      if (client->read_length < frame_size)
        break;
      event_push(&client->events, EGN_NET_EVENT_MESSAGE, 0,
                 client->read_buffer + 4, message_size);
      memmove(client->read_buffer, client->read_buffer + frame_size,
               client->read_length - frame_size);
      client->read_length -= frame_size;
    }
  }
}

b8 egn_net_client_send(EgnNetClient *client, const void *data, u32 size)
{
  if (!client || !client->connected)
    return false;
  if (client->udp)
    return send(client->socket, data, size, MSG_NOSIGNAL) >= 0;
  return tcp_send_message(client->socket, data, size);
}

b8 egn_net_client_poll_event(EgnNetClient *client, EgnNetEvent *out_event)
{
  return client && event_pop(&client->events, out_event);
}

b8 egn_net_client_is_connected(EgnNetClient *client)
{
  return client && client->connected;
}

// ---------------------------------------------------------------------------
// Host
// ---------------------------------------------------------------------------

EgnNetHost *egn_net_host_create(u16 port, b8 udp)
{
  EgnNetHost *host = MEMOZALLOC(sizeof(EgnNetHost), MEMO_SCENE);
  if (!host)
    return 0;

  host->udp = udp;
  host->port = port;
  host->server = egn_net_server_create(port, udp);
  if (!host->server)
  {
    MEMOFREE(host);
    return 0;
  }

  host->client = egn_net_client_create("127.0.0.1", port, udp);
  if (!host->client)
  {
    egn_net_server_destroy(host->server);
    MEMOFREE(host);
    return 0;
  }

  return host;
}

void egn_net_host_destroy(EgnNetHost *host)
{
  if (!host)
    return;
  egn_net_client_destroy(host->client);
  egn_net_server_destroy(host->server);
  MEMOFREE(host);
}

void egn_net_host_poll(EgnNetHost *host)
{
  if (!host)
    return;
  egn_net_server_poll(host->server);
  egn_net_client_poll(host->client);
}

b8 egn_net_host_send(EgnNetHost *host, const void *data, u32 size)
{
  return host && egn_net_client_send(host->client, data, size);
}

b8 egn_net_host_broadcast(EgnNetHost *host, const void *data, u32 size)
{
  return host && egn_net_server_broadcast(host->server, data, size);
}

b8 egn_net_host_poll_server_event(EgnNetHost *host, EgnNetEvent *out_event)
{
  return host && egn_net_server_poll_event(host->server, out_event);
}

b8 egn_net_host_poll_client_event(EgnNetHost *host, EgnNetEvent *out_event)
{
  return host && egn_net_client_poll_event(host->client, out_event);
}

u32 egn_net_host_client_count(EgnNetHost *host)
{
  return host ? egn_net_server_client_count(host->server) : 0;
}

b8 egn_net_host_client_connected(EgnNetHost *host)
{
  return host && egn_net_client_is_connected(host->client);
}
