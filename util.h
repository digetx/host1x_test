/* kate: replace-tabs true; indent-width 4
 *
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef UTIL_H
#define UTIL_H

#include "gem.h"
#include <stdexcept>
#include <vector>

#include "tegra_drm.h"

class ioctl_error : public std::runtime_error {
public:
    ioctl_error(const char *message);

    int error;
};

class Channel {
public:
    Channel(DrmDevice &drm);
    ~Channel();
    uint32_t syncpoint(uint32_t index);

    uint64_t _context;
    DrmDevice &_drm;
};

struct SubmitQuirks {
    SubmitQuirks();

    uint32_t force_cmdbuf_words;
    uint32_t force_cmdbuf_offset;
};

class Submit {
private:
    std::vector<uint32_t> _cmdbuf;
    std::vector<drm_tegra_syncpt> _incrs;
    std::vector<drm_tegra_reloc> _relocs;
    uint32_t _flags;

public:
    Submit();

    void set_flags(uint32_t flags);
    void push(uint32_t cmd);
    void add_incr(uint32_t syncpt, int count);
    void add_reloc(uint32_t cmdbuf_offset, uint32_t target,
                   uint32_t target_offset, uint32_t shift);

    drm_tegra_submit submit(Channel &ch);

    SubmitQuirks quirks;
};

void wait_syncpoint(DrmDevice &drm, uint32_t id, uint32_t threshold, uint32_t timeout);

#endif

