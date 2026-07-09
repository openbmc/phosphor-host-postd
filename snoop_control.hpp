#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>
#include <sdeventplus/source/io.hpp>
#include <xyz/openbmc_project/State/Boot/Progress/common.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <variant>

/* Watches BootProgress on the host state object and disables the LPC snoop IO
 * source once PostComplete is reached. */
class SnoopControl
{
    using Progress =
        sdbusplus::common::xyz::openbmc_project::state::boot::Progress;

  public:
    SnoopControl(sdbusplus::bus_t& busIn,
                 std::optional<sdeventplus::source::IO>& ioSourceIn) :
        bus(busIn), ioSource(ioSourceIn),
        signal(busIn,
               sdbusplus::bus::match::rules::propertiesChanged(
                   hostStatePath, Progress::interface),
               std::bind(onBootProgressChanged, std::ref(ioSourceIn),
                         std::placeholders::_1))
    {
        if (!queryCurrentState())
        {
            nameWatch.emplace(
                busIn,
                sdbusplus::bus::match::rules::nameOwnerChanged(
                    stateHostService),
                [this](sdbusplus::message_t& msg) { onNameOwnerChanged(msg); });
        }
    }

    SnoopControl() = delete;
    ~SnoopControl() = default;
    SnoopControl(const SnoopControl&) = delete;
    SnoopControl& operator=(const SnoopControl&) = delete;
    SnoopControl(SnoopControl&&) = delete;
    SnoopControl& operator=(SnoopControl&&) = delete;

  private:
    static constexpr auto stateHostService = "xyz.openbmc_project.State.Host";
    static constexpr auto hostStatePath = "/xyz/openbmc_project/state/host0";
    static constexpr auto bootProgressProperty = "BootProgress";

    sdbusplus::bus_t& bus;
    std::optional<sdeventplus::source::IO>& ioSource;
    sdbusplus::bus::match_t signal;
    std::optional<sdbusplus::bus::match_t> nameWatch;

    static void setCapture(std::optional<sdeventplus::source::IO>& ioSource,
                           bool enabled)
    {
        if (ioSource)
            ioSource->set_enabled(enabled ? sdeventplus::source::Enabled::On
                                          : sdeventplus::source::Enabled::Off);
    }

    bool queryCurrentState()
    {
        try
        {
            auto m =
                bus.new_method_call(stateHostService, hostStatePath,
                                    "org.freedesktop.DBus.Properties", "Get");
            m.append(Progress::interface, bootProgressProperty);
            std::variant<std::string> val;
            bus.call(m).read(val);
            auto stage = Progress::convertStringToProgressStages(
                std::get<std::string>(val));
            if (stage && *stage == Progress::ProgressStages::PostComplete)
                setCapture(ioSource, false);
            return true;
        }
        catch (const sdbusplus::exception_t&)
        {
            return false;
        }
    }

    void onNameOwnerChanged(sdbusplus::message_t& msg)
    {
        std::string name, oldOwner, newOwner;
        msg.read(name, oldOwner, newOwner);
        if (newOwner.empty())
            return;
    }
    queryCurrentState();

}

static void
    onBootProgressChanged(std::optional<sdeventplus::source::IO>& ioSource,
                          sdbusplus::message_t& msg)
{
    std::string statusInterface;
    std::map<std::string, std::variant<std::string, uint64_t>> msgData;
    msg.read(statusInterface, msgData);

    auto propertyMap = msgData.find(bootProgressProperty);
    if (propertyMap != msgData.end())
    {
        auto stage = Progress::convertStringToProgressStages(
            std::get<std::string>(propertyMap->second));
        if (stage)
            setCapture(ioSource,
                       *stage != Progress::ProgressStages::PostComplete);
    }
}
}
