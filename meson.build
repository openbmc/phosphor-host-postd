project(
  'phosphor-host-postd',
  'cpp',
  default_options: [
    'warning_level=3',
    'werror=true',
    'cpp_std=c++17',
  ],
  license: 'Apache-2.0',
  version: '1.0',
)

build_tests = get_option('tests')

postd_headers = include_directories('.')

phosphor_dbus_interfaces = dependency('phosphor-dbus-interfaces')
sdbusplus = dependency('sdbusplus')
sdeventplus = dependency('sdeventplus')
systemd = dependency('systemd')
libgpiodcxx = dependency('libgpiodcxx')

conf_data = configuration_data()
conf_data.set('bindir', get_option('prefix') / get_option('bindir'))
conf_data.set('SNOOP_DEVICE', get_option('snoop-device'))
conf_data.set('POST_CODE_BYTES', get_option('post-code-bytes'))
conf_data.set('HOST_INSTANCES', get_option('host-instances'))

conf_data.set('SYSTEMD_TARGET', get_option('systemd-target'))

if(get_option('snoop').enabled())
    add_project_arguments('-DENABLE_IPMI_SNOOP',language:'cpp')
endif

snoopd_args = '-b ' + get_option('post-code-bytes').to_string()
if get_option('snoop-device') != ''
  snoopd_args += ' -d /dev/' + get_option('snoop-device')
endif
conf_data.set('SNOOPD_ARGS', snoopd_args)

configure_file(
  input: 'lpcsnoop.service.in',
  output: 'lpcsnoop.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir'))

executable(
  'snoopd',
  'main.cpp',
  dependencies: [
    sdbusplus,
    sdeventplus,
    phosphor_dbus_interfaces,
    libgpiodcxx,
  ],
  install: true,
)

executable(
  'snooper',
  'example.cpp',
  dependencies: [
    sdbusplus,
    sdeventplus,
    phosphor_dbus_interfaces,
  ],
  install: true,
)

if get_option('7seg').enabled()
  udevdir = dependency('udev', required : false).get_pkgconfig_variable('udevdir')
  assert(udevdir != '', 'Cannot find udevdir')
  install_data(['80-7seg.rules'], install_dir : udevdir)

  install_data(
    ['postcode-7seg@.service'],
    install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir')
  )

  executable(
    'postcode_7seg',
    '7seg.cpp',
    dependencies: [
      sdbusplus,
      phosphor_dbus_interfaces,
    ],
    link_args : [
      '-lstdc++fs',
    ],
    install: true,
  )
endif

install_headers(
  'lpcsnoop/snoop.hpp',
  'lpcsnoop/snoop_listen.hpp',
  subdir: 'lpcsnoop')

if not build_tests.disabled()
  subdir('test')
endif
