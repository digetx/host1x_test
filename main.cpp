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
#include <ctime>
#include <vector>
#include <stdexcept>
#include <cerrno>

#include <poll.h>
#include <sched.h>

#include "gem.h"
#include "host1x.h"
#include "util.h"
#include "platform.h"

#include <libdrm/tegra_drm.h>

Platform platform;

void test_submit_wait(std::string& message) {
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

void test_submit_timeout(std::string& message) {
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

void test_invalid_cmdbuf(std::string& message) {
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

void test_invalid_reloc(std::string& message) {
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

float submit_performance_test(std::string& message, unsigned num_batches,
                              unsigned num_submits, unsigned num_relocs)
{
    DrmDevice drm;
    Channel ch(drm);
    uint32_t syncpt = ch.syncpoint(0);
    unsigned i = 0, k;

    std::vector<GemBuffer*> relocs(num_relocs);

    for (auto &bo : relocs) {
        bo = new GemBuffer(drm);

        if (bo->allocate(4096))
            throw std::runtime_error("Allocation failed");
    }

    Submit submit;
    for (auto &bo : relocs) {
        submit.push(host1x_opcode_nonincr(0x2b, 1));
        submit.push(0xdeadbeef);
    }
    submit.push(host1x_opcode_nonincr(0, 1));
    submit.push(platform.incrementSyncpointOp(syncpt));

    submit.add_incr(syncpt, 1);

    for (auto &bo : relocs)
        submit.add_reloc(i++ * 8 + 4, bo->handle(), 0, 0);

    clock_t clocks = 0;

    for (i = 0; i < num_batches; i++) {
        drm_tegra_submit result;
        clock_t begin = clock();

        for (k = 0; k < num_submits; k++)
            result = submit.submit(ch);

        clocks += clock() - begin;
        wait_syncpoint(drm, syncpt, result.fence, DRM_TEGRA_NO_TIMEOUT);
    }

    for (auto &bo : relocs)
        delete bo;

    char buffer[256];
    float elapsed = double(clocks) / CLOCKS_PER_SEC;

    sprintf(buffer, "perf: %3u batches of %3u submits of %3u "
                    "relocations took %f sec per batch on average, "
                    "one submit takes %f us\n",
            i, k, relocs.size(),
            elapsed / i, elapsed / i / k * 1000000);

    message += buffer;

    return elapsed;
}

void test_submit_performance(std::string& message) {
    std::string path = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
    std::string governor;

    /*
     * Bind process to CPU0 and change its freq governor to reduce
     * performance jitter.
     */
    try {
        governor = read_file(path);
        write_file(path, "performance");

        if (read_file(path).compare("performance") < 0)
            governor.clear();
    }
    catch (...) {
        governor.clear();
    }

    if (governor.empty())
        message += "perf: CPU frequency scaling governor change failed!\n";

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);

    if (sched_setaffinity(0, sizeof(mask), &mask) != 0)
         message += "perf: Binding to CPU0 failed!\n";

    float time = 0;

    for (unsigned i = 0; i < 22; i += 3) {
        time += submit_performance_test(message, 50,  10, i);
        time += submit_performance_test(message, 30,  50, i);
        time += submit_performance_test(message, 10, 255, i);
    }

    message += "perf: spent " + std::to_string(time) + " sec in total\n";

    /* Restore original governor */
    if (!governor.empty())
        write_file(path, governor);
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
        void (*func)(std::string& message);
    };
    std::vector<TestCase> tests;

#define PUSH_TEST(name) tests.push_back({ #name, name })
    PUSH_TEST(test_submit_wait);
    PUSH_TEST(test_submit_timeout);
    PUSH_TEST(test_invalid_cmdbuf);
    PUSH_TEST(test_invalid_reloc);
    PUSH_TEST(test_submit_performance);

    for (const auto &test : tests) {
        fprintf(stderr, "- %-40s ", test.name);
        try {
            std::string message;
            (test.func)(message);
            fprintf(stderr, "PASSED\n%s", message.c_str());
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
