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
  COMMAND dolce-make-bootparam app_memsize 0x4000 attribute 0x03 directory_max_level 0x12 ${CMAKE_CURRENT_BINARY_DIR}/boot_param2.bin
)

project(appbgservice)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O3 -fno-builtin")

add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}
  appbgservice/main.c
  appbgservice/ftpvita.c
)

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
  SceRtc_stub
  SceFiber_stub
  SceNotificationUtil_stub
  SceIncomingDialog_stub
  SceLibc_stub
)

set(DOLCE_ELF_CREATE_FLAGS "${DOLCE_ELF_CREATE_FLAGS} -h 14680064")

set_target_properties(${PROJECT_NAME}
  PROPERTIES LINK_FLAGS "-nostdlib"
)

dolce_create_self(eboot2.bin
  ${PROJECT_NAME}
  UNSAFE
  BOOT_PARAM ${CMAKE_CURRENT_BINARY_DIR}/boot_param2.bin
)

project(appmain)
  
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O3 -fno-builtin")
  
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}
  appmain/main.c
)

link_directories(${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(${PROJECT_NAME}
  SceLibKernel_stub
  SceSysmodule_stub
  SceAppMgr_stub
  SceBgAppUtil_stub
)

set(DOLCE_ELF_CREATE_FLAGS "${DOLCE_ELF_CREATE_FLAGS} -h 262144")

set_target_properties(${PROJECT_NAME}
  PROPERTIES LINK_FLAGS "-nostdlib"
)

dolce_create_self(eboot.bin
  ${PROJECT_NAME}
  UNSAFE
  UNCOMPRESSED
  BOOT_PARAM ${CMAKE_CURRENT_BINARY_DIR}/boot_param.bin
)

set(DOLCE_MKSFOEX_FLAGS "${DOLCE_MKSFOEX_FLAGS} -d ATTRIBUTE=16814080 -d SAVEDATA_MAX_SIZE=1024 -s CATEGORY=gdc -s PUBTOOLINFO=has_bg=1")

dolce_create_vpk(${PROJECT_NAME}.vpk GRVA00001 eboot.bin
  VERSION 03.10
  NAME "BGFTP"
  CONTENT_ID_LABEL "BGFTPSERVER00000"
  FILE ${CMAKE_CURRENT_BINARY_DIR}/eboot2.bin eboot2.bin
  FILE common_data/sce_sys/param2.sfo sce_sys/param2.sfo
  FILE common_data/sce_sys/icon0.png sce_sys/icon0.png
  FILE common_data/sce_sys/pic0.png sce_sys/pic0.png
  FILE common_data/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
  FILE common_data/sce_sys/livearea/contents/gate.png sce_sys/livearea/contents/gate.png
  FILE common_data/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
