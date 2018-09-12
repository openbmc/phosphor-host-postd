AM_DEFAULT_SOURCE_EXT = .cpp

sbin_PROGRAMS = snoopd snooper

snoopd_SOURCES = main.cpp
snoopd_LDADD = $(SDBUSPLUS_LIBS) $(PHOSPHOR_DBUS_INTERFACES_LIBS) -lpthread -lsdeventplus
snoopd_CXXFLAGS = $(SDBUSPLUS_CFLAGS) $(PHOSPHOR_DBUS_INTERFACES_CFLAGS)

snooper_SOURCES = example.cpp
snooper_LDADD = $(SDBUSPLUS_LIBS) $(PHOSPHOR_DBUS_INTERFACES_LIBS)
snooper_CXXFLAGS = $(SDBUSPLUS_CFLAGS) $(PHOSPHOR_DBUS_INTERFACES_CFLAGS)

# Installs in $(includedir)/lpcsnoop/
nobase_include_HEADERS = lpcsnoop/snoop.hpp lpcsnoop/snoop_listen.hpp

SUBDIRS = . test
