/* Updates a single TCP connection through polling. */
enum { TCP_FIN = 1, TCP_SYN = 2, TCP_RST = 4, TCP_PSH = 8, TCP_ACK = 16 };

enum { TS_CLOSED, TS_SYN_SENT, TS_ESTAB, TS_CLOSE_WAIT,
       TS_FIN_WAIT1, TS_FIN_WAIT2, TS_LAST_ACK, TS_TIME_WAIT };

enum { TA_SEND_SYN = 1, TA_SEND_ACK = 2, TA_SEND_FIN = 4, TA_SEND_DATA = 8,
       TA_CONNECTED = 16, TA_DELIVER = 32, TA_CLOSED = 64, TA_ERROR = 128 };

#define TCP_RTO    100
#define TCP_MAXTRY 4

typedef struct {
    int state;
    u32 snd_una, snd_nxt;
    u32 rcv_nxt;
    u32 sent_at;
    int tries;
} Tcb;

static inline int tcp_seq_le(u32 a, u32 b)
{
    return (i32)(a - b) <= 0;
}

static inline int tcp_open(Tcb *t, u32 iss, u32 now)
{
    t->state = TS_SYN_SENT;
    t->snd_una = iss;
    t->snd_nxt = iss + 1;
    t->rcv_nxt = 0;
    t->sent_at = now;
    t->tries = 0;
    return TA_SEND_SYN;
}

static inline int tcp_mark_sent(Tcb *t, int len, u32 now)
{
    t->snd_nxt += (u32)len;
    t->sent_at = now;
    t->tries = 0;
    return 0;
}

static inline int tcp_input(Tcb *t, int flags, u32 seq, u32 ack, int paylen,
                     int *del_off, int *del_len)
{
    *del_off = 0;
    *del_len = 0;
    if (t->state == TS_CLOSED) return 0;
    if (flags & TCP_RST) {
        t->state = TS_CLOSED;
        return TA_ERROR | TA_CLOSED;
    }

    if (t->state == TS_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) &&
            ack == t->snd_nxt) {
            t->rcv_nxt = seq + 1;
            t->snd_una = ack;
            t->state = TS_ESTAB;
            t->tries = 0;
            return TA_SEND_ACK | TA_CONNECTED;
        }
        return 0;
    }

    int act = 0;

    if (flags & TCP_ACK) {
        if (tcp_seq_le(t->snd_una, ack) && tcp_seq_le(ack, t->snd_nxt)) {
            if (ack != t->snd_una) t->tries = 0;
            t->snd_una = ack;
        }
        if (t->snd_una == t->snd_nxt) {
            if (t->state == TS_FIN_WAIT1) t->state = TS_FIN_WAIT2;
            else if (t->state == TS_LAST_ACK) {
                t->state = TS_CLOSED;
                return TA_CLOSED;
            }
        }
    }

    if (paylen > 0) {
        if (seq == t->rcv_nxt) {
            *del_len = paylen;
        } else if (tcp_seq_le(seq, t->rcv_nxt)) {
            u32 off = t->rcv_nxt - seq;
            if (off < (u32)paylen) {
                *del_off = (int)off;
                *del_len = paylen - (int)off;
            }
        }
        if (*del_len) {
            t->rcv_nxt += (u32)*del_len;
            act |= TA_DELIVER;
        }
        act |= TA_SEND_ACK;
    }

    if ((flags & TCP_FIN) && seq + (u32)paylen == t->rcv_nxt) {
        t->rcv_nxt += 1;
        act |= TA_SEND_ACK;
        if (t->state == TS_ESTAB) t->state = TS_CLOSE_WAIT;
        else if (t->state == TS_FIN_WAIT1) t->state = TS_TIME_WAIT;
        else if (t->state == TS_FIN_WAIT2) {
            t->state = TS_TIME_WAIT;
            act |= TA_CLOSED;
        }
    }
    return act;
}

static inline int tcp_tick(Tcb *t, u32 now)
{
    if (t->state == TS_CLOSED || t->state == TS_TIME_WAIT ||
        t->state == TS_FIN_WAIT2 || t->state == TS_CLOSE_WAIT)
        return 0;
    if (t->state != TS_SYN_SENT && t->snd_una == t->snd_nxt)
        return 0;
    if ((i32)(now - t->sent_at) <= TCP_RTO) return 0;
    if (++t->tries >= TCP_MAXTRY) {
        t->state = TS_CLOSED;
        return TA_ERROR;
    }
    t->sent_at = now;
    if (t->state == TS_SYN_SENT) return TA_SEND_SYN;
    if (t->state == TS_FIN_WAIT1 || t->state == TS_LAST_ACK) return TA_SEND_FIN;
    return TA_SEND_DATA;
}

static inline int tcp_close(Tcb *t, u32 now)
{
    if (t->state == TS_ESTAB) t->state = TS_FIN_WAIT1;
    else if (t->state == TS_CLOSE_WAIT) t->state = TS_LAST_ACK;
    else return 0;
    t->snd_nxt += 1;
    t->sent_at = now;
    t->tries = 0;
    return TA_SEND_FIN;
}
