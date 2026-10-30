#include "product_media_store.hpp"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

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

void makeDir(const char *path)
{
    if (!path || !path[0])
        return;
#ifdef _WIN32
    (void)_mkdir(path);
#else
    (void)mkdir(path, 0755);
#endif
}

// Compact SHA-256 so the store can check a finished asset without mbedtls on host.
struct Sha256 {
    uint32_t state[8];
    uint64_t bits;
    uint8_t buffer[64];
    int filled;
};

uint32_t rotr(uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

void shaInit(Sha256 &sha)
{
    const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(sha.state, iv, sizeof(iv));
    sha.bits = 0;
    sha.filled = 0;
}

void shaBlock(Sha256 &sha, const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
               (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = sha.state[0], b = sha.state[1], c = sha.state[2], d = sha.state[3];
    uint32_t e = sha.state[4], f = sha.state[5], g = sha.state[6], h = sha.state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + k[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    sha.state[0] += a;
    sha.state[1] += b;
    sha.state[2] += c;
    sha.state[3] += d;
    sha.state[4] += e;
    sha.state[5] += f;
    sha.state[6] += g;
    sha.state[7] += h;
}

void shaUpdate(Sha256 &sha, const uint8_t *data, size_t length)
{
    sha.bits += (uint64_t)length * 8;
    while (length > 0) {
        const int room = 64 - sha.filled;
        const int take = (int)length < room ? (int)length : room;
        std::memcpy(sha.buffer + sha.filled, data, (size_t)take);
        sha.filled += take;
        data += take;
        length -= (size_t)take;
        if (sha.filled == 64) {
            shaBlock(sha, sha.buffer);
            sha.filled = 0;
        }
    }
}

void shaFinal(Sha256 &sha, uint8_t out[32])
{
    sha.buffer[sha.filled++] = 0x80;
    if (sha.filled > 56) {
        while (sha.filled < 64)
            sha.buffer[sha.filled++] = 0;
        shaBlock(sha, sha.buffer);
        sha.filled = 0;
    }
    while (sha.filled < 56)
        sha.buffer[sha.filled++] = 0;
    for (int i = 7; i >= 0; --i)
        sha.buffer[sha.filled++] = (uint8_t)(sha.bits >> (i * 8));
    shaBlock(sha, sha.buffer);
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t)(sha.state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(sha.state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(sha.state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)sha.state[i];
    }
}

void encodeHash(const uint8_t digest[32], char out[65])
{
    static const char *digits = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
    out[64] = 0;
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

void MediaStore::discardSession()
{
    for (auto &asset : assets_) {
        if (!asset.used)
            continue;
        char file[256];
        if (path(asset, file, (int)sizeof(file)))
            std::remove(file);
        asset = {};
    }
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

bool MediaStore::begin(const char *messageId, MediaKind kind, uint32_t total, const char *sha256)
{
    if (!messageId || !messageId[0] || std::strlen(messageId) >= 48)
        return false;
    makeDir(root_);
    char sub[256];
    std::snprintf(sub, sizeof(sub), "%s/%s", root_, kind == MediaKind::Audio ? "audio" : "sketch");
    makeDir(sub);
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
    if (!asset->used || asset->total != total || std::strcmp(asset->sha, sha256 ? sha256 : "") != 0) {
        char file[256];
        Asset scratch{};
        copyText(scratch.id, (int)sizeof(scratch.id), messageId);
        scratch.kind = kind;
        if (asset->used && path(*asset, file, (int)sizeof(file)))
            std::remove(file);
        *asset = {};
        copyText(asset->id, (int)sizeof(asset->id), messageId);
        asset->kind = kind;
        asset->total = total;
        copyText(asset->sha, (int)sizeof(asset->sha), sha256 ? sha256 : "");
        asset->used = true;
    }
    return true;
}

bool MediaStore::hashFile(const Asset &asset, char out[65]) const
{
    char file[256];
    if (!path(asset, file, (int)sizeof(file)))
        return false;
    FILE *handle = std::fopen(file, "rb");
    if (!handle)
        return false;
    Sha256 sha;
    shaInit(sha);
    uint8_t chunk[kReadCap];
    while (true) {
        const size_t got = std::fread(chunk, 1, sizeof(chunk), handle);
        if (got > 0)
            shaUpdate(sha, chunk, got);
        if (got < sizeof(chunk))
            break;
    }
    std::fclose(handle);
    uint8_t digest[32];
    shaFinal(sha, digest);
    encodeHash(digest, out);
    return true;
}

MediaStore::Append MediaStore::append(const char *messageId, MediaKind kind, uint32_t offset, const uint8_t *data,
                                      int length)
{
    Asset *asset = messageId ? find(messageId, kind) : nullptr;
    if (!asset || asset->failed || !data || length <= 0 || offset != asset->stored)
        return Append::Rejected;
    if (asset->total > 0 && offset + (uint32_t)length > asset->total)
        return Append::Rejected;
    char file[256];
    if (!path(*asset, file, (int)sizeof(file)))
        return Append::Rejected;
    FILE *handle = std::fopen(file, asset->stored == 0 ? "wb" : "r+b");
    if (!handle)
        return Append::Rejected;
    if (std::fseek(handle, (long)offset, SEEK_SET) != 0) {
        std::fclose(handle);
        return Append::Rejected;
    }
    const bool wrote = std::fwrite(data, 1, (size_t)length, handle) == (size_t)length;
    std::fclose(handle);
    if (!wrote)
        return Append::Rejected;
    asset->stored += (uint32_t)length;
    asset->interrupted = false;
    if (asset->total == 0 || asset->stored < asset->total)
        return Append::Ok;
    char digest[65];
    if (asset->sha[0] && (!hashFile(*asset, digest) || std::strcmp(digest, asset->sha) != 0)) {
        asset->failed = true;
        asset->complete = false;
        return Append::HashMismatch;
    }
    asset->complete = true;
    asset->failed = false;
    return Append::Complete;
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
