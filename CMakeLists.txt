cmake_minimum_required(VERSION 2.8)
set(CMAKE_C_FLAGS_DEBUG "-gdwarf-4 -g3")

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
