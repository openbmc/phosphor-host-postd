#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/test/sdbus_mock.hpp>

#include "lpcsnoop/snoop.hpp"

using ::testing::_;
using ::testing::IsNull;
using ::testing::Return;
using ::testing::StrEq;

/*
 * A boring test that tests PostReporter object emits object added
 * on the expected path.
 */
TEST(PostReporterTest, EmitObjectTest)
{
    sdbusplus::SdBusMock sdbus_mock;
    auto bus_mock = sdbusplus::get_mocked_new(&sdbus_mock);

    EXPECT_CALL(sdbus_mock,
                sd_bus_emit_object_added(IsNull(), StrEq(SNOOP_OBJECTPATH)))
        .WillOnce(Return(0));

    PostReporter testReporter(bus_mock, SNOOP_OBJECTPATH, true);
    testReporter.emit_object_added();
}
