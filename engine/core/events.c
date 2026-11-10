#include "events.h"
#include "logger.h"
#include "containers/egnarray.h"

typedef struct RegisteredListener
{
  void *linstener;
  PFN_on_event callback;
} RegisteredListener;

typedef struct RegisteredEvent
{
  RegisteredListener *listeners;
} RegisteredEvent;

#define MAX_EVENT_CODE 4096

typedef struct EventsState
{
  RegisteredEvent events[MAX_EVENT_CODE];
} EventsState;

static EventsState *state_ptr;

b8 events_initialize(u64 *out_memory_requirement, void *state)
{
  if (!state)
  {
    *out_memory_requirement = sizeof(EventsState);
    return false;
  }

  state_ptr = (EventsState *)state;

  EGNINFO("Core Events initialized!");
  return true;
}

void events_shutdown()
{
  for (u16 i = 0; i < MAX_EVENT_CODE; i++)
  {
    if (state_ptr->events[i].listeners != 0)
    {
      egnarray_destroy(state_ptr->events[i].listeners);
    }
  }

  state_ptr = 0;
}

void events_register(u16 code, void *listener, PFN_on_event on_event)
{
  if (state_ptr->events[code].listeners == 0)
  {
    state_ptr->events[code].listeners = EGNARRAY(RegisteredListener);
  }

  RegisteredListener lis = {listener, on_event};
  egnarray_push(state_ptr->events[code].listeners, &lis);
}

b8 events_unregister(u16 code, PFN_on_event on_event)
{
  for (u16 i = 0; i < egnarray_len(state_ptr->events[code].listeners); i++)
  {
    if (state_ptr->events[code].listeners[i].callback == on_event)
    {
      egnarray_pop_at(state_ptr->events[code].listeners, i);
      return true;
    }
  }
  return false;
}

b8 events_fire(u16 code, void *sender, EventContext data)
{
  if (state_ptr->events[code].listeners == 0)
    return false;

  for (u16 i = 0; i < egnarray_len(state_ptr->events[code].listeners); i++)
  {
    if (state_ptr->events[code].listeners[i].callback(code, sender, data))
    {
      return true;
    }
  }

  return false;
}

b8 events_fire2(u16 code, void *sender, u64 data0, u64 data1)
{
  EventContext ctx = {0};
  ctx.data.u64[0] = data0;
  ctx.data.u64[1] = data1;
  return events_fire(code, sender, ctx);
}
