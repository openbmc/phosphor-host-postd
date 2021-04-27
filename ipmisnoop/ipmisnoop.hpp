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
std::vector<gpiod_line*> leds; /*libgpiod obj for seven segment display */

/* The LPC snoop on port 80h is mapped to this dbus path. */
#define IPMI_SNOOP_OBJECTPATH "/xyz/openbmc_project/state/boot/raw"

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
                sdbusplus::bus::match::rules::interface(PropertiesIntf),
            [this](sdbusplus::message::message& msg) {
                std::string objectName;
                std::string InterfaceName;
                std::map<std::string, std::variant<uint64_t>> msgData;
                msg.read(InterfaceName, msgData);

                std::filesystem::path name(msg.get_path());
                objectName = name.filename();
                std::string hostNumStr = objectName.substr(3);
                int hostNum = std::stoi(hostNumStr);

                // Check if it was the Value property that changed.
                auto valPropMap = msgData.find("Value");
                if (valPropMap != msgData.end())
                {
                    int ret;
                    // write postcode into seven segment display
                    ret = postCodeDisplay(
                        std::get<uint64_t>(valPropMap->second), hostNum);
                    if (ret < 0)
                    {
                        fprintf(stderr, "Error in display the postcode\n");
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
    int postCodeDisplay(uint8_t, uint16_t);
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
    std::string gpio;
    // Need to define gpio names LED_POST_CODE_0 to 8 in dts file
    std::string gpioName = "LED_POST_CODE_";

    for (int iteration = 0; iteration < 8; iteration++)
    {
        gpio = gpioName + std::to_string(iteration);
        gpiod_line* led = gpiod_line_find(gpio.c_str());
        if (!led)
        {
            return -1;
        }

        leds.push_back(led);
        int ret =
            gpiod_line_request_output(led, "LEDs", GPIOD_LINE_ACTIVE_STATE_LOW);
        if (ret < 0)
        {
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
        return -1;
    }

    return 0;
}

// Display the received postcode into seven segment display
int IpmiPostReporter ::postCodeDisplay(uint8_t status, uint16_t host)
{
    int ret;
    uint16_t hostSWPos = -1;

async_get([&](boost::system::error_code ec, uint16_t value) 
{ 

if(ec)
{

getFailed();
return;
}
  hostSWPos = value; 


});



    if (hostSWPos == host)
    {
        /*
         * 8 GPIOs connected to seven segment display from BMC
         * to display postcode
         */
        for (int iteration = 0; iteration < 8; iteration++)
        {
            // spilt byte to write into GPIOs
            int value = (status >> iteration) & 1;
            ret = writePostCode(leds[iteration], value);
            if (ret < 0)
            {
                return -1;
            }
        }
    }

    return 0;
}
