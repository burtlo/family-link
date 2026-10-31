#include "product_inbox_service.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#ifdef ESP_PLATFORM
#include "esp_random.h"
#include "esp_log.h"
#endif

namespace product_ui
{
namespace
{
template <size_t N>
void copy(char (&destination)[N], const char *source)
{
    std::snprintf(destination, N, "%s", source ? source : "");
}
bool safeId(const char *id, size_t cap)
{
    if (!id || !*id)
        return false;
    for (size_t i = 0; i < cap; ++i) {
        const unsigned char c = id[i];
        if (!c)
            return true;
        if (c < 32 || c == '"' || c == '\\')
            return false;
    }
    return false;
}
constexpr int kLastViewed = 2;
} // namespace

InboxService::InboxService(InboxTransport transport) : transport_(transport)
{
#ifdef ESP_PLATFORM
    if (!transport_.start)
        transport_.start = product_inbox_http::worker_start;
    if (!transport_.submit)
        transport_.submit = product_inbox_http::submit;
    if (!transport_.poll)
        transport_.poll = product_inbox_http::poll;
    const uint32_t random = esp_random();
#else
    static uint32_t seed = 1;
    const uint32_t random = seed++;
#endif
    std::snprintf(operation_prefix_, sizeof(operation_prefix_), "%08x-0000-4000-8000", static_cast<unsigned>(random));
}

InboxService::~InboxService()
{
    std::free(metadata_);
}

void InboxService::noteSessionUnready()
{
    fetchFailure();
}

void InboxService::signOut()
{
    std::free(metadata_);
    metadata_ = nullptr;
    user_[0] = bearer_[0] = acknowledged_[0] = pending_message_[0] = 0;
    selected_ = -1;
    fetch_operation_ = write_operation_ = 0;
    write_pending_ = selection_pending_ = false;
    connecting_ = refresh_requested_ = true;
    retry_index_ = 0;
    ++generation_;
}

void InboxService::setSession(const char *user, const char *bearer, uint32_t generation,
                              bool wifi, bool credentialReady, int64_t now)
{
    now_ = now;
    if (!user || !*user) {
        if (user_[0])
            signOut();
        wifi_ = wifi;
        credential_ready_ = false;
        return;
    }
    if (std::strcmp(user_, user) != 0 || auth_generation_ != generation) {
        signOut();
        copy(user_, user);
        auth_generation_ = generation;
        fetch_due_ = now;
    }
    const bool recovered = (!wifi_ && wifi) || (!credential_ready_ && credentialReady);
    wifi_ = wifi;
    credential_ready_ = credentialReady;
    copy(bearer_, bearer);
    if (!wifi_ || !credential_ready_)
        connecting_ = true;
    if (recovered) {
        retry_index_ = 0;
        fetch_due_ = now;
        refresh_requested_ = true;
    }
}

uint32_t InboxService::nextOperation()
{
    if (++next_operation_ == 0)
        ++next_operation_;
    return next_operation_;
}

int InboxService::find(const fl_product_inbox::InboxMetadataHeader *block, const char *id) const
{
    if (!block || !id || !*id)
        return -1;
    const auto *messages = fl_product_inbox::metadata_messages(block);
    for (int i = 0; i < block->message_count; ++i)
        if (std::strcmp(messages[i].message_id, id) == 0)
            return i;
    return -1;
}

void InboxService::selectionChanged()
{
    selection_pending_ = selected_ >= 0;
    selection_due_ = now_ + fl_product_inbox::kLastViewedCoalesceMs;
}

bool InboxService::applyMetadata(fl_product_inbox::InboxMetadataHeader *block)
{
    if (!block || block->message_count < 0)
        return false;
    const auto *fresh = fl_product_inbox::metadata_messages(block);
    for (int i = 0; i < block->message_count; ++i) {
        if (!safeId(fresh[i].message_id, sizeof(fresh[i].message_id)))
            return false;
    }
    if (metadata_ && block->inbox_revision < metadata_->inbox_revision)
        return false;
    if (metadata_ && block->inbox_revision == metadata_->inbox_revision) {
        if (block->message_count != metadata_->message_count)
            return false;
        const auto *old = fl_product_inbox::metadata_messages(metadata_);
        for (int i = 0; i < block->message_count; ++i)
            if (std::strcmp(old[i].message_id, fresh[i].message_id) != 0)
                return false;
    }
    int next = find(block, selectedMessageId());
    if (!metadata_) {
        next = find(block, block->last_viewed_message_id);
        if (next < 0)
            next = block->message_count - 1;
    } else if (next < 0 && block->message_count > 0) {
        const auto *old = fl_product_inbox::metadata_messages(metadata_);
        for (int i = selected_ - 1; i >= 0 && next < 0; --i)
            next = find(block, old[i].message_id);
        for (int i = selected_ + 1; i < metadata_->message_count && next < 0; ++i)
            next = find(block, old[i].message_id);
        if (next < 0)
            next = block->message_count - 1;
    }
    const bool changed = next < 0 ? selected_ >= 0 : std::strcmp(selectedMessageId(), fresh[next].message_id) != 0;
    std::free(metadata_);
    metadata_ = block;
    selected_ = next;
    connecting_ = false;
    retry_index_ = 0;
    fetch_due_ = now_ + fl_product_inbox::kInboxFetchPeriodMs;
    if (changed)
        selectionChanged();
    return true;
}

bool InboxService::submitExternal(const product_inbox_http::Job *job)
{
    return job && transport_.submit && transport_.submit(job);
}

void InboxService::fetchFailure()
{
    static const int waits[] = {1000, 2000, 4000, 30000};
    connecting_ = true;
    fetch_due_ = now_ + waits[std::min(retry_index_, 3)];
    retry_index_ = std::min(retry_index_ + 1, 3);
}

void InboxService::consume(const product_inbox_http::Result &result)
{
    auto release = [this](void *block) {
        if (!block)
            return;
        if (release_)
            release_(block);
        else
            std::free(block);
    };
    if (result.kind == product_inbox_http::kPlaybackMediaJob ||
        result.kind == product_inbox_http::kPlaybackMediaMaintenanceJob) {
        if (media_sink_)
            media_sink_(media_context_, result);
        release(result.data);
        release(result.parsed);
        return;
    }
    if (result.kind != product_inbox_http::kMetadataJob && result.kind != kLastViewed) {
        release(result.data);
        release(result.parsed);
        return;
    }
    auto *block = static_cast<fl_product_inbox::InboxMetadataHeader *>(result.parsed);
    if (result.generation == generation_ && user_[0]) {
        if (result.kind == product_inbox_http::kMetadataJob && result.operation_id == fetch_operation_ && fetch_operation_) {
            fetch_operation_ = 0;
            if (block && metadata_ && block->inbox_revision < metadata_->inbox_revision) {
                fetch_due_ = now_ + fl_product_inbox::kInboxFetchPeriodMs;
            } else if (!result.perform_ok || result.http_status != 200 || !wifi_ || !credential_ready_ ||
                       !applyMetadata(block))
                fetchFailure();
            else
                block = nullptr;
            if (refresh_requested_)
                fetch_due_ = now_;
        } else if (result.kind == kLastViewed && result.operation_id == write_operation_ && write_operation_) {
            write_operation_ = 0;
            if (result.perform_ok && (result.http_status == 200 || result.http_status == 404 ||
                                      result.http_status == 409 || result.http_status == 410)) {
                if (result.http_status == 200)
                    copy(acknowledged_, pending_message_);
                write_pending_ = false;
            } else
                write_due_ = now_ + 1000;
        }
    }
    release(block);
    release(result.data);
}

bool InboxService::submitMetadata()
{
    product_inbox_http::Job job{};
    job.kind = product_inbox_http::kMetadataJob;
    copy(job.method, "GET");
    copy(job.path, "/v2/inbox");
    copy(job.bearer, bearer_);
    job.generation = generation_;
    job.operation_id = nextOperation();
    job.timeout_ms = connecting_ ? 6000 : 1000;
    if (!transport_.submit || !transport_.submit(&job)) {
#ifdef ESP_PLATFORM
        ESP_LOGW("product_inbox_service", "metadata queue unavailable; retry scheduled");
#endif
        fetch_due_ = now_ + 1000;
        return false;
    }
    fetch_operation_ = job.operation_id;
    refresh_requested_ = false;
    return true;
}

bool InboxService::submitLastViewed()
{
    product_inbox_http::Job job{};
    job.kind = kLastViewed;
    copy(job.method, "PUT");
    copy(job.path, "/v2/inbox/last-viewed");
    copy(job.bearer, bearer_);
    copy(job.message_id, pending_message_);
    copy(job.body, write_body_);
    job.body_len = write_body_len_;
    job.generation = generation_;
    job.operation_id = nextOperation();
    job.timeout_ms = 1000;
    if (!transport_.submit || !transport_.submit(&job)) {
#ifdef ESP_PLATFORM
        ESP_LOGW("product_inbox_service", "last_viewed queue unavailable; retry scheduled");
#endif
        write_due_ = now_ + 1000;
        return false;
    }
    write_operation_ = job.operation_id;
    return true;
}

void InboxService::advance(int64_t now)
{
    now_ = now;
    if (!started_ && user_[0] && transport_.start)
        started_ = transport_.start();
    if (started_ && transport_.poll) {
        // Bound work per presentation pass; later ticks continue draining.
        for (int i = 0; i < 4; ++i) {
            const auto *result = transport_.poll();
            if (!result)
                break;
            consume(*result);
        }
    }
    if (!started_ || !user_[0] || !wifi_ || !credential_ready_)
        return;
    if (!fetch_operation_ && now_ >= fetch_due_ && !fetch_held_)
        submitMetadata();
    if (write_pending_ && !write_operation_ && now_ >= write_created_ + 60000) {
        write_pending_ = false;
        // Keep a newer settled selection pending. The expired message remains
        // in pending_message_ so the same old operation is never reminted.
    }
    if (!write_operation_ && selection_pending_ && now_ >= selection_due_ && selected_ >= 0 &&
        std::strcmp(selectedMessageId(), pending_message_) != 0 &&
        std::strcmp(selectedMessageId(), acknowledged_) != 0) {
        copy(pending_message_, selectedMessageId());
        const uint32_t sequence = nextOperation();
        std::snprintf(operation_uuid_, sizeof(operation_uuid_), "%s-%012llx", operation_prefix_,
                      static_cast<unsigned long long>(sequence));
        write_body_len_ = std::snprintf(write_body_, sizeof(write_body_),
                                        "{\"schema\":\"fl.last-viewed-update/2\",\"operation_id\":\"%s\",\"message_id\":\"%s\"}",
                                        operation_uuid_, pending_message_);
        write_pending_ = true;
        selection_pending_ = false;
        write_created_ = write_due_ = now_;
    }
    if (!write_operation_ && write_pending_ && now_ >= write_due_)
        submitLastViewed();
}

void InboxService::requestRefresh()
{
    refresh_requested_ = true;
    fetch_due_ = now_;
}

bool InboxService::retainSelection(const char *messageId)
{
    if (!metadata_ || !messageId || !messageId[0])
        return false;
    const int index = find(metadata_, messageId);
    if (index < 0)
        return false;
    if (selected_ != index) {
        selected_ = index;
        selectionChanged();
    }
    return true;
}

bool InboxService::selectIndex(int index, const char *expectedId)
{
    if (!user_[0] || connecting_ || !wifi_ || !credential_ready_)
        return false;
    const auto *message = messageAt(index);
    if (!message || (expectedId && std::strcmp(message->message_id, expectedId) != 0))
        return false;
    if (selected_ != index) {
        selected_ = index;
        selectionChanged();
    }
    return true;
}

const fl_product_inbox::InboxMessage *InboxService::messageAt(int index) const
{
    return metadata_ && index >= 0 && index < metadata_->message_count ? &fl_product_inbox::metadata_messages(metadata_)[index] : nullptr;
}
const char *InboxService::selectedMessageId() const
{
    const auto *message = messageAt(selected_);
    return message ? message->message_id : "";
}
InboxSnapshot InboxService::snapshot() const
{
    InboxSnapshot result;
    result.count = metadata_ ? metadata_->message_count : 0;
    result.selectedIndex = selected_;
    result.revision = metadata_ ? metadata_->inbox_revision : 0;
    result.dotPhase = static_cast<int>((now_ / 400) % 3);
    if (!wifi_)
        result.body = InboxSnapshot::Body::NoWifi;
    else if (connecting_ || !credential_ready_)
        result.body = InboxSnapshot::Body::Connecting;
    else
        result.body = result.count ? InboxSnapshot::Body::Cards : InboxSnapshot::Body::Empty;
    return result;
}

} // namespace product_ui
