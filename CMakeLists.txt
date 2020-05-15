cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{DOLCESDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DOLCESDK}/share/dolce.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define DOLCESDK to point to your SDK path!")
  endif()
endif()

include("$ENV{DOLCESDK}/share/dolce.cmake" REQUIRED)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/boot_param.bin
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/boot_param2.bin
  COMMAND dolce-make-bootparam app_memsize 0x1000 ${CMAKE_CURRENT_BINARY_DIR}/boot_param.bin
  COMMAND dolce-make-bootparam app_memsize 0x4000 attribute 0x03 ${CMAKE_CURRENT_BINARY_DIR}/boot_param2.bin
)

project(appbgservice)
dolce_gen_libs(SceNotificationUtil_stubs
  SceNotificationUtil.yml
  LIB SceNotificationUtil_stub_weak)
dolce_gen_libs(SceIncomingDialog_stubs
  SceIncomingDialog.yml
  LIB SceIncomingDialog_stub_weak)
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}
  appbgservice/main.c
  appbgservice/ftpvita.c
)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wall")
link_directories(${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PROJECT_NAME}
  SceNetCtl_stub
  SceNet_stub
  SceLibKernel_stub
  SceRegistryMgr_stub
  SceIofilemgr_stub
  SceSysmodule_stub
  SceKernelThreadMgr_stub
  SceProcessmgr_stub
  SceSysmem_stub
  SceAppMgr_stub
  SceAppUtil_stub 
  ScePower_stub
  SceNotificationUtil_stub_weak
  SceIncomingDialog_stub_weak
)
dolce_create_self(eboot2.bin
  ${PROJECT_NAME}
  UNSAFE
  BOOT_PARAM ${CMAKE_CURRENT_BINARY_DIR}/boot_param2.bin
)

project(appmain)
dolce_gen_libs(SceBgAppUtil_stubs
  SceBgAppUtil.yml
  LIB SceBgAppUtil_stub_weak)
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/appmain/main.c)
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wall")
link_directories(${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PROJECT_NAME}
  SceLibKernel_stub
  SceIofilemgr_stub
  SceCtrl_stub
  SceSysmodule_stub
  SceAppMgr_stub
  SceLibKernel_stub_weak
  SceBgAppUtil_stub_weak
  SceGxm_stub_weak 
)
dolce_create_self(eboot.bin
  ${PROJECT_NAME}
  UNSAFE
  BOOT_PARAM ${CMAKE_CURRENT_BINARY_DIR}/boot_param.bin
)

set(DOLCE_MKSFOEX_FLAGS "${DOLCE_MKSFOEX_FLAGS} -d ATTRIBUTE=16814080 -s CATEGORY=gdc -s PUBTOOLINFO=has_bg=1")

dolce_create_vpk(${PROJECT_NAME}.vpk BGFTP0010 eboot.bin
  VERSION 02.00
  NAME "BGFTP"
  FILE ${CMAKE_CURRENT_BINARY_DIR}/eboot2.bin eboot2.bin
  FILE common_data/sce_sys/param2.sfo sce_sys/param2.sfo
  FILE common_data/sce_sys/icon0.png sce_sys/icon0.png
  FILE common_data/sce_sys/pic0.png sce_sys/pic0.png
  FILE common_data/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
  FILE common_data/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
