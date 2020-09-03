#pragma once

#include <gpiod.hpp>
#include <iostream>
#include <sdbusplus/asio/connection.hpp>
#include <xyz/openbmc_project/State/Boot/Raw/server.hpp>

/* The LPC snoop on port 80h is mapped to this dbus path. */
#define SNOOP_OBJECTPATH "/xyz/openbmc_project/state/boot/raw"
/* The LPC snoop on port 80h is mapped to this dbus service. */
#define SNOOP_BUSNAME "xyz.openbmc_project.State.Boot.Raw"

using message = sdbusplus::message::message;

static uint16_t totalHost; /* Number of host */

// libgpiod obj for seven segment display
std::vector<gpiod_line*> leds;

template <typename... T>
using ServerObject = typename sdbusplus::server::object::object<T...>;
using PostInterface = sdbusplus::xyz::openbmc_project::State::Boot::server::Raw;
using PostObject = ServerObject<PostInterface>;

class PostReporter : public PostObject
{
  public:
    PostReporter(sdbusplus::bus::bus& bus, const char* objPath, bool defer) :
        PostObject(bus, objPath, defer)
    {
    }

    ~PostReporter()
    {
    }

    void readPostCode(uint16_t postcode, uint16_t host);
    int postCodeDisplay(uint8_t status, uint16_t host);
};

// There should be hard or soft logic in the front panel to select the host
// to display the correposnding postcode in mult-host platform.
// This method reads the host position from the D-bus interface expose by
// other process and display it in the seven segment display based on the match.
int readHostSelectionPos(int& pos)
{
    boost::asio::io_context io;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);
    // Below D-Bus interfaces should be expose by other services to update the
    // host position
    auto value = conn->new_method_call("xyz.openbmc_project.Misc.Frontpanel",
                                       "/xyz/openbmc_project/misc/frontpanel",
                                       "xyz.openbmc_project.Misc.Frontpanel",
                                       "readHostPosition");
    try
    {
        message intMsg = conn->call(value);
        intMsg.read(pos);
    }
    catch (sdbusplus::exception::SdBusError& e)
    {
        std::cerr << "Failed to read the host position\n";
        return -1;
    }

    return 0;
}

// Configure the seven segment display connected GPIOs direction
int configGPIODirOutput(void)
{

    std::string gpio;
    // Need to define gpio names LED_POST_CODE_0 to 8 in dts file
    std::string gpioName = "LED_POST_CODE_";

    for (int iteration = 0; iteration < 8; iteration++)
    {
        int ret;
        gpiod_line* led;

        gpio = gpioName + std::to_string(iteration);
        led = gpiod_line_find(gpio.c_str());
        if (!led)
        {
            std::cerr << "Failed to find the gpioname : " << gpio << std::endl;
            return -1;
        }

        leds.push_back(led);
        ret =
            gpiod_line_request_output(led, "LEDs", GPIOD_LINE_ACTIVE_STATE_LOW);
        if (ret < 0)
        {
            std::cerr << "Request gpio line as output failed.\n";
            return -1;
        }
    }

    return 0;
}

int writePostCode(gpiod_line* gpioLine, const char value)
{
    int ret;

    fprintf(stderr, "Display_PostCode: 0x%" PRIx8 "\n", value);
    // Write the postcode into GPIOs
    ret = gpiod_line_set_value(gpioLine, value);
    if (ret < 0)
    {
        std::cerr << "Set line output failed..\n";
        return -1;
    }

    return 0;
}

// Display the received postcode into seven segment display
int PostReporter ::postCodeDisplay(uint8_t status, uint16_t host)
{
    int ret;
    int hostSWPos = -1;

    ret = readHostSelectionPos(hostSWPos);
    if (ret < 0)
    {
        std::cerr << "Read host position failed.\n";
        return -1;
    }
    //
    if (hostSWPos == host)
    {
        /*
         * 8 GPIOs connected to seven segment display from BMC
         * to display postcode
         */
        for (int iteration = 0; iteration < 8; iteration++)
        {
            char value;
            // spilt byte to write into GPIOs
            value = (status >> iteration) & 1;
            ret = writePostCode(leds[iteration], value);
            if (ret < 0)
            {
                std::cerr << "Failed to write the postcode in display\n";
                return -1;
            }
        }
    }
    return 0;
}

// To handle multi-host postcode
std::vector<std::unique_ptr<PostReporter>> reporters;

/*
 * This D-Bus method exposesd to other services through phosphor-dbus-interfaces
 * Other services calls this method to send the post code after port 80 received
 * from host with corresponding host id. The expected host-id starts with 0 for
 * first host.
 */
void PostReporter ::readPostCode(uint16_t postcode, uint16_t host)
{
    int ret;
    uint64_t code = 0;

    // write postcode into seven segment display
    ret = postCodeDisplay(postcode, host);
    if (ret < 0)
    {
        std::cerr << "Error in display the postcode\n";
    }

    code = le64toh(postcode);

    // The expected host id must be 0.
    if ((host + 1) <= totalHost)
    {
        // HACK: Always send property changed signal even for the same code
        // since we are single threaded, external users will never see the
        // first value.
        reporters[host]->value(~code, true);
        reporters[host]->value(code);
    }
    else
    {
        fprintf(stderr, "Invalid Host :%d\n", totalHost);
    }
}
