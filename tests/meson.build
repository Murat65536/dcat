foreach name : ['args', 'char_render']
  test(
    name,
    executable('test_' + name, 'test_' + name + '.c', dependencies: [dcat_core_dep, unity_dep]),
    suite: 'unit'
  )
endforeach

# The model loader is exercised against the checked-in public-domain "Spot"
# fixtures. Pass their source-tree location so the test finds them regardless
# of build dir.
spot_dir = meson.current_source_dir() / 'fixtures' / 'spot'
test(
  'model',
  executable(
    'test_model',
    'test_model.c',
    c_args: ['-DSPOT_MODEL_DIR="' + spot_dir + '"'],
    dependencies: [dcat_core_dep, unity_dep],
  ),
  suite: 'unit',
)
