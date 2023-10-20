#include "ipmisnoop.hpp"

std::vector<std::unique_ptr<IpmiPostReporter>> reporters;
bool sevenSegmentLedEnabled = true;
std::vector<gpiod::line> led_lines;

uint32_t getSelectorPosition(sdbusplus::bus_t& bus)
{
    const std::string propertyName = "Position";

    auto method = bus.new_method_call(selectorService.c_str(),
                                      selectorObject.c_str(),
                                      "org.freedesktop.DBus.Properties", "Get");
    method.append(selectorIface.c_str(), propertyName);

    try
    {
        std::variant<uint32_t> value{};
        auto reply = bus.call(method);
        reply.read(value);
        return std::get<uint32_t>(value);
    }
    catch (const sdbusplus::exception_t& ex)
    {
        std::cerr << "GetProperty call failed. " << ex.what() << std::endl;
        return 0;
    }
}

// Configure the seven segment display connected GPIOs direction
static int configGPIODirOutput()
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

            /* sevenSegmentLedEnabled flag is unset when GPIO pins are not there
             * 7 seg display for fewer platforms.
             */
            sevenSegmentLedEnabled = false;
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

void IpmiPostReporter::getSelectorPositionSignal(sdbusplus::bus_t& bus)
{
    constexpr uint8_t minPositionVal = 0;
    constexpr uint8_t maxPositionVal = 5;

    size_t posVal = 0;

    static auto matchSignal = std::make_unique<sdbusplus::bus::match_t>(
        bus,
        sdbusplus::bus::match::rules::propertiesChanged(selectorObject,
                                                        selectorIface),
        [&](sdbusplus::message_t& msg) {
        std::string objectName;
        std::map<std::string, Selector::PropertiesVariant> msgData;
        msg.read(objectName, msgData);

        auto valPropMap = msgData.find("Position");
        {
            if (valPropMap == msgData.end())
            {
                std::cerr << "Position property not found " << std::endl;
                return;
            }

            posVal = std::get<size_t>(valPropMap->second);

            if (posVal > minPositionVal && posVal < maxPositionVal)
            {
                std::tuple<uint64_t, secondary_post_code_t> postcodes =
                    reporters[posVal - 1]->value();
                uint64_t postcode = std::get<uint64_t>(postcodes);

                // write postcode into seven segment display
                if (postCodeDisplay(postcode) < 0)
                {
                    fprintf(stderr, "Error in display the postcode\n");
                }
            }
        }
    });
}

// handle muti-host D-bus
int postCodeIpmiHandler(const std::string& snoopObject,
                        const std::string& snoopDbus, sdbusplus::bus_t& bus,
                        std::span<std::string> host)
{
    int ret = 0;

    try
    {
        for (size_t iteration = 0; iteration < host.size(); iteration++)
        {
            std::string objPathInst = snoopObject + host[iteration];

            sdbusplus::server::manager_t m{bus, objPathInst.c_str()};

            /* Create a monitor object and let it do all the rest */
            reporters.emplace_back(
                std::make_unique<IpmiPostReporter>(bus, objPathInst.c_str()));

            reporters[iteration]->emit_object_added();
        }

        bus.request_name(snoopDbus.c_str());

        /* sevenSegmentLedEnabled flag is unset when GPIO pins are not there 7
        seg display for fewer platforms. So, the code for postcode dispay and
        Get Selector position can be skipped in those platforms.
        */
        if (sevenSegmentLedEnabled)
        {
            reporters[0]->getSelectorPositionSignal(bus);
        }
        else
        {
            reporters.clear();
        }
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
    }

    // Configure seven segment dsiplay connected to GPIOs as output
    ret = configGPIODirOutput();
    if (ret < 0)
    {
        fprintf(stderr, "Failed find the gpio line. Cannot display postcodes "
                        "in seven segment display..\n");
    }

    while (true)
    {
        bus.process_discard();
        bus.wait();
    }
    exit(EXIT_SUCCESS);
}
