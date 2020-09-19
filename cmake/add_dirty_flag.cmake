include(cmake/CcacheVersion.cmake)
file(WRITE cmake/CcacheVersion.cmake "set(CCACHE_VER ${CCACHE_VER}.dirty)\n")
