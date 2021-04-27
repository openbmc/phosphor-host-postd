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

#include "ipmisnoop/ipmisnoop.hpp"
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
#include <memory>
#include <sdeventplus/event.hpp>
#include <sdeventplus/source/event.hpp>
#include <sdeventplus/source/io.hpp>
#include <thread>

static size_t codeSize = 1; /* Size of each POST code in bytes */
static int numOfHost = 0;   /* Number of host */

// To handle multi-host postcode
std::vector<std::unique_ptr<IpmiPostReporter>> reporters;

static void usage(const char* name)
{
    fprintf(stderr,
            "Usage: %s [-d <DEVICE>]\n"
            "  -b, --bytes <SIZE>     set POST code length to <SIZE> bytes. "
            "Default is %zu\n"
            "  -d, --device <DEVICE>  use <DEVICE> file.\n"
            "  -h, --host <TOTAL HOST>  . Default is '%d'\n"
            "  -v, --verbose  Prints verbose information while running\n\n",
            name, codeSize, numOfHost);
}

/*
 * Callback handling IO event from the POST code fd. i.e. there is new
 * POST code available to read.
 */
void PostCodeEventHandler(sdeventplus::source::IO& s, int postFd, uint32_t,
                          PostReporter* reporter, bool verbose)
{
    uint64_t code = 0;
    ssize_t readb;
    while ((readb = read(postFd, &code, codeSize)) > 0)
    {
        code = le64toh(code);
        if (verbose)
        {
            fprintf(stderr, "Code: 0x%" PRIx64 "\n", code);
        }
        // HACK: Always send property changed signal even for the same code
        // since we are single threaded, external users will never see the
        // first value.
        reporter->value(std::make_tuple(~code, secondary_post_code_t{}), true);
        reporter->value(std::make_tuple(code, secondary_post_code_t{}));

        // read depends on old data being cleared since it doens't always read
        // the full code size
        code = 0;
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

// handle muti-host D-bus
int postCodeIpmiHandler(const char* snoopObject, const char* snoopDbus)
{
    int ret = 0;

    auto bus = sdbusplus::bus::new_default();

    try
    {

        for (int iteration = 0; iteration < numOfHost; iteration++)
        {
            auto objPathInst =
                std::string{snoopObject} + std::to_string(iteration + 1);

            sdbusplus::server::manager_t m{bus, objPathInst.c_str()};

            /* Create a monitor object and let it do all the rest */
            reporters.push_back(
                std::make_unique<IpmiPostReporter>(bus, objPathInst.c_str()));

            reporters[iteration]->emit_object_added();
        }

        bus.request_name(snoopDbus);
    }

    catch (const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
    }

    // Configure seven segment dsiplay connected to GPIOs as output
#if 1
    ret = configGPIODirOutput();
    if (ret < 0)
    {
        fprintf(stderr, "Failed find the gpio line\n");
    }
#endif
    while (true)
    {
        bus.process_discard();
        bus.wait();
    }
    exit(EXIT_SUCCESS);
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
    bool verbose = false;

    // clang-format off
    static const struct option long_options[] = {
        {"host", required_argument, NULL, 'h'},
        {"bytes",  required_argument, NULL, 'b'},
        {"device", optional_argument, NULL, 'd'},
        {"verbose", no_argument, NULL, 'v'},
        {0, 0, 0, 0}
    };
    // clang-format on

    while ((opt = getopt_long(argc, argv, "h:b:d:v", long_options, NULL)) != -1)
    {
        switch (opt)
        {
            case 0:
                break;
            case 'h':
                numOfHost = atoi(optarg);
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

 		fprintf(stderr, "numOfHost %d\n", numOfHost);

                if (numOfHost < 0)
                {
                  fprintf(stderr, "numOfHost %d\n", numOfHost);
                 
                }

                else
                {
                postFd = open(optarg, O_NONBLOCK);
                if (postFd < 0)
                {
                    fprintf(stderr, "Unable to open: %s\n", optarg);
                    return -1;
                }
                }
                break;
            case 'v':
                verbose = true;
                break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    auto bus = sdbusplus::bus::new_default();

    if (numOfHost < 0)
    {

   fprintf(stderr, "%d\n", numOfHost);

      // Add systemd object manager.
    sdbusplus::server::manager::manager(bus, snoopObject);

    PostReporter reporter(bus, snoopObject, deferSignals);
    reporter.emit_object_added();
    bus.request_name(snoopDbus);

    // Create sdevent and add IO source
    try
    {
        sdeventplus::Event event = sdeventplus::Event::get_default();
        if (postFd > 0)
        {

            sdeventplus::source::IO reporterSource(
                event, postFd, EPOLLIN | EPOLLET,
                std::bind(PostCodeEventHandler, std::placeholders::_1,
                          std::placeholders::_2, std::placeholders::_3,
                          &reporter, verbose));
        }
        // Enable bus to handle incoming IO and bus events
        bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
        rc = event.loop();
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "numOfHost:%s\n", e.what());
    }

    if (postFd > -1)
    {
        close(postFd);
    }

    return rc;
    }
    else
    {

         const char* ipmiSnoopObject = IPMI_SNOOP_OBJECTPATH;
        fprintf(stderr, "numOfHost:%d\n", numOfHost);
        postCodeIpmiHandler(ipmiSnoopObject, snoopDbus);
    }
}
