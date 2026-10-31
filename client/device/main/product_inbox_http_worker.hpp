#pragma once

#include <cstdint>

namespace product_inbox_http
{

// Same GET endpoint as legacy kind 1, but parses only dynamically sized
// metadata. Result.parsed is InboxMetadataHeader followed by message records.
constexpr int kMetadataJob = 5;

// Playback media is written by this worker to Job::file_path. One operation_id
// covers every slice of one asset, and range_start is the bytes already stored
// so the worker can continue its SHA-256. A failed slice is not resumed in
// place: the caller deletes the file, mints a new operation_id, and starts
// again at offset 0. The worker does not rebuild a hash from a nonzero offset.
constexpr int kPlaybackMediaJob = 6;
// Deletes Job::file_path. No HTTP call.
constexpr int kPlaybackMediaMaintenanceJob = 7;

/* Copied by value onto the worker queue. bearer is the session credential, not a log field. */
struct Job {
    int kind;
    char method[8];
    char path[96];
    char body[256];
    int body_len;
    char range_header[40];
    char bearer[48];
    char message_id[48];
    char sha256[65];
    uint32_t generation;
    uint32_t operation_id;
    uint32_t range_start;
    uint32_t asset_bytes;
    int timeout_ms;
    // 0 = audio, 1 = sketch. Used by kPlaybackMediaJob.
    int asset;
    char file_path[96];
};

/* data and parsed are heap blocks. The UI tick frees both after it applies the result.
 * parsed is an fl_product_inbox::InboxParseResult allocated on the worker. */
struct Result {
    int kind;
    uint32_t generation;
    uint32_t operation_id;
    char message_id[48];
    int perform_ok;
    int http_status;
    int terminal;
    int full_asset;
    int hash_matches;
    int data_len;
    uint32_t bytes_written;
    uint8_t *data;
    void *parsed;
};

bool worker_start();
void worker_stop();
/* Returns false when the queue is full or the worker is stopped. Does not block. */
bool submit(const Job *job);
/* UI thread. Null when no result is waiting. Valid until the next poll. */
const Result *poll();

} /* namespace product_inbox_http */
