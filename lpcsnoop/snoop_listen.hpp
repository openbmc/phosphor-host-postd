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

#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/server.hpp>

#include "lpcsnoop/snoop.hpp"

namespace lpcsnoop
{

using DbusSignalHandler = int (*)(sd_bus_message*, void*, sd_bus_error*);

/* Returns matching string for what signal to listen on Dbus */
static const std::string GetMatchRule()
{
    return std::string("type='signal',"
                       "interface='org.freedesktop.DBus.Properties',"
                       "member='PropertiesChanged',"
                       "path='" +
                       std::string{SNOOP_OBJECTPATH} + "'");
}

class SnoopListen
{
  public:
    SnoopListen(sdbusplus::bus::bus& busIn, DbusSignalHandler handler) :
        bus(busIn), signal(busIn, GetMatchRule().c_str(), handler, this)
    {
    }

    SnoopListen(const SnoopListen&) = delete;
    SnoopListen& operator=(const SnoopListen&) = delete;

  private:
    sdbusplus::bus::bus& bus;
    sdbusplus::server::match::match signal;
};

} // namespace lpcsnoop
