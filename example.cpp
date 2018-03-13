/**
 * Copyright 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdio>
#include <iostream>
#include <memory>

#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/server.hpp>

#include "lpcsnoop/snoop.hpp"

/*
 * Handle incoming dbus signal we care about.
 */
static int DbusHandleSignal(sd_bus_message* msg, void* data, sd_bus_error* err);

/*
 * Get the match signal for dbus.
 */
static std::string GetMatch(void);

// Example object that listens for dbus updates.
class SnoopListen
{
    public:
        SnoopListen(sdbusplus::bus::bus& bus)
            : _bus(bus),
              _signal(bus,
                      GetMatch().c_str(),
                      DbusHandleSignal,
                      this)
        { }

    private:
        sdbusplus::bus::bus& _bus;
        sdbusplus::server::match::match _signal;
};

/*
 * This is the entry point for the application.
 *
 * This application simply creates an object that registers for incoming value
 * updates for the POST code dbus object.
 */
int main(int argc, char* argv[])
{
    auto ListenBus = sdbusplus::bus::new_default();
    std::unique_ptr<SnoopListen> snoop = std::make_unique<SnoopListen>(ListenBus);

    while (true)
    {
        ListenBus.process_discard();
        ListenBus.wait();
    }

    return 0;
}

static int DbusHandleSignal(sd_bus_message* msg, void* data, sd_bus_error* err)
{
    auto sdbpMsg = sdbusplus::message::message(msg);

    std::string msgSensor, busName{SNOOP_BUSNAME};
    std::map<std::string, sdbusplus::message::variant<uint64_t>> msgData;
    sdbpMsg.read(msgSensor, msgData);

    if (msgSensor == busName)
    {
        auto valPropMap = msgData.find("Value");
        if (valPropMap != msgData.end())
        {
            uint64_t rawValue = sdbusplus::message::variant_ns::get<uint64_t>
                               (valPropMap->second);

            std::printf("recv: 0x%x\n", static_cast<uint8_t>(rawValue));
        }
    }

    return 0;
}

static std::string GetMatch(void)
{
    std::string obj{SNOOP_OBJECTPATH};
    return std::string("type='signal',"
                       "interface='org.freedesktop.DBus.Properties',"
                       "member='PropertiesChanged',"
                       "path='" + obj + "'");
}
