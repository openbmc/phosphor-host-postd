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

#include <function2/function2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>

namespace lpcsnoop
{

class SnoopListen
{
    using postcode_handler_t = fu2::unique_function<void(postcode_t)>;

  public:
    SnoopListen(sdbusplus::bus_t& bus, postcode_handler_t&& h) :
        signal(bus, rule, [h = std::move(h)](sdbusplus::message_t& m) {
            std::string messageBusName;
            std::unordered_map<std::string, std::variant<postcode_t>>
                messageData;

            m.read(messageBusName, messageData);

            auto it = messageData.find(std::string("Value"));
            if (it != messageData.end())
            {
                h(f, std::get<postcode_t>(it->second));
            }
        })
    {
    }

  private:
    sdbusplus::bus::match_t signal;

    static inline constexpr auto rule = []() {
        return sdbusplus::bus::match::rules::propertiesChanged(snoopObject,
                                                               snoopDbus);
    }();
};

} // namespace lpcsnoop
