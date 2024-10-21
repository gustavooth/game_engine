#include "events.h"
#include "logger.h"
#include "containers/egnarray.h"

typedef struct registered_listener
{
    void *linstener;
    PFN_on_event callback;
} registered_listener;

typedef struct registered_event
{
    registered_listener *listeners;
} registered_event;

#define MAX_EVENT_CODE 4096

typedef struct events_state
{
    registered_event events[MAX_EVENT_CODE];
} events_state;

static events_state state;

b8 event_initialize()
{
    EGNINFO("Events system initialized!");
    return true;
}

void event_shutdown()
{
    for (u16 i = 0; i < MAX_EVENT_CODE; i++)
    {
        if (state.events[i].listeners != 0)
        {
            egnarray_destroy(state.events[i].listeners);
        }
    }
}

void event_register(u16 code, void *listener, PFN_on_event on_event)
{
    if (state.events[code].listeners == 0)
    {
        state.events[code].listeners = EGNARRAY(registered_listener);
    }

    registered_listener lis = {listener, on_event};
    egnarray_push(state.events[code].listeners, &lis);
}

b8 event_unregister(u16 code, PFN_on_event on_event)
{
    for (u16 i = 0; i < egnarray_len(state.events[code].listeners); i++)
    {
        if (state.events[code].listeners[i].callback == on_event)
        {
            egnarray_pop_at(state.events[code].listeners, i);
            return true;
        }
    }
    return false;
}

b8 event_fire(u16 code, void *sender, EventContext data)
{
    if (state.events[code].listeners == 0)
        return false;

    for (u16 i = 0; i < egnarray_len(state.events[code].listeners); i++)
    {
        if (state.events[code].listeners[i].callback(code, sender, data))
        {
            return true;
        }
    }

    return false;
}
