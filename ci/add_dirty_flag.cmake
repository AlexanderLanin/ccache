include(../cmake/CCacheVersion.cmake)
file(WRITE ../cmake/CCacheVersion.cmake "set(CCACHE_VER ${CCACHE_VER}.dirty)\n")
