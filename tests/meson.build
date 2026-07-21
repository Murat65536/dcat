# UBSan findings only print by default; halt_on_error makes them fail `meson test` (no-op in non-sanitized builds; ASan already halts on its own).
add_test_setup('default',
  is_default: true,
  env: ['UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1'],
)

foreach name : ['args', 'chafa_driver', 'input_handler']
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
