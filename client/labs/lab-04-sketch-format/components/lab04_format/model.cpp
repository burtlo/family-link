#include "model.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace lab04
{
namespace
{
constexpr char kAllocationFailureReason[] = "no_memory";

#ifdef LAB04_HOST_TEST
bool allocation_failure_armed = false;
#endif

void throw_if_allocation_failure_armed()
{
#ifdef LAB04_HOST_TEST
    // The host test arms one failure. Replacing global operator new crashes
    // on Windows: libstdc++ and this executable then free across two heaps.
    if (!allocation_failure_armed)
        return;
    allocation_failure_armed = false;
    throw std::bad_alloc();
#endif
}

void set_allocation_failure_reason(std::string &reason)
{
    // This short reason fits the target std::string's inline storage, so
    // reporting the allocation failure does not need another heap allocation.
    reason.clear();
    reason.assign(kAllocationFailureReason);
}

void put8(std::vector<std::uint8_t> &out, std::uint8_t v)
{
    out.push_back(v);
}
void put16(std::vector<std::uint8_t> &out, std::uint16_t v)
{
    put8(out, v & 0xff);
    put8(out, v >> 8);
}
void put32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
    put16(out, v & 0xffff);
    put16(out, v >> 16);
}
bool get8(const std::uint8_t *p, std::size_t n, std::size_t &at, std::uint8_t &v)
{
    if (at >= n)
        return false;
    v = p[at++];
    return true;
}
bool get16(const std::uint8_t *p, std::size_t n, std::size_t &at, std::uint16_t &v)
{
    std::uint8_t a, b;
    if (!get8(p, n, at, a) || !get8(p, n, at, b))
        return false;
    v = static_cast<std::uint16_t>(a | (static_cast<std::uint16_t>(b) << 8));
    return true;
}
bool get32(const std::uint8_t *p, std::size_t n, std::size_t &at, std::uint32_t &v)
{
    std::uint16_t a, b;
    if (!get16(p, n, at, a) || !get16(p, n, at, b))
        return false;
    v = static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 16);
    return true;
}
void put_segment(std::vector<std::uint8_t> &out, const Segment &s, bool include_time, bool include_color)
{
    if (include_time)
        put32(out, s.time_ms);
    put8(out, s.x0);
    put8(out, s.y0);
    put8(out, s.x1);
    put8(out, s.y1);
    if (include_color)
        put8(out, s.color);
    put8(out, s.pen_up ? 1 : 0);
}
bool read_segment(const std::uint8_t *p, std::size_t n, std::size_t &at, Segment &s,
                  bool include_time, bool include_color, std::uint8_t stroke_color = 0)
{
    std::uint8_t flag;
    if (include_time && !get32(p, n, at, s.time_ms))
        return false;
    if (!get8(p, n, at, s.x0) || !get8(p, n, at, s.y0) || !get8(p, n, at, s.x1) || !get8(p, n, at, s.y1))
        return false;
    if (include_color) {
        if (!get8(p, n, at, s.color))
            return false;
    } else
        s.color = stroke_color;
    if (!get8(p, n, at, flag) || flag > 1)
        return false;
    s.pen_up = flag != 0;
    return true;
}
void header(std::vector<std::uint8_t> &out, Encoding e, const Drawing &d, std::uint16_t count)
{
    out.insert(out.end(), {'L', '4', 'S', 'F'});
    put8(out, 1);
    put8(out, static_cast<std::uint8_t>(e));
    put16(out, count);
    put16(out, d.width);
    put16(out, d.height);
    put32(out, d.duration_ms);
}
bool read_header(const std::uint8_t *p, std::size_t n, Encoding e, std::size_t &at,
                 std::uint16_t &count, Drawing &d, std::string &reason)
{
    if (!p || n < 16) {
        reason = "truncated_header";
        return false;
    }
    if (std::memcmp(p, "L4SF", 4) != 0) {
        reason = "bad_magic";
        return false;
    }
    at = 4;
    std::uint8_t version, mode;
    if (!get8(p, n, at, version) || !get8(p, n, at, mode) || !get16(p, n, at, count) ||
        !get16(p, n, at, d.width) || !get16(p, n, at, d.height) || !get32(p, n, at, d.duration_ms)) {
        reason = "truncated_header";
        return false;
    }
    if (version != 1 || mode != static_cast<std::uint8_t>(e)) {
        reason = "unsupported_format";
        return false;
    }
    if (d.width != kSide || d.height != kSide) {
        reason = "wrong_raster_size";
        return false;
    }
    if (count > kMaximumSegments) {
        reason = "too_many_segments";
        return false;
    }
    return true;
}
} // namespace

#ifdef LAB04_HOST_TEST
void arm_one_allocation_failure_for_test()
{
    allocation_failure_armed = true;
}
#endif

bool valid_drawing(const Drawing &d, std::string *reason)
{
    auto fail = [reason](const char *why) { if (reason) *reason = why; return false; };
    if (d.width != kSide || d.height != kSide)
        return fail("wrong_raster_size");
    if (d.segments.size() > kMaximumSegments)
        return fail("too_many_segments");
    std::uint32_t last = 0;
    for (const auto &s : d.segments) {
        if (s.time_ms < last || s.time_ms >= d.duration_ms)
            return fail("invalid_time");
        if (s.x0 >= kSide || s.y0 >= kSide || s.x1 >= kSide || s.y1 >= kSide)
            return fail("invalid_coordinate");
        if (s.color >= 6 && s.color != kUncolored)
            return fail("wrong_color_count");
        last = s.time_ms;
    }
    if (reason)
        reason->clear();
    return true;
}

Result encode(const Drawing &d, Encoding e)
{
    Result r;
    std::string why;
    try {
        if (!valid_drawing(d, &why)) {
            r.reason = why;
            return r;
        }
        if (e != Encoding::LegacyPoints && std::any_of(d.segments.begin(), d.segments.end(),
                                                       [](const Segment &s) { return s.color == kUncolored; })) {
            r.reason = "wrong_color_count";
            return r;
        }
        if (e == Encoding::LegacyPoints && std::any_of(d.segments.begin(), d.segments.end(),
                                                       [](const Segment &s) { return s.color != kUncolored; })) {
            r.reason = "unsupported_legacy";
            return r;
        }
        throw_if_allocation_failure_armed();
        std::uint16_t record_count = static_cast<std::uint16_t>(d.segments.size());
        if (e == Encoding::TimedStrokes) {
            record_count = 0;
            for (std::size_t i = 0; i < d.segments.size();) {
                const auto color = d.segments[i].color;
                std::size_t j = i + 1;
                while (j < d.segments.size() && !d.segments[j - 1].pen_up && d.segments[j].color == color)
                    ++j;
                if (j - i > 255 || record_count == std::numeric_limits<std::uint16_t>::max()) {
                    r.reason = "stroke_too_long";
                    return r;
                }
                ++record_count;
                i = j;
            }
        }
        header(r.bytes, e, d, record_count);
        if (e == Encoding::TimedPoints || e == Encoding::SegmentPreview) {
            for (const auto &s : d.segments)
                put_segment(r.bytes, s, true, true);
            if (e == Encoding::SegmentPreview)
                r.bytes.resize(r.bytes.size() + kPreviewBytes, 0);
        } else if (e == Encoding::TimedStrokes) {
            for (std::size_t i = 0; i < d.segments.size();) {
                const std::size_t start = i;
                const std::uint8_t color = d.segments[i].color;
                while (i < d.segments.size() && d.segments[i].color == color &&
                       (i == start || !d.segments[i - 1].pen_up))
                    ++i;
                put8(r.bytes, color);
                put8(r.bytes, static_cast<std::uint8_t>(i - start));
                for (std::size_t j = start; j < i; ++j)
                    put_segment(r.bytes, d.segments[j], true, false);
            }
        } else {
            for (const auto &s : d.segments)
                put_segment(r.bytes, s, true, false);
        }
        if (r.bytes.size() > kMaximumInputSize) {
            r.bytes.clear();
            r.reason = "input_too_large";
            return r;
        }
        r.ok = true;
        return r;
    } catch (const std::bad_alloc &) {
        r.bytes.clear();
        set_allocation_failure_reason(r.reason);
        r.ok = false;
        return r;
    }
}

static bool decode_impl(const std::uint8_t *p, std::size_t n, Encoding e, Drawing &destination,
                        std::string &reason, bool cancel_midway, bool &cancelled,
                        DecodeReservationObserver reservation_observer,
                        void *observer_context)
{
    cancelled = false;
    if (n > kMaximumInputSize) {
        reason = "input_too_large";
        return false;
    }
    Drawing candidate;
    std::size_t at = 0;
    std::uint16_t count = 0;
    if (!read_header(p, n, e, at, count, candidate, reason))
        return false;
    throw_if_allocation_failure_armed();
    candidate.segments.reserve(kMaximumSegments);
    if (reservation_observer && e == Encoding::SegmentPreview) {
        // SegmentPreview's header count is the expected decoded segment count.
        // The observer sees the successful capacity reservation before records
        // are processed; callers decide whether a later semantic decode result
        // is eligible for publication.
        reservation_observer(observer_context, candidate.segments.capacity(), count);
    }
    if (e == Encoding::TimedPoints || e == Encoding::SegmentPreview) {
        const std::size_t expected = at + static_cast<std::size_t>(count) * 10 + (e == Encoding::SegmentPreview ? kPreviewBytes : 0);
        if (n != expected) {
            reason = n < expected ? "truncated_records" : "unexpected_trailing_bytes";
            return false;
        }
        for (unsigned i = 0; i < count; i++) {
            if (cancel_midway && i >= std::max(1u, static_cast<unsigned>(count / 2))) {
                reason = "cancelled_mid_decode";
                cancelled = true;
                return false;
            }
            Segment s{};
            if (!read_segment(p, n, at, s, true, true)) {
                reason = "malformed_record";
                return false;
            }
            if (s.color == kUncolored) {
                reason = "wrong_color_count";
                return false;
            }
            candidate.segments.push_back(s);
        }
        if (e == Encoding::SegmentPreview)
            at += kPreviewBytes;
    } else if (e == Encoding::TimedStrokes) {
        std::size_t total = 0;
        for (unsigned i = 0; i < count; i++) {
            if (cancel_midway && i >= std::max(1u, static_cast<unsigned>(count / 2))) {
                reason = "cancelled_mid_decode";
                cancelled = true;
                return false;
            }
            std::uint8_t color, segments;
            if (!get8(p, n, at, color) || !get8(p, n, at, segments) || segments == 0) {
                reason = "truncated_stroke";
                return false;
            }
            if (color >= 6) {
                reason = "wrong_color_count";
                return false;
            }
            total += segments;
            if (total > kMaximumSegments) {
                reason = "too_many_segments";
                return false;
            }
            for (unsigned j = 0; j < segments; j++) {
                Segment s{};
                if (!read_segment(p, n, at, s, true, false, color)) {
                    reason = "truncated_stroke";
                    return false;
                }
                candidate.segments.push_back(s);
            }
        }
    } else if (e == Encoding::LegacyPoints) {
        const std::size_t expected = at + static_cast<std::size_t>(count) * 9;
        if (n != expected) {
            reason = n < expected ? "truncated_records" : "unexpected_trailing_bytes";
            return false;
        }
        for (unsigned i = 0; i < count; i++) {
            if (cancel_midway && i >= std::max(1u, static_cast<unsigned>(count / 2))) {
                reason = "cancelled_mid_decode";
                cancelled = true;
                return false;
            }
            Segment s{};
            if (!read_segment(p, n, at, s, true, false)) {
                reason = "malformed_record";
                return false;
            }
            s.color = kUncolored;
            candidate.segments.push_back(s);
        }
    } else {
        reason = "unsupported_format";
        return false;
    }
    if (at != n) {
        reason = "unexpected_trailing_bytes";
        return false;
    }
    if (!valid_drawing(candidate, &reason))
        return false;
    destination = std::move(candidate);
    reason.clear();
    return true;
}

bool decode(const std::uint8_t *p, std::size_t n, Encoding e, Drawing &destination,
            std::string &reason,
            DecodeReservationObserver reservation_observer,
            void *observer_context)
{
    bool cancelled = false;
    try {
        return decode_impl(p, n, e, destination, reason, false, cancelled,
                           reservation_observer, observer_context);
    } catch (const std::bad_alloc &) {
        set_allocation_failure_reason(reason);
        return false;
    }
}

CheckpointResult checkpoint_seek(Encoding encoding, const Drawing &drawing,
                                 std::uint32_t position_ms)
{
    (void)encoding;
    (void)drawing;
    (void)position_ms;
    return {};
}

std::size_t preview_storage_bytes(std::uint16_t width, std::uint16_t height)
{
    return static_cast<std::size_t>(width) * height * 2;
}

bool map_capture_2x(std::uint16_t x, std::uint16_t y, std::uint8_t &lx, std::uint8_t &ly)
{
    if (x >= 240 || y >= 240)
        return false;
    lx = static_cast<std::uint8_t>(x / 2);
    ly = static_cast<std::uint8_t>(y / 2);
    return true;
}

std::size_t raster_update(std::uint16_t *pixels, const Drawing &d, std::size_t &rendered,
                          std::uint32_t position_ms, const std::uint16_t palette[6],
                          std::size_t requested, std::string *reason)
{
    if (!pixels || !palette || rendered > d.segments.size()) {
        if (reason)
            *reason = "invalid_raster_state";
        return 0;
    }
    std::string why;
    if (!valid_drawing(d, &why)) {
        if (reason)
            *reason = why;
        return 0;
    }
    const std::size_t cap = std::min(requested, kRasterWorkCap);
    std::size_t work = 0;
    while (rendered < d.segments.size() && work < cap && d.segments[rendered].time_ms <= position_ms) {
        const Segment &s = d.segments[rendered];
        if (s.color == kUncolored) {
            if (reason)
                *reason = "unsupported_legacy";
            return work;
        }
        int x = s.x0, y = s.y0, dx = std::abs(static_cast<int>(s.x1) - x), sx = x < s.x1 ? 1 : -1;
        int dy = -std::abs(static_cast<int>(s.y1) - y), sy = y < s.y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            pixels[y * kSide + x] = palette[s.color];
            if (x == s.x1 && y == s.y1)
                break;
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y += sy;
            }
        }
        ++rendered;
        ++work;
    }
    if (reason)
        reason->clear();
    return work;
}

DecodeOperation::DecodeOperation(std::uint64_t id)
{
    operation_.id = id;
}
bool DecodeOperation::finish(Terminal t, std::string &&why)
{
    if (operation_.terminal != Terminal::None)
        return false;
    operation_.terminal = t;
    operation_.terminal_count++;
    operation_.reason = std::move(why);
    operation_.live_buffers = 0;
    return true;
}
bool DecodeOperation::run(const std::uint8_t *bytes, std::size_t size, Encoding e, Drawing &dst,
                          bool cancel_midway,
                          DecodeReservationObserver reservation_observer,
                          void *observer_context)
{
    if (operation_.terminal != Terminal::None)
        return false;
    operation_.live_buffers = 1;
    std::string reason;
    bool cancelled = false;
    bool ok = false;
    try {
        ok = decode_impl(bytes, size, e, dst, reason, cancel_midway, cancelled,
                         reservation_observer, observer_context);
    } catch (const std::bad_alloc &) {
        set_allocation_failure_reason(reason);
    }
    finish(cancelled ? Terminal::Cancelled : (ok ? Terminal::Success : Terminal::Failure), std::move(reason));
    return ok;
}
bool DecodeOperation::cancel()
{
    return finish(Terminal::Cancelled, std::string("cancelled"));
}

bool EncodeQueue::push(const Drawing &d)
{
    if (operation_.terminal != Terminal::None)
        return false;
    if (items_.size() == kEncodeQueueCapacity) {
        ++overflow_count_;
        operation_.terminal = Terminal::Failure;
        operation_.terminal_count = 1;
        operation_.reason = "overflow";
        return false;
    }
    try {
        items_.push_back(d);
        return true;
    } catch (const std::bad_alloc &) {
        operation_.terminal = Terminal::Failure;
        operation_.terminal_count = 1;
        set_allocation_failure_reason(operation_.reason);
        return false;
    }
}
bool EncodeQueue::pop(Drawing &d)
{
    if (items_.empty())
        return false;
    d = std::move(items_.front());
    items_.erase(items_.begin());
    return true;
}

} // namespace lab04
