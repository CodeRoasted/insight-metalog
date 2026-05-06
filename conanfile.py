from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain


class InsightMetalogConan(ConanFile):
    name = "insight_metalog"
    version = "1.3.0"
    package_type = "library"
    description = "MetaLog spec v0.2.0 producer: bounded statistical fingerprint of a window of log behaviour, with behavior, stability, diff/compose, and HLL cardinality blocks (https://github.com/coderoast-dev/metalog-spec)."
    settings = "os", "arch", "compiler", "build_type"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
    }

    exports_sources = "CMakeLists.txt", "src/*", "api/*"

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.get_safe("shared"):
            self.options.rm_safe("fPIC")

    def requirements(self):
        self.requires("insight_canon/1.3.0", transitive_headers=True, transitive_libs=True)
        self.requires("nlohmann_json/3.11.3", transitive_headers=True, transitive_libs=True)
        self.requires("picosha2/1.0.0")

    def build_requirements(self):
        self.test_requires("gtest/1.17.0")
        self.test_requires("benchmark/1.8.3")

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
