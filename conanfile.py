import os
from shutil import which

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class GentestConan(ConanFile):
    name = "gentest"
    version = "1.0.0"
    description = "Attribute-driven test discovery with clang tooling"
    license = "MIT"
    url = "https://github.com/jkammerland/gentest"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "with_boost_json": [True, False],
        "with_boost_uuid": [True, False],
    }
    default_options = {
        "with_boost_json": False,
        "with_boost_uuid": False,
    }

    def layout(self):
        cmake_layout(self)
        self.folders.build = os.path.join("build", "conan", str(self.settings.build_type))
        self.folders.generators = os.path.join("build", "conan", "generators")

    def requirements(self):
        self.requires("fmt/11.0.2")
        self.requires("zlib/1.3.1")
        self.requires("zstd/1.5.6")
        if self.options.with_boost_json or self.options.with_boost_uuid:
            self.requires("boost/1.86.0")

    def validate(self):
        if not which("llvm-config"):
            raise ConanInvalidConfiguration(
                "gentest requires system LLVM/Clang; please install llvm-config and Clang headers/libraries"
            )

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["GENTEST_USE_BOOST_JSON"] = bool(self.options.with_boost_json)
        tc.variables["GENTEST_USE_BOOST_UUID"] = bool(self.options.with_boost_uuid)
        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()
