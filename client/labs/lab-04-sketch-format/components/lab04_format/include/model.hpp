#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lab04 {

constexpr std::uint16_t kSide = 120;
constexpr std::size_t kPreviewBytes = 120u * 120u * 2u;
constexpr std::size_t kMaximumInputSize = 65536;
constexpr std::size_t kMaximumSegments = 4096;
constexpr std::size_t kRasterWorkCap = 64;
constexpr std::size_t kEncodeQueueCapacity = 4;
constexpr std::uint8_t kUncolored = 0xff;

struct Segment {
    std::uint32_t time_ms;
    std::uint8_t x0, y0, x1, y1;
    std::uint8_t color;
    bool pen_up;
};

struct Drawing {
    std::uint16_t width = kSide;
    std::uint16_t height = kSide;
    std::uint32_t duration_ms = 0;
    std::vector<Segment> segments;
};

enum class Encoding { TimedPoints = 1, TimedStrokes = 2, SegmentPreview = 3, LegacyPoints = 4 };
enum class Terminal { None, Success, Failure, Cancelled };
enum class CheckpointOutcome { Unsupported };

struct CheckpointResult {
    CheckpointOutcome outcome = CheckpointOutcome::Unsupported;
    std::string reason = "unsupported";
};

struct Result {
    bool ok = false;
    std::string reason;
    std::vector<std::uint8_t> bytes;
};

// Optional LAB-04 preview instrumentation. For SegmentPreview,
// expected_output_segments is the exact output count declared by the header.
// The callback runs after the
// maximum-capacity reservation and before record insertion; the vector size is
// therefore still zero at observation time. Callers publish a measurement only
// if the semantic decode subsequently succeeds.
using DecodeReservationObserver = void (*)(void* context,
                                            std::size_t allocated_capacity,
                                            std::size_t expected_output_segments);

bool valid_drawing(const Drawing& drawing, std::string* reason = nullptr);
Result encode(const Drawing& drawing, Encoding encoding);
bool decode(const std::uint8_t* bytes, std::size_t size, Encoding encoding,
            Drawing& destination, std::string& reason,
            DecodeReservationObserver reservation_observer = nullptr,
            void* observer_context = nullptr);
CheckpointResult checkpoint_seek(Encoding encoding, const Drawing& drawing,
                                 std::uint32_t position_ms);
std::size_t preview_storage_bytes(std::uint16_t width, std::uint16_t height);
bool map_capture_2x(std::uint16_t display_x, std::uint16_t display_y,
                    std::uint8_t& logical_x, std::uint8_t& logical_y);
std::size_t raster_update(std::uint16_t* pixels, const Drawing& drawing,
                          std::size_t& rendered, std::uint32_t position_ms,
                          const std::uint16_t rgb565_palette[6],
                          std::size_t requested_work = kRasterWorkCap,
                          std::string* reason = nullptr);

struct Operation {
    std::uint64_t id = 0;
    Terminal terminal = Terminal::None;
    std::size_t live_buffers = 0;
    std::size_t terminal_count = 0;
    std::string reason;
};

class DecodeOperation {
public:
    explicit DecodeOperation(std::uint64_t id);
    bool run(const std::uint8_t* bytes, std::size_t size, Encoding encoding,
             Drawing& destination, bool cancel_midway = false,
             DecodeReservationObserver reservation_observer = nullptr,
             void* observer_context = nullptr);
    bool cancel();
    const Operation& state() const { return operation_; }
private:
    bool finish(Terminal terminal, std::string&& reason);
    Operation operation_;
};

class EncodeQueue {
public:
    bool push(const Drawing& drawing);
    bool pop(Drawing& drawing);
    std::size_t size() const { return items_.size(); }
    std::size_t overflow_count() const { return overflow_count_; }
    const Operation& operation() const { return operation_; }
private:
    std::vector<Drawing> items_;
    std::size_t overflow_count_ = 0;
    Operation operation_{0, Terminal::None, 0, 0, ""};
};

} // namespace lab04
