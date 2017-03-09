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

#include "tegra_drm.h"

void test_submit_wait() {
    DrmDevice drm;
    Channel ch(drm);

    uint32_t syncpt = ch.syncpoint(0);

    Submit submit;
    submit.push(host1x_opcode_nonincr(0, 1));
    submit.push(syncpt | (1 << 8));

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
    submit.push(syncpt | (1 << 8));

    submit.add_incr(syncpt, 2);

    auto result = submit.submit(ch);

    try {
        wait_syncpoint(drm, syncpt, result.fence, 100);
    }
    catch (...) {
        return;
    }

    throw std::runtime_error("Syncpoint wait did not timeout");
}

void test_submit_sync_postfence() {
    DrmDevice drm;
    Channel ch(drm);

    uint32_t syncpt = ch.syncpoint(0);

    Submit submit;
    submit.push(host1x_opcode_nonincr(0, 1));
    submit.push(syncpt | (1 << 8));

    submit.add_incr(syncpt, 1);
    submit.set_flags(DRM_TEGRA_SUBMIT_CREATE_FENCE_FD);

    auto result = submit.submit(ch);

    struct pollfd pfd;
    pfd.events = POLLIN;
    pfd.fd = result.fence;

    int err = poll(&pfd, 1, 100);
    if (err != 1 || !(pfd.revents & POLLIN))
        throw std::runtime_error("Fence FD poll failed or timed out");
}

void test_submit_postfence_timeout() {
    DrmDevice drm;
    Channel ch(drm);

    uint32_t syncpt = ch.syncpoint(0);

    Submit submit;
    submit.push(host1x_opcode_nonincr(0, 1));
    submit.push(syncpt | (1 << 8));

    submit.add_incr(syncpt, 2);
    submit.set_flags(DRM_TEGRA_SUBMIT_CREATE_FENCE_FD);

    auto result = submit.submit(ch);

    struct pollfd pfd;
    pfd.events = POLLIN;
    pfd.fd = result.fence;

    int err = poll(&pfd, 1, 1000);
    if (err == 1 && pfd.revents & POLLIN)
        throw std::runtime_error("Fence FD poll returned without timeout");
    else if (err != 0)
        throw std::runtime_error("Fence FD poll failed");
}

int main(int argc, char **argv) {
    fprintf(stderr, "host1x_test - Linux host1x driver test suite\n");

    struct TestCase {
        const char *name;
        void (*func)();
    };
    std::vector<TestCase> tests;

#define PUSH_TEST(name) tests.push_back({ #name, name })
    PUSH_TEST(test_submit_wait);
    PUSH_TEST(test_submit_sync_postfence);
    PUSH_TEST(test_submit_timeout);
    PUSH_TEST(test_submit_postfence_timeout);

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
