#include "lpcsnoop/snoop.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/test/sdbus_mock.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

namespace
{

// Fixture for testing class PostReporter
class PostReporterTest : public ::testing::Test
{
  protected:
    PostReporterTest() : bus_mock(), bus(sdbusplus::get_mocked_new(&bus_mock))
    {}

    ~PostReporterTest() {}

    NiceMock<sdbusplus::SdBusMock> bus_mock;
    sdbusplus::bus_t bus;
};

TEST_F(PostReporterTest, EmitsObjectsOnExpectedDbusPath)
{
    EXPECT_CALL(bus_mock,
                sd_bus_emit_object_added(IsNull(), StrEq(snoopObject)))
        .WillOnce(Return(0));

    PostReporter testReporter(bus, snoopObject, true);
    testReporter.emit_object_added();
}

TEST_F(PostReporterTest, AddsObjectWithExpectedName)
{
    auto slotcb = [](sd_bus*, sd_bus_slot** slot, auto&&...) {
        *slot = reinterpret_cast<sd_bus_slot*>(0xdefa);
        return 0;
    };

    EXPECT_CALL(bus_mock,
                sd_bus_add_object_vtable(IsNull(), _, StrEq(snoopObject),
                                         StrEq(snoopDbus), _, _))
        .WillOnce(slotcb);

    PostReporter testReporter(bus, snoopObject, true);
}

TEST_F(PostReporterTest, ValueReadsDefaultToEmpty)
{
    PostReporter testReporter(bus, snoopObject, true);
    EXPECT_TRUE(std::get<0>(testReporter.value()).empty());
}

TEST_F(PostReporterTest, SetValueToPositiveValueWorks)
{
    PostReporter testReporter(bus, snoopObject, true);
    primary_post_code_t primaryCode = {122, 126, 127};
    secondary_post_code_t secondaryCode = {123, 124, 125};
    testReporter.value(std::make_tuple(primaryCode, secondaryCode));
    EXPECT_EQ(primaryCode, std::get<0>(testReporter.value()));
    EXPECT_EQ(secondaryCode, std::get<1>(testReporter.value()));
}

TEST_F(PostReporterTest, SetValueMultipleTimesWorks)
{
    PostReporter testReporter(bus, snoopObject, true);
    primary_post_code_t primaryCode = {20, 21, 0, 123};
    secondary_post_code_t secondaryCode = {10, 40, 0, 245, 56};
    testReporter.value(std::make_tuple(primaryCode, secondaryCode));
    EXPECT_EQ(primaryCode, std::get<0>(testReporter.value()));
    EXPECT_EQ(secondaryCode, std::get<1>(testReporter.value()));

    primaryCode = {44, 45};
    secondaryCode = {0, 0, 0, 0, 0};
    testReporter.value(std::make_tuple(primaryCode, secondaryCode));
    EXPECT_EQ(primaryCode, std::get<0>(testReporter.value()));
    EXPECT_EQ(secondaryCode, std::get<1>(testReporter.value()));

    primaryCode = {0};
    secondaryCode = {23, 200, 0, 45, 2};
    testReporter.value(std::make_tuple(primaryCode, secondaryCode));
    EXPECT_EQ(primaryCode, std::get<0>(testReporter.value()));
    EXPECT_EQ(secondaryCode, std::get<1>(testReporter.value()));

    primaryCode = {46};
    secondaryCode = {10, 40, 0, 35, 78};
    testReporter.value(std::make_tuple(primaryCode, secondaryCode));
    EXPECT_EQ(primaryCode, std::get<0>(testReporter.value()));
    EXPECT_EQ(secondaryCode, std::get<1>(testReporter.value()));

    primaryCode = {46};
    secondaryCode = {10, 40, 0, 35, 78};
    testReporter.value(std::make_tuple(primaryCode, secondaryCode));
    EXPECT_EQ(primaryCode, std::get<0>(testReporter.value()));
    EXPECT_EQ(secondaryCode, std::get<1>(testReporter.value()));
}

} // namespace
