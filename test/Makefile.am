# Common flags for all binaries in this subdirectory.
# Per-program specific flags can be specified using
# prg_CPPFLAGS = ....
AM_CPPFLAGS = -I${top_srcdir} $(GTEST_CFLAGS)
AM_CXXFLAGS = $(PTHREAD_CFLAGS) $(GTEST_CFLAGS)
AM_LDFLAGS = $(GTEST_LIBS) $(PTHREAD_LIBS) $(OESDK_TESTCASE_FLAGS)

# The list of unit test programs we are building and running.
check_PROGRAMS = post_reporter_test

# Run all 'check' test programs
TESTS = $(check_PROGRAMS)

# List sources for each test.
post_reporter_test_SOURCES = post_reporter_test.cpp
