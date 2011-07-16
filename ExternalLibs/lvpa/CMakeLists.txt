cmake_minimum_required(VERSION 2.6)
project(lvpa)

option(USE_ZLIB "Add zlib/deflate support" TRUE)
option(USE_LZMA "Add LZMA support" TRUE)
option(USE_LZO "Add LZO support" TRUE)
option(USE_LZF "Add LZF support" TRUE)
option(STATIC_LIB "Link as static library without runtime dependencies (Note: To get rid of this setting with MSVC, the cmake cache must be cleared)" FALSE)

option(BUILD_TTVFS_BINDINGS "Build bindings for ttvfs?" FALSE)
option(BUILD_LVPAK "Build lvpak commandline utility?" TRUE)
option(BUILD_TESTPROG "Build test program?" TRUE)

option(USE_INTERNAL_ZLIB "Use included zlib" TRUE)
option(USE_INTERNAL_LZMA "Use included LZMA libary" TRUE)
option(USE_INTERNAL_LZO "Use included LZO library" TRUE)
option(USE_INTERNAL_LZF "Use included LZF library" TRUE)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

if(STATIC_LIB)
    if(MSVC)
        foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
            if(${flag_var} MATCHES "/MD")
                string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
            endif(${flag_var} MATCHES "/MD")
            if(${flag_var} MATCHES "/MDd")
                string(REGEX REPLACE "/MDd" "/MTd" ${flag_var} "${${flag_var}}")
            endif(${flag_var} MATCHES "/MDd")
        endforeach()
        
        # no idea why, but MSVC will prduce linker errors otherwise
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /NODEFAULTLIB")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /NODEFAULTLIB:msvcrt.lib,msvcrtd.lib") # not sure if this is correct
        set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:msvcrt.lib,msvcrtd.lib")
    else()
        add_definitions(-static)
    endif()
endif()

# run-time type info (RTTI) not required
if(MSVC)
    add_definitions("/GR-")
endif()


set(lvpa_TARGET_LIBS "")

if(USE_ZLIB)
    add_definitions(-DLVPA_SUPPORT_ZLIB)
    if(USE_INTERNAL_ZLIB)
        add_subdirectory(zlib)
    endif()
    list(APPEND lvpa_TARGET_LIBS zlib)
endif()

if(USE_LZO)
    add_definitions(-DLVPA_SUPPORT_LZO)
    if(USE_INTERNAL_LZO)
        add_subdirectory(lzo)
    endif()
    list(APPEND lvpa_TARGET_LIBS lzo)
endif()

if(USE_LZMA)
    add_definitions(-DLVPA_SUPPORT_LZMA)
    if(USE_INTERNAL_LZMA)
        add_subdirectory(lzma)
    endif()
    list(APPEND lvpa_TARGET_LIBS lzma)
endif()

if(USE_LZF)
    add_definitions(-DLVPA_SUPPORT_LZF)
    if(USE_INTERNAL_LZF)
        add_subdirectory(lzf)
    endif()
    list(APPEND lvpa_TARGET_LIBS lzf)
endif()


add_subdirectory(include)
add_subdirectory(lvpa)


if(BUILD_LVPAK)
    add_subdirectory(lvpak)
endif()

if(BUILD_TTVFS_BINDINGS)
    add_definitions(-DLVPA_SUPPORT_TTVFS)
    add_subdirectory(lvpa_ttvfs)
endif()

if(BUILD_TESTPROG)
    add_subdirectory(tests)
endif()
