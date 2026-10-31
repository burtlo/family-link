#include "product_media_store.hpp"

#include <cstdio>
#include <cstring>

namespace product_ui
{
namespace
{
void copyText(char *destination, int cap, const char *source)
{
    if (!destination || cap <= 0)
        return;
    destination[0] = 0;
    if (!source)
        return;
    std::snprintf(destination, static_cast<size_t>(cap), "%s", source);
}

bool safeId(const char *messageId, char *out, int cap)
{
    if (!messageId || !messageId[0] || !out || cap < 2)
        return false;
    int n = 0;
    for (const char *p = messageId; *p && n + 1 < cap; ++p) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '_';
        out[n++] = ok ? c : '_';
    }
    out[n] = 0;
    return n > 0;
}
} // namespace

MediaStore::MediaStore(const char *root)
{
    copyText(root_, (int)sizeof(root_), root ? root : "playback-cache");
}

void MediaStore::setPlaybackAllowed(bool allowed)
{
    playback_allowed_ = allowed;
}

void MediaStore::pushPending(const Asset &asset)
{
    if (pending_count_ >= kMaxAssets)
        return;
    char file[256];
    if (!path(asset, file, (int)sizeof(file)) || std::strlen(file) >= 96)
        return;
    copyText(pending_[pending_count_], 96, file);
    ++pending_count_;
}

void MediaStore::discardSession()
{
    for (auto &asset : assets_) {
        if (!asset.used)
            continue;
        pushPending(asset);
        asset = {};
    }
}

bool MediaStore::popDiscard(char out[96])
{
    if (!out || pending_count_ <= 0)
        return false;
    --pending_count_;
    copyText(out, 96, pending_[pending_count_]);
    pending_[pending_count_][0] = 0;
    return out[0] != 0;
}

void MediaStore::restoreDiscard(const char path[96])
{
    if (!path || !path[0] || pending_count_ >= kMaxAssets)
        return;
    copyText(pending_[pending_count_], 96, path);
    ++pending_count_;
}

MediaStore::Asset *MediaStore::find(const char *messageId, MediaKind kind)
{
    for (auto &asset : assets_)
        if (asset.used && asset.kind == kind && std::strcmp(asset.id, messageId) == 0)
            return &asset;
    return nullptr;
}

const MediaStore::Asset *MediaStore::find(const char *messageId, MediaKind kind) const
{
    for (const auto &asset : assets_)
        if (asset.used && asset.kind == kind && std::strcmp(asset.id, messageId) == 0)
            return &asset;
    return nullptr;
}

bool MediaStore::path(const Asset &asset, char *out, int cap) const
{
    char id[48];
    if (!safeId(asset.id, id, (int)sizeof(id)))
        return false;
    return std::snprintf(out, (size_t)cap, "%s/%s/%s", root_, asset.kind == MediaKind::Audio ? "audio" : "sketch",
                         id) < cap;
}

bool MediaStore::prepare(const char *messageId, MediaKind kind, uint32_t total, const char *sha256)
{
    if (!messageId || !messageId[0] || std::strlen(messageId) >= 48)
        return false;
    Asset *asset = find(messageId, kind);
    if (!asset) {
        for (auto &slot : assets_) {
            if (!slot.used) {
                asset = &slot;
                break;
            }
        }
    }
    if (!asset)
        return false;
    const char *sha = sha256 ? sha256 : "";
    if (asset->used && (asset->total != total || std::strcmp(asset->sha, sha) != 0)) {
        pushPending(*asset);
        *asset = {};
    }
    if (!asset->used) {
        *asset = {};
        copyText(asset->id, (int)sizeof(asset->id), messageId);
        asset->kind = kind;
        asset->total = total;
        copyText(asset->sha, (int)sizeof(asset->sha), sha);
        asset->used = true;
    }
    return true;
}

bool MediaStore::filePath(const char *messageId, MediaKind kind, char *out, int cap) const
{
    const Asset *asset = find(messageId, kind);
    return asset && path(*asset, out, cap);
}

uint32_t MediaStore::operation(const char *messageId, MediaKind kind) const
{
    const Asset *asset = find(messageId, kind);
    return asset ? asset->operation : 0;
}

void MediaStore::setOperation(const char *messageId, MediaKind kind, uint32_t operation)
{
    if (Asset *asset = find(messageId, kind))
        asset->operation = operation;
}

void MediaStore::abandon(const char *messageId, MediaKind kind)
{
    Asset *asset = find(messageId, kind);
    if (!asset)
        return;
    pushPending(*asset);
    asset->stored = 0;
    asset->complete = false;
    asset->failed = false;
    asset->operation = 0;
    asset->interrupted = false;
}

MediaStore::Note MediaStore::noteWritten(const char *messageId, MediaKind kind, uint32_t added, bool full,
                                         bool hash_matches)
{
    Asset *asset = messageId ? find(messageId, kind) : nullptr;
    if (!asset || asset->failed || added == 0)
        return Note::Rejected;
    if (!hash_matches) {
        pushPending(*asset);
        asset->stored = 0;
        asset->complete = false;
        asset->failed = true;
        asset->operation = 0;
        return Note::Failed;
    }
    if (asset->total > 0 && asset->stored + added > asset->total)
        return Note::Rejected;
    asset->stored += added;
    asset->interrupted = false;
    if (full || (asset->total > 0 && asset->stored >= asset->total)) {
        asset->complete = true;
        asset->failed = false;
        return Note::Complete;
    }
    return Note::Ok;
}

void MediaStore::interrupt(const char *messageId, MediaKind kind)
{
    if (Asset *asset = find(messageId, kind))
        asset->interrupted = true;
}

bool MediaStore::complete(const char *messageId, MediaKind kind) const
{
    const Asset *asset = find(messageId, kind);
    return asset && asset->complete && !asset->failed;
}

bool MediaStore::failed(const char *messageId, MediaKind kind) const
{
    const Asset *asset = find(messageId, kind);
    return asset && asset->failed;
}

uint32_t MediaStore::stored(const char *messageId, MediaKind kind) const
{
    const Asset *asset = find(messageId, kind);
    return asset ? asset->stored : 0;
}

int MediaStore::read(const Asset &asset, uint32_t offset, uint8_t *destination, int capacity) const
{
    if (!destination || capacity <= 0 || offset >= asset.stored)
        return 0;
    char file[256];
    if (!path(asset, file, (int)sizeof(file)))
        return 0;
    if (io_probe_)
        io_probe_(io_context_);
    FILE *handle = std::fopen(file, "rb");
    if (!handle)
        return 0;
    if (std::fseek(handle, (long)offset, SEEK_SET) != 0) {
        std::fclose(handle);
        return 0;
    }
    const int cap = capacity > kReadCap ? kReadCap : capacity;
    const int room = (int)(asset.stored - offset);
    const int ask = cap < room ? cap : room;
    const int got = (int)std::fread(destination, 1, (size_t)ask, handle);
    std::fclose(handle);
    return got;
}

int MediaStore::readAudio(const char *messageId, uint32_t offset, uint8_t *destination, int capacity) const
{
    if (!playback_allowed_)
        return 0;
    const Asset *asset = find(messageId, MediaKind::Audio);
    return asset ? read(*asset, offset, destination, capacity) : 0;
}

int MediaStore::readSketch(const char *messageId, uint32_t offset, uint8_t *destination, int capacity) const
{
    const Asset *asset = find(messageId, MediaKind::Sketch);
    return asset ? read(*asset, offset, destination, capacity) : 0;
}
} // namespace product_ui
