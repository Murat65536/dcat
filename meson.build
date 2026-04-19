project(
  'dcat',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c17',
    'warning_level=3',
  ],
)

cc = meson.get_compiler('c')
buildtype = get_option('buildtype')
is_windows = host_machine.system() == 'windows'
is_msvc = cc.get_id() == 'msvc'

if buildtype == 'release'
  release_args = ['-march=native', '-ffast-math', '-funroll-loops']
  foreach arg : release_args
    if cc.has_argument(arg)
      add_project_arguments(arg, language: 'c')
    endif
  endforeach
endif

if is_msvc
  # MSVC requires this flag for <stdatomic.h> support.
  c11_atomics_flag = '/experimental:c11atomics'
  if cc.has_argument(c11_atomics_flag)
    add_project_arguments(c11_atomics_flag, language: 'c')
  endif
endif

if get_option('profile')
  if cc.has_argument('-pg')
    add_project_arguments('-pg', language: 'c')
  endif
  if cc.has_link_argument('-pg')
    add_project_link_arguments('-pg', language: 'c')
  endif
endif

target_override_options = []
if buildtype == 'debug' and get_option('use_sanitizers') and not is_msvc
  target_override_options += ['b_sanitize=address,undefined']
endif

vulkan_dep = dependency('vulkan', method: 'pkg-config', required: false)
if not vulkan_dep.found()
  vulkan_dep = dependency('vulkan-loader', method: 'pkg-config', required: false)
endif
if not vulkan_dep.found()
  if is_windows
    vulkan_dep = dependency('vulkan', method: 'system', required: true)
  else
    vulkan_dep = dependency('vulkan', required: true)
  endif
endif

assimp_dep = dependency('assimp', method: 'pkg-config', required: false)
if not assimp_dep.found()
  if is_windows
    assimp_dep = dependency('assimp', method: 'cmake', required: true)
  else
    assimp_dep = dependency('assimp', required: true)
  endif
endif

cglm_dep = dependency('cglm', method: 'pkg-config', required: false)
if not cglm_dep.found()
  if is_windows
    cglm_dep = dependency('cglm', method: 'system', required: true)
  else
    cglm_dep = dependency('cglm', required: true)
  endif
endif

vips_dep = dependency('vips', method: 'pkg-config', required: false)
if not vips_dep.found()
  if is_windows
    vips_dep = dependency('vips', method: 'system', required: true)
  else
    vips_dep = dependency('vips', required: true)
  endif
endif
threads_dep = dependency('threads', required: true)

deps = [
  vulkan_dep,
  assimp_dep,
  cglm_dep,
  vips_dep,
  threads_dep,
]

if not is_windows
  sixel_dep = dependency('libsixel', method: 'pkg-config', required: false)
  if not sixel_dep.found()
    sixel_dep = dependency('libsixel', required: true)
  endif
  deps += [sixel_dep]

  m_dep = cc.find_library('m', required: true)
  rt_dep = cc.find_library('rt', required: true)
  deps += [m_dep, rt_dep]
endif

inc_src = include_directories('src')

debug_defs = []
if buildtype == 'debug'
  debug_defs += [
    'DEBUG_BUILD=1',
    'VK_ENABLE_VALIDATION=1',
  ]
endif

if buildtype == 'release' or get_option('profile')
  debug_defs += ['NDEBUG=1']
endif

target_c_args = []
foreach d : debug_defs
  target_c_args += ['-D' + d]
endforeach

# Vulkan depth clip space is [0, 1]
target_c_args += ['-DCGLM_FORCE_DEPTH_ZERO_TO_ONE']

sources = files(
  'src/main.c',
  'src/core/args.c',
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
  'src/terminal/truecolor_characters.c',
  'src/terminal/palette_characters.c',
  'src/terminal/block_characters.c',
  'src/terminal/sixel.c',
  'src/terminal/kitty.c',
  'src/terminal/kitty_shm.c',
)

shader_sources = [
  'shader.vert',
  'shader.frag',
  'skydome.vert',
  'skydome.frag',
]

shader_input_files = files(
  'shaders/' + shader_sources[0],
  'shaders/' + shader_sources[1],
  'shaders/' + shader_sources[2],
  'shaders/' + shader_sources[3],
)

glslc = find_program('glslc', required: true)
shader_install_dir = join_paths(get_option('datadir'), 'dcat', 'shaders')
shader_args = []
if buildtype == 'debug'
  shader_args += ['-g']
endif

foreach idx : range(0, shader_sources.length())
  shader_name = shader_sources[idx]
  shader_input = shader_input_files[idx]

  custom_target(
    'compile_' + shader_name,
    input: shader_input,
    output: shader_name + '.spv',
    command: [glslc] + shader_args + ['@INPUT@', '-o', '@OUTPUT@'],
    build_by_default: true,
    install: true,
    install_dir: shader_install_dir,
  )
endforeach

dcat = executable(
  'dcat',
  sources,
  include_directories: [inc_src],
  dependencies: deps,
  c_args: target_c_args,
  override_options: target_override_options,
  install: true,
)
