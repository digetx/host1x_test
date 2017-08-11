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

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <cerrno>

#include <poll.h>

#include "gem.h"
#include "host1x.h"
#include "util.h"
#include "platform.h"

#include <libdrm/tegra_drm.h>

Platform platform;

void test_submit_wait() {
    DrmDevice drm;
    Channel ch(drm);

    uint32_t syncpt = ch.syncpoint(0);

    Submit submit;
    submit.push(host1x_opcode_nonincr(0, 1));
    submit.push(platform.incrementSyncpointOp(syncpt));

    submit.add_incr(syncpt, 1);

    auto result = submit.submit(ch);

    wait_syncpoint(drm, syncpt, result.fence, 1000);
}

void test_submit_timeout() {
    DrmDevice drm;
    Channel ch(drm);

    uint32_t syncpt = ch.syncpoint(0);

    Submit submit;
    submit.push(host1x_opcode_nonincr(0, 1));
    submit.push(platform.incrementSyncpointOp(syncpt));

    submit.add_incr(syncpt, 2);

    auto result = submit.submit(ch);

    try {
        wait_syncpoint(drm, syncpt, result.fence, 100);
    }
    catch (...) {
        /* Wait for jobs timeout to avoid further tests failures */
        wait_syncpoint(drm, syncpt, result.fence, DRM_TEGRA_NO_TIMEOUT);
        return;
    }

    throw std::runtime_error("Syncpoint wait did not timeout");
}

void test_invalid_cmdbuf() {
    DrmDevice drm;
    Channel ch(drm);

    uint32_t syncpt = ch.syncpoint(0);

    /* Command buffer larger than BO */
    {
        Submit submit;
        submit.push(host1x_opcode_nonincr(0, 1));
        submit.push(platform.incrementSyncpointOp(syncpt));

        submit.add_incr(syncpt, 1);

        submit.quirks.force_cmdbuf_words = 10000;

        try {
            submit.submit(ch);
        }
        catch (...) {
            goto test_2;
        }
    }

    throw std::runtime_error("Submit 1 did not return error");

    /* Command buffer with unaligned offset */
test_2:
    {
        Submit submit;
        submit.push(host1x_opcode_nonincr(0, 1));
        submit.push(platform.incrementSyncpointOp(syncpt));

        submit.add_incr(syncpt, 1);

        submit.quirks.force_cmdbuf_offset = 1;

        try {
            submit.submit(ch);
        }
        catch (...) {
            goto done;
        }
    }

    throw std::runtime_error("Submit 2 did not return error");

done:
    return;
}

void test_invalid_reloc() {
    DrmDevice drm;
    Channel ch(drm);

    GemBuffer target_bo(drm);
    if (target_bo.allocate(128))
        throw std::runtime_error("Allocation failed");

    uint32_t syncpt = ch.syncpoint(0);

    /* Reloc with offset larger than BO */
    {
        Submit submit;
        submit.push(host1x_opcode_nonincr(0, 1));
        submit.push(platform.incrementSyncpointOp(syncpt));

        submit.add_incr(syncpt, 1);

        submit.add_reloc(8192, target_bo.handle(), 0, 0);

        try {
            submit.submit(ch);
        }
        catch (...) {
            goto test_2;
        }
    }

    throw std::runtime_error("Submit 1 did not return error");

    /* Reloc with unaligned offset */
test_2:
    {
        Submit submit;
        submit.push(host1x_opcode_nonincr(0, 1));
        submit.push(platform.incrementSyncpointOp(syncpt));

        submit.add_incr(syncpt, 1);

        submit.add_reloc(1, target_bo.handle(), 0, 0);

        try {
            submit.submit(ch);
        }
        catch (...) {
            goto test_3;
        }
    }

    throw std::runtime_error("Submit 2 did not return error");

    /* Reloc with target offset larger than target BO */
test_3:
    {
        Submit submit;
        submit.push(host1x_opcode_nonincr(0, 1));
        submit.push(platform.incrementSyncpointOp(syncpt));

        submit.add_incr(syncpt, 1);

        submit.add_reloc(0, target_bo.handle(), 8192, 0);

        try {
            submit.submit(ch);
        }
        catch (...) {
            goto done;
        }
    }

    throw std::runtime_error("Submit 3 did not return error");

done:
    return;
}

int main(int argc, char **argv) {
    fprintf(stderr, "host1x_test - Linux host1x driver test suite\n");

    if (platform.initialize()) {
        const char *name;
        switch (platform.soc()) {
            case Platform::Tegra20:
                name = "Tegra20 [Tegra 2]";
                break;
            case Platform::Tegra30:
                name = "Tegra30 [Tegra 3]";
                break;
            case Platform::Tegra114:
                name = "Tegra114 [Tegra 4]";
                break;
            case Platform::Tegra124:
                name = "Tegra124 [Tegra K1]";
                break;
            case Platform::Tegra210:
                name = "Tegra210 [Tegra X1]";
                break;
            case Platform::Tegra186:
                name = "Tegra186 [Tegra X2]";
                break;
        }

        fprintf(stderr, "Platform: %s\n", name);
    } else {
        fprintf(stderr, "Failed to detect platform, defaulting to Tegra210\n");
        platform.setSoc(Platform::Tegra210);
    }

    struct TestCase {
        const char *name;
        void (*func)();
    };
    std::vector<TestCase> tests;

#define PUSH_TEST(name) tests.push_back({ #name, name })
    PUSH_TEST(test_submit_wait);
    PUSH_TEST(test_submit_timeout);
    PUSH_TEST(test_invalid_cmdbuf);
    PUSH_TEST(test_invalid_reloc);

    for (const auto &test : tests) {
        fprintf(stderr, "- %-40s ", test.name);
        try {
            (test.func)();
            fprintf(stderr, "PASSED\n");
        }
        catch (ioctl_error e) {
            fprintf(stderr, "FAILED\n");
            fprintf(stderr, "  Reason: %s\n", e.what());
            fprintf(stderr, "  IOCTL error: %d (%s)\n", e.error, strerror(e.error));
        }
        catch (std::runtime_error e) {
            fprintf(stderr, "FAILED\n");
            fprintf(stderr, "  Reason: %s\n", e.what());
        }
    }

    return 0;
}
