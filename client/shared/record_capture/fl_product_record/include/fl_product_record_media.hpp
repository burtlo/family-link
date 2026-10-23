#ifndef FL_PRODUCT_RECORD_MEDIA_HPP
#define FL_PRODUCT_RECORD_MEDIA_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace fl_product_record {

/* Offset read/write, append, and flush. Host tests supply a file.
 * Device SD stays behind this interface; this header has no ESP-IDF types.
 */
class MediaBackend {
public:
    virtual ~MediaBackend();

    virtual bool write_at(uint64_t offset, const uint8_t *data, size_t len) = 0;
    virtual bool read_at(uint64_t offset, uint8_t *dest, size_t len) = 0;
    virtual bool append(const uint8_t *data, size_t len, uint64_t *out_offset) = 0;
    virtual bool flush() = 0;
};

class FileMediaBackend : public MediaBackend {
public:
    FileMediaBackend();
    ~FileMediaBackend() override;

    bool open_path(const char *path, bool truncate);
    void close();

    bool write_at(uint64_t offset, const uint8_t *data, size_t len) override;
    bool read_at(uint64_t offset, uint8_t *dest, size_t len) override;
    bool append(const uint8_t *data, size_t len, uint64_t *out_offset) override;
    bool flush() override;

private:
    FileMediaBackend(const FileMediaBackend &);
    FileMediaBackend &operator=(const FileMediaBackend &);

    FILE *file_;
    uint64_t size_;
};

} /* namespace fl_product_record */

#endif
