include_directories("../")
macro(new_module n)
	add_library(${n} MODULE "${n}.c")
	set_target_properties(${n} PROPERTIES PREFIX "")
endmacro()

new_module("test00")
new_module("test01")
new_module("test02")
new_module("test03")
new_module("test04")
new_module("test05")

add_library(p2p_common STATIC client_behaviour.c resource.c gaussian.c cloud_behaviour.c)
set_target_properties(p2p_common PROPERTIES POSITION_INDEPENDENT_CODE true)
target_link_libraries(p2p_common m)

add_library(tc1 SHARED test_client1.c)
target_link_libraries(tc1 p2p_common)
set_target_properties(tc1 PROPERTIES PREFIX "")

add_library(tc2 SHARED test_client2.c)
target_link_libraries(tc2 p2p_common)
set_target_properties(tc2 PROPERTIES PREFIX "")

add_library(tc3 SHARED test_client3.c)
target_link_libraries(tc3 p2p_common)
set_target_properties(tc3 PROPERTIES PREFIX "")

add_library(p2p SHARED p2p.c)
target_link_libraries(p2p p2p_common)
set_target_properties(p2p PROPERTIES PREFIX "")
