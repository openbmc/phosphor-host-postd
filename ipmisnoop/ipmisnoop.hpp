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
#include <xyz/openbmc_project/State/Boot/Raw/server.hpp>

const static constexpr char* PropertiesIntf = "org.freedesktop.DBus.Properties";

/* The LPC snoop on port 80h is mapped to this dbus path. */
#define IPMI_SNOOP_OBJECTPATH "/xyz/openbmc_project/state/boot/raw"
#define IPMI_SNOOP_INTERFACE "xyz.openbmc_project.State.Boot.Raw"

#define HOST_PARSE_IDX 3
#define MAX_POSTCODE 255

std::vector<gpiod::line> led_lines;

template <typename... T>
using ServerObject = typename sdbusplus::server::object::object<T...>;
using PostInterface = sdbusplus::xyz::openbmc_project::State::Boot::server::Raw;
using PostObject = ServerObject<PostInterface>;

struct IpmiPostReporter : PostObject
{

    IpmiPostReporter(sdbusplus::bus::bus& bus, const char* objPath) :
        PostObject(bus, objPath), bus(bus),
        propertiesChangedSignalRaw(
            bus,
            sdbusplus::bus::match::rules::type::signal() +
                sdbusplus::bus::match::rules::member("PropertiesChanged") +
                sdbusplus::bus::match::rules::path(objPath) +
                sdbusplus::bus::match::rules::interface(PropertiesIntf) +
                sdbusplus::bus::match::rules::argN(0, IPMI_SNOOP_INTERFACE),
            [this](sdbusplus::message::message& msg) {
                std::string objectName;
                std::string InterfaceName;
                std::map<std::string, std::variant<uint64_t>> msgData;
                msg.read(InterfaceName, msgData);

                std::filesystem::path name(msg.get_path());
                objectName = name.filename();

                std::string hostNumStr = objectName.substr(HOST_PARSE_IDX);
                int hostNum = std::stoi(hostNumStr);

                // Check if it was the Value property that changed.
                auto valPropMap = msgData.find("Value");
                if (valPropMap != msgData.end())
                {
                    uint8_t postcode = std::get<uint64_t>(valPropMap->second);
                    if (postcode < MAX_POSTCODE)
                    {
                        // write postcode into seven segment display
                        if (postCodeDisplay(postcode, hostNum) < 0)
                        {
                            fprintf(stderr, "Error in display the postcode\n");
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

    ~IpmiPostReporter()
    {
    }

    sdbusplus::bus::bus& bus;
    sdbusplus::bus::match_t propertiesChangedSignalRaw;
    int postCodeDisplay(uint8_t, int);
};

template <class Handler>
void async_get(Handler&& handler)
{
    const std::string service = "xyz.openbmc_project.Chassis.Buttons";
    const std::string path = "/xyz/openbmc_project/Chassis/Buttons/Selector0";
    const std::string interface =
        "xyz.openbmc_project.Chassis.Buttons.Selector";
    const std::string name = "Position";

    boost::asio::io_context io;
    auto conn2 = std::make_shared<sdbusplus::asio::connection>(io);
    sdbusplus::asio::connection& conn = *conn2;

    sdbusplus::asio::getProperty<uint16_t>(conn, service, path, interface, name,
                                           std::forward<Handler>(handler));
}

auto getFailed()
{
    return [](boost::system::error_code error) {
        std::cerr << "error_code" << error << "\n";
    };
}

// Configure the seven segment display connected GPIOs direction
int configGPIODirOutput(void)
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

int writePostCode(gpiod_line* gpioLine, const char value)
{
    int ret;

    // Write the postcode into GPIOs
    ret = gpiod_line_set_value(gpioLine, value);
    if (ret < 0)
    {
        std::cerr << "Set line output failed.\n";
        return -1;
    }

    return 0;
}

// Display the received postcode into seven segment display
int IpmiPostReporter::postCodeDisplay(uint8_t status, int host)
{
    int hostSWPos = -1;

    async_get([&](boost::system::error_code ec, int value) {
        if (ec)
        {

            getFailed();
            return;
        }
        hostSWPos = value;
    });
    if (hostSWPos == host)
    {
        for (int iteration = 0; iteration < 8; iteration++)
        {
            // spilt byte to write into GPIOs
            int value = (status >> iteration) & 1;
            led_lines[iteration].set_value(value);
        }
    }

    return 0;
}
