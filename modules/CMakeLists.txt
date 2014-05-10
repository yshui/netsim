include_directories("../")
add_library(test SHARED test.c)
set_target_properties(test PROPERTIES PREFIX "")
