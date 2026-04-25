glslc = find_program('glslc', required: true)

shader_sources = [
  'shader.vert',
  'shader.frag',
  'skydome.vert',
  'skydome.frag'
]

glslc_args = []
if get_option('buildtype') == 'debug'
  glslc_args += '-g'
endif

foreach s : shader_sources
  custom_target(
    s.underscorify() + '_spv',
    input: s,
    output: s + '.spv',
    command: [glslc, glslc_args, '@INPUT@', '-o', '@OUTPUT@'],
    install: true,
    install_dir: get_option('datadir') / 'dcat' / 'shaders',
    build_by_default: true
  )
endforeach
