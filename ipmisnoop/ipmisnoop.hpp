#pragma once

#include "lpcsnoop/snoop.hpp"

#include <boost/asio.hpp>
#include <gpiod.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Chassis/Buttons/HostSelector/server.hpp>
#include <xyz/openbmc_project/State/Boot/Raw/server.hpp>

#include <filesystem>
#include <iostream>
#include <span>

const std::string ipmiSnoopObject = "/xyz/openbmc_project/state/boot/raw";

const int hostParseIdx = 3;
const int maxPostcode = 255;
const int maxPosition = 4;

extern bool sevenSegmentLedEnabled;

extern std::vector<gpiod::line> led_lines;

using Selector =
    sdbusplus::xyz::openbmc_project::Chassis::Buttons::server::HostSelector;

const std::string selectorService = "xyz.openbmc_project.Chassis.Buttons";
const std::string selectorObject =
    "/xyz/openbmc_project/Chassis/Buttons/HostSelector";
const std::string selectorIface =
    "xyz.openbmc_project.Chassis.Buttons.HostSelector";

const std::string rawObject = "/xyz/openbmc_project/state/boot";
const std::string rawIface = "xyz.openbmc_project.State.Boot.Raw";
const std::string rawService = "xyz.openbmc_project.State.Boot.Raw";

int postCodeIpmiHandler(const std::string& snoopObject,
                        const std::string& snoopDbus, sdbusplus::bus_t& bus,
                        std::span<std::string> host);

uint32_t getSelectorPosition(sdbusplus::bus_t& bus);

struct IpmiPostReporter : PostObject
{
    IpmiPostReporter(sdbusplus::bus_t& bus, const char* objPath) :
        PostObject(bus, objPath), bus(bus),
        propertiesChangedSignalRaw(
            bus,
            sdbusplus::bus::match::rules::propertiesChanged(objPath, rawIface),

            [this, &bus](sdbusplus::message_t& msg) {
        using primarycode_t = uint64_t;
        using secondarycode_t = std::vector<uint8_t>;
        using postcode_t = std::tuple<primarycode_t, secondarycode_t>;

        /* sevenSegmentLedEnabled flag is set when GPIO pins are not
        there 7 seg display for fewer platforms. So, the code for
        postcode dispay and Get Selector position can be skipped in
        those platforms.
        */
        if (!sevenSegmentLedEnabled)
        {
            return;
        }

        std::string objectName;
        std::string InterfaceName;
        std::map<std::string, std::variant<postcode_t>> msgData;
        msg.read(InterfaceName, msgData);

        std::filesystem::path name(msg.get_path());
        objectName = name.filename();

        std::string hostNumStr = objectName.substr(hostParseIdx);
        size_t hostNum = std::stoi(hostNumStr);

        size_t position = getSelectorPosition(bus);

        if (position > maxPosition)
        {
            std::cerr << "Invalid position. Position should be 1 to 4 "
                         "for all hosts "
                      << std::endl;
        }

        // Check if it was the Value property that changed.
        auto valPropMap = msgData.find("Value");
        if (valPropMap == msgData.end())
        {
            std::cerr << "Value property is not found " << std::endl;
            return;
        }
        uint64_t postcode =
            std::get<0>(std::get<postcode_t>(valPropMap->second));

        if (postcode <= maxPostcode)
        {
            if (position == hostNum)
            {
                uint8_t postcode_8bit =
                    static_cast<uint8_t>(postcode & 0x0000FF);

                // write postcode into seven segment display
                if (postCodeDisplay(postcode_8bit) < 0)
                {
                    fprintf(stderr, "Error in display the postcode\n");
                }
            }
            else
            {
                fprintf(stderr, "Host Selector Position and host "
                                "number is not matched..\n");
            }
        }
        else
        {
            fprintf(stderr, "invalid postcode value \n");
        }
            })
    {}

    sdbusplus::bus_t& bus;
    sdbusplus::bus::match_t propertiesChangedSignalRaw;
    int postCodeDisplay(uint8_t);
    void getSelectorPositionSignal(sdbusplus::bus_t& bus);
};
