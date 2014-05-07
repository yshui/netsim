add_executable(netsim
	connect.c
	sim.c
	flow.c
	range.c
	user.c
	main.c
	record.c
)
target_link_libraries(netsim dl)

add_subdirectory(modules)
