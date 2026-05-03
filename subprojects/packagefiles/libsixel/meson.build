project('libsixel', 'c', version: '1.10.5', default_options: ['werror=false', 'warning_level=0'])

cc = meson.get_compiler('c')

sixel_srcs = [
  'src/allocator.c',
  'src/chunk.c',
  'src/decoder.c',
  'src/dither.c',
  'src/encoder.c',
  'src/frame.c',
  'src/fromgif.c',
  'src/frompnm.c',
  'src/fromsixel.c',
  'src/loader.c',
  'src/output.c',
  'src/pixelformat.c',
  'src/quant.c',
  'src/scale.c',
  'src/status.c',
  'src/tosixel.c',
  'src/tty.c',
  'src/writer.c'
]

conf_data = configuration_data()
conf_data.set('HAVE_SYS_TYPES_H', 1)
conf_data.set('HAVE_UNISTD_H', 1)
conf_data.set('HAVE_ASSERT_H', 1)
conf_data.set('HAVE_ERRNO_H', 1)
conf_data.set('HAVE_MEMORY_H', 1)
conf_data.set('HAVE_STRING_H', 1)
conf_data.set('HAVE_MATH_H', 1)
conf_data.set('HAVE_LIMITS_H', 1)
conf_data.set('HAVE_INTTYPES_H', 1)
conf_data.set('HAVE_STDINT_H', 1)
conf_data.set('HAVE_STDLIB_H', 1)
conf_data.set('HAVE_MEMCPY', 1)
conf_data.set('HAVE_MEMMOVE', 1)
conf_data.set('_POSIX_C_SOURCE', '199309L')

configure_file(
  output: 'config.h',
  configuration: conf_data
)

sixel_conf = configuration_data()
sixel_conf.set('PACKAGE_VERSION', meson.project_version())
sixel_conf.set('LS_LTVERSION', meson.project_version())
sixel_conf.set('PACKAGE_DESCRIPTION', 'libsixel')
sixel_conf.set('attr_func_deprecated', '')

configured_sixel_header = configure_file(
  input: 'include/sixel.h.in',
  output: 'sixel.h.in.configured',
  configuration: sixel_conf
)

python = import('python').find_installation('python3', required: false)
if not python.found()
  python = find_program('python', required: true)
endif

configure_file(
  input: configured_sixel_header,
  output: 'sixel.h',
  command: [
    python,
    files('sixel_static_header.py'),
    '@INPUT@',
    '@OUTPUT@'
  ]
)

sixel_inc = include_directories('include', 'src', '.')

libsixel_args = ['-D_POSIX_C_SOURCE=199309L']
if cc.get_id() == 'msvc'
  libsixel_args += ['-D_CRT_SECURE_NO_WARNINGS']
endif

libsixel_lib = static_library('sixel',
  sixel_srcs,
  include_directories: sixel_inc,
  c_args: libsixel_args
)

sixel_dep = declare_dependency(
  link_with: libsixel_lib,
  include_directories: include_directories('include', '.')
)

meson.override_dependency('libsixel', sixel_dep)
