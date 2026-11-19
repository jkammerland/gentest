include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

# CMake 4.3 and 4.4 each used a different opt-in value for SPDX generation.
if(CMAKE_VERSION VERSION_LESS 4.4)
    set(_activation -DCMAKE_EXPERIMENTAL_GENERATE_SBOM=ca494ed3-b261-4205-a01f-603c95e4cae0)
elseif(CMAKE_VERSION VERSION_LESS 4.5)
    set(_activation -DCMAKE_EXPERIMENTAL_GENERATE_SBOM=2d856d6d-53e8-488b-a17f-d486d2cac317)
else()
    set(_config_suffix "-Release")
endif()

file(REMOVE_RECURSE "${BUILD_ROOT}")
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${BUILD_ROOT}/provider" -G "${GENERATOR}"
    "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DLLVM_DIR=${LLVM_DIR}" "-DClang_DIR=${Clang_DIR}"
    -DCMAKE_BUILD_TYPE=Release -Dgentest_BUILD_TESTING=OFF -Dgentest_INSTALL=ON -DGENTEST_BUILD_CODEGEN=ON
    -DGENTEST_ENABLE_SBOM=ON ${_activation})

# Both SPDX documents belong to the default install component and are generated at configure time.
set(_stage "${BUILD_ROOT}/stage")
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_ROOT}/provider" --config Release
    --component Unspecified --prefix "${_stage}")
gentest_check_run_or_fail(COMMAND "${Python3_EXECUTABLE}" -B -c [=[
from pathlib import Path
import sys
from validate_package_metadata import validate_sbom
validate_sbom(Path(sys.argv[1]), {"gentest_codegen", "gentest_supervise"})
]=]
    "${_stage}/share/sbom/gentest/gentest-tools${_config_suffix}.spdx.json"
    WORKING_DIRECTORY "${SOURCE_DIR}/scripts")
