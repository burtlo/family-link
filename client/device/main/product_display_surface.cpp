#include "product_display_surface.hpp"

#include <cstdlib>
#include <cstring>

namespace product_ui
{
namespace
{
void *hostAllocate(size_t bytes, SurfacePlace requested, SurfacePlace *placed, void *)
{
    if (requested != SurfacePlace::Host)
        return nullptr;
    void *block = std::malloc(bytes);
    if (block && placed)
        *placed = SurfacePlace::Host;
    return block;
}
void hostRelease(void *block, void *)
{
    std::free(block);
}
void copyOwner(char *destination, const char *owner)
{
    destination[0] = 0;
    if (!owner)
        return;
    std::strncpy(destination, owner, 23);
    destination[23] = 0;
}
} // namespace

DisplaySurfaceBroker::~DisplaySurfaceBroker()
{
    if (live_.pixels)
        releaseBlock(live_.pixels);
    if (scratch_.pixels)
        releaseBlock(scratch_.pixels);
    if (record_.pixels)
        releaseBlock(record_.pixels);
}

void DisplaySurfaceBroker::setAllocator(Allocate allocate, Release release, void *context)
{
    allocate_ = allocate;
    release_ = release;
    context_ = context;
}

void *DisplaySurfaceBroker::allocate(size_t bytes, SurfacePlace place, SurfacePlace *placed)
{
    if (allocate_)
        return allocate_(bytes, place, placed, context_);
    return hostAllocate(bytes, place, placed, nullptr);
}

void DisplaySurfaceBroker::releaseBlock(void *block)
{
    if (release_)
        release_(block, context_);
    else
        hostRelease(block, nullptr);
}

bool DisplaySurfaceBroker::take(Slot &slot, bool scratch, const char *owner, SurfacePlace place,
                                SurfaceLease *lease)
{
    if (!lease || slot.held)
        return false;
    if (!slot.pixels) {
        SurfacePlace placed = place;
        auto *pixels = static_cast<uint16_t *>(allocate(kBytes, place, &placed));
        if (!pixels)
            return false;
        slot.pixels = pixels;
        slot.placed = placed;
    }
    slot.held = true;
    slot.consumers = 0;
    if (++slot.generation == 0)
        ++slot.generation;
    copyOwner(slot.owner, owner);
    std::memset(lease, 0, sizeof(*lease));
    lease->generation = slot.generation;
    copyOwner(lease->owner, slot.owner);
    lease->width = kEdge;
    lease->height = kEdge;
    lease->bytes = kBytes;
    lease->requested = place;
    lease->placed = slot.placed;
    lease->scratch = scratch;
    return true;
}

bool DisplaySurfaceBroker::acquire(const char *owner, SurfacePlace place, SurfaceLease *lease)
{
    return take(live_, false, owner, place, lease);
}

bool DisplaySurfaceBroker::acquireScratch(const char *owner, SurfacePlace place, SurfaceLease *lease)
{
    return take(scratch_, true, owner, place, lease);
}

bool DisplaySurfaceBroker::acquireRecord(const char *owner, SurfacePlace place, SurfaceLease *lease)
{
    if (!lease || record_.held)
        return false;
    SurfacePlace placed = place;
    auto *pixels = static_cast<uint16_t *>(allocate(kRecordBytes, place, &placed));
    if (!pixels)
        return false;
    if (record_.pixels)
        releaseBlock(record_.pixels);
    record_.pixels = pixels;
    record_.placed = placed;
    record_.held = true;
    record_.consumers = 0;
    if (++record_.generation == 0)
        ++record_.generation;
    copyOwner(record_.owner, owner);
    std::memset(lease, 0, sizeof(*lease));
    lease->generation = record_.generation;
    copyOwner(lease->owner, record_.owner);
    lease->width = kRecordEdge;
    lease->height = kRecordEdge;
    lease->bytes = kRecordBytes;
    lease->requested = place;
    lease->placed = placed;
    lease->record = true;
    return true;
}

DisplaySurfaceBroker::Slot *DisplaySurfaceBroker::match(const SurfaceLease &lease)
{
    Slot *slot = lease.record ? &record_ : lease.scratch ? &scratch_
                                                         : &live_;
    if (!slot->held || lease.generation == 0 || lease.generation != slot->generation)
        return nullptr;
    return slot;
}

const DisplaySurfaceBroker::Slot *DisplaySurfaceBroker::match(const SurfaceLease &lease) const
{
    const Slot *slot = lease.record ? &record_ : lease.scratch ? &scratch_
                                                               : &live_;
    if (!slot->held || lease.generation == 0 || lease.generation != slot->generation)
        return nullptr;
    return slot;
}

bool DisplaySurfaceBroker::addConsumer(SurfaceLease *lease)
{
    Slot *slot = lease ? match(*lease) : nullptr;
    if (!slot)
        return false;
    ++slot->consumers;
    lease->consumers = slot->consumers;
    return true;
}

bool DisplaySurfaceBroker::removeConsumer(SurfaceLease *lease)
{
    Slot *slot = lease ? match(*lease) : nullptr;
    if (!slot || slot->consumers <= 0)
        return false;
    --slot->consumers;
    lease->consumers = slot->consumers;
    return true;
}

bool DisplaySurfaceBroker::release(SurfaceLease *lease)
{
    Slot *slot = lease ? match(*lease) : nullptr;
    if (!slot || slot->consumers > 0)
        return false;
    const bool record = slot == &record_;
    slot->held = false;
    slot->consumers = 0;
    slot->owner[0] = 0;
    if (record && slot->pixels) {
        releaseBlock(slot->pixels);
        slot->pixels = nullptr;
    }
    std::memset(lease, 0, sizeof(*lease));
    return true;
}

bool DisplaySurfaceBroker::transfer(SurfaceLease *lease, const char *owner)
{
    Slot *slot = lease ? match(*lease) : nullptr;
    if (!slot || slot->consumers > 0)
        return false;
    if (++slot->generation == 0)
        ++slot->generation;
    copyOwner(slot->owner, owner);
    lease->generation = slot->generation;
    copyOwner(lease->owner, slot->owner);
    return true;
}

uint16_t *DisplaySurfaceBroker::pixels(const SurfaceLease &lease) const
{
    const Slot *slot = match(lease);
    return slot ? slot->pixels : nullptr;
}

int DisplaySurfaceBroker::heldCount() const
{
    return (live_.held ? 1 : 0) + (scratch_.held ? 1 : 0) + (record_.held ? 1 : 0);
}

bool DisplaySurfaceBroker::holdsScratch() const
{
    return scratch_.held;
}
} // namespace product_ui
