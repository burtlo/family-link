#include "fl_product_record_media.hpp"

#include <stdio.h>
#include <unistd.h>

namespace fl_product_record {

MediaBackend::~MediaBackend() {}

FileMediaBackend::FileMediaBackend() : file_(0), size_(0) {}

FileMediaBackend::~FileMediaBackend() { close(); }

bool FileMediaBackend::open_path(const char *path, bool truncate)
{
    long end;
    close();
    if (path == 0 || path[0] == '\0') {
        return false;
    }
    file_ = fopen(path, truncate ? "w+b" : "r+b");
    if (file_ == 0) {
        return false;
    }
    if (fseek(file_, 0, SEEK_END) != 0) {
        close();
        return false;
    }
    end = ftell(file_);
    if (end < 0) {
        close();
        return false;
    }
    size_ = (uint64_t)end;
    return true;
}

void FileMediaBackend::close()
{
    if (file_ != 0) {
        fclose(file_);
        file_ = 0;
    }
}

bool FileMediaBackend::write_at(uint64_t offset, const uint8_t *data, size_t len)
{
    if (file_ == 0 || (data == 0 && len != 0)) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (offset > (uint64_t)0x7fffffff || (uint64_t)len > (uint64_t)0x7fffffff) {
        return false;
    }
    if (fseek(file_, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    if (fwrite(data, 1, len, file_) != len) {
        return false;
    }
    if (offset + (uint64_t)len > size_) {
        size_ = offset + (uint64_t)len;
    }
    return true;
}

bool FileMediaBackend::read_at(uint64_t offset, uint8_t *dest, size_t len)
{
    if (file_ == 0 || (dest == 0 && len != 0)) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (offset > (uint64_t)0x7fffffff || (uint64_t)len > (uint64_t)0x7fffffff) {
        return false;
    }
    if (fseek(file_, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    if (fread(dest, 1, len, file_) != len) {
        return false;
    }
    return true;
}

bool FileMediaBackend::append(const uint8_t *data, size_t len, uint64_t *out_offset)
{
    uint64_t at;
    if (file_ == 0 || out_offset == 0) {
        return false;
    }
    at = size_;
    if (!write_at(at, data, len)) {
        return false;
    }
    *out_offset = at;
    return true;
}

bool FileMediaBackend::flush()
{
    if (file_ == 0) {
        return false;
    }
    /* fflush alone leaves the FAT directory entry at its old size until close. */
    return fflush(file_) == 0 && fsync(fileno(file_)) == 0;
}

} /* namespace fl_product_record */
