slangc = find_program('slangc', required: true)

shaders = [
  ['shader.vert', 'vertex'],
  ['shader.frag', 'fragment'],
  ['skydome.vert', 'vertex'],
  ['skydome.frag', 'fragment'],
]

slang_args = ['-matrix-layout-column-major']

foreach s : shaders
  custom_target(
    s[0].underscorify() + '_spv',
    input: s[0] + '.slang',
    output: s[0] + '.spv',
    command: [slangc, '@INPUT@', '-target', 'spirv', '-entry', 'main',
              '-stage', s[1], slang_args, '-o', '@OUTPUT@'],
    depend_files: files('common.slang'),
    install: true,
    install_dir: get_option('datadir') / 'dcat' / 'shaders',
    build_by_default: true
  )
endforeach
