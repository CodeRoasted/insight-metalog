from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class InsightMetalogTestPackageConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)
        # glaze is impl-only in insight_metalog (visible=False), so it does not
        # reach consumers. This smoke test re-parses to_json() output to assert
        # the serialised contract, so it brings its own parser — exactly as a
        # real external consumer inspecting the JSON would.
        self.requires("glaze/7.4.0")

    def build_requirements(self):
        self.test_requires("gtest/1.17.0")

    def configure(self):
        self.options["gtest"].shared = False

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generator = "Ninja"
        # The oracle for `producer.version` is the RELEASED PACKAGE's version, taken from the
        # reference under test — never a literal re-typed here. Two authorities have to agree for
        # the assertion to mean anything: the recipe version (this value) and the C++ constant
        # `kProducerVersion` that the engine stamps into every document. Re-typing the number in
        # the test would collapse them into one and measure nothing but the typist.
        tc.cache_variables["INSIGHT_METALOG_TESTED_VERSION"] = str(
            self.dependencies["insight_metalog"].ref.version
        )
        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            cmake = CMake(self)
            cmake.ctest(cli_args=["--output-on-failure"])
