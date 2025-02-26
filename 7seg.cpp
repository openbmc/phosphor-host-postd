/**
 * Copyright 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <lpcsnoop/snoop_listen.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static void DisplayDbusValue(FILE* f, postcode_t postcodes)
{
    const auto& postcode = std::get<0>(postcodes);
    // Uses cstdio instead of streams because the device file has
    // very strict requirements about the data format and streaming
    // abstractions tend to muck it up.
    if (f && !postcode.empty())
    {
        int rc =
            std::fprintf(f, "%d%02x\n", postcode.size() > 1, postcode.back());
        if (rc < 0)
        {
            std::fprintf(stderr, "failed to write 7seg value: rc=%d\n", rc);
        }
        std::fflush(f);
    }
}

/*
 * This is the entry point for the application.
 *
 * This application simply creates an object that registers for incoming value
 * updates for the POST code dbus object.
 */
int main(int argc, const char* argv[])
{
    if (argc != 2 || !fs::exists(argv[1]))
    {
        std::fprintf(stderr, "usage: %s <device_node>\n", argv[0]);
        return -1;
    }

    static bool sig_recv = false;
    FILE* f = std::fopen(argv[1], "r+");

    auto ListenBus = sdbusplus::bus::new_default();
    std::unique_ptr<lpcsnoop::SnoopListen> snoop =
        std::make_unique<lpcsnoop::SnoopListen>(ListenBus, DisplayDbusValue, f);

    signal(SIGINT, [](int signum) {
        if (signum == SIGINT)
        {
            sig_recv = true;
        }
    });

    while (!sig_recv)
    {
        ListenBus.process_discard();
        ListenBus.wait();
    }

    std::fclose(f);
    return 0;
}
