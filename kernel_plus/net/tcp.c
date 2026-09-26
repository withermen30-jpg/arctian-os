#include "tcp.h"
#include "ipv4.h"
#include "net.h"
#include "kernel.h"
#include <string.h>

static tcp_connection_t tcp_connections[TCP_MAX_CONNECTIONS];
static uint16_t tcp_ephemeral_port = 1024;
static uint32_t tcp_ticks = 0;

static inline uint16_t htons16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline uint32_t htonl32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

void tcp_init(void) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connections[i].state = TCP_STATE_CLOSED;
        tcp_connections[i].send_buf_len = 0;
        tcp_connections[i].recv_buf_head = 0;
        tcp_connections[i].recv_buf_tail = 0;
        tcp_connections[i].send_seq = 0;
        tcp_connections[i].recv_ack = 0;
        tcp_connections[i].send_unack = 0;
        tcp_connections[i].retransmit_count = 0;
        tcp_connections[i].retransmit_timer = 0;
        tcp_connections[i].last_activity = 0;
    }
}

static int tcp_alloc_conn(void) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (tcp_connections[i].state == TCP_STATE_CLOSED) {
            tcp_connections[i].send_buf_len = 0;
            tcp_connections[i].recv_buf_head = 0;
            tcp_connections[i].recv_buf_tail = 0;
            tcp_connections[i].retransmit_count = 0;
            tcp_connections[i].retransmit_timer = 0;
            tcp_connections[i].send_unack = 0;
            return i;
        }
    }
    return -1;
}

static uint16_t tcp_calc_checksum(const uint8_t *src_ip, const uint8_t *dst_ip,
                                  const uint8_t *tcp_segment, uint16_t tcp_len) {
    uint32_t sum = 0;
    uint8_t pseudo[12];
    for (int i = 0; i < 4; i++) pseudo[i] = src_ip[i];
    for (int i = 0; i < 4; i++) pseudo[4 + i] = dst_ip[i];
    pseudo[8] = 0;
    pseudo[9] = IPV4_PROTO_TCP;
    pseudo[10] = (tcp_len >> 8) & 0xFF;
    pseudo[11] = tcp_len & 0xFF;

    for (int i = 0; i < 12; i += 2) {
        sum += ((uint16_t)pseudo[i] << 8) | pseudo[i + 1];
    }
    for (uint16_t i = 0; i < tcp_len - 1; i += 2) {
        sum += ((uint16_t)tcp_segment[i] << 8) | tcp_segment[i + 1];
    }
    if (tcp_len & 1) sum += (uint16_t)tcp_segment[tcp_len - 1] << 8;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static bool tcp_send_segment(int conn_id, uint8_t flags,
                             const uint8_t *data, uint16_t data_len) {
    tcp_connection_t *conn = &tcp_connections[conn_id];
    if (conn->state == TCP_STATE_CLOSED) return false;

    uint16_t tcp_len = 20 + data_len;
    uint8_t segment[1500];

    tcp_header_t *hdr = (tcp_header_t *)segment;
    hdr->src_port  = htons16(conn->local_port);
    hdr->dst_port  = htons16(conn->remote_port);
    hdr->seq_num   = htonl32(conn->send_seq);
    hdr->ack_num   = htonl32(conn->recv_ack);
    hdr->data_offset = 0x50;
    hdr->flags     = flags;
    uint32_t used = conn->recv_buf_tail - conn->recv_buf_head;
    uint16_t win = (used < TCP_RXBUF_SIZE) ? (uint16_t)(TCP_RXBUF_SIZE - used) : 0;
    hdr->window    = htons16(win);
    hdr->urgent_ptr = 0;
    hdr->checksum  = 0;

    for (uint16_t i = 0; i < data_len; i++) segment[20 + i] = data[i];

    hdr->checksum = htons16(tcp_calc_checksum(conn->local_ip, conn->remote_ip,
                                              segment, tcp_len));

    bool ok = ipv4_send(conn->remote_ip, IPV4_PROTO_TCP, segment, tcp_len);
    if (ok && !(flags & TCP_FLAG_SYN)) {
        conn->send_seq += data_len;
    }
    return ok;
}

int tcp_connect(const uint8_t dst_ip[4], uint16_t dst_port) {
    int id = tcp_alloc_conn();
    if (id < 0) return -1;

    tcp_connection_t *conn = &tcp_connections[id];
    for (int i = 0; i < 4; i++) {
        conn->local_ip[i] = ipv4_config.src_ip[i];
        conn->remote_ip[i] = dst_ip[i];
    }
    conn->local_port  = tcp_ephemeral_port++;
    conn->remote_port = dst_port;
    conn->send_seq = 0x12345678;
    conn->recv_ack = 0;
    conn->send_unack = conn->send_seq;
    conn->state = TCP_STATE_SYN_SENT;
    conn->retransmit_count = 0;
    conn->retransmit_timer = tcp_ticks;
    conn->last_activity = tcp_ticks;
    conn->is_client = true;

    if (!tcp_send_segment(id, TCP_FLAG_SYN, 0, 0)) {
        conn->state = TCP_STATE_CLOSED;
        return -1;
    }
    conn->send_seq++;

    for (int retry = 0; retry < 600; retry++) {
        tcp_poll();
        if (conn->state == TCP_STATE_ESTABLISHED) return id;
        if (conn->state == TCP_STATE_CLOSED) return -1;
        for (int w = 0; w < 100000; w++) { __asm__ volatile("pause"); }
    }
    conn->state = TCP_STATE_CLOSED;
    return -1;
}

bool tcp_send(int conn_id, const uint8_t *data, uint16_t length) {
    tcp_connection_t *conn = &tcp_connections[conn_id];
    if (conn->state != TCP_STATE_ESTABLISHED) return false;

    uint32_t offset = 0;
    while (offset < length) {
        uint16_t chunk = length - offset;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        if (!tcp_send_segment(conn_id, TCP_FLAG_PSH | TCP_FLAG_ACK,
                              data + offset, chunk)) return false;
        offset += chunk;
    }
    conn->last_activity = tcp_ticks;
    return true;
}

int tcp_receive(int conn_id, uint8_t *buf, uint16_t buf_size) {
    tcp_connection_t *conn = &tcp_connections[conn_id];
    if (conn->state == TCP_STATE_CLOSED ||
        conn->state == TCP_STATE_TIME_WAIT) return -1;

    tcp_poll();

    uint32_t available = conn->recv_buf_tail - conn->recv_buf_head;
    if (available == 0) return 0;
    if (available > buf_size) available = buf_size;

    memcpy(buf, &conn->recv_buffer[conn->recv_buf_head], available);

    conn->recv_buf_head += available;
    if (conn->recv_buf_head == conn->recv_buf_tail) {
        conn->recv_buf_head = 0;
        conn->recv_buf_tail = 0;
    } else if (conn->recv_buf_head > 0) {
        uint32_t remain = conn->recv_buf_tail - conn->recv_buf_head;
        memmove(conn->recv_buffer, &conn->recv_buffer[conn->recv_buf_head], remain);
        conn->recv_buf_head = 0;
        conn->recv_buf_tail = remain;
    }

    if (conn->state == TCP_STATE_ESTABLISHED || conn->state == TCP_STATE_CLOSE_WAIT)
        tcp_send_segment(conn_id, TCP_FLAG_ACK, 0, 0);

    return (int)available;
}

bool tcp_close(int conn_id) {
    tcp_connection_t *conn = &tcp_connections[conn_id];
    if (conn->state != TCP_STATE_ESTABLISHED &&
        conn->state != TCP_STATE_CLOSE_WAIT) return false;

    conn->state = TCP_STATE_FIN_WAIT1;
    tcp_send_segment(conn_id, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    conn->send_seq++;

    for (int retry = 0; retry < 20; retry++) {
        tcp_poll();
        if (conn->state == TCP_STATE_CLOSED ||
            conn->state == TCP_STATE_TIME_WAIT) return true;
        for (int w = 0; w < 50000; w++) { __asm__ volatile("pause"); }
    }
    conn->state = TCP_STATE_CLOSED;
    return true;
}

int tcp_state(int conn_id) {
    return tcp_connections[conn_id].state;
}

bool tcp_receive_packet(const uint8_t *src_ip, const uint8_t *packet, uint16_t length) {
    if (length < 20) return false;

    tcp_header_t *hdr = (tcp_header_t *)packet;
    uint16_t src_port = htons16(hdr->src_port);
    uint16_t dst_port = htons16(hdr->dst_port);

    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connection_t *conn = &tcp_connections[i];
        if (conn->state == TCP_STATE_CLOSED) continue;

        if (conn->remote_port == src_port && conn->local_port == dst_port &&
            conn->remote_ip[0] == src_ip[0] && conn->remote_ip[1] == src_ip[1] &&
            conn->remote_ip[2] == src_ip[2] && conn->remote_ip[3] == src_ip[3]) {

            conn->last_activity = tcp_ticks;
            uint32_t seq = htonl32(hdr->seq_num);
            uint32_t ack = htonl32(hdr->ack_num);
            uint8_t flags = hdr->flags;
            uint8_t data_offset = hdr->data_offset >> 4;
            uint16_t hdr_len = data_offset * 4;
            uint16_t data_len = length - hdr_len;
            const uint8_t *data = packet + hdr_len;

            if (conn->state == TCP_STATE_SYN_SENT) {
                if (flags & TCP_FLAG_SYN) {
                    conn->recv_ack = seq + 1;
                    conn->recv_seq = seq;
                    conn->send_seq = ack;
                    conn->state = TCP_STATE_ESTABLISHED;
                } else if (flags & TCP_FLAG_RST) {
                    conn->state = TCP_STATE_CLOSED;
                }
                return true;
            }

            if (conn->state == TCP_STATE_FIN_WAIT1) {
                if (flags & TCP_FLAG_ACK) {
                    conn->state = TCP_STATE_FIN_WAIT2;
                    conn->send_seq = ack;
                }
                return true;
            }

            if (conn->state == TCP_STATE_FIN_WAIT2 && (flags & TCP_FLAG_FIN)) {
                conn->recv_ack = seq + 1;
                conn->state = TCP_STATE_TIME_WAIT;
                tcp_send_segment(i, TCP_FLAG_ACK, 0, 0);
                return true;
            }

            if (conn->state == TCP_STATE_CLOSE_WAIT) {
                if (flags & TCP_FLAG_ACK) {
                    conn->send_seq = ack;
                }
                if (data_len > 0) {
                    if ((uint32_t)conn->recv_buf_tail + data_len <= TCP_RXBUF_SIZE) {
                        memcpy(&conn->recv_buffer[conn->recv_buf_tail], data, data_len);
                        conn->recv_buf_tail += data_len;
                        conn->recv_ack = seq + data_len;
                    }
                    tcp_send_segment(i, TCP_FLAG_ACK, 0, 0);
                }
                return true;
            }

            if (conn->state == TCP_STATE_ESTABLISHED) {
                if (flags & TCP_FLAG_ACK) {
                    conn->send_seq = ack;
                }

                if (data_len > 0 &&
                    (uint32_t)conn->recv_buf_tail + data_len <= TCP_RXBUF_SIZE) {
                    memcpy(&conn->recv_buffer[conn->recv_buf_tail], data, data_len);
                    conn->recv_buf_tail += data_len;
                    conn->recv_ack = seq + data_len;
                }

                if (flags & TCP_FLAG_FIN) {
                    conn->recv_ack = seq + data_len + 1;
                    conn->state = TCP_STATE_CLOSE_WAIT;
                    tcp_send_segment(i, TCP_FLAG_ACK, 0, 0);
                    return true;
                }

                if (data_len > 0) {
                    tcp_send_segment(i, TCP_FLAG_ACK, 0, 0);
                }
                return true;
            }

            return true;
        }
    }
    return false;
}

void tcp_poll(void) {
    tcp_ticks++;

    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connection_t *conn = &tcp_connections[i];
        if (conn->state == TCP_STATE_CLOSED ||
            conn->state == TCP_STATE_TIME_WAIT) continue;

        ipv4_poll();
    }

    if (tcp_ticks % 50 == 0) {
        for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
            if (tcp_connections[i].state == TCP_STATE_TIME_WAIT &&
                tcp_ticks - tcp_connections[i].last_activity > 200) {
                tcp_connections[i].state = TCP_STATE_CLOSED;
            }
        }
    }
}
