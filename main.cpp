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

#ifdef ENABLE_IPMI_SNOOP
#include "ipmisnoop/ipmisnoop.hpp"
#endif
#include "lpcsnoop/snoop.hpp"

#include <endian.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/epoll.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#include <sdeventplus/event.hpp>
#include <sdeventplus/source/event.hpp>
#include <sdeventplus/source/io.hpp>
#include <sdeventplus/source/signal.hpp>
#include <sdeventplus/source/time.hpp>
#include <sdeventplus/utility/sdbus.hpp>
#include <stdplus/signal.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <thread>

static size_t codeSize = 1; /* Size of each POST code in bytes */
static bool verbose = false;

static void usage(const char* name)
{
    fprintf(stderr,
            "Usage: %s\n"
#ifdef ENABLE_IPMI_SNOOP
            "  -h, --host <host instances>  Default is '0'\n"
#else
            "  -d, --device <DEVICE>  use <DEVICE> file.\n"
            "  -r, --rate-limit=<N>   Only process N POST codes from the "
            "device per second.\n"
            "  -b, --bytes <SIZE>     set POST code length to <SIZE> bytes. "
            "Default is 1\n"
#endif
            "  -v, --verbose  Prints verbose information while running\n\n",
            name);
}

/**
 * Call once for each POST code received. If the number of POST codes exceeds
 * the configured rate limit, this function will disable the snoop device IO
 * source until the end of the 1 second interval, then re-enable it.
 *
 * @return Whether the rate limit is exceeded.
 */
bool rateLimit(PostReporter& reporter, sdeventplus::source::IO& ioSource)
{
    if (reporter.rateLimit == 0)
    {
        // Rate limiting is disabled.
        return false;
    }

    using Clock = sdeventplus::Clock<sdeventplus::ClockId::Monotonic>;

    static constexpr std::chrono::seconds rateLimitInterval(1);
    static unsigned int rateLimitCount = 0;
    static Clock::time_point rateLimitEndTime;

    const sdeventplus::Event& event = ioSource.get_event();

    if (rateLimitCount == 0)
    {
        // Initialize the end time when we start a new interval
        rateLimitEndTime = Clock(event).now() + rateLimitInterval;
    }

    if (++rateLimitCount < reporter.rateLimit)
    {
        return false;
    }

    rateLimitCount = 0;

    if (rateLimitEndTime < Clock(event).now())
    {
        return false;
    }

    if (verbose)
    {
        fprintf(stderr, "Hit POST code rate limit - disabling temporarily\n");
    }

    ioSource.set_enabled(sdeventplus::source::Enabled::Off);
    sdeventplus::source::Time<sdeventplus::ClockId::Monotonic>(
        event, rateLimitEndTime, std::chrono::milliseconds(100),
        [&ioSource](auto&, auto) {
        if (verbose)
        {
            fprintf(stderr, "Reenabling POST code handler\n");
        }
        ioSource.set_enabled(sdeventplus::source::Enabled::On);
        })
        .set_floating(true);
    return true;
}

/*
 * Callback handling IO event from the POST code fd. i.e. there is new
 * POST code available to read.
 */
void PostCodeEventHandler(PostReporter* reporter, sdeventplus::source::IO& s,
                          int postFd, uint32_t)
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

        if (rateLimit(*reporter, s))
        {
            return;
        }
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
    int postFd = -1;
    unsigned int rateLimit = 0;

    int opt;

    std::vector<std::string> host;

    // clang-format off
    static const struct option long_options[] = {
#ifdef ENABLE_IPMI_SNOOP
        {"host", optional_argument, NULL, 'h'},
#else
        {"device", optional_argument, NULL, 'd'},
        {"rate-limit", optional_argument, NULL, 'r'},
        {"bytes",  required_argument, NULL, 'b'},
#endif
        {"verbose", no_argument, NULL, 'v'},
        {0, 0, 0, 0}
    };
    // clang-format on

    constexpr const char* optstring =
#ifdef ENABLE_IPMI_SNOOP
        "h:"
#else
        "d:r:b:"
#endif
        "v";

    while ((opt = getopt_long(argc, argv, optstring, long_options, NULL)) != -1)
    {
        switch (opt)
        {
            case 0:
                break;
            case 'h':
            {
                std::string_view instances = optarg;
                size_t pos = 0;

                while ((pos = instances.find(" ")) != std::string::npos)
                {
                    host.emplace_back(instances.substr(0, pos));
                    instances.remove_prefix(pos + 1);
                }
                host.emplace_back(instances);
                break;
            }
            case 'b':
            {
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
            }
            case 'd':

                postFd = open(optarg, O_NONBLOCK);
                if (postFd < 0)
                {
                    fprintf(stderr, "Unable to open: %s\n", optarg);
                    return -1;
                }
                break;
            case 'r':
            {
                int argVal = -1;
                try
                {
                    argVal = std::stoi(optarg);
                }
                catch (...)
                {}

                if (argVal < 1)
                {
                    fprintf(stderr, "Invalid rate limit '%s'. Must be >= 1.\n",
                            optarg);
                    return EXIT_FAILURE;
                }

                rateLimit = static_cast<unsigned int>(argVal);
                fprintf(stderr, "Rate limiting to %d POST codes per second.\n",
                        argVal);
                break;
            }
            case 'v':
                verbose = true;
                break;
            default:
                usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    auto bus = sdbusplus::bus::new_default();

#ifdef ENABLE_IPMI_SNOOP
    std::cout << "Verbose = " << verbose << std::endl;
    int ret = postCodeIpmiHandler(ipmiSnoopObject, snoopDbus, bus, host);
    if (ret < 0)
    {
        fprintf(stderr, "Error in postCodeIpmiHandler\n");
        return ret;
    }
    return 0;
#endif

    bool deferSignals = true;

    // Add systemd object manager.
    sdbusplus::server::manager_t snoopdManager(bus, snoopObject);

    PostReporter reporter(bus, snoopObject, deferSignals);
    reporter.emit_object_added();
    bus.request_name(snoopDbus);

    // Create sdevent and add IO source
    try
    {
        sdeventplus::Event event = sdeventplus::Event::get_default();
        std::optional<sdeventplus::source::IO> reporterSource;
        if (postFd > 0)
        {
            reporter.rateLimit = rateLimit;
            reporterSource.emplace(
                event, postFd, EPOLLIN,
                std::bind_front(PostCodeEventHandler, &reporter));
        }
        // Enable bus to handle incoming IO and bus events
        auto intCb = [](sdeventplus::source::Signal& source,
                        const struct signalfd_siginfo*) {
            source.get_event().exit(0);
        };
        stdplus::signal::block(SIGINT);
        sdeventplus::source::Signal(event, SIGINT, intCb).set_floating(true);
        stdplus::signal::block(SIGTERM);
        sdeventplus::source::Signal(event, SIGTERM, std::move(intCb))
            .set_floating(true);
        return sdeventplus::utility::loopWithBus(event, bus);
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
    }

    if (postFd > -1)
    {
        close(postFd);
    }

    return 0;
}
