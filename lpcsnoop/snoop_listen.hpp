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

#include "lpcsnoop/snoop.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>

namespace lpcsnoop
{
using std::get;

/* Returns matching string for what signal to listen on Dbus */
static const std::string GetMatchRule()
{
    using namespace sdbusplus::bus::match::rules;

    return type::signal() + interface("org.freedesktop.DBus.Properties") +
           member("PropertiesChanged") + path(snoopObject);
}

class SnoopListen
{
    using message_handler_t = std::function<void(sdbusplus::message_t&)>;
    using postcode_handler_t = std::function<void(FILE*, postcode_t)>;

  public:
    SnoopListen(sdbusplus::bus_t& busIn, sd_bus_message_handler_t handler) :
        signal(busIn, GetMatchRule().c_str(), handler, this)
    {}

    SnoopListen(sdbusplus::bus_t& busIn, message_handler_t handler) :
        signal(busIn, GetMatchRule(), handler)
    {}

    SnoopListen(sdbusplus::bus_t& busIn, postcode_handler_t handler,
                FILE* f = NULL) :
        SnoopListen(busIn, std::bind(defaultMessageHandler, handler, f,
                                     std::placeholders::_1))
    {}

    SnoopListen() = delete; // no default constructor
    ~SnoopListen() = default;
    SnoopListen(const SnoopListen&) = delete;
    SnoopListen& operator=(const SnoopListen&) = delete;
    SnoopListen(SnoopListen&&) = default;
    SnoopListen& operator=(SnoopListen&&) = default;

  private:
    sdbusplus::bus::match_t signal;

    /*
     * Default message handler which listens to published messages on snoop
     * DBus path, and calls the given postcode_handler on each value received.
     */
    static void defaultMessageHandler(postcode_handler_t& handler, FILE* f,
                                      sdbusplus::message_t& m)
    {
        std::string messageBusName;
        std::map<std::string, std::variant<postcode_t>> messageData;
        constexpr char propertyKey[] = "Value";

        m.read(messageBusName, messageData);

        if (messageBusName == snoopDbus &&
            messageData.find(propertyKey) != messageData.end())
        {
            handler(f, get<postcode_t>(messageData[propertyKey]));
        }
    }
};

} // namespace lpcsnoop
