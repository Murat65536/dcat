project('dcat', 'c',
  version: '0.1.0',
  meson_version: '>=1.0.0',
  default_options: [
    'c_std=c17',
    'cpp_std=c++17',
    'warning_level=3',
    'werror=false',
  ]
)

cc = meson.get_compiler('c')

msys2_prefix_name = ''
if host_machine.system() == 'windows'
  msys2_prefixes = ['clang64', 'ucrt64', 'mingw64', 'mingw32', 'clangarm64']
  cc_program = find_program(cc.cmd_array()[0], required: false)
  if cc_program.found()
    cc_path = cc_program.full_path().replace('\\', '/').to_lower()
    foreach prefix_name : msys2_prefixes
      if ('/' + prefix_name + '/bin/') in cc_path
        msys2_prefix_name = prefix_name
      endif
    endforeach
  endif

  pkg_config_program = find_program('pkg-config', required: false)
  if pkg_config_program.found()
    pkg_config_path = pkg_config_program.full_path().replace('\\', '/').to_lower()
    pkg_config_prefix_name = ''
    foreach prefix_name : msys2_prefixes
      if ('/' + prefix_name + '/bin/') in pkg_config_path
        pkg_config_prefix_name = prefix_name
      endif
    endforeach

    if msys2_prefix_name != '' and pkg_config_prefix_name != '' and msys2_prefix_name != pkg_config_prefix_name
      error('MSYS2 toolchain mismatch: C compiler is from @0@ but pkg-config is from @1@. Configure from the matching MSYS2 shell or put C:/msys64/@0@/bin first on PATH.'.format(msys2_prefix_name, pkg_config_prefix_name))
    endif
  endif
endif

# Dependencies
vulkan_dep = dependency('vulkan', required: true)

# Pin to pkg-config: CMake's find_package probes prefixes adjacent to PATH, so
# under WSL (Windows MSYS2 dirs on PATH) it returns Windows .dll.a/.a libs that
# cannot link on Linux. pkg-config does not search those prefixes. Fallbacks below
# still cover systems without a system install.
assimp_dep = dependency('assimp', method: 'pkg-config', fallback: ['assimp', 'assimp_dep'])
cglm_dep = dependency('cglm', method: 'pkg-config', fallback: ['cglm', 'cglm_dep'])
vips_dep = dependency('vips', fallback: ['vips', 'vips_dep'])

bundled_libsixel = get_option('bundled_libsixel')
if bundled_libsixel == 'enabled' or (bundled_libsixel == 'auto' and host_machine.system() == 'windows')
  sixel_dep = subproject('libsixel').get_variable('sixel_dep')
elif bundled_libsixel == 'disabled'
  sixel_dep = dependency('libsixel', required: true)
else
  sixel_dep = dependency('libsixel', fallback: ['libsixel', 'sixel_dep'])
endif

threads_dep = dependency('threads')
m_dep = cc.find_library('m', required: false)
rt_dep = cc.find_library('rt', required: false)

dcat_deps = [
  vulkan_dep,
  assimp_dep,
  cglm_dep,
  vips_dep,
  sixel_dep,
  threads_dep
]

if m_dep.found()
  dcat_deps += m_dep
endif
if rt_dep.found()
  dcat_deps += rt_dep
endif

add_project_arguments('-DCGLM_FORCE_DEPTH_ZERO_TO_ONE', language: ['c', 'cpp'])

shader_install_dir = get_option('prefix') / get_option('datadir') / 'dcat' / 'shaders'
add_project_arguments('-DSHADER_INSTALL_DIR="' + shader_install_dir + '"', language: ['c', 'cpp'])

if get_option('buildtype') == 'debug'
  add_project_arguments('-DDEBUG_BUILD=1', '-DVK_ENABLE_VALIDATION=1', language: ['c', 'cpp'])
endif

if cc.get_id() == 'msvc'
  if cc.has_argument('/experimental:c11atomics')
    add_project_arguments('/experimental:c11atomics', language: 'c')
  endif
else
  if get_option('profile')
    add_project_arguments('-pg', language: ['c', 'cpp'])
    add_project_link_arguments('-pg', language: ['c', 'cpp'])
  endif
endif

subdir('shaders')

dcat_core_sources = [
  'src/platform/io.c',
  'src/platform/path.c',
  'src/core/app.c',
  'src/core/args.c',
  'src/core/signals.c',
  'src/graphics/camera.c',
  'src/graphics/model.c',
  'src/graphics/animation.c',
  'src/graphics/texture.c',
  'src/graphics/texture_loader.c',
  'src/graphics/skydome.c',
  'src/renderer/vulkan_renderer.c',
  'src/renderer/vk_device.c',
  'src/renderer/vk_memory.c',
  'src/renderer/vk_shader.c',
  'src/renderer/vk_pipeline.c',
  'src/renderer/vk_resources.c',
  'src/renderer/vk_upload.c',
  'src/input/input_handler_common.c',
  'src/terminal/terminal.c',
  'src/terminal/session.c',
  'src/terminal/driver_factory.c',
  'src/terminal/char_render.c',
  'src/terminal/truecolor_characters.c',
  'src/terminal/palette_characters.c',
  'src/terminal/block_characters.c',
  'src/terminal/sixel.c',
  'src/terminal/kitty.c',
  'src/terminal/kitty_shm.c'
]

if host_machine.system() == 'windows'
  dcat_core_sources += 'src/input/input_handler_win.c'
else
  dcat_core_sources += 'src/input/input_handler_posix.c'
endif

# The bundled assimp builds as a static C++ library, so the final link of every
# C executable using dcat_core must pull in the C++ runtime. Windows links it
# automatically; on Linux the C driver does not, so add it there only (avoids
# breaking MSYS2/libc++).
dcat_link_args = []
if host_machine.system() != 'windows'
  dcat_link_args += '-lstdc++'
endif

dcat_inc = include_directories('src')

# Everything except main.c lives in a static library so the unit tests can link
# against the same objects as the installed executable.
dcat_core_lib = static_library('dcat_core',
  dcat_core_sources,
  include_directories: dcat_inc,
  dependencies: dcat_deps,
)

dcat_core_dep = declare_dependency(
  link_with: dcat_core_lib,
  include_directories: dcat_inc,
  dependencies: dcat_deps,
  link_args: dcat_link_args,
)

dcat_exe = executable('dcat',
  'src/main.c',
  dependencies: dcat_core_dep,
  install: true
)

# Unity test framework via an explicit subproject() call: CI's
# --wrap-mode=nofallback only blocks dependency() fallbacks, not this.
unity_subp = subproject('unity',
  required: get_option('tests'),
  default_options: ['default_library=static'],
)
if unity_subp.found()
  unity_dep = unity_subp.get_variable('unity_dep')
  subdir('tests')
endif

