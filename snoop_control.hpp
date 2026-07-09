#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>
#include <sdeventplus/source/io.hpp>
#include <xyz/openbmc_project/State/Boot/Progress/common.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

/* Watches BootProgress on the host state object and disables the LPC snoop IO
 * source once PostComplete is reached. */
class SnoopControl
{
    using Progress =
        sdbusplus::common::xyz::openbmc_project::state::boot::Progress;
    using ProgressStages = Progress::ProgressStages;

  public:
    SnoopControl(sdbusplus::bus_t& busIn,
                 std::optional<sdeventplus::source::IO>& ioSourceIn) :
        bus(busIn), ioSource(ioSourceIn),
        propertiesWatch(
            busIn,
            sdbusplus::bus::match::rules::propertiesChanged(
                hostStatePath, Progress::interface),
            [this](sdbusplus::message_t& msg) { onPropertiesChanged(msg); }),
        nameOwnerWatch(
            busIn,
            sdbusplus::bus::match::rules::nameOwnerChanged(stateHostService),
            [this](sdbusplus::message_t& msg) { onNameOwnerChanged(msg); })
    {
        queryCurrentState();
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

    sdbusplus::bus_t& bus;
    std::optional<sdeventplus::source::IO>& ioSource;
    sdbusplus::bus::match_t propertiesWatch;
    sdbusplus::bus::match_t nameOwnerWatch;

    void updateCapture(ProgressStages stage)
    {
        if (ioSource)
        {
            ioSource->set_enabled(stage == ProgressStages::PostComplete
                                      ? sdeventplus::source::Enabled::Off
                                      : sdeventplus::source::Enabled::On);
        }
    }

    void queryCurrentState()
    {
        try
        {
            auto m =
                bus.new_method_call(stateHostService, hostStatePath,
                                    "org.freedesktop.DBus.Properties", "Get");
            m.append(Progress::interface,
                     Progress::property_names::boot_progress);
            auto value = bus.call(m).unpack<std::variant<ProgressStages>>();
            updateCapture(std::get<ProgressStages>(value));
        }
        catch (const sdbusplus::exception_t&)
        {
            // State.Host not on D-Bus yet; retried from onNameOwnerChanged.
        }
    }

    void onNameOwnerChanged(sdbusplus::message_t& msg)
    {
        std::string name, oldOwner, newOwner;
        msg.read(name, oldOwner, newOwner);
        if (!newOwner.empty())
        {
            queryCurrentState();
        }
    }

    void onPropertiesChanged(sdbusplus::message_t& msg)
    {
        try
        {
            std::string interface;
            std::unordered_map<std::string, Progress::PropertiesVariant>
                properties;
            msg.read(interface, properties);

            auto it = properties.find(Progress::property_names::boot_progress);
            if (it == properties.end())
            {
                return;
            }
            if (const auto* stage = std::get_if<ProgressStages>(&it->second))
            {
                updateCapture(*stage);
            }
        }
        catch (const sdbusplus::exception_t&)
        {
            // Ignore malformed or unknown BootProgress values.
        }
    }
};
