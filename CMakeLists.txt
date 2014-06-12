cmake_minimum_required(VERSION 2.8)
set(CMAKE_C_FLAGS_DEBUG "-gdwarf-4 -g3")
set(CMAKE_C_FLAGS_RELEASE "-march=native -O3 -flto -fuse-linker-plugin -DNDEBUG ")
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
	set(CMAKE_AR "gcc-ar")
	set(CMAKE_RANLIB "gcc-ranlib")
endif()

if (NDEBUG)
	set(CMAKE_C_FLAGS_DEBUG "-g -DNDEBUG")
endif()


add_executable(netsim
	sim.c
	flow.c
	range.c
	user.c
	main.c
	record.c
)
target_link_libraries(netsim dl)

add_subdirectory(modules)
add_subdirectory(analyzer)

add_executable(skiplist_test
	skiplist_test.c
)
