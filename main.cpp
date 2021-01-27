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

#include <endian.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/epoll.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <memory>
#include <sdeventplus/event.hpp>
#include <sdeventplus/source/event.hpp>
#include <sdeventplus/source/io.hpp>
#include <sstream>
#include <thread>

struct snoopChannel
{
    std::string filename;
    int handle;
};

static std::vector<struct snoopChannel> snoopChannels = {
    {"aspeed-lpc-snoop0", -1}};
static size_t codeSize = 1; /* Size of each POST code in bytes */

static void usage(const char* name)
{
    fprintf(stderr,
            "Usage: %s [-d <DEVICE>]\n"
            "  -b, --bytes <SIZE>     set POST code length to <SIZE> bytes. "
            "Default is %zu\n"
            "  -d, --device <DEVICE>  use <DEVICE> file. Default is '%s'\n"
            "  -v, --verbose  Prints verbose information while running\n\n",
            name, codeSize, snoopChannels[0].filename.c_str());
}

static void parse_device_string(const char* devices)
{
    std::string s(devices);
    std::istringstream iss(s);
    std::vector<std::string> v((std::istream_iterator<std::string>(iss)),
                               std::istream_iterator<std::string>());
    snoopChannels.clear();
    for (auto e : v)
    {
        struct snoopChannel p
        {
            "/dev/" + e, -1
        };
        snoopChannels.push_back(p);
    }
}

/*
 * Callback handling IO event from the POST code fd. i.e. there is new
 * POST code available to read.
 */
void PostCodeEventHandler(sdeventplus::source::IO& s, int, uint32_t,
                          PostReporter* reporter, bool verbose)
{
    uint64_t code = 0;
    uint64_t combined_code = 0;
    ssize_t readb;
    bool status = true;

    while (status)
    {
        combined_code = 0;
        // if data is ready on the first snoop channel,
        // we should check all of them
        for (unsigned int i = 0; i < snoopChannels.size(); i++)
        {
            // read depends on old data being cleared
            // since it doesn't always read the full code size
            code = 0;
            readb = read(snoopChannels[i].handle, &code, codeSize);
            if (readb > 0)
            {
                combined_code |= (code << (8 * i));
            }
            else
            {
                status = false;
                break;
            }
        }
        if (!status)
        {
            break;
        }
        combined_code = le64toh(combined_code);

        if (verbose)
        {
            fprintf(stderr, "Code: 0x%" PRIx64 "\n", combined_code);
        }

        // HACK: Always send property changed signal even for the same code
        // since we are single threaded, external users will never see the
        // first value.
        reporter->value(~combined_code, true);
        reporter->value(combined_code);
    }

    if (readb < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
        return;
    }

    /* Read failure. */
    if (readb == 0)
    {
        fprintf(stderr, "Unexpected EOF reading postcode\n");
    }
    else
    {
        fprintf(stderr, "Failed to read postcode: %s\n", strerror(errno));
    }
    s.get_event().exit(1);
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
    bool verbose = false;

    // clang-format off
    static const struct option long_options[] = {
        {"bytes",  required_argument, NULL, 'b'},
        {"device", required_argument, NULL, 'd'},
        {"verbose", no_argument, NULL, 'v'},
        {0, 0, 0, 0}
    };
    // clang-format on

    while ((opt = getopt_long(argc, argv, "b:d:v", long_options, NULL)) != -1)
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
                parse_device_string(optarg);
                break;
            case 'v':
                verbose = true;
                break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if ((codeSize > 1) && (snoopChannels.size() != 1))
    {
        fprintf(stderr, "Error, setting codeSize>1 with several "
                        "active snoopChannels is not supported\n");
        exit(EXIT_FAILURE);
    }

    for (auto& ch : snoopChannels)
    {
        ch.handle = open(ch.filename.c_str(), O_NONBLOCK);
        if (ch.handle < 0)
        {
            fprintf(stderr, "Unable to open: %s\n", ch.filename.c_str());
            return -1;
        }
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
        // Handle POST code if there is data on the first snoop channel
        sdeventplus::source::IO reporterSource(
            event, snoopChannels[0].handle, EPOLLIN | EPOLLET,
            std::bind(PostCodeEventHandler, std::placeholders::_1,
                      std::placeholders::_2, std::placeholders::_3, &reporter,
                      verbose));
        // Enable bus to handle incoming IO and bus events
        bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
        rc = event.loop();
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
    }

    for (auto ch : snoopChannels)
    {
        if (ch.handle > -1)
        {
            close(ch.handle);
        }
    }
    return rc;
}
