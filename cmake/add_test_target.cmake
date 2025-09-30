# Modern testing utilities that work with CMake presets
# This replaces the need for ENABLE_X flags by relying on preset configuration

# Define directory property for libraries
define_property(
  DIRECTORY
  PROPERTY TEST_LIBRARIES
  BRIEF_DOCS "Libraries to be linked with test targets"
  FULL_DOCS "A directory property holding the list of libraries to be linked with test targets.")

# Function to set test libraries (replaces existing list)
macro(set_test_libraries)
  set_property(DIRECTORY PROPERTY TEST_LIBRARIES ${ARGN})
endmacro()

# Function to append test libraries (adds to existing list)
macro(append_test_libraries)
  set_property(
    DIRECTORY
    APPEND
    PROPERTY TEST_LIBRARIES ${ARGN})
endmacro()

#[=======================================================================[.rst:
TestingUtilities
----------------

Modern CMake testing utilities designed to work with CMake presets for
configuration-based testing with sanitizers and profiling.

This module provides functions to create and manage test targets using
CMake's preset system, eliminating the need for manual flag management.

Overview
^^^^^^^^

The module provides a modern approach to test configuration where build
settings (sanitizers, profiling, etc.) are controlled via CMake presets
rather than function parameters. This ensures consistency across the
build and makes CI/CD configuration simpler.

Functions
^^^^^^^^^

.. command:: add_test_target

  Create or update a test executable and register it with CTest::

    add_test_target(
      TARGET_NAME <name>
      [SOURCES <source1> [<source2> ...]]
      [INCLUDES <dir1> [<dir2> ...]]
      [LINK_LIBS <lib1> [<lib2> ...]]
      [TIMEOUT <seconds>]
      [WORKING_DIRECTORY <dir>]
      [LABELS <label1> [<label2> ...]]
      [ENVIRONMENT <var1=val1> [<var2=val2> ...]]
    )

  ``TARGET_NAME``
    Required. The name of the test executable to create.

  ``SOURCES``
    Optional. Source files for the test executable. Can be omitted if sources
    will be added later using ``target_sources()`` or ``add_test_sources()``.

  ``INCLUDES``
    Optional. Additional include directories for the test target.

  ``LINK_LIBS``
    Optional. Libraries to link with the test executable in addition to
    any libraries set via ``set_test_libraries()``.

  ``TIMEOUT``
    Optional. Test timeout in seconds. Tests exceeding this time will be
    terminated and marked as failed.

  ``WORKING_DIRECTORY``
    Optional. Working directory for test execution.

  ``LABELS``
    Optional. Labels to associate with the test for CTest filtering.
    Example: ``LABELS "unit" "fast"``

  ``ENVIRONMENT``
    Optional. Environment variables to set during test execution.
    Example: ``ENVIRONMENT "ASAN_OPTIONS=verbosity=2" "LSAN_OPTIONS=suppressions=lsan.supp"``

  **Examples**::

    # Simple test with sources
    add_test_target(
      TARGET_NAME test_math
      SOURCES test_math.cpp math_helpers.cpp
      LINK_LIBS math_lib
    )

    # Test with deferred source addition
    add_test_target(TARGET_NAME test_parser)
    add_test_sources(test_parser 
      test_parser_basic.cpp
      test_parser_advanced.cpp
    )

    # Test with timeout and labels
    add_test_target(
      TARGET_NAME test_integration
      SOURCES test_integration.cpp
      TIMEOUT 30
      LABELS "integration" "slow"
    )

    # Test with custom environment for sanitizers
    add_test_target(
      TARGET_NAME test_memory
      SOURCES test_memory.cpp
      ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:halt_on_error=0"
    )

.. command:: set_test_libraries

  Set the list of libraries to be automatically linked with all test targets.
  This replaces any previously set test libraries::

    set_test_libraries(<lib1> [<lib2> ...])

  **Example**::

    # Set Google Test as the default testing framework
    set_test_libraries(GTest::gtest GTest::gtest_main pthread)

.. command:: append_test_libraries

  Append libraries to the existing list of test libraries::

    append_test_libraries(<lib1> [<lib2> ...])

  **Example**::

    # Add a mock library to existing test libraries
    append_test_libraries(mock_lib)

.. command:: add_test_sources

  Add source files to an existing test target::

    add_test_sources(<target_name> <source1> [<source2> ...])

  ``target_name``
    The name of an existing test target.

  **Example**::

    add_test_target(TARGET_NAME test_core)
    add_test_sources(test_core
      test_string_utils.cpp
      test_file_utils.cpp
      test_math_utils.cpp
    )

.. command:: is_sanitizer_preset

  Check if the current build configuration has any sanitizer enabled::

    is_sanitizer_preset(<output_var> [<sanitizer_type_var>])

  ``output_var``
    Variable to store the result (TRUE/FALSE).

  ``sanitizer_type_var``
    Optional. Variable to store the sanitizer type if active.

  **Example**::

    is_sanitizer_preset(has_sanitizer sanitizer_type)
    if(has_sanitizer)
      message(STATUS "Building with ${sanitizer_type}")
    endif()

.. command:: get_active_sanitizer

  Get the currently active sanitizer type::

    get_active_sanitizer(<output_var>)

  ``output_var``
    Variable to store the sanitizer name. Possible values:
    
    - ``AddressSanitizer``
    - ``MemorySanitizer``
    - ``UndefinedBehaviorSanitizer``
    - ``LeakSanitizer``
    - ``none``

  **Example**::

    get_active_sanitizer(current_sanitizer)
    if(current_sanitizer STREQUAL "MemorySanitizer")
      message(WARNING "Some tests may run slowly under MSan")
    endif()

.. command:: exclude_test_for_sanitizer

  Disable a test when running under a specific sanitizer::

    exclude_test_for_sanitizer(<target_name> <sanitizer_name>)

  ``target_name``
    The test target to conditionally disable.

  ``sanitizer_name``
    The sanitizer under which to disable the test.
    Use the same names returned by ``get_active_sanitizer()``.

  **Example**::

    # Disable a test that has false positives under AddressSanitizer
    add_test_target(
      TARGET_NAME test_low_level
      SOURCES test_asm_operations.cpp
    )
    exclude_test_for_sanitizer(test_low_level "AddressSanitizer")

.. command:: add_test_target_legacy

  **DEPRECATED**: Backward compatibility wrapper for the old flag-based API.
  
  This function is provided only for migration purposes and will be removed
  in a future version. New code should use ``add_test_target()`` with presets::

    add_test_target_legacy(
      TARGET_NAME <name>
      [ENABLE_ASAN]
      [ENABLE_PROFILING]
      [SOURCES <source1> ...]
      [INCLUDES <dir1> ...]
      [LINK_LIBS <lib1> ...]
    )

  Migrate to preset-based configuration:
  
  - Instead of ``ENABLE_ASAN``, use preset: ``cmake --preset=asan``
  - Instead of ``ENABLE_PROFILING``, use preset: ``cmake --preset=profile``

Usage Examples
^^^^^^^^^^^^^^

**Basic Test Setup**::

  # In your CMakeLists.txt
  include(TestingUtilities)

  # Set default test framework
  set_test_libraries(GTest::gtest_main)

  # Add a simple test
  add_test_target(
    TARGET_NAME test_utils
    SOURCES test_string.cpp test_file.cpp
  )

**Incremental Test Building**::

  # Create test target without sources
  add_test_target(TARGET_NAME test_app)

  # Add sources conditionally or from different directories
  if(ENABLE_FEATURE_X)
    add_test_sources(test_app test_feature_x.cpp)
  endif()

  add_subdirectory(components)  # Can call add_test_sources from there

**Working with Presets**::

  # CMakePresets.json
  {
    "configurePresets": [
      {
        "name": "asan",
        "cacheVariables": {
          "CMAKE_BUILD_TYPE": "Debug",
          "CMAKE_CXX_FLAGS": "-fsanitize=address -fno-omit-frame-pointer"
        }
      },
      {
        "name": "ubsan",
        "cacheVariables": {
          "CMAKE_BUILD_TYPE": "Debug", 
          "CMAKE_CXX_FLAGS": "-fsanitize=undefined"
        }
      }
    ]
  }

  # Build with AddressSanitizer
  cmake --preset=asan
  cmake --build build
  ctest --test-dir build

**Conditional Test Configuration**::

  add_test_target(
    TARGET_NAME test_performance
    SOURCES test_perf.cpp
    TIMEOUT 60
    LABELS "performance"
  )

  # Disable performance tests under sanitizers (they're too slow)
  is_sanitizer_preset(has_sanitizer)
  if(has_sanitizer)
    set_tests_properties(test_performance PROPERTIES DISABLED TRUE)
  endif()

**Test Organization with Labels**::

  # Unit tests - fast, isolated
  add_test_target(
    TARGET_NAME test_unit_core
    SOURCES test_core.cpp
    LABELS "unit" "fast"
    TIMEOUT 5
  )

  # Integration tests - slower, broader scope
  add_test_target(
    TARGET_NAME test_integration_db
    SOURCES test_database.cpp
    LABELS "integration" "slow" "requires-db"
    TIMEOUT 120
    ENVIRONMENT "DB_CONNECTION_STRING=test.db"
  )

  # Run only unit tests
  ctest -L "unit"

  # Run all except slow tests
  ctest -LE "slow"

Best Practices
^^^^^^^^^^^^^^

1. **Use Presets for Build Variants**: Define presets for different configurations
   (debug, release, asan, ubsan, profile) rather than using flags in code.

2. **Leverage Labels**: Use labels to categorize tests (unit, integration, slow, fast)
   for selective test execution in CI/CD pipelines.

3. **Set Appropriate Timeouts**: Prevent hanging tests from blocking CI/CD pipelines
   by setting reasonable timeouts.

4. **Handle Sanitizer Incompatibilities**: Some tests may not work correctly under
   certain sanitizers. Use ``exclude_test_for_sanitizer()`` for these cases.

5. **Incremental Source Addition**: For complex tests split across multiple files,
   create the target first and add sources incrementally.

6. **Environment Variables**: Use the ENVIRONMENT parameter to configure sanitizer
   options or test-specific settings without modifying source code.

Notes
^^^^^

* Test targets are automatically registered with CTest
* All test targets inherit libraries set via ``set_test_libraries()``
* The module automatically detects and reports active sanitizers from preset configuration
* Test properties can be further customized using standard CMake ``set_tests_properties()``

#]=======================================================================]
function(add_test_target)
  cmake_parse_arguments(
    ARG
    "" # No boolean flags - handled by presets
    "TARGET_NAME;TIMEOUT;WORKING_DIRECTORY" # Single-value arguments
    "SOURCES;INCLUDES;LINK_LIBS;LABELS;ENVIRONMENT" # Multi-value arguments
    ${ARGN})

  # Validate required arguments
  if(NOT ARG_TARGET_NAME)
    message(FATAL_ERROR "TARGET_NAME is required for add_test_target")
  endif()

  # Create or update test executable
  if(NOT TARGET ${ARG_TARGET_NAME})
    # Create executable - sources can be added later with target_sources
    add_executable(${ARG_TARGET_NAME})
    if(ARG_SOURCES)
      target_sources(${ARG_TARGET_NAME} PRIVATE ${ARG_SOURCES})
    endif()
    add_test(NAME ${ARG_TARGET_NAME} COMMAND ${ARG_TARGET_NAME})
  else()
    if(ARG_SOURCES)
      target_sources(${ARG_TARGET_NAME} PRIVATE ${ARG_SOURCES})
    endif()
    message(STATUS "Target ${ARG_TARGET_NAME} already exists - updating properties")
  endif()

  # Link libraries
  get_directory_property(test_libraries PROPERTY TEST_LIBRARIES)
  if(test_libraries)
    target_link_libraries(${ARG_TARGET_NAME} PRIVATE ${test_libraries})
  endif()

  if(ARG_LINK_LIBS)
    target_link_libraries(${ARG_TARGET_NAME} PRIVATE ${ARG_LINK_LIBS})
  endif()

  # Include directories
  if(ARG_INCLUDES)
    target_include_directories(${ARG_TARGET_NAME} PRIVATE ${ARG_INCLUDES})
  endif()

  # Set test properties if specified
  if(ARG_TIMEOUT)
    set_tests_properties(${ARG_TARGET_NAME} PROPERTIES TIMEOUT ${ARG_TIMEOUT})
  endif()
  
  if(ARG_LABELS)
    set_tests_properties(${ARG_TARGET_NAME} PROPERTIES LABELS "${ARG_LABELS}")
  endif()
  
  if(ARG_ENVIRONMENT)
    set_tests_properties(${ARG_TARGET_NAME} PROPERTIES ENVIRONMENT "${ARG_ENVIRONMENT}")
  endif()
  
  if(ARG_WORKING_DIRECTORY)
    set_tests_properties(${ARG_TARGET_NAME} PROPERTIES WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}")
  endif()

  # Detect and report active configurations from presets
  get_active_sanitizer(active_sanitizer)
  if(NOT active_sanitizer STREQUAL "none")
    message(STATUS "${ARG_TARGET_NAME}: ${active_sanitizer} sanitizer enabled via preset")
  endif()
  
  if(CMAKE_CXX_FLAGS MATCHES "-pg" OR CMAKE_EXE_LINKER_FLAGS MATCHES "-pg")
    message(STATUS "${ARG_TARGET_NAME}: Profiling enabled via preset")
  endif()
endfunction()

# Backward compatibility wrapper with deprecation notice
function(add_test_target_legacy)
  cmake_parse_arguments(
    ARG 
    "ENABLE_PROFILING;ENABLE_ASAN" 
    "TARGET_NAME" 
    "SOURCES;INCLUDES;LINK_LIBS" 
    ${ARGN})

  # Deprecation warning
  if(ARG_ENABLE_PROFILING OR ARG_ENABLE_ASAN)
    message(DEPRECATION 
      "ENABLE_X flags in add_test_target are deprecated.\n"
      "Please use CMake presets instead:\n"
      "  - cmake --preset=asan   # For AddressSanitizer\n"
      "  - cmake --preset=profile # For profiling")
  endif()

  # First create the target using modern function
  add_test_target(
    TARGET_NAME ${ARG_TARGET_NAME}
    SOURCES ${ARG_SOURCES}
    INCLUDES ${ARG_INCLUDES}
    LINK_LIBS ${ARG_LINK_LIBS})

  # Apply target-specific flags for backward compatibility
  if(ARG_ENABLE_ASAN)
    target_compile_options(${ARG_TARGET_NAME} PRIVATE 
      -fsanitize=address 
      -fno-omit-frame-pointer 
      -g)
    target_link_options(${ARG_TARGET_NAME} PRIVATE 
      -fsanitize=address)
    message(STATUS "${ARG_TARGET_NAME}: AddressSanitizer enabled (legacy)")
  endif()
  
  if(ARG_ENABLE_PROFILING)
    target_compile_options(${ARG_TARGET_NAME} PRIVATE -pg)
    target_link_options(${ARG_TARGET_NAME} PRIVATE -pg)
    message(STATUS "${ARG_TARGET_NAME}: Profiling enabled (legacy)")
  endif()
endfunction()

# Utility to check if running under a sanitizer preset
function(is_sanitizer_preset output_var)
  get_active_sanitizer(sanitizer_type)
  if(NOT sanitizer_type STREQUAL "none")
    set(${output_var} TRUE PARENT_SCOPE)
    # Optionally return the sanitizer type as second argument
    if(ARGC GREATER 1)
      set(${ARGV1} ${sanitizer_type} PARENT_SCOPE)
    endif()
  else()
    set(${output_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

# Utility to get active sanitizer type
function(get_active_sanitizer output_var)
  if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=address")
    set(${output_var} "AddressSanitizer" PARENT_SCOPE)
  elseif(CMAKE_CXX_FLAGS MATCHES "-fsanitize=memory")
    set(${output_var} "MemorySanitizer" PARENT_SCOPE)
  elseif(CMAKE_CXX_FLAGS MATCHES "-fsanitize=undefined")
    set(${output_var} "UndefinedBehaviorSanitizer" PARENT_SCOPE)
  elseif(CMAKE_CXX_FLAGS MATCHES "-fsanitize=leak")
    set(${output_var} "LeakSanitizer" PARENT_SCOPE)
  else()
    set(${output_var} "none" PARENT_SCOPE)
  endif()
endfunction()

# Utility to add sources to an existing test target
function(add_test_sources target_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "Target ${target_name} does not exist")
  endif()
  
  target_sources(${target_name} PRIVATE ${ARGN})
endfunction()

# Utility to conditionally exclude tests when certain sanitizers are active
function(exclude_test_for_sanitizer target_name sanitizer_name)
  get_active_sanitizer(active_sanitizer)
  if(active_sanitizer STREQUAL sanitizer_name)
    set_tests_properties(${target_name} PROPERTIES DISABLED TRUE)
    message(STATUS "Test ${target_name} disabled under ${sanitizer_name}")
  endif()
endfunction()