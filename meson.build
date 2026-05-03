project('dcat', ['c', 'cpp'],
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
cxx = meson.get_compiler('cpp')

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

assimp_dep = dependency('assimp', fallback: ['assimp', 'assimp_dep'])
cglm_dep = dependency('cglm', fallback: ['cglm', 'cglm_dep'])
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

dcat_sources = [
  'src/main.c',
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
  'src/input/input_handler.c',
  'src/terminal/terminal.c',
  'src/terminal/session.c',
  'src/terminal/driver_factory.c',
  'src/terminal/truecolor_characters.c',
  'src/terminal/palette_characters.c',
  'src/terminal/block_characters.c',
  'src/terminal/sixel.c',
  'src/terminal/kitty.c',
  'src/terminal/kitty_shm.c'
]

dcat_exe = executable('dcat',
  dcat_sources,
  include_directories: include_directories('src'),
  dependencies: dcat_deps,
  install: true
)

python = import('python').find_installation('python3', required: false)
if host_machine.system() == 'windows'
  msys2_python = find_program('python3', required: false)
  if msys2_prefix_name != ''
    msys2_python = find_program('C:/msys64' / msys2_prefix_name / 'bin' / 'python3.exe', required: false)
  endif
  if msys2_python.found()
    python = msys2_python
  endif
endif
if not python.found()
  python = find_program('python', required: false)
endif

if python.found()
  test('pytest_cli',
    python,
    args: ['-m', 'pytest', meson.current_source_dir() / 'tests'],
    env: ['DCAT_EXE=' + dcat_exe.full_path()],
    depends: dcat_exe
  )
endif
