file(GLOB TEST_SOURCES
    LIST_DIRECTORIES FALSE
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    test-*.cc)
foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable("${TEST_NAME}" "${TEST_SOURCE}")
    target_compile_definitions(${TEST_NAME} PUBLIC BOOST_ALLOW_DEPRECATED_HEADERS=1)
    target_link_libraries(${TEST_NAME} PUBLIC Boost::system)
    target_link_libraries(${TEST_NAME} PUBLIC Boost::thread)
    target_link_libraries(${TEST_NAME} PUBLIC Boost::chrono)
    target_link_libraries(${TEST_NAME} PUBLIC GTest::GTest GTest::Main)
    add_test(
        NAME "${TEST_NAME}"
        COMMAND $<TARGET_FILE:${TEST_NAME}>)
endforeach()
