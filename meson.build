project(
  'dcat',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=gnu2x',
    'warning_level=3',
  ],
)

cc = meson.get_compiler('c')
buildtype = get_option('buildtype')

if buildtype == 'release'
  add_project_arguments(
    '-march=native',
    '-ffast-math',
    '-funroll-loops',
    language: 'c',
  )
endif

if get_option('profile')
  add_project_arguments('-pg', language: 'c')
  add_project_link_arguments('-pg', language: 'c')
endif

target_override_options = []
if buildtype == 'debug' and get_option('use_sanitizers')
  target_override_options += ['b_sanitize=address,undefined']
endif

vulkan_dep = dependency('vulkan', required: true)
assimp_dep = dependency('assimp', required: true)
cglm_dep = dependency('cglm', required: true)
sixel_dep = dependency('libsixel', required: true)
vips_dep = dependency('vips', required: true)
threads_dep = dependency('threads', required: true)
m_dep = cc.find_library('m', required: true)
rt_dep = cc.find_library('rt', required: true)

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
  'src/input/input_device.c',
  'src/input/input_handler.c',
  'src/terminal/terminal.c',
  'src/terminal/truecolor_characters.c',
  'src/terminal/palette_characters.c',
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
  dependencies: [
    vulkan_dep,
    assimp_dep,
    cglm_dep,
    sixel_dep,
    vips_dep,
    threads_dep,
    m_dep,
    rt_dep,
  ],
  c_args: target_c_args,
  override_options: target_override_options,
  install: true,
)
