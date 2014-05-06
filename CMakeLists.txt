add_executable(netsim
	connect.c
	sim.c
	flow.c
	range.c
	user.c
	main.c
	statistic.c
)
target_link_libraries(netsim dl)

