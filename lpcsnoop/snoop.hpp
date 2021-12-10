#pragma once

#include <chrono>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/State/Boot/Raw/server.hpp>

/* The LPC snoop on port 80h is mapped to this dbus path. */
#define SNOOP_OBJECTPATH "/xyz/openbmc_project/state/boot/raw0"
/* The LPC snoop on port 80h is mapped to this dbus service. */
#define SNOOP_BUSNAME "xyz.openbmc_project.State.Boot.Raw"

template <typename... T>
using ServerObject = typename sdbusplus::server::object::object<T...>;
using PostInterface = sdbusplus::xyz::openbmc_project::State::Boot::server::Raw;
using PostObject = ServerObject<PostInterface>;
using primary_post_code_t = uint64_t;
using secondary_post_code_t = std::vector<uint8_t>;
using postcode_t = std::tuple<primary_post_code_t, secondary_post_code_t>;

class PostReporter : public PostObject
{
  public:
    PostReporter(sdbusplus::bus::bus& bus, const char* objPath, bool defer) :
        PostObject(bus, objPath, defer), count(0)
    {
    }

  private:
    uint64_t count;
    system_clock::time_point currentTime;
};
