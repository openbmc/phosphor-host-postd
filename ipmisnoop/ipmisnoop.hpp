#pragma once

#include <boost/asio.hpp>
#include <filesystem>
#include <gpiod.hpp>
#include <iostream>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Chassis/Buttons/HostSelector/server.hpp>
#include <xyz/openbmc_project/State/Boot/Raw/server.hpp>

#include "lpcsnoop/snoop.hpp"

const std::string ipmiSnoopObject = "/xyz/openbmc_project/state/boot/raw";

const int hostParseIdx = 3;
const int maxPostcode = 255;
const int maxPosition = 4;

std::vector<gpiod::line> led_lines;

using Selector =
    sdbusplus::xyz::openbmc_project::Chassis::Buttons::server::HostSelector;

std::unique_ptr<sdbusplus::bus::match_t> matchSignal;

const std::string selectorService = "xyz.openbmc_project.Chassis.Buttons";
const std::string selectorObject =
    "/xyz/openbmc_project/Chassis/Buttons/HostSelector";
const std::string selectorIface =
    "xyz.openbmc_project.Chassis.Buttons.HostSelector";

const std::string rawObject = "/xyz/openbmc_project/state/boot";
const std::string rawIface = "xyz.openbmc_project.State.Boot.Raw";
const std::string rawService = "xyz.openbmc_project.State.Boot.Raw";

uint32_t getSelectorPosition(sdbusplus::bus::bus& bus)
{
    const std::string propertyName = "Position";

    auto method =
        bus.new_method_call(selectorService.c_str(), selectorObject.c_str(),
                            "org.freedesktop.DBus.Properties", "Get");
    method.append(selectorIface.c_str(), propertyName);

    try
    {
        std::variant<uint32_t> value{};
        auto reply = bus.call(method);
        reply.read(value);
        return std::get<uint32_t>(value);
    }
    catch (const sdbusplus::exception::exception& ex)
    {
        std::cerr << "GetProperty call failed ";
        throw std::runtime_error("GetProperty call failed");
    }
}

struct IpmiPostReporter : PostObject
{
    IpmiPostReporter(sdbusplus::bus::bus& bus, const char* objPath) :
        PostObject(bus, objPath), bus(bus),
        propertiesChangedSignalRaw(
            bus,
            sdbusplus::bus::match::rules::propertiesChanged(objPath, rawIface),

            [this, &bus](sdbusplus::message::message& msg) {
                using primarycode_t = uint64_t;
                using secondarycode_t = std::vector<uint8_t>;
                using postcode_t = std::tuple<primarycode_t, secondarycode_t>;

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
                    std::cerr << "Value property is not found " <<std::endl;
                    return;
                }
                else
                {
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
                                fprintf(stderr,
                                        "Error in display the postcode\n");
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
                }
            })
    {
    }

    sdbusplus::bus::bus& bus;
    sdbusplus::bus::match_t propertiesChangedSignalRaw;
    int postCodeDisplay(uint8_t);
    void getSelectorPositionSignal(sdbusplus::bus::bus& bus);
};

// Configure the seven segment display connected GPIOs direction
int configGPIODirOutput()
{
    std::string gpioStr;
    // Need to define gpio names LED_POST_CODE_0 to 8 in dts file
    std::string gpioName = "LED_POST_CODE_";
    const int value = 0;

    for (int iteration = 0; iteration < 8; iteration++)
    {
        gpioStr = gpioName + std::to_string(iteration);
        gpiod::line gpioLine = gpiod::find_line(gpioStr);

        if (!gpioLine)
        {
            std::string errMsg = "Failed to find the " + gpioStr + " line";
            std::cerr << errMsg.c_str() << std::endl;
            return -1;
        }

        led_lines.push_back(gpioLine);
        // Request GPIO output to specified value
        try
        {
            gpioLine.request({__FUNCTION__,
                              gpiod::line_request::DIRECTION_OUTPUT,
                              gpiod::line_request::FLAG_ACTIVE_LOW},
                             value);
        }
        catch (std::exception&)
        {
            std::string errMsg = "Failed to request " + gpioStr + " output";
            std::cerr << errMsg.c_str() << std::endl;
            return -1;
        }
    }

    return 0;
}

// Display the received postcode into seven segment display
int IpmiPostReporter::postCodeDisplay(uint8_t status)
{
    for (int iteration = 0; iteration < 8; iteration++)
    {
        // split byte to write into GPIOs
        int value = !((status >> iteration) & 0x01);

        led_lines[iteration].set_value(value);
    }
    return 0;
}
