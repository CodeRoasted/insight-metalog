import os
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain


required_conan_version = ">=2.28"


class InsightMetalogConan(ConanFile):
    name = "insight_metalog"
    version = "1.5.0"
    license = "BUSL-1.1"
    package_type = "library"
    description = "MetaLog spec v0.5.0 producer: bounded statistical fingerprint of a window of log behaviour, with behavior, stability, diff/compose, and HLL cardinality blocks (https://github.com/CodeRoasted/metalog-spec)."
    settings = "os", "arch", "compiler", "build_type"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
    }

    exports_sources = "CMakeLists.txt", "modules/*", "src/*", "api/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.get_safe("shared"):
            self.options.rm_safe("fPIC")

    def layout(self):
        self.cpp.source.includedirs = ["api"]
        build_dir = os.environ.get("MALF_EDITABLE_BUILD_DIR", "build")
        self.cpp.build.libdirs = [build_dir]
        # Editable: the build-tree export()'d insight_metalog-config.cmake (carrying the
        # FILE_SET CXX_MODULES) lives in the build dir → consumers find it there (§10.9).
        self.cpp.build.builddirs = [build_dir]

    def requirements(self):
        # insight_canon provides logging and types; transitive headers needed.
        # Don't use transitive_libs since it pulls in spdlog which is header-only.
        self.requires("insight_canon/1.5.0", transitive_headers=True)
        # glaze is the JSON serializer, used only in metalog_serialize.cpp and never
        # in a public header — a private, non-propagated build dependency.
        self.requires("glaze/7.4.0", visible=False)
        self.requires("picosha2/1.0.0")

    def build_requirements(self):
        self.test_requires("gtest/1.17.0")
        self.test_requires("benchmark/1.9.5")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generator = "Ninja"
        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["insight_metalog"]
        self.cpp_info.set_property("cmake_file_name", "insight_metalog")
        self.cpp_info.set_property("cmake_target_name", "insight::metalog")
        # insight_canon handles spdlog/fmt internally; glaze is impl-only (not
        # propagated). Only these reach consumers:
        self.cpp_info.requires = [
            "insight_canon::insight_canon",
            "picosha2::picosha2"
        ]
        # Cross-package C++ modules (§10.7): defer to the package's OWN cmake config
        # (it carries FILE_SET CXX_MODULES; conan's generator does not emit it).
        # Editable build-tree config dir + create install path both listed.
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs = [
            os.environ.get("MALF_EDITABLE_BUILD_DIR", "build"),
            "lib/cmake/insight_metalog",
        ]
