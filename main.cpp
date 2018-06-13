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

#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sys/epoll.h>
#include <systemd/sd-event.h>
#include <thread>

#include "lpcsnoop/snoop.hpp"

static const char* snoopFilename = "/dev/aspeed-lpc-snoop0";

/*
 * 256 bytes is a nice amount.  It's improbable we'd need this many, but its
 * gives us leg room in the event the driver poll doesn't return in a timely
 * fashion.  So, mostly arbitrarily chosen.
 */
static constexpr size_t BUFFER_SIZE = 256;

/*
 * Callback handling IO event from the POST code fd. i.e. there is new
 * POST code available to read.
 */
int PostCodeEventHandler(sd_event_source* s, int postFd, uint32_t revents,
                         void* userdata)
{
    PostReporter* reporter = static_cast<PostReporter*>(userdata);
    std::array<uint8_t, BUFFER_SIZE> buffer;
    int readb;

    // TODO(kunyi): more error handling for EPOLLPRI/EPOLLERR.
    if (revents & EPOLLIN)
    {
        readb = read(postFd, buffer.data(), buffer.size());
        if (readb < 0)
        {
            /* Read failure. */
            return readb;
        }

        /* Broadcast the bytes read. */
        for (int i = 0; i < readb; i++)
        {
            reporter->value(buffer[i]);
        }
    }

    return 0;
}

static void usage(const char* name)
{
    fprintf(stderr,
            "Usage: %s [-d <DEVICE>]\n"
            "  -d, --device <DEVICE>  use <DEVICE> file. Default is '%s'\n\n",
            name, snoopFilename);
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
    sd_event* event = NULL;

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

    static const struct option long_options[] = {
        {"device", required_argument, NULL, 'd'}, {0, 0, 0, 0}};

    while ((opt = getopt_long(argc, argv, "d:", long_options, NULL)) != -1)
    {
        switch (opt)
        {
            case 0:
                break;
            case 'd':
                snoopFilename = optarg;
                break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    postFd = open(snoopFilename, 0);
    if (postFd < 0)
    {
        fprintf(stderr, "Unable to open: %s\n", snoopFilename);
        return -1;
    }

    auto bus = sdbusplus::bus::new_default();

    // Add systemd object manager.
    sdbusplus::server::manager::manager(bus, snoopObject);

    PostReporter reporter(bus, snoopObject, deferSignals);

    // Create sdevent and add IO source
    // TODO(kunyi): the current interface is really C-style. Move to a C++
    // wrapper when there is a SdEventPlus or some sort of that is ready.
    rc = sd_event_default(&event);

    if (rc < 0)
    {
        fprintf(stderr, "Failed to allocate event loop:%s\n", strerror(-rc));
        goto finish;
    }

    sd_event_source* source;
    rc = sd_event_add_io(event, &source, postFd, EPOLLIN, PostCodeEventHandler,
                         &reporter);
    if (rc < 0)
    {
        fprintf(stderr, "Failed to add sdevent io source:%s\n", strerror(-rc));
        goto finish;
    }

    // Enable bus to handle incoming IO and bus events
    reporter.emit_object_added();
    bus.request_name(snoopDbus);
    bus.attach_event(event, SD_EVENT_PRIORITY_NORMAL);

    rc = sd_event_loop(event);

finish:
    if (postFd > -1)
    {
        close(postFd);
    }

    return rc;
}
