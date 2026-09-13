add_executable(gui_job_error_tests
    job_error_tests.cpp
)
target_include_directories(gui_job_error_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    $<TARGET_PROPERTY:libslic3r_gui,INCLUDE_DIRECTORIES>
)
target_compile_definitions(gui_job_error_tests PRIVATE
    $<TARGET_PROPERTY:libslic3r_gui,COMPILE_DEFINITIONS>
)
target_link_libraries(gui_job_error_tests PRIVATE libslic3r_gui)
set_property(TARGET gui_job_error_tests PROPERTY FOLDER "tests")
add_test(NAME gui_job_error_tests COMMAND gui_job_error_tests)
