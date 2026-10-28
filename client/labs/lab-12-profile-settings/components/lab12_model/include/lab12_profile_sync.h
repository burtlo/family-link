#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LAB12_USER_ID_MAX 32
#define LAB12_AVATAR_ID_MAX 16
#define LAB12_ACCENT_MAX 8
typedef enum { LAB12_SYNC_IDLE,
               LAB12_SYNC_PENDING,
               LAB12_SYNC_UNCERTAIN,
               LAB12_SYNC_ACKNOWLEDGED,
               LAB12_SYNC_REJECTED,
               LAB12_SYNC_STALE } lab12_sync_status_t;
typedef struct {
    char user_id[LAB12_USER_ID_MAX];
    uint32_t revision;
    char avatar_id[LAB12_AVATAR_ID_MAX];
    char profile_accent[LAB12_ACCENT_MAX];
} lab12_profile_t;
typedef struct {
    char user_id[LAB12_USER_ID_MAX];
    uint32_t session_generation;
    uint32_t operation_id;
    uint32_t expected_revision;
    char avatar_id[LAB12_AVATAR_ID_MAX];
    char profile_accent[LAB12_ACCENT_MAX];
    bool change_avatar;
    bool change_accent;
    uint32_t preview_generation;
    bool valid;
} lab12_profile_update_t;
typedef struct {
    lab12_profile_t acknowledged;
    lab12_profile_t preview;
    uint32_t session_generation;
    uint32_t preview_generation;
    bool preview_dirty;
    lab12_profile_update_t in_flight;
    lab12_sync_status_t status;
} lab12_profile_sync_t;
void lab12_profile_sync_init(lab12_profile_sync_t *sync, const lab12_profile_t *initial, uint32_t session_generation);
bool lab12_profile_sync_preview(lab12_profile_sync_t *sync, const lab12_profile_t *candidate);
bool lab12_profile_sync_cancel_preview(lab12_profile_sync_t *sync);
bool lab12_profile_sync_begin(lab12_profile_sync_t *sync, uint32_t operation_id, bool change_avatar, bool change_accent);
bool lab12_profile_sync_complete(lab12_profile_sync_t *sync, uint32_t session_generation, uint32_t operation_id, bool accepted, const lab12_profile_t *canonical);
bool lab12_profile_sync_transport_uncertain(lab12_profile_sync_t *sync, uint32_t session_generation, uint32_t operation_id);
bool lab12_profile_sync_reconcile(lab12_profile_sync_t *sync, uint32_t session_generation, const lab12_profile_t *canonical);
void lab12_profile_sync_switch_user(lab12_profile_sync_t *sync, const lab12_profile_t *profile, uint32_t session_generation);
