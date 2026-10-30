#pragma once

#include <cstddef>
#include <cstdint>

namespace product_ui
{

// Where a lease was satisfied. Host tests use Host. Device placement is
// recorded so internal and PSRAM can be compared on target; it is not final
// until that measurement exists.
enum class SurfacePlace { Host,
                          Internal,
                          Psram };

// Exclusive pixel lease. The broker does not reserve a 240x240 record surface.
struct SurfaceLease {
    uint32_t generation = 0;
    char owner[24]{};
    int width = 0;
    int height = 0;
    int bytes = 0;
    SurfacePlace requested = SurfacePlace::Host;
    SurfacePlace placed = SurfacePlace::Host;
    int consumers = 0;
    bool scratch = false;
    // 240x240 record lease. Released bytes are freed; nothing is reserved at boot.
    bool record = false;
};

// Lazy one-at-a-time 120x120 RGB565 surfaces. A second acquire of the same
// role fails until release. Release fails while consumers are outstanding or
// the generation is stale.
class DisplaySurfaceBroker
{
  public:
    static constexpr int kEdge = 120;
    static constexpr int kBytes = kEdge * kEdge * 2;
    static constexpr int kRecordEdge = 240;
    static constexpr int kRecordBytes = kRecordEdge * kRecordEdge * 2;

    using Allocate = void *(*)(size_t bytes, SurfacePlace requested, SurfacePlace *placed, void *context);
    using Release = void (*)(void *block, void *context);

    DisplaySurfaceBroker() = default;
    ~DisplaySurfaceBroker();
    DisplaySurfaceBroker(const DisplaySurfaceBroker &) = delete;
    DisplaySurfaceBroker &operator=(const DisplaySurfaceBroker &) = delete;

    void setAllocator(Allocate allocate, Release release, void *context);
    bool acquire(const char *owner, SurfacePlace place, SurfaceLease *lease);
    bool acquireScratch(const char *owner, SurfacePlace place, SurfaceLease *lease);
    bool acquireRecord(const char *owner, SurfacePlace place, SurfaceLease *lease);
    bool addConsumer(SurfaceLease *lease);
    bool removeConsumer(SurfaceLease *lease);
    // Fails when consumers remain, the lease was already released, or generation mismatches.
    bool release(SurfaceLease *lease);
    // Fails while consumers remain. Bumps generation so the previous holder is stale.
    bool transfer(SurfaceLease *lease, const char *owner);
    uint16_t *pixels(const SurfaceLease &lease) const;
    int heldCount() const;
    bool holdsScratch() const;

  private:
    struct Slot {
        uint16_t *pixels = nullptr;
        uint32_t generation = 0;
        int consumers = 0;
        bool held = false;
        SurfacePlace placed = SurfacePlace::Host;
        char owner[24]{};
    };
    bool take(Slot &slot, bool scratch, const char *owner, SurfacePlace place, SurfaceLease *lease);
    Slot *match(const SurfaceLease &lease);
    const Slot *match(const SurfaceLease &lease) const;
    void *allocate(size_t bytes, SurfacePlace place, SurfacePlace *placed);
    void releaseBlock(void *block);

    Allocate allocate_ = nullptr;
    Release release_ = nullptr;
    void *context_ = nullptr;
    Slot live_{};
    Slot scratch_{};
    Slot record_{};
};

} // namespace product_ui
