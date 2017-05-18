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

#include "util.h"

#include <cerrno>
#include <cstring>

#include "host1x.h"

ioctl_error::ioctl_error(const char *message) : std::runtime_error(message) {
    error = errno;
}

Channel::Channel(DrmDevice &drm) : _drm(drm) {
    drm_tegra_open_channel open_channel_args;
    memset(&open_channel_args, 0, sizeof(open_channel_args));
    open_channel_args.client = HOST1X_CLASS_VIC;

    int err = drm.ioctl(DRM_IOCTL_TEGRA_OPEN_CHANNEL, &open_channel_args);
    if (err == -1)
        throw ioctl_error("Channel open failed");

    _context = open_channel_args.context;
}

Channel::~Channel() {
    drm_tegra_close_channel close_channel_args;
    memset(&close_channel_args, 0, sizeof(close_channel_args));
    close_channel_args.context = _context;

    _drm.ioctl(DRM_IOCTL_TEGRA_CLOSE_CHANNEL, &close_channel_args);
}

uint32_t Channel::syncpoint(uint32_t index) {
    drm_tegra_get_syncpt get_syncpt_args;
    memset(&get_syncpt_args, 0, sizeof(get_syncpt_args));
    get_syncpt_args.context = _context;
    get_syncpt_args.index = index;

    int err = _drm.ioctl(DRM_IOCTL_TEGRA_GET_SYNCPT, &get_syncpt_args);
    if (err == -1)
        throw ioctl_error("Syncpt get failed");

    return get_syncpt_args.id;
}

Submit::Submit() : _flags(0) {
}

void Submit::set_flags(uint32_t flags) {
    _flags = flags;
}

void Submit::push(uint32_t cmd) {
    _cmdbuf.push_back(cmd);
}

void Submit::add_incr(uint32_t syncpt, int count) {
    drm_tegra_syncpt spt;
    spt.id = syncpt;
    spt.incrs = count;

    _incrs.push_back(spt);
}

void Submit::add_reloc(uint32_t cmdbuf_offset, uint32_t target,
                       uint32_t target_offset, uint32_t shift)
{
    drm_tegra_reloc reloc;
    memset(&reloc, 0, sizeof(reloc));
    reloc.cmdbuf.handle = 0;
    reloc.cmdbuf.offset = cmdbuf_offset;
    reloc.target.handle = target;
    reloc.target.offset = target_offset;
    reloc.shift = shift;

    _relocs.push_back(reloc);
}

drm_tegra_submit Submit::submit(Channel &ch) {
    GemBuffer cmdbuf_bo(ch._drm);
    if (cmdbuf_bo.allocate(_cmdbuf.size() * sizeof(uint32_t)))
        throw ioctl_error("Cmdbuf GEM allocation failed");

    for (auto &reloc : _relocs)
        reloc.cmdbuf.handle = cmdbuf_bo.handle();

    void *cmdbuf_ptr = cmdbuf_bo.map();
    if (!cmdbuf_ptr)
        throw std::runtime_error("Cmdbuf GEM mapping failed");

    memcpy(cmdbuf_ptr, &_cmdbuf[0], _cmdbuf.size() * sizeof(uint32_t));

    drm_tegra_cmdbuf cmdbuf_desc;
    cmdbuf_desc.handle = cmdbuf_bo.handle();
    cmdbuf_desc.offset = 0;
    cmdbuf_desc.words = quirks.force_cmdbuf_words || _cmdbuf.size();

    drm_tegra_submit submit_desc;
    memset(&submit_desc, 0, sizeof(submit_desc));
    submit_desc.context = ch._context;
    submit_desc.num_syncpts = _incrs.size();
    submit_desc.num_cmdbufs = 1;
    submit_desc.num_relocs = _relocs.size();
    submit_desc.syncpts = (uintptr_t)&_incrs[0];
    submit_desc.cmdbufs = (uintptr_t)&cmdbuf_desc;
    submit_desc.relocs = (uintptr_t)&_relocs[0];

    int err = ch._drm.ioctl(DRM_IOCTL_TEGRA_SUBMIT, &submit_desc);
    if (err == -1)
        throw ioctl_error("Submit failed");

    return submit_desc;
}

void wait_syncpoint(DrmDevice &drm, uint32_t id, uint32_t threshold, uint32_t timeout) {
    drm_tegra_syncpt_wait syncpt_wait_args;
    memset(&syncpt_wait_args, 0, sizeof(syncpt_wait_args));
    syncpt_wait_args.id = id;
    syncpt_wait_args.thresh = threshold;
    syncpt_wait_args.timeout = timeout;

    int err = drm.ioctl(DRM_IOCTL_TEGRA_SYNCPT_WAIT, &syncpt_wait_args);
    if (err == -1)
        throw ioctl_error("Syncpoint wait failed");
}

SubmitQuirks::SubmitQuirks()
: force_cmdbuf_words(0)
{ }
