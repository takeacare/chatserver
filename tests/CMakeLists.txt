find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
    add_test(
        NAME integration_chat
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_chat.py -v
    )
    set_tests_properties(integration_chat PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        TIMEOUT 300
    )
else()
    message(STATUS "Python3 not found; integration test disabled")
endif()
