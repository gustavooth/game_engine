// TCP framing regression test: messages of every size up to the maximum must
// arrive intact and in order, and oversized messages must be refused.
#include "network/network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
  const u16 port = 47231;
  EgnNetServer *server = egn_net_server_create(port, false);
  assert(server);
  EgnNetClient *client = egn_net_client_create("127.0.0.1", port, false);
  assert(client);

  u8 payload[EGN_NET_MESSAGE_MAX + 1];
  for (u32 i = 0; i < sizeof(payload); i++)
    payload[i] = (u8)(i * 7 + 3);

  // Oversized messages are rejected instead of corrupting the stream.
  assert(!egn_net_client_send(client, payload, EGN_NET_MESSAGE_MAX + 1));

  // Sizes chosen to straddle the read buffer in every possible way.
  const u32 sizes[] = {0, 1, 3, 4, 5, 500, 1023, 1024, 1024, 1, 1024, 777, 1024, 2};
  const u32 count = sizeof(sizes) / sizeof(sizes[0]);
  const u32 rounds = 40;

  u32 received = 0;
  EgnNetEvent event;
  for (u32 round = 0; round < rounds; round++)
  {
    for (u32 i = 0; i < count; i++)
      assert(egn_net_client_send(client, payload, sizes[i]));

    // Drain as we go so the event queue (128 entries) never overflows.
    for (u32 attempt = 0; attempt < 200 && received < (round + 1) * count; attempt++)
    {
      egn_net_server_poll(server);
      while (egn_net_server_poll_event(server, &event))
      {
        if (event.type != EGN_NET_EVENT_MESSAGE)
          continue;
        u32 expected = sizes[received % count];
        assert(event.size == expected);
        assert(memcmp(event.data, payload, expected) == 0);
        received++;
      }
      if (received < (round + 1) * count)
        usleep(1000);
    }
  }
  assert(received == rounds * count);

  // Server -> client direction.
  assert(egn_net_server_broadcast(server, payload, EGN_NET_MESSAGE_MAX));
  b8 got = false;
  for (u32 attempt = 0; attempt < 200 && !got; attempt++)
  {
    egn_net_client_poll(client);
    while (egn_net_client_poll_event(client, &event))
    {
      if (event.type == EGN_NET_EVENT_MESSAGE)
      {
        assert(event.size == EGN_NET_MESSAGE_MAX);
        assert(memcmp(event.data, payload, EGN_NET_MESSAGE_MAX) == 0);
        got = true;
      }
    }
    if (!got)
      usleep(1000);
  }
  assert(got);

  egn_net_client_destroy(client);
  egn_net_server_destroy(server);
  printf("network_test: all checks passed\n");
  return 0;
}
