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

#include "lpcsnoop/snoop.hpp"

#include <fcntl.h>
#include <getopt.h>
#include <sys/epoll.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <sdeventplus/event.hpp>
#include <sdeventplus/source/event.hpp>
#include <sdeventplus/source/io.hpp>
#include <thread>

static const char* snoopFilename = "/dev/aspeed-lpc-snoop0";
static size_t codeSize = 1; /* Size of each POST code in bytes */

/*
 * 256 bytes is a nice amount.  It's improbable we'd need this many, but its
 * gives us leg room in the event the driver poll doesn't return in a timely
 * fashion.  So, mostly arbitrarily chosen.
 */
static constexpr size_t BUFFER_SIZE = 256;

static void usage(const char* name)
{
    fprintf(stderr,
            "Usage: %s [-d <DEVICE>]\n"
            "  -b, --bytes <SIZE>     set POST code length to <SIZE> bytes. "
            "Default is %zu\n"
            "  -d, --device <DEVICE>  use <DEVICE> file. Default is '%s'\n\n",
            name, codeSize, snoopFilename);
}

static uint64_t assembleBytes(std::array<uint8_t, BUFFER_SIZE> buf, int start,
                              int size)
{
    uint64_t result = 0;

    for (int i = start + size - 1; i >= start; i--)
    {
        result <<= 8;
        result |= buf[i];
    }

    return result;
}

/*
 * Callback handling IO event from the POST code fd. i.e. there is new
 * POST code available to read.
 */
void PostCodeEventHandler(sdeventplus::source::IO& s, int postFd,
                          uint32_t revents, PostReporter* reporter)
{
    std::array<uint8_t, BUFFER_SIZE> buffer;
    int readb;

    readb = read(postFd, buffer.data(), buffer.size());
    if (readb < 0)
    {
        /* Read failure. */
        s.get_event().exit(1);
        return;
    }

    if (readb % codeSize != 0)
    {
        fprintf(stderr,
                "Warning: read size %d not a multiple of "
                "POST code length %zu. Some codes may be "
                "corrupt or missing\n",
                readb, codeSize);
        readb -= (readb % codeSize);
    }

    /* Broadcast the values read. */
    for (int i = 0; i < readb; i += codeSize)
    {
        reporter->value(assembleBytes(buffer, i, codeSize));
    }
}

/*
 * TODO(venture): this only listens one of the possible snoop ports, but
 * doesn't share the namespace.
 *
 * This polls() the lpc snoop character device and it owns the dbus object
 * whose value is the latest port 80h value.
 */
int main(int argc, char* argv[])
{
    int rc = 0;
    int opt;
    int postFd = -1;

    /*
     * These string constants are only used in this method within this object
     * and this object is the only object feeding into the final binary.
     *
     * If however, another object is added to this binary it would be proper
     * to move these declarations to be global and extern to the other object.
     */
    const char* snoopObject = SNOOP_OBJECTPATH;
    const char* snoopDbus = SNOOP_BUSNAME;

    bool deferSignals = true;

    // clang-format off
    static const struct option long_options[] = {
        {"bytes",  required_argument, NULL, 'b'},
        {"device", required_argument, NULL, 'd'},
        {0, 0, 0, 0}
    };
    // clang-format on

    while ((opt = getopt_long(argc, argv, "b:d:", long_options, NULL)) != -1)
    {
        switch (opt)
        {
            case 0:
                break;
            case 'b':
                codeSize = atoi(optarg);

                if (codeSize < 1 || codeSize > 8)
                {
                    fprintf(stderr,
                            "Invalid POST code size '%s'. Must be "
                            "an integer from 1 to 8.\n",
                            optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'd':
                snoopFilename = optarg;
                break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    postFd = open(snoopFilename, O_NONBLOCK);
    if (postFd < 0)
    {
        fprintf(stderr, "Unable to open: %s\n", snoopFilename);
        return -1;
    }

    auto bus = sdbusplus::bus::new_default();

    // Add systemd object manager.
    sdbusplus::server::manager::manager(bus, snoopObject);

    PostReporter reporter(bus, snoopObject, deferSignals);
    reporter.emit_object_added();
    bus.request_name(snoopDbus);

    // Create sdevent and add IO source
    try
    {
        sdeventplus::Event event = sdeventplus::Event::get_default();
        sdeventplus::source::IO reporterSource(
            event, postFd, EPOLLIN,
            std::bind(PostCodeEventHandler, std::placeholders::_1,
                      std::placeholders::_2, std::placeholders::_3, &reporter));
        // Enable bus to handle incoming IO and bus events
        bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
        rc = event.loop();
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
    }

    if (postFd > -1)
    {
        close(postFd);
    }

    return rc;
}
