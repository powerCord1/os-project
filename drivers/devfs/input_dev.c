#include <devfs.h>
#include <string.h>
#include <waitqueue.h>

#define EV_KEY 1
#define INPUT_BUF_SIZE 256

typedef struct {
    uint32_t time_sec;
    uint32_t time_usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
} input_event_t;

static input_event_t ring[INPUT_BUF_SIZE];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;
static waitqueue_t input_wq;
static bool input_active = false;

extern volatile uint64_t system_ticks;

void input_dev_push_event(uint8_t scancode, bool pressed)
{
    if (!input_active)
        return;

    uint32_t next = (ring_head + 1) % INPUT_BUF_SIZE;
    if (next == ring_tail)
        return; /* full, drop event */

    input_event_t *ev = &ring[ring_head];
    ev->time_sec = system_ticks / 1000;
    ev->time_usec = (system_ticks % 1000) * 1000;
    ev->type = EV_KEY;
    ev->code = scancode;
    ev->value = pressed ? 1 : 0;

    ring_head = next;
    waitqueue_wake_one(&input_wq);
}

static int input_open(void **state)
{
    *state = NULL;
    input_active = true;
    return 0;
}

static int64_t input_read(void *state, uint8_t *buf, uint32_t count,
                          uint64_t offset, bool nonblock)
{
    (void)state;
    (void)offset;
    uint32_t ev_size = sizeof(input_event_t);
    uint32_t copied = 0;

    while (copied + ev_size <= count) {
        if (ring_tail == ring_head) {
            if (copied > 0 || nonblock)
                break;
            waitqueue_sleep(&input_wq);
            continue;
        }

        memcpy(buf + copied, &ring[ring_tail], ev_size);
        ring_tail = (ring_tail + 1) % INPUT_BUF_SIZE;
        copied += ev_size;
    }

    if (copied == 0 && nonblock)
        return -11; /* EAGAIN */

    return copied;
}

static int64_t input_write(void *state, const uint8_t *buf, uint32_t count,
                           uint64_t offset)
{
    (void)state;
    (void)offset;
    (void)buf;
    (void)count;
    return -22; /* EINVAL */
}

static int64_t input_ioctl(void *state, int32_t req, void *argp, uint32_t size)
{
    (void)state;
    (void)req;
    (void)argp;
    (void)size;
    return -25; /* ENOTTY */
}

static bool input_poll_readable(void *state)
{
    (void)state;
    return ring_head != ring_tail;
}

static bool input_poll_writable(void *state)
{
    (void)state;
    return false;
}

static void input_close(void *state)
{
    (void)state;
    input_active = false;
}

void input_dev_init(void)
{
    waitqueue_init(&input_wq);

    static const devfs_device_t input_dev = {
        .name = "input/event0",
        .open = input_open,
        .read = input_read,
        .write = input_write,
        .ioctl = input_ioctl,
        .poll_readable = input_poll_readable,
        .poll_writable = input_poll_writable,
        .close = input_close,
    };
    devfs_register(&input_dev);
}
