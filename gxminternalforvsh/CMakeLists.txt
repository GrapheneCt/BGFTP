set(LIB "SceGxm_stub_weak")
set(LIB_FILE "lib${LIB}.a")
set(LIB_YAML "${CMAKE_CURRENT_SOURCE_DIR}/SceGxm.yml")
set(LIB_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/stubs")

add_custom_target("${LIB}.target"
  DEPENDS "${LIB_FILE}"
)

add_custom_command(
  OUTPUT "${LIB_FILE}"
  DEPENDS "${LIB_YAML}"
  COMMAND vita-libs-gen "${LIB_YAML}" "${LIB_BUILD_DIR}"
  COMMAND make -C "${LIB_BUILD_DIR}"
  COMMAND mv "${LIB_BUILD_DIR}/libSceGxm_stub_weak.a" "${LIB_FILE}"
)

add_library("${LIB}" STATIC IMPORTED GLOBAL)

add_dependencies("${LIB}" "${LIB}.target")

set_target_properties("${LIB}" PROPERTIES
  IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/${LIB_FILE}"
)
