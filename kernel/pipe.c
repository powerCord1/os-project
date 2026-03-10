#include <pipe.h>
#include <string.h>

static pipe_t pipe_table[PIPE_MAX];

static uint16_t pipe_count(pipe_t *p)
{
    return (p->head - p->tail + PIPE_BUF_SIZE) % PIPE_BUF_SIZE;
}

static uint16_t pipe_free(pipe_t *p)
{
    return PIPE_BUF_SIZE - 1 - pipe_count(p);
}

int pipe_alloc(void)
{
    for (int i = 0; i < PIPE_MAX; i++) {
        if (!pipe_table[i].active) {
            pipe_t *p = &pipe_table[i];
            memset(p, 0, sizeof(pipe_t));
            p->active = true;
            p->read_refs = 1;
            p->write_refs = 1;
            waitqueue_init(&p->read_wq);
            waitqueue_init(&p->write_wq);
            return i;
        }
    }
    return -1;
}

pipe_t *pipe_get(int pipe_id)
{
    if (pipe_id < 0 || pipe_id >= PIPE_MAX)
        return NULL;
    if (!pipe_table[pipe_id].active)
        return NULL;
    return &pipe_table[pipe_id];
}

int pipe_read(int pipe_id, uint8_t *buf, int count)
{
    pipe_t *p = pipe_get(pipe_id);
    if (!p)
        return -1;

    int total = 0;
    while (total < count) {
        while (pipe_count(p) == 0) {
            if (p->write_refs <= 0)
                return total;
            waitqueue_sleep(&p->read_wq);
        }
        while (total < count && pipe_count(p) > 0) {
            buf[total++] = p->buf[p->tail];
            p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
        }
        waitqueue_wake_one(&p->write_wq);
        break;
    }
    return total;
}

int pipe_write(int pipe_id, const uint8_t *buf, int count)
{
    pipe_t *p = pipe_get(pipe_id);
    if (!p)
        return -1;

    int total = 0;
    while (total < count) {
        if (p->read_refs <= 0)
            return -1;
        while (pipe_free(p) == 0) {
            if (p->read_refs <= 0)
                return -1;
            waitqueue_wake_one(&p->read_wq);
            waitqueue_sleep(&p->write_wq);
        }
        while (total < count && pipe_free(p) > 0) {
            p->buf[p->head] = buf[total++];
            p->head = (p->head + 1) % PIPE_BUF_SIZE;
        }
        waitqueue_wake_one(&p->read_wq);
    }
    return total;
}

void pipe_ref_read(int pipe_id)
{
    pipe_t *p = pipe_get(pipe_id);
    if (p)
        p->read_refs++;
}

void pipe_ref_write(int pipe_id)
{
    pipe_t *p = pipe_get(pipe_id);
    if (p)
        p->write_refs++;
}

void pipe_unref_read(int pipe_id)
{
    pipe_t *p = pipe_get(pipe_id);
    if (!p)
        return;
    p->read_refs--;
    if (p->read_refs <= 0)
        waitqueue_wake_all(&p->write_wq);
}

void pipe_unref_write(int pipe_id)
{
    pipe_t *p = pipe_get(pipe_id);
    if (!p)
        return;
    p->write_refs--;
    if (p->write_refs <= 0)
        waitqueue_wake_all(&p->read_wq);
}
