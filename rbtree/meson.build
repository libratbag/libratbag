src_rbtree = [
  'src/rbtree/shared-rbtree.h',
  'src/rbtree/shared-rbtree.c',
]

inc_rbtree = include_directories('src')

lib_rbtree = static_library('rbtree',
  src_rbtree,
  dependencies : deps_libshared,
  include_directories : inc_rbtree
)

dep_rbtree = declare_dependency(
  link_with : lib_rbtree,
  include_directories : inc_rbtree
)
