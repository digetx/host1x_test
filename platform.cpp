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

#include "platform.h"

#include <cstdio>
#include <cstring>

#include "host1x.h"

Platform::Platform()
{
}

bool Platform::initialize() {
    FILE *fp = fopen("/sys/firmware/devicetree/base/compatible", "r");
    if (!fp)
        return false;

    char buf[257] = {0};
    size_t len = fread(buf, 1, 256, fp);
    fclose(fp);
    if (len == 0)
        return false;

    char *next = buf;
    while (next < buf+len) {
        if (!strcmp(next, "nvidia,tegra20")) {
            _soc = Tegra20;
            return true;
        }
        if (!strcmp(next, "nvidia,tegra30")) {
            _soc = Tegra30;
            return true;
        }
        if (!strcmp(next, "nvidia,tegra114")) {
            _soc = Tegra114;
            return true;
        }
        if (!strcmp(next, "nvidia,tegra124")) {
            _soc = Tegra124;
            return true;
        }
        if (!strcmp(next, "nvidia,tegra210")) {
            _soc = Tegra210;
            return true;
        }
        if (!strcmp(next, "nvidia,tegra186")) {
            _soc = Tegra186;
            return true;
        }
        next += strlen(next)+1;
    }
}

uint32_t Platform::incrementSyncpointOp(uint32_t syncpoint) const
{
    switch (_soc) {
    case Tegra20:
    case Tegra30:
    case Tegra114:
    case Tegra124:
    case Tegra210:
        return syncpoint | (1 << 8);
    case Tegra186:
        return syncpoint | (1 << 10);
    }
}

uint32_t Platform::defaultClass() const
{
    switch (_soc) {
    case Tegra20:
    case Tegra30:
    case Tegra114:
        return HOST1X_CLASS_GR2D;
    case Tegra124:
    case Tegra210:
    case Tegra186:
        return HOST1X_CLASS_VIC;
    }
}
