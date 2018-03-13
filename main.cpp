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
#include <poll.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include "xyz/openbmc_project/State/Boot/Raw/server.hpp"

#include "lpcsnoop/snoop.hpp"

template <typename... T>
using ServerObject = typename sdbusplus::server::object::object<T...>;
using PostInterface = sdbusplus::xyz::openbmc_project::State::Boot::server::Raw;
using PostObject = ServerObject<PostInterface>;

class PostReporter : public PostObject
{
  public:
    PostReporter(sdbusplus::bus::bus& bus, const char* objPath, bool defer) :
        PostObject(bus, objPath, defer)
    {
    }
};

/*
 * 256 bytes is a nice amount.  It's improbable we'd need this many, but its
 * gives us leg room in the event the driver poll doesn't return in a timely
 * fashion.  So, mostly arbitrarily chosen.
 */
static constexpr size_t BUFFER_SIZE = 256;

/*
 * Process any incoming dbus inquiries, which include introspection etc.
 */
void ProcessDbus(sdbusplus::bus::bus& bus)
{
    while (true)
    {
        bus.process_discard();
        bus.wait(); // wait indefinitely
    }

    return;
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
    struct pollfd pollset;
    int pollr;
    int readb;
    int postFd = -1;
    uint8_t buffer[BUFFER_SIZE];

    /*
     * These string constants are only used in this method within this object
     * and this object is the only object feeding into the final binary.
     *
     * If however, another object is added to this binary it would be proper
     * to move these declarations to be global and extern to the other object.
     */
    auto snoopObject = SNOOP_OBJECTPATH;
    auto snoopDbus = SNOOP_BUSNAME;
    /*
     * The following string would be promoted if used in another file under the
     * same header.
     */
    auto snoopFilename = "/dev/aspeed-lpc-snoop0";
    bool deferSignals = true;

    postFd = open(snoopFilename, 0);
    if (postFd < 0)
    {
        fprintf(stderr, "Unable to open: %s\n", snoopFilename);
        return -1;
    }

    pollset.fd = postFd;
    pollset.events |= POLLIN;

    auto bus = sdbusplus::bus::new_default();

    // Add systemd object manager.
    sdbusplus::server::manager::manager(bus, snoopObject);

    auto reporter =
        std::make_unique<PostReporter>(bus, snoopObject, deferSignals);
    reporter->emit_object_added();

    bus.request_name(snoopDbus);

    /*
     * I don't see a public interface for getting the underlying sd_bus*
     * so instead of poll(bus, driver), I'll just create a separate thread.
     *
     * TODO(venture): There may be a way to use sdevent to poll both the file
     * and the dbus in the same event loop.  If I could get the sdbus pointer
     * from bus directly, I'd grab a file handler from it, and then just poll on
     * both in one loop.  From a cursory look at sdevent, I should be able to do
     * something similar with that at some point.
     */
    std::thread lt(ProcessDbus, std::ref(bus));

    /* infinitely listen for POST codes and broadcast. */
    while (true)
    {
        pollr = poll(&pollset, 1, -1); /* polls indefinitely. */
        if (pollr < 0)
        {
            /* poll returned error. */
            rc = -errno;
            goto exit;
        }

        if (pollr > 0)
        {
            if (pollset.revents & POLLIN)
            {
                readb = read(postFd, buffer, BUFFER_SIZE);
                if (readb < 0)
                {
                    /* Read failure. */
                    rc = readb;
                    goto exit;
                }
                else
                {
                    /* Broadcast the bytes read. */
                    for (int i = 0; i < readb; i++)
                    {
                        reporter->value(buffer[i]);
                    }
                }
            }
        }
    }

exit:
    if (postFd > -1)
    {
        close(postFd);
    }

    lt.join();

    return rc;
}
