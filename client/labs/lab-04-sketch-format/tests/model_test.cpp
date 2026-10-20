#include "model.hpp"
#include "fixture_map.hpp"
#include "preview_accounting.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
namespace lab04 {
void arm_one_allocation_failure_for_test();
}

using namespace lab04;

struct ReservationObservation {
    std::size_t calls = 0;
    std::size_t capacity = 0;
    std::size_t expected_segments = 0;
};

static void observe_reservation(void* context, std::size_t capacity,
                                std::size_t expected_segments) {
    auto* observation = static_cast<ReservationObservation*>(context);
    ++observation->calls;
    observation->capacity = capacity;
    observation->expected_segments = expected_segments;
}

static Drawing load_corpus_b(const char* path) {
    Drawing d; d.duration_ms=2400;
    std::ifstream in(path); assert(in.good());
    std::string line;
    while (std::getline(in,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line); Segment s{}; int x0,y0,x1,y1,color,pen;
        row >> s.time_ms >> x0 >> y0 >> x1 >> y1 >> color >> pen;
        s.x0=static_cast<std::uint8_t>(x0); s.y0=static_cast<std::uint8_t>(y0);
        s.x1=static_cast<std::uint8_t>(x1); s.y1=static_cast<std::uint8_t>(y1);
        s.color=static_cast<std::uint8_t>(color);
        assert(row.good() || row.eof()); s.pen_up=pen!=0; d.segments.push_back(s);
    }
    return d;
}

static Drawing load_corpus_a_dense(const char* path) {
    std::ifstream in(path, std::ios::binary);
    assert(in.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    assert(bytes.size() == 40968 && std::string(bytes.begin(), bytes.begin()+4) == "L3SK");
    const std::size_t count = bytes[6] | (static_cast<std::size_t>(bytes[7]) << 8);
    assert(count == 4096 && bytes.size() == 8 + count * 10);
    const std::uint16_t rgb565[] = {0x001f, 0x07e0, 0xf800};
    Drawing d; d.duration_ms=3000; d.segments.reserve(count);
    for (std::size_t i=0; i<count; ++i) {
        const auto* p=bytes.data()+8+i*10;
        const auto time=static_cast<std::uint32_t>(p[0]) |
                        (static_cast<std::uint32_t>(p[1])<<8) |
                        (static_cast<std::uint32_t>(p[2])<<16) |
                        (static_cast<std::uint32_t>(p[3])<<24);
        const auto color=static_cast<std::uint16_t>(p[8] | (p[9]<<8));
        std::uint8_t index=0xff;
        for (std::uint8_t c=0;c<3;++c) if (rgb565[c]==color) index=c;
        assert(index != 0xff);
        d.segments.push_back({time,p[4],p[5],p[6],p[7],index,false});
    }
    std::string reason; assert(valid_drawing(d,&reason));
    return d;
}

int main(int argc,char** argv) {
    const auto failed_preview = preview_accounting(2, false, false);
    assert(failed_preview.bytes == 0 && failed_preview.buffers == 0);
    const auto successful_preview = preview_accounting(2, true, false);
    assert(successful_preview.bytes == 28800 && successful_preview.buffers == 1);
    const auto allocated_scratch = preview_accounting(0, false, true);
    assert(allocated_scratch.bytes == 28800 && allocated_scratch.buffers == 1);
    const auto absent_scratch = preview_accounting(1, false, false);
    assert(absent_scratch.bytes == 0 && absent_scratch.buffers == 0);
    assert(argc==3 || argc==4);
    assert(valid_fixture_index(0) && std::string(fixture_name(0)) == "corpus-a-short");
    assert(valid_fixture_index(1) && std::string(fixture_name(1)) == "corpus-a-dense");
    assert(valid_fixture_index(2) && std::string(fixture_name(2)) == "corpus-b");
    assert(!valid_fixture_index(3) && fixture_name(3) == nullptr);
    assert(!valid_fixture_index(255) && fixture_name(255) == nullptr);
    const Drawing source=load_corpus_b(argv[1]);
    const Drawing dense=load_corpus_a_dense(argv[2]);
    std::string reason;
    assert(valid_drawing(source,&reason));
    int output_index=0;
    for (Encoding e : {Encoding::TimedPoints,Encoding::TimedStrokes,Encoding::SegmentPreview}) {
        auto encoded=encode(source,e); assert(encoded.ok);
        if (argc==4) {
            const char* names[]={"points.bin","strokes.bin","preview.bin"};
            std::string path=std::string(argv[3])+"/"+names[output_index];
            std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(encoded.bytes.data()),encoded.bytes.size()); assert(out.good());
        }
        ++output_index;
        Drawing decoded; if (!decode(encoded.bytes.data(),encoded.bytes.size(),e,decoded,reason)) { std::cerr << "decode arm " << static_cast<int>(e) << " failed: " << reason << "\n"; assert(false); }
        assert(decoded.duration_ms==source.duration_ms && decoded.segments.size()==source.segments.size());
        for (std::size_t i=0;i<source.segments.size();++i) {
            const auto& a=source.segments[i]; const auto& b=decoded.segments[i];
            assert(a.time_ms==b.time_ms && a.x0==b.x0 && a.y0==b.y0 && a.x1==b.x1 && a.y1==b.y1);
            assert(a.color==b.color && a.pen_up==b.pen_up);
        }
    }
    assert(preview_storage_bytes(120,120)==28800);
    std::uint8_t mapped_x=0,mapped_y=0;
    assert(map_capture_2x(239,238,mapped_x,mapped_y) && mapped_x==119 && mapped_y==119);
    assert(!map_capture_2x(240,0,mapped_x,mapped_y));
    std::uint16_t pixels[120*120]={}; const std::uint16_t palette[6]={0,0xffff,0x1234,0x2345,0x3456,0x4567};
    std::size_t rendered=0;
    const auto control_eligible=static_cast<std::size_t>(std::upper_bound(
        source.segments.begin(),source.segments.end(),source.duration_ms,
        [](std::uint32_t position,const Segment& segment){return position<segment.time_ms;})-source.segments.begin());
    const auto control_work=raster_update(pixels,source,rendered,source.duration_ms,palette,64);
    assert(control_eligible<=64 && control_work==control_eligible && rendered==control_eligible);
    assert(control_eligible-rendered==0);
    const auto dense_eligible=static_cast<std::size_t>(std::upper_bound(
        dense.segments.begin(),dense.segments.end(),dense.duration_ms,
        [](std::uint32_t position,const Segment& segment){return position<segment.time_ms;})-dense.segments.begin());
    assert(dense_eligible>64);
    rendered=0;
    while (rendered<dense_eligible) {
        const auto before=rendered;
        const auto work=raster_update(pixels,dense,rendered,dense.duration_ms,palette,64);
        assert(work<=64 && rendered-before==work);
        assert(dense_eligible-rendered <= dense_eligible-before);
        if (before==0) assert(dense_eligible-rendered==dense_eligible-64);
    }
    assert(rendered==dense_eligible && dense_eligible-rendered==0);

    auto good=encode(source,Encoding::TimedPoints); assert(good.ok);
    auto preview=encode(source,Encoding::SegmentPreview); assert(preview.ok);
    ReservationObservation reservation;
    Drawing observed_decode;
    assert(decode(preview.bytes.data(),preview.bytes.size(),Encoding::SegmentPreview,
                  observed_decode,reason,observe_reservation,&reservation));
    assert(reservation.calls==1);
    assert(reservation.capacity>=kMaximumSegments);
    assert(reservation.expected_segments==source.segments.size());
    assert(observed_decode.segments.size()==reservation.expected_segments);
    Drawing preserved=source; preserved.segments[0].x0=77;
    assert(!decode(good.bytes.data(),good.bytes.size()-1,Encoding::TimedPoints,preserved,reason));
    assert(preserved.segments[0].x0==77 && reason=="truncated_records");
    DecodeOperation malformed(41);
    assert(!malformed.run(good.bytes.data(),good.bytes.size()-1,Encoding::TimedPoints,preserved));
    assert(malformed.state().terminal==Terminal::Failure && malformed.state().terminal_count==1);
    assert(malformed.state().reason=="truncated_records" && malformed.state().live_buffers==0);
    assert(!malformed.cancel() && malformed.state().terminal_count==1);

    // Force the actual production reservation and encode-vector allocations
    // to fail. The decode operation must publish one failure terminal and
    // preserve its destination; encoding must return a failed Result.
    DecodeOperation reserve_failure(43);
    Drawing reserve_destination = source;
    const auto preserved_segment_count = reserve_destination.segments.size();
    arm_one_allocation_failure_for_test();
    assert(!reserve_failure.run(preview.bytes.data(), preview.bytes.size(),
                                Encoding::SegmentPreview, reserve_destination));
    assert(reserve_failure.state().terminal == Terminal::Failure);
    assert(reserve_failure.state().terminal_count == 1);
    assert(reserve_failure.state().live_buffers == 0);
    assert(reserve_failure.state().reason == "no_memory");
    assert(reserve_destination.segments.size() == preserved_segment_count);

    arm_one_allocation_failure_for_test();
    const auto encode_failure = encode(source, Encoding::TimedPoints);
    assert(!encode_failure.ok && encode_failure.bytes.empty());
    assert(encode_failure.reason == "no_memory");
    auto bad_color=good.bytes; bad_color[16+8]=6;
    assert(!decode(bad_color.data(),bad_color.size(),Encoding::TimedPoints,preserved,reason));
    assert(reason=="wrong_color_count" && preserved.segments[0].x0==77);
    for (Encoding e : {Encoding::TimedPoints, Encoding::TimedStrokes, Encoding::SegmentPreview}) {
        auto colored=encode(source,e); assert(colored.ok);
        auto uncolored_bytes=colored.bytes;
        const std::size_t color_offset=e==Encoding::TimedStrokes ? 16 : 24;
        uncolored_bytes[color_offset]=kUncolored;
        assert(!decode(uncolored_bytes.data(),uncolored_bytes.size(),e,preserved,reason));
        assert(reason=="wrong_color_count" && preserved.segments[0].x0==77);
    }

    std::vector<std::uint8_t> oversized(kMaximumInputSize+1,0);
    assert(!decode(oversized.data(),oversized.size(),Encoding::TimedPoints,preserved,reason));
    assert(reason=="input_too_large");
    Drawing huge=source; huge.segments.resize(kMaximumSegments+1,source.segments[0]);
    assert(!encode(huge,Encoding::TimedPoints).ok);

    EncodeQueue queue;
    for (std::size_t i=0;i<kEncodeQueueCapacity;i++) assert(queue.push(source));
    assert(!queue.push(source));
    assert(queue.size()==kEncodeQueueCapacity && queue.overflow_count()==1);
    assert(queue.operation().terminal==Terminal::Failure && queue.operation().terminal_count==1);
    assert(queue.operation().reason=="overflow");

    DecodeOperation cancelled(42); Drawing untouched;
    assert(!cancelled.run(good.bytes.data(),good.bytes.size(),Encoding::TimedPoints,untouched,true));
    assert(cancelled.state().terminal==Terminal::Cancelled && cancelled.state().terminal_count==1);
    assert(cancelled.state().reason=="cancelled_mid_decode" && cancelled.state().live_buffers==0);
    assert(untouched.segments.empty() && !cancelled.cancel());
    assert(cancelled.state().terminal_count==1);

    Drawing legacy; legacy.duration_ms=1000;
    legacy.segments.push_back({10,1,2,3,4,kUncolored,false});
    auto old=encode(legacy,Encoding::LegacyPoints); assert(old.ok);
    Drawing old_roundtrip; assert(decode(old.bytes.data(),old.bytes.size(),Encoding::LegacyPoints,old_roundtrip,reason));
    assert(old_roundtrip.segments[0].color==kUncolored);
    for (Encoding e : {Encoding::TimedPoints, Encoding::TimedStrokes, Encoding::SegmentPreview})
        assert(encode(legacy,e).reason=="wrong_color_count");
    assert(encode(source,Encoding::LegacyPoints).reason=="unsupported_legacy");
    for (Encoding e : {Encoding::TimedPoints, Encoding::TimedStrokes, Encoding::SegmentPreview, Encoding::LegacyPoints}) {
        for (std::uint32_t position : {0u, 1200u, 2399u}) {
            auto checkpoint=checkpoint_seek(e,source,position);
            assert(checkpoint.outcome==CheckpointOutcome::Unsupported && checkpoint.reason=="unsupported");
        }
    }
    std::cout << "LAB04 model: roundtrip, bounds, malformed, overflow, cancel, legacy, checkpoint unsupported, bounded raster accounting PASS\n";
}
