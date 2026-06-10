foreach name : ['args', 'char_render']
  test(
    name,
    executable('test_' + name, 'test_' + name + '.c', dependencies: [dcat_core_dep, unity_dep]),
    suite: 'unit'
  )
endforeach
