/**
 * Copyright 2026 Beken Corporation
 *
 * Pairing state model adapted from Meta's muse-gadget-sdk at commit
 * 812c46f9fe42b69d916613a9dc702cbac5ff7b54 (Apache-2.0).
 */
#include "muse_pairing.h"

#include <os/os.h>

static beken_mutex_t s_lock;
static bool s_initialized;
static muse_pairing_state_t s_state;
static uint32_t s_generation;

static uint32_t next_generation(uint32_t current)
{
    current++;
    return current == 0 ? 1 : current;
}

static bool current_locked(uint32_t generation)
{
    return generation != 0 && generation == s_generation;
}

void muse_pairing_init(void)
{
    if (!s_initialized && rtos_init_mutex(&s_lock) == BK_OK) {
        s_initialized = true;
    }
    if (s_initialized) {
        rtos_lock_mutex(&s_lock);
        s_state = MUSE_PAIRING_IDLE;
        s_generation = next_generation(s_generation);
        rtos_unlock_mutex(&s_lock);
    }
}

void muse_pairing_reset(void)
{
    if (!s_initialized) {
        return;
    }
    rtos_lock_mutex(&s_lock);
    s_state = MUSE_PAIRING_IDLE;
    s_generation = next_generation(s_generation);
    rtos_unlock_mutex(&s_lock);
}

uint32_t muse_pairing_begin(void)
{
    uint32_t generation = 0;
    if (!s_initialized) {
        return 0;
    }
    rtos_lock_mutex(&s_lock);
    s_generation = next_generation(s_generation);
    s_state = MUSE_PAIRING_WAIT_CLIENT_FINISHED;
    generation = s_generation;
    rtos_unlock_mutex(&s_lock);
    return generation;
}

bool muse_pairing_client_finished(uint32_t generation)
{
    bool ok;
    if (!s_initialized) {
        return false;
    }
    rtos_lock_mutex(&s_lock);
    ok = current_locked(generation)
        && s_state == MUSE_PAIRING_WAIT_CLIENT_FINISHED;
    if (ok) {
        s_state = MUSE_PAIRING_CONFIRM_REQUIRED;
    }
    rtos_unlock_mutex(&s_lock);
    return ok;
}

bool muse_pairing_arm_confirmation(uint32_t generation)
{
    bool ok;
    if (!s_initialized) {
        return false;
    }
    rtos_lock_mutex(&s_lock);
    ok = current_locked(generation)
        && s_state == MUSE_PAIRING_CONFIRM_REQUIRED;
    rtos_unlock_mutex(&s_lock);
    return ok;
}

bool muse_pairing_confirm(uint32_t generation)
{
    bool ok;
    if (!s_initialized) {
        return false;
    }
    rtos_lock_mutex(&s_lock);
    ok = current_locked(generation)
        && s_state == MUSE_PAIRING_CONFIRM_REQUIRED;
    if (ok) {
        s_state = MUSE_PAIRING_READY;
    }
    rtos_unlock_mutex(&s_lock);
    return ok;
}

bool muse_pairing_begin_provisioning(uint32_t generation)
{
    bool ok;
    if (!s_initialized) {
        return false;
    }
    rtos_lock_mutex(&s_lock);
    ok = current_locked(generation) && s_state == MUSE_PAIRING_READY;
    if (ok) {
        s_state = MUSE_PAIRING_PROVISIONING;
    }
    rtos_unlock_mutex(&s_lock);
    return ok;
}

bool muse_pairing_commit(uint32_t generation)
{
    bool ok;
    if (!s_initialized) {
        return false;
    }
    rtos_lock_mutex(&s_lock);
    ok = current_locked(generation) && s_state == MUSE_PAIRING_PROVISIONING;
    if (ok) {
        s_state = MUSE_PAIRING_IDLE;
    }
    rtos_unlock_mutex(&s_lock);
    return ok;
}

muse_pairing_state_t muse_pairing_state(void)
{
    muse_pairing_state_t state = MUSE_PAIRING_IDLE;
    if (!s_initialized) {
        return state;
    }
    rtos_lock_mutex(&s_lock);
    state = s_state;
    rtos_unlock_mutex(&s_lock);
    return state;
}
