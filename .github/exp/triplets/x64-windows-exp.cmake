set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
# The yajl C library's DLL would be yajl.dll, the same name as the lua-yajl module
if(PORT STREQUAL "yajl")
  set(VCPKG_LIBRARY_LINKAGE static)
endif()
