#pragma once

#include "fl_product_record_media.hpp"
#include "fl_product_record_session.hpp"
#include "lvgl.h"

#include <cstdint>

namespace product_ui
{

// One record session and its file. The view borrows pixels; this object
// outlives the sketch screen. Unfinished bytes are not a committed send.
class CaptureService
{
  public:
    using Arm = void (*)(bool armed);
    using Failed = bool (*)();

    CaptureService() = default;
    ~CaptureService();
    CaptureService(const CaptureService &) = delete;
    CaptureService &operator=(const CaptureService &) = delete;

    bool open(const char *path);
    bool ready() const
    {
        return open_;
    }
    void setMicAvailable(bool available)
    {
        mic_available_ = available;
    }
    void setHardware(Arm arm, Failed failed);
    bool canStart() const;
    bool canStartSecond();
    void stageIdentity(const fl_product_record::SendIdentity &identity);
    bool begin(const char *clientMessageId, int64_t now);
    void advance(int64_t now);
    int consumeNotice();
    bool pushPcm(const uint8_t *pcm, size_t length);
    void notifyMicFailed();
    bool routeTouch(int16_t x, int16_t y, uint8_t phase);
    void render();
    bool finish();
    void cancel();
    void discard();
    void beginReverse(int64_t now);
    void beginCollapse(int64_t now);
    bool inputOpen() const;
    bool wiping() const;
    bool recording() const;
    bool reversing() const;
    int32_t ringRadius() const;
    uint8_t selectedColor() const;
    bool blocksInboxFetch();
    void attachSurface(uint16_t *pixels, lv_obj_t *canvas);
    void detachSurface();
    bool finishedInfo(const char *clientMessageId, uint32_t *audioBytes, uint32_t *sketchBytes);
    bool readFinished(const char *clientMessageId, int kind, uint32_t offset, uint8_t *dest, uint32_t length,
                      uint32_t *read);
    bool markUploading(const char *clientMessageId);
    void clearUpload(const char *clientMessageId);
    bool identityFor(const char *clientMessageId, fl_product_record::SendIdentity *identity);
    const fl_product_record::StoredObject *finishedSlot(int index);

  private:
    class Lock
    {
      public:
        explicit Lock(CaptureService *owner);
        ~Lock();

      private:
        CaptureService *owner_;
    };
    void ensureLock();
    fl_product_record::FileMediaBackend media_;
    fl_product_record::RecordSession session_;
    fl_product_record::SendIdentity staged_{};
    Arm arm_ = nullptr;
    Failed failed_ = nullptr;
    bool open_ = false;
    bool mic_available_ = true;
    bool armed_ = false;
#ifdef ESP_PLATFORM
    void *lock_ = nullptr;
#endif
};

} // namespace product_ui
