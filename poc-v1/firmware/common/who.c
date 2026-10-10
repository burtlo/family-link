#include "who.h"

/*
 * Unique 31-char tags + NUL. flash.py finds each tag in family_link_demo.bin
 * and overwrites the 32-byte slot with id/token/name/peer. Do not shorten
 * or reuse these strings; they must stay unique in the image.
 * Keep in sync with scripts/flash.py WHO_TAGS.
 *
 * used: every demo links who.c; two-box mains read the slots. Keep them
 * even when the current demo never mentions DEMO_DEVICE_ID.
 */
#define WHO_SLOT __attribute__((used))

const char WHO_DEVICE_ID[32] WHO_SLOT = "FLWHO/id///////////////////////";
const char WHO_DEVICE_TOKEN[32] WHO_SLOT = "FLWHO/token////////////////////";
const char WHO_DEVICE_NAME[32] WHO_SLOT = "FLWHO/name/////////////////////";
const char WHO_PEER_NAME[32] WHO_SLOT = "FLWHO/peer/////////////////////";

/*
 * Linker anchor: demos that never read WHO_* still need all four placeholder
 * tags in family_link_demo.bin so flash.py can stamp id/token/name/peer.
 * main/CMakeLists.txt passes -u for each WHO_DEVICE_* symbol.
 */
__attribute__((used)) static const char *const s_who_slot_ptrs[] = {
    WHO_DEVICE_ID,
    WHO_DEVICE_TOKEN,
    WHO_DEVICE_NAME,
    WHO_PEER_NAME,
};

const char *who_slots_link_anchor(void)
{
    return s_who_slot_ptrs[0];
}
