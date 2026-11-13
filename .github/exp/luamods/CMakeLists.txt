cmake_minimum_required(VERSION 3.20)
project(mudlet_luamods C)

set(MODSRC "" CACHE PATH "Directory holding the extracted module sources")
set(LUA_INC "" CACHE PATH "Lua 5.1 headers")
set(LUA_LIB "" CACHE FILEPATH "lua51 import library")

find_path(PCRE2_INC pcre2.h REQUIRED)
find_library(PCRE2_LIB NAMES pcre2-8 REQUIRED)
find_path(YAJL_INC yajl/yajl_parse.h REQUIRED)
find_library(YAJL_LIB NAMES yajl yajl_s REQUIRED)
find_path(ZIP_INC zip.h REQUIRED)
find_library(ZIP_LIB NAMES zip REQUIRED)
find_path(SQLITE_INC sqlite3.h REQUIRED)
find_library(SQLITE_LIB NAMES sqlite3 REQUIRED)

function(luamod target outname outdir)
  add_library(${target} SHARED ${ARGN})
  target_include_directories(${target} PRIVATE ${LUA_INC})
  target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(${target} PRIVATE ${LUA_LIB})
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME ${outname}
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/${outdir}")
endfunction()

luamod(m_lfs lfs "" ${MODSRC}/lfs/src/lfs.c)

luamod(m_utf8 lua-utf8 "" ${MODSRC}/luautf8/lutf8lib.c)

luamod(m_yajl yajl "" ${MODSRC}/lua-yajl/lua_yajl.c)
target_include_directories(m_yajl PRIVATE ${YAJL_INC})
target_link_libraries(m_yajl PRIVATE ${YAJL_LIB})

luamod(m_zip zip "brimworks" ${MODSRC}/lua-zip/lua_zip.c)
target_include_directories(m_zip PRIVATE ${ZIP_INC})
target_link_libraries(m_zip PRIVATE ${ZIP_LIB})

luamod(m_rex rex_pcre2 "" ${MODSRC}/lrexlib/src/common.c ${MODSRC}/lrexlib/src/pcre2/lpcre2.c ${MODSRC}/lrexlib/src/pcre2/lpcre2_f.c)
target_include_directories(m_rex PRIVATE ${PCRE2_INC})
target_compile_definitions(m_rex PRIVATE VERSION="2.9.2" PCRE2_CODE_UNIT_WIDTH=8)
target_link_libraries(m_rex PRIVATE ${PCRE2_LIB})

luamod(m_luasql sqlite3 "luasql" ${MODSRC}/luasql/src/luasql.c ${MODSRC}/luasql/src/ls_sqlite3.c)
target_include_directories(m_luasql PRIVATE ${SQLITE_INC})
target_link_libraries(m_luasql PRIVATE ${SQLITE_LIB})

file(GLOB LPEG_SRC ${MODSRC}/lpeg/lp*.c)
luamod(m_lpeg lpeg "" ${LPEG_SRC})

luamod(m_lsqlite3 lsqlite3 "" ${MODSRC}/lsqlite3/lsqlite3.c)
target_include_directories(m_lsqlite3 PRIVATE ${SQLITE_INC})
target_compile_definitions(m_lsqlite3 PRIVATE LSQLITE_VERSION="0.9.6")
target_link_libraries(m_lsqlite3 PRIVATE ${SQLITE_LIB})
