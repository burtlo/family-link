#!/usr/bin/env python3
"""Compile the actual fixed H38 marker formatter with synthetic identifiers."""
from __future__ import annotations

import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> None:
    source = (ROOT / "firmware/common/h38_filesystem_io.c").read_text()
    function = source.split("static bool marker_contents(", 1)[1].split("static bool write_marker(", 1)[0]
    function = "static bool marker_contents(" + function
    definitions = "\n".join(re.findall(r"^#define H38_IO_[A-Z_]+ .+$", source, re.M))
    required = ("content[H38_IO_MARKER_BUFFER_BYTES]", "expected[H38_IO_MARKER_BUFFER_BYTES]",
                "actual[H38_IO_MARKER_BUFFER_BYTES]", "marker[H38_IO_MARKER_BUFFER_BYTES]")
    if any(item not in source for item in required):
        raise AssertionError("marker write/verify/inventory capacities differ")
    preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    const char *epoch, *runtime_elf_sha256, *source_revision, *intent_sha256;
    const char *h35_reference_epoch, *private_cid_sha256;
} marker_context;
typedef struct { const marker_context *ctx; } io_state_t;
'''
    harness = r'''
static void fill(char *out, size_t n) { memset(out, 'a', n); out[n] = '\0'; }
int main(void) {
    char epoch[33], elf[65], revision[41], intent[65], reference[33], cid[65];
    fill(epoch,32); fill(elf,64); fill(revision,40); fill(intent,64); fill(reference,32); fill(cid,64);
    marker_context context = {epoch,elf,revision,intent,reference,cid};
    io_state_t state = {&context};
    char actual[H38_IO_MARKER_BUFFER_BYTES], copy[H38_IO_MARKER_BUFFER_BYTES];
    size_t n = 0, exact = 0;
    assert(H38_IO_MAX_MARKER == 4096u && H38_IO_MARKER_BUFFER_BYTES == 1024u);
    assert(!marker_contents(&state, actual, 512, &n));
    assert(marker_contents(&state, actual, sizeof(actual), &n));
    assert(n == 620 && actual[n] == '\0' && n < H38_IO_MAX_MARKER);
    assert(strstr(actual,"schema=h38-marker-v1\n") == actual);
    assert(strstr(actual,"allocation_unit_bytes=4096\n") != NULL);
    assert(!marker_contents(&state, copy, n, &exact));
    assert(marker_contents(&state, copy, n+1, &exact));
    assert(exact == n && !memcmp(actual,copy,n+1));
    char oversized[2049]; fill(oversized,2048); context.epoch = oversized;
    assert(!marker_contents(&state, actual, sizeof(actual), &exact));
    puts("H38 actual C marker checks passed: 620 bytes, 1024 capacity, truncation boundaries");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="h38-marker-check-") as directory:
        root = pathlib.Path(directory)
        code, executable = root / "check.c", root / "check"
        code.write_text(preamble + definitions + "\n" + function + harness)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(code),
                        "-o", str(executable)], check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    main()
