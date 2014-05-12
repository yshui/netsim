include_directories("../")
add_library(test00 SHARED test00.c)
set_target_properties(test00 PROPERTIES PREFIX "")

add_library(test01 SHARED test01.c)
set_target_properties(test01 PROPERTIES PREFIX "")

add_library(test02 SHARED test02.c)
set_target_properties(test02 PROPERTIES PREFIX "")
