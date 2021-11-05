cmake_minimum_required(VERSION 3.15)

if (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(pedeps VERSION 0.1.0)

include(CTest)
enable_testing()

add_library(pedeps SHARED
    lib/pedeps.c
    lib/pestructs.c
)
target_include_directories(pedeps PUBLIC lib)
target_compile_definitions(pedeps PRIVATE BUILD_PEDEPS_DLL)

add_library(pedeps_static STATIC
    lib/pedeps.c
    lib/pestructs.c
)
target_include_directories(pedeps_static PUBLIC lib)
target_compile_definitions(pedeps_static PUBLIC DLL_EXPORT_PEDEPS=)

if (WIN32)
    add_executable(copypedeps src/copypedeps.cpp)
    target_compile_options(copypedeps PRIVATE -wd4993)
    target_include_directories(copypedeps PRIVATE src)
    target_link_libraries(copypedeps pedeps_static Shlwapi)
endif()

add_executable(listpedeps src/listpedeps.c)
target_include_directories(listpedeps PRIVATE src)
target_link_libraries(listpedeps pedeps_static)

add_executable(listperesources src/listperesources.c)
target_include_directories(listperesources PRIVATE src)
target_link_libraries(listperesources pedeps_static)


set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
