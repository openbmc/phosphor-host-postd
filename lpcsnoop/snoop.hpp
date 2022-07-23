#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/State/Boot/Raw/server.hpp>

/* The LPC snoop on port 80h is mapped to this dbus path. */
constexpr char snoopObject[] = "/xyz/openbmc_project/state/boot/raw0";
/* The LPC snoop on port 80h is mapped to this dbus service. */
constexpr char snoopDbus[] = "xyz.openbmc_project.State.Boot.Raw";

template <typename... T>
using ServerObject = typename sdbusplus::server::object_t<T...>;
using PostInterface = sdbusplus::xyz::openbmc_project::State::Boot::server::Raw;
using PostObject = ServerObject<PostInterface>;
using primary_post_code_t = uint64_t;
using secondary_post_code_t = std::vector<uint8_t>;
using postcode_t = std::tuple<primary_post_code_t, secondary_post_code_t>;

class PostReporter : public PostObject
{
  public:
    PostReporter(sdbusplus::bus_t& bus, const char* objPath, bool defer) :
        PostObject(bus, objPath,
                   defer ? PostObject::action::defer_emit
                         : PostObject::action::emit_object_added)
    {
    }
};
