module egn

// Non-blocking TCP/UDP networking.
//
// Servers accept up to 16 clients. TCP messages are length-prefixed; UDP
// messages are delivered as datagrams. Poll the network every frame and then
// drain the event queue.

const net_message_max = 1024

pub enum NetEventType {
	none         = 0
	connected    = 1
	disconnected = 2
	message      = 3
	error        = 4
}

// NetEvent is a connection or message notification.
pub struct NetEvent {
pub mut:
	event_type NetEventType
	client_id  u32
	size       u32
	data       [net_message_max]u8
}

// message returns the event payload as a string.
pub fn (e NetEvent) message() string {
	if e.size == 0 {
		return ''
	}
	n := if e.size > net_message_max { net_message_max } else { int(e.size) }
	return e.data[..n].bytestr()
}

fn net_event_from_c(event NetEvent) NetEvent {
	mut result := event
	return result
}

// Server accepts TCP or UDP clients and broadcasts/sends messages.
pub struct Server {
mut:
	handle voidptr
pub:
	port u16
	udp  bool
}

// server_create starts a TCP (udp = false) or UDP (udp = true) server.
pub fn server_create(port u16, udp bool) !Server {
	h := C.egn_net_server_create(port, udp)
	if h == 0 {
		return error('failed to create the network server on port ${port}')
	}
	return Server{
		handle: h
		port:   port
		udp:    udp
	}
}

pub fn (s &Server) is_valid() bool {
	return s.handle != 0
}

pub fn (s &Server) poll() {
	if s.handle != 0 {
		C.egn_net_server_poll(s.handle)
	}
}

pub fn (s &Server) broadcast(data []u8) bool {
	if s.handle == 0 || data.len == 0 {
		return false
	}
	return C.egn_net_server_broadcast(s.handle, data.data, u32(data.len))
}

pub fn (s &Server) broadcast_text(text string) bool {
	return s.broadcast(text.bytes())
}

pub fn (s &Server) send(client_id u32, data []u8) bool {
	if s.handle == 0 || data.len == 0 {
		return false
	}
	return C.egn_net_server_send(s.handle, client_id, data.data, u32(data.len))
}

pub fn (s &Server) send_text(client_id u32, text string) bool {
	return s.send(client_id, text.bytes())
}

pub fn (s &Server) poll_event() ?NetEvent {
	if s.handle == 0 {
		return none
	}
	mut event := NetEvent{}
	if C.egn_net_server_poll_event(s.handle, &event) {
		return net_event_from_c(event)
	}
	return none
}

pub fn (s &Server) client_count() u32 {
	if s.handle == 0 {
		return 0
	}
	return C.egn_net_server_client_count(s.handle)
}

pub fn (mut s Server) destroy() {
	if s.handle != 0 {
		C.egn_net_server_destroy(s.handle)
		s.handle = voidptr(0)
	}
}

// Client connects to a remote server.
pub struct Client {
mut:
	handle voidptr
pub:
	host string
	port u16
	udp  bool
}

pub fn client_create(host string, port u16, udp bool) !Client {
	h := C.egn_net_client_create(host.str, port, udp)
	if h == 0 {
		return error('failed to connect to ${host}:${port}')
	}
	return Client{
		handle: h
		host:   host
		port:   port
		udp:    udp
	}
}

pub fn (c &Client) is_valid() bool {
	return c.handle != 0
}

pub fn (c &Client) poll() {
	if c.handle != 0 {
		C.egn_net_client_poll(c.handle)
	}
}

pub fn (c &Client) send(data []u8) bool {
	if c.handle == 0 || data.len == 0 {
		return false
	}
	return C.egn_net_client_send(c.handle, data.data, u32(data.len))
}

pub fn (c &Client) send_text(text string) bool {
	return c.send(text.bytes())
}

pub fn (c &Client) poll_event() ?NetEvent {
	if c.handle == 0 {
		return none
	}
	mut event := NetEvent{}
	if C.egn_net_client_poll_event(c.handle, &event) {
		return net_event_from_c(event)
	}
	return none
}

pub fn (c &Client) connected() bool {
	if c.handle == 0 {
		return false
	}
	return C.egn_net_client_is_connected(c.handle)
}

pub fn (mut c Client) destroy() {
	if c.handle != 0 {
		C.egn_net_client_destroy(c.handle)
		c.handle = voidptr(0)
	}
}

// Host is a server plus a local client connected to 127.0.0.1.
pub struct Host {
mut:
	handle voidptr
pub:
	port   u16
	udp    bool
	server Server
	client Client
}

pub fn host_create(port u16, udp bool) !Host {
	h := C.egn_net_host_create(port, udp)
	if h == 0 {
		return error('failed to create the network host on port ${port}')
	}
	return Host{
		handle: h
		port:   port
		udp:    udp
		server: Server{
			port: port
			udp:  udp
		}
		client: Client{
			host: '127.0.0.1'
			port: port
			udp:  udp
		}
	}
}

pub fn (h &Host) is_valid() bool {
	return h.handle != 0
}

pub fn (h &Host) poll() {
	if h.handle != 0 {
		C.egn_net_host_poll(h.handle)
	}
}

pub fn (h &Host) send(data []u8) bool {
	if h.handle == 0 || data.len == 0 {
		return false
	}
	return C.egn_net_host_send(h.handle, data.data, u32(data.len))
}

pub fn (h &Host) send_text(text string) bool {
	return h.send(text.bytes())
}

pub fn (h &Host) broadcast(data []u8) bool {
	if h.handle == 0 || data.len == 0 {
		return false
	}
	return C.egn_net_host_broadcast(h.handle, data.data, u32(data.len))
}

pub fn (h &Host) broadcast_text(text string) bool {
	return h.broadcast(text.bytes())
}

pub fn (h &Host) poll_server_event() ?NetEvent {
	if h.handle == 0 {
		return none
	}
	mut event := NetEvent{}
	if C.egn_net_host_poll_server_event(h.handle, &event) {
		return net_event_from_c(event)
	}
	return none
}

pub fn (h &Host) poll_client_event() ?NetEvent {
	if h.handle == 0 {
		return none
	}
	mut event := NetEvent{}
	if C.egn_net_host_poll_client_event(h.handle, &event) {
		return net_event_from_c(event)
	}
	return none
}

pub fn (h &Host) client_count() u32 {
	if h.handle == 0 {
		return 0
	}
	return C.egn_net_host_client_count(h.handle)
}

pub fn (h &Host) local_client_connected() bool {
	if h.handle == 0 {
		return false
	}
	return C.egn_net_host_client_connected(h.handle)
}

pub fn (mut h Host) destroy() {
	if h.handle != 0 {
		C.egn_net_host_destroy(h.handle)
		h.handle = voidptr(0)
	}
}
