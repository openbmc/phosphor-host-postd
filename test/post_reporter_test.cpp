#include "lpcsnoop/snoop.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/test/sdbus_mock.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{
using ::testing::_;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

// Fixture for testing class PostReporter
class PostReporterTest : public ::testing::Test
{
  protected:
    PostReporterTest() : bus_mock(), bus(sdbusplus::get_mocked_new(&bus_mock))
    {
    }

    ~PostReporterTest()
    {
    }

    NiceMock<sdbusplus::SdBusMock> bus_mock;
    sdbusplus::bus::bus bus;
};

TEST_F(PostReporterTest, EmitsObjectsOnExpectedDbusPath)
{

    EXPECT_CALL(bus_mock,
                sd_bus_emit_object_added(IsNull(), StrEq(SNOOP_OBJECTPATH)))
        .WillOnce(Return(0));

    PostReporter testReporter(bus, SNOOP_OBJECTPATH, true);
    testReporter.emit_object_added();
}

TEST_F(PostReporterTest, AddsObjectWithExpectedName)
{
    EXPECT_CALL(bus_mock,
                sd_bus_add_object_vtable(IsNull(), _, StrEq(SNOOP_OBJECTPATH),
                                         StrEq(SNOOP_BUSNAME), _, _))
        .WillOnce(Return(0));

    PostReporter testReporter(bus, SNOOP_OBJECTPATH, true);
}

TEST_F(PostReporterTest, ValueReadsDefaultToZero)
{
    PostReporter testReporter(bus, SNOOP_OBJECTPATH, true);
    EXPECT_EQ(0, std::get<primary_post_code_t>(testReporter.value()));
}

TEST_F(PostReporterTest, SetValueToPositiveValueWorks)
{
    PostReporter testReporter(bus, SNOOP_OBJECTPATH, true);
    secondary_post_code_t secondaryCode = {123, 124, 125};
    testReporter.value(std::make_tuple(65537, secondaryCode));
    EXPECT_EQ(65537, std::get<primary_post_code_t>(testReporter.value()));
    EXPECT_EQ(secondaryCode,
              std::get<secondary_post_code_t>(testReporter.value()));
}

TEST_F(PostReporterTest, SetValueMultipleTimesWorks)
{
    PostReporter testReporter(bus, SNOOP_OBJECTPATH, true);
    secondary_post_code_t secondaryCode = {10, 40, 0, 245, 56};
    testReporter.value(std::make_tuple(123, secondaryCode));
    EXPECT_EQ(123, std::get<primary_post_code_t>(testReporter.value()));
    EXPECT_EQ(secondaryCode,
              std::get<secondary_post_code_t>(testReporter.value()));

    secondaryCode = {0, 0, 0, 0, 0};
    testReporter.value(std::make_tuple(45, secondaryCode));
    EXPECT_EQ(45, std::get<primary_post_code_t>(testReporter.value()));
    EXPECT_EQ(secondaryCode,
              std::get<secondary_post_code_t>(testReporter.value()));

    secondaryCode = {23, 200, 0, 45, 2};
    testReporter.value(std::make_tuple(0, secondaryCode));
    EXPECT_EQ(0, std::get<primary_post_code_t>(testReporter.value()));
    EXPECT_EQ(secondaryCode,
              std::get<secondary_post_code_t>(testReporter.value()));

    secondaryCode = {10, 40, 0, 35, 78};
    testReporter.value(std::make_tuple(46, secondaryCode));
    EXPECT_EQ(46, std::get<primary_post_code_t>(testReporter.value()));
    EXPECT_EQ(secondaryCode,
              std::get<secondary_post_code_t>(testReporter.value()));

    secondaryCode = {10, 40, 0, 35, 78};
    testReporter.value(std::make_tuple(46, secondaryCode));
    EXPECT_EQ(46, std::get<primary_post_code_t>(testReporter.value()));
    EXPECT_EQ(secondaryCode,
              std::get<secondary_post_code_t>(testReporter.value()));
}

} // namespace
