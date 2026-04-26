project('dcat', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'c_std=c17',
    'cpp_std=c++17',
    'warning_level=3',
    'werror=false',
  ]
)

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')

# Dependencies
vulkan_dep = dependency('vulkan', required: true)

assimp_dep = dependency('assimp', fallback: ['assimp', 'assimp_dep'])
cglm_dep = dependency('cglm', fallback: ['cglm', 'cglm_dep'])
vips_dep = dependency('vips', fallback: ['vips', 'vips_dep'])
sixel_dep = dependency('libsixel', fallback: ['libsixel', 'sixel_dep'])

poly2tri_dep = cc.find_library('poly2tri', required: false)
if not poly2tri_dep.found()
  poly2tri_dep = dependency('poly2tri', fallback: ['poly2tri', 'poly2tri_dep'], required: false)
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
if poly2tri_dep.found()
  dcat_deps += poly2tri_dep
endif

add_project_arguments('-DCGLM_FORCE_DEPTH_ZERO_TO_ONE', language: ['c', 'cpp'])

if get_option('buildtype') == 'debug'
  add_project_arguments('-DDEBUG_BUILD=1', '-DVK_ENABLE_VALIDATION=1', language: ['c', 'cpp'])
elif get_option('buildtype') == 'release'
  add_project_arguments('-DNDEBUG=1', language: ['c', 'cpp'])
endif

if cc.get_id() == 'msvc'
  if cc.has_argument('/experimental:c11atomics')
    add_project_arguments('/experimental:c11atomics', language: 'c')
  endif
else
  if get_option('buildtype') == 'release'
    foreach arg : ['-march=native', '-ffast-math', '-funroll-loops']
      if cc.has_argument(arg)
        add_project_arguments(arg, language: ['c', 'cpp'])
      endif
    endforeach
  endif
  if get_option('profile')
    add_project_arguments('-pg', language: ['c', 'cpp'])
    add_project_link_arguments('-pg', language: ['c', 'cpp'])
  endif
endif

subdir('shaders')

dcat_sources = [
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
  'src/terminal/kitty_shm.c'
]

executable('dcat',
  dcat_sources,
  include_directories: include_directories('src'),
  dependencies: dcat_deps,
  install: true
)
