#pragma once

#include "defines.h"

#define EGN_NET_MAX_CLIENTS 16
#define EGN_NET_MESSAGE_MAX 1024
#define EGN_NET_EVENT_QUEUE 128

enum
{
  EGN_NET_EVENT_NONE = 0,
  EGN_NET_EVENT_CONNECTED = 1,
  EGN_NET_EVENT_DISCONNECTED = 2,
  EGN_NET_EVENT_MESSAGE = 3,
  EGN_NET_EVENT_ERROR = 4
};

typedef struct EgnNetEvent
{
  i32 type;
  u32 client_id;
  u32 size;
  u8 data[EGN_NET_MESSAGE_MAX];
} EgnNetEvent;

typedef struct EgnNetServer EgnNetServer;
typedef struct EgnNetClient EgnNetClient;
typedef struct EgnNetHost EgnNetHost;

// udp = false creates a TCP server/client, true creates a UDP one.
EGNAPI EgnNetServer *egn_net_server_create(u16 port, b8 udp);
EGNAPI void egn_net_server_destroy(EgnNetServer *server);
EGNAPI void egn_net_server_poll(EgnNetServer *server);
EGNAPI b8 egn_net_server_broadcast(EgnNetServer *server, const void *data, u32 size);
EGNAPI b8 egn_net_server_send(EgnNetServer *server, u32 client_id,
                              const void *data, u32 size);
EGNAPI b8 egn_net_server_poll_event(EgnNetServer *server, EgnNetEvent *out_event);
EGNAPI u32 egn_net_server_client_count(EgnNetServer *server);

EGNAPI EgnNetClient *egn_net_client_create(const char *host, u16 port, b8 udp);
EGNAPI void egn_net_client_destroy(EgnNetClient *client);
EGNAPI void egn_net_client_poll(EgnNetClient *client);
EGNAPI b8 egn_net_client_send(EgnNetClient *client, const void *data, u32 size);
EGNAPI b8 egn_net_client_poll_event(EgnNetClient *client, EgnNetEvent *out_event);
EGNAPI b8 egn_net_client_is_connected(EgnNetClient *client);

// A host is a server plus a local client connected to 127.0.0.1.
EGNAPI EgnNetHost *egn_net_host_create(u16 port, b8 udp);
EGNAPI void egn_net_host_destroy(EgnNetHost *host);
EGNAPI void egn_net_host_poll(EgnNetHost *host);
EGNAPI b8 egn_net_host_send(EgnNetHost *host, const void *data, u32 size);
EGNAPI b8 egn_net_host_broadcast(EgnNetHost *host, const void *data, u32 size);
EGNAPI b8 egn_net_host_poll_server_event(EgnNetHost *host, EgnNetEvent *out_event);
EGNAPI b8 egn_net_host_poll_client_event(EgnNetHost *host, EgnNetEvent *out_event);
EGNAPI u32 egn_net_host_client_count(EgnNetHost *host);
EGNAPI b8 egn_net_host_client_connected(EgnNetHost *host);
