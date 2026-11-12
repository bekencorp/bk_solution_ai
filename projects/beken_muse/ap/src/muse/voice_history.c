/**
 * @file voice_history.c
 * @brief Ring buffer of recent Muse turn replies. See the header.
 *
 * Storage lives in PSRAM (3.4 KB) to stay off the tight SRAM heap; the
 * mutex is lazily created on first use so the module needs no init call
 * (push can happen before any UI exists).
 */
#include "voice_history.h"

#if CONFIG_MUSE_GADGET

#include <string.h>

#include <os/os.h>

typedef struct {
    char title[VOICE_HISTORY_TITLE_MAX];
    char body[VOICE_HISTORY_BODY_MAX];
} voice_history_entry_t;

static voice_history_entry_t s_entries[VOICE_HISTORY_MAX]
    __attribute__((section(".psram.bss")));
static int s_head;      /* index of the NEWEST entry */
static int s_count;
static beken_mutex_t s_lock;

static void history_lock(void)
{
    if (s_lock == NULL) {
        /* Benign if two racers each create one: the loser leaks a mutex,
         * which is cheaper than adding an init-order dependency here. */
        beken_mutex_t created = NULL;
        if (rtos_init_mutex(&created) == 0 && created != NULL) {
            if (__sync_bool_compare_and_swap(&s_lock, NULL, created) == false) {
                rtos_deinit_mutex(&created);
            }
        }
    }
    if (s_lock != NULL) {
        rtos_lock_mutex(&s_lock);
    }
}

static void history_unlock(void)
{
    if (s_lock != NULL) {
        rtos_unlock_mutex(&s_lock);
    }
}

static void copy_capped(char *dst, size_t cap, const char *src)
{
    if (cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void voice_history_push(const char *title, const char *body)
{
    if (body == NULL || body[0] == '\0') {
        return;
    }
    history_lock();
    s_head = (s_head + 1) % VOICE_HISTORY_MAX;
    copy_capped(s_entries[s_head].title, VOICE_HISTORY_TITLE_MAX, title);
    copy_capped(s_entries[s_head].body, VOICE_HISTORY_BODY_MAX, body);
    if (s_count < VOICE_HISTORY_MAX) {
        s_count++;
    }
    history_unlock();
}

int voice_history_count(void)
{
    history_lock();
    int n = s_count;
    history_unlock();
    return n;
}

bool voice_history_get(int idx, char *title, size_t tcap, char *body, size_t bcap)
{
    history_lock();
    bool ok = (idx >= 0 && idx < s_count);
    if (ok) {
        int pos = s_head - idx;
        if (pos < 0) {
            pos += VOICE_HISTORY_MAX;
        }
        copy_capped(title, tcap, s_entries[pos].title);
        copy_capped(body, bcap, s_entries[pos].body);
    }
    history_unlock();
    return ok;
}

void voice_history_clear(void)
{
    history_lock();
    s_head = 0;
    s_count = 0;
    history_unlock();
}

#else  /* !CONFIG_MUSE_GADGET */

void voice_history_push(const char *title, const char *body)
{ (void)title; (void)body; }
int  voice_history_count(void) { return 0; }
bool voice_history_get(int idx, char *title, size_t tcap, char *body, size_t bcap)
{ (void)idx; (void)title; (void)tcap; (void)body; (void)bcap; return false; }
void voice_history_clear(void) {}

#endif /* CONFIG_MUSE_GADGET */
