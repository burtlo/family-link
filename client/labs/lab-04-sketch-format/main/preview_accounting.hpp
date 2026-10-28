#pragma once

#include <cstdint>

namespace lab04
{

constexpr std::uint32_t kPreviewStorageBytes = 28800;

struct PreviewAccounting {
    std::uint32_t bytes;
    std::uint32_t buffers;
};

// The preview variant stores pixels in its encoded payload. Points and strokes
// use the separately allocated scratch image, which can remain live even when
// their encode operation fails.
constexpr PreviewAccounting preview_accounting(std::uint8_t variant,
                                               bool encode_succeeded,
                                               bool scratch_preview_allocated)
{
    const bool storage_available = variant == 2 ? encode_succeeded : scratch_preview_allocated;
    return storage_available ? PreviewAccounting{kPreviewStorageBytes, 1} : PreviewAccounting{0, 0};
}

} // namespace lab04
