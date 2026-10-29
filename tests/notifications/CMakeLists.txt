add_executable(gentest_notification_core_tests core.cpp)
target_link_libraries(gentest_notification_core_tests PRIVATE gentest::notifications Threads::Threads)
target_compile_features(gentest_notification_core_tests PRIVATE cxx_std_20)

# TSan defines global new/delete. Count calls through the ELF linker wrapper
# instead of replacing the sanitizer's allocator or disabling the allocation check.
gentest_detect_thread_sanitizer(_gentest_notification_tsan)
if(_gentest_notification_tsan AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_gentest_notification_new_symbol _Znwm)
    else()
        set(_gentest_notification_new_symbol _Znwj)
    endif()
    target_compile_definitions(gentest_notification_core_tests PRIVATE
        "GENTEST_TEST_REAL_NEW=__real_${_gentest_notification_new_symbol}"
        "GENTEST_TEST_WRAP_NEW=__wrap_${_gentest_notification_new_symbol}")
    target_link_options(gentest_notification_core_tests PRIVATE "LINKER:--wrap=${_gentest_notification_new_symbol}")
    unset(_gentest_notification_new_symbol)
endif()
unset(_gentest_notification_tsan)

add_test(NAME notification_core_contract COMMAND gentest_notification_core_tests)
set_tests_properties(notification_core_contract PROPERTIES LABELS "notifications;public-api")

add_executable(gentest_notification_framework_tests framework.cpp)
target_link_libraries(gentest_notification_framework_tests PRIVATE gentest::gentest_runtime gentest::gentest)
add_test(NAME notification_framework_contract COMMAND gentest_notification_framework_tests)
set_tests_properties(notification_framework_contract PROPERTIES TIMEOUT 30 LABELS "notifications;public-api")

if(TARGET gentest_notifications_linux)
    add_executable(gentest_notification_linux_tests linux.cpp)
    target_link_libraries(gentest_notification_linux_tests PRIVATE gentest::notifications_linux)
    add_test(NAME notification_linux_contract COMMAND gentest_notification_linux_tests)
    set_tests_properties(notification_linux_contract PROPERTIES TIMEOUT 45 LABELS notifications)
endif()
