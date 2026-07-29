slangc = find_program('slangc', 'slangc.exe', required: true)

shaders = [
  ['shader.vert', 'vertex'],
  ['shader.frag', 'fragment'],
  ['skydome.vert', 'vertex'],
  ['skydome.frag', 'fragment'],
]

slang_args = ['-matrix-layout-column-major', '-profile', 'spirv_1_3']

embedded_shaders = []

foreach s : shaders
  embedded_shaders += custom_target(
    s[0].underscorify() + '_spv',
    input: s[0] + '.slang',
    output: s[0] + '.spv.h',
    command: [
      slangc, '@INPUT@',
      '-target', 'spirv',
      '-entry', 'main',
      '-stage', s[1],
      slang_args,
      '-source-embed-style', 'u32',
      '-source-embed-name', s[0].underscorify() + '_spv',
      '-o', '@OUTPUT@'
    ],
    depend_files: files('common.slang'),
    build_by_default: true
  )
endforeach
