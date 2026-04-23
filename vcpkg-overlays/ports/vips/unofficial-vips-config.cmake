if(NOT TARGET unofficial::vips::vips)
    add_library(unofficial::vips::vips SHARED IMPORTED)
    set_target_properties(unofficial::vips::vips PROPERTIES
        IMPORTED_IMPLIB "${CMAKE_CURRENT_LIST_DIR}/../../lib/libvips.lib"
        IMPORTED_IMPLIB_DEBUG "${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/libvips.lib"
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../bin/libvips-42.dll"
        IMPORTED_LOCATION_DEBUG "${CMAKE_CURRENT_LIST_DIR}/../../debug/bin/libvips-42.dll"
        INTERFACE_INCLUDE_DIRECTORIES
            "${CMAKE_CURRENT_LIST_DIR}/../../include;${CMAKE_CURRENT_LIST_DIR}/../../include/glib-2.0;${CMAKE_CURRENT_LIST_DIR}/../../lib/glib-2.0/include"
        INTERFACE_LINK_LIBRARIES
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/libglib-2.0.lib;${CMAKE_CURRENT_LIST_DIR}/../../lib/libgobject-2.0.lib;${CMAKE_CURRENT_LIST_DIR}/../../lib/libgio-2.0.lib;${CMAKE_CURRENT_LIST_DIR}/../../lib/libgmodule-2.0.lib"
    )
endif()
