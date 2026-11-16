# Settings specific to each CPack generator (read at packaging time).
if(CPACK_GENERATOR MATCHES "ZIP|TGZ")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
else()
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
endif()
