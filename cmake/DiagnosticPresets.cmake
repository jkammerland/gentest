# DiagnosticPresets.cmake - Utility functions for diagnostic preset configuration
# 
# Provides runtime sanitizer configuration and validation utilities
# for the diagnostic preset system.

# Detect sanitizer availability at configure time
function(diagnostic_check_sanitizer_support)
    include(CheckCXXCompilerFlag)
    
    # Check AddressSanitizer support (with LeakSanitizer)
    check_cxx_compiler_flag("-fsanitize=address,leak" DIAGNOSTIC_SUPPORTS_ASAN_LSAN)
    if(DIAGNOSTIC_SUPPORTS_ASAN_LSAN)
        message(STATUS "Diagnostic: ASan+LSan support detected")
        set(DIAGNOSTIC_ASAN_AVAILABLE TRUE PARENT_SCOPE)
    endif()
    
    # Check UndefinedBehaviorSanitizer support  
    check_cxx_compiler_flag("-fsanitize=undefined" DIAGNOSTIC_SUPPORTS_UBSAN)
    if(DIAGNOSTIC_SUPPORTS_UBSAN)
        message(STATUS "Diagnostic: UBSan support detected")
        set(DIAGNOSTIC_UBSAN_AVAILABLE TRUE PARENT_SCOPE)
    endif()
    
    # Check ThreadSanitizer support
    check_cxx_compiler_flag("-fsanitize=thread" DIAGNOSTIC_SUPPORTS_TSAN)
    if(DIAGNOSTIC_SUPPORTS_TSAN)
        message(STATUS "Diagnostic: TSan support detected")  
        set(DIAGNOSTIC_TSAN_AVAILABLE TRUE PARENT_SCOPE)
    endif()
    
    # Check MemorySanitizer support (Clang-only)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        check_cxx_compiler_flag("-fsanitize=memory" DIAGNOSTIC_SUPPORTS_MSAN)
        if(DIAGNOSTIC_SUPPORTS_MSAN)
            message(STATUS "Diagnostic: MSan support detected (Clang)")
            set(DIAGNOSTIC_MSAN_AVAILABLE TRUE PARENT_SCOPE)
        endif()
    endif()
    
    # Check Control Flow Integrity + HardwareAddressSanitizer
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        check_cxx_compiler_flag("-fsanitize=cfi" DIAGNOSTIC_SUPPORTS_CFI)
        check_cxx_compiler_flag("-fsanitize=hwaddress" DIAGNOSTIC_SUPPORTS_HWASAN)
        if(DIAGNOSTIC_SUPPORTS_CFI AND DIAGNOSTIC_SUPPORTS_HWASAN)
            message(STATUS "Diagnostic: CFI+HWASan support detected (Clang)")
            set(DIAGNOSTIC_HARDENED_AVAILABLE TRUE PARENT_SCOPE)
        endif()
    endif()
endfunction()

# Configure sanitizer options for optimal diagnostic output
function(diagnostic_configure_sanitizer_environment)
    # Set environment variables for sanitizer configuration
    # These are used by the preset system but can be overridden
    
    if(DIAGNOSTIC_ASAN_AVAILABLE)
        # ASan configuration optimized for development workflow
        set(ENV{ASAN_OPTIONS} "detect_leaks=1:halt_on_error=1:print_stats=1:check_initialization_order=1:strict_init_order=1:detect_stack_use_after_return=1")
        set(ENV{LSAN_OPTIONS} "print_suppressions=false:report_objects=1:use_unaligned=1")
        message(STATUS "Diagnostic: ASan+LSan environment configured")
    endif()
    
    if(DIAGNOSTIC_UBSAN_AVAILABLE)
        # UBSan configuration for comprehensive undefined behavior detection
        set(ENV{UBSAN_OPTIONS} "halt_on_error=1:print_stacktrace=1:report_error_type=1:suppressions=${CMAKE_SOURCE_DIR}/ubsan.supp")
        message(STATUS "Diagnostic: UBSan environment configured")
    endif()
    
    if(DIAGNOSTIC_TSAN_AVAILABLE) 
        # TSan configuration optimized for race detection
        set(ENV{TSAN_OPTIONS} "halt_on_error=1:print_stats=1:history_size=7:io_sync=0:detect_deadlocks=1")
        message(STATUS "Diagnostic: TSan environment configured")
    endif()
    
    if(DIAGNOSTIC_MSAN_AVAILABLE)
        # MSan configuration for uninitialized memory detection
        set(ENV{MSAN_OPTIONS} "halt_on_error=1:print_stats=1:print_module_map=1:track_origins=2")
        message(STATUS "Diagnostic: MSan environment configured")
    endif()
    
    if(DIAGNOSTIC_HARDENED_AVAILABLE)
        # CFI+HWASan configuration for security diagnostics
        set(ENV{HWASAN_OPTIONS} "halt_on_error=1:print_stats=1:stack_history_size=1024")
        set(ENV{CFI_OPTIONS} "print_stats=1:cross_dso=1")
        message(STATUS "Diagnostic: CFI+HWASan environment configured")
    endif()
endfunction()

# Validate sanitizer combination compatibility
function(diagnostic_validate_combination sanitizer_flags)
    string(FIND "${sanitizer_flags}" "address" HAS_ASAN)
    string(FIND "${sanitizer_flags}" "thread" HAS_TSAN) 
    string(FIND "${sanitizer_flags}" "memory" HAS_MSAN)
    
    # ASan and TSan are mutually exclusive
    if(NOT HAS_ASAN EQUAL -1 AND NOT HAS_TSAN EQUAL -1)
        message(FATAL_ERROR "Diagnostic: AddressSanitizer and ThreadSanitizer are mutually exclusive")
    endif()
    
    # ASan and MSan are mutually exclusive  
    if(NOT HAS_ASAN EQUAL -1 AND NOT HAS_MSAN EQUAL -1)
        message(FATAL_ERROR "Diagnostic: AddressSanitizer and MemorySanitizer are mutually exclusive")
    endif()
    
    # TSan and MSan are mutually exclusive
    if(NOT HAS_TSAN EQUAL -1 AND NOT HAS_MSAN EQUAL -1)
        message(FATAL_ERROR "Diagnostic: ThreadSanitizer and MemorySanitizer are mutually exclusive")
    endif()
    
    message(STATUS "Diagnostic: Sanitizer combination validated: ${sanitizer_flags}")
endfunction()

# Print diagnostic preset usage information
function(diagnostic_print_usage)
    message(STATUS "")
    message(STATUS "=== Diagnostic Presets Usage ===")
    message(STATUS "Quick feedback:     cmake --preset=diagnostic-quick")
    message(STATUS "Comprehensive:      cmake --preset=diagnostic-deep")  
    message(STATUS "Threading issues:   cmake --preset=diagnostic-thread")
    message(STATUS "Memory init:        cmake --preset=diagnostic-memory")
    message(STATUS "Security focused:   cmake --preset=diagnostic-hardened")
    message(STATUS "")
    message(STATUS "Recommended workflows:")
    message(STATUS "Development:        cmake --workflow --preset=diagnostic-quick-workflow")
    message(STATUS "Pre-commit:         cmake --workflow --preset=diagnostic-deep-workflow")
    message(STATUS "Threading:          cmake --workflow --preset=diagnostic-thread-workflow")
    message(STATUS "")
endfunction()

# Auto-detect and configure best available diagnostic preset
function(diagnostic_auto_configure)
    diagnostic_check_sanitizer_support()
    
    if(DIAGNOSTIC_ASAN_AVAILABLE)
        message(STATUS "Diagnostic: Recommending 'diagnostic-quick' for development")
        message(STATUS "            or 'diagnostic-deep' for comprehensive testing")
    elseif(DIAGNOSTIC_TSAN_AVAILABLE)
        message(STATUS "Diagnostic: ASan unavailable, 'diagnostic-thread' available")
    else()
        message(WARNING "Diagnostic: No sanitizers detected. Consider installing sanitizer runtime libraries.")
    endif()
    
    diagnostic_print_usage()
endfunction()

# Initialize diagnostic system
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    diagnostic_auto_configure()
endif()