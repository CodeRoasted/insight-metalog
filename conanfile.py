import os
import runpy
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain


required_conan_version = ">=2.28"


class InsightMetalogConan(ConanFile):
    name = "insight_metalog"
    version = "1.10.7"
    license = "BUSL-1.1"
    package_type = "library"
    description = "MetaLog spec v0.9.0 producer: bounded statistical fingerprint of a window of log behaviour, with behavior, stability, diff/compose, and HLL cardinality blocks (https://github.com/CodeRoasted/metalog-spec)."
    settings = "os", "arch", "compiler", "build_type"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
    }

    # The one build compiles and runs the tests (DN-142.D5), so everything they read rides in the
    # export: the scripts/ scenario headers they include and the determinism corpus they replay.
    exports_sources = "CMakeLists.txt", "src/*", "api/*", "tests/*", "benchmarks/*", "scripts/*.hpp", "scripts/determinism_corpus/*"

    def export_sources(self):
        # The export is the TRACKED files under exports_sources, never the disk's (DN-142.D4 (b)):
        # malf's helper, staged in the conan home and named by global.conf, drops every file git
        # does not track. Without it the export fails; it never falls back to the disk silently.
        runpy.run_path(self.conf.get("user.malf:recipe_exports"))["narrow_to_tracked"](self)

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.get_safe("shared"):
            self.options.rm_safe("fPIC")

    def layout(self):
        self.cpp.source.includedirs = ["api"]
        # Keyed editable build dir. malf exports MALF_EDITABLE_BUILD_DIR for every profile
        # (sanitizer included), so each build type gets its own tree. A RAW
        # `conan create --profile X` exports nothing and falls back to the profile's [conf]
        # instead: this dep then publishes its X-keyed build dir, and a consumer under ANY
        # profile links that X build rather than the libc++-default `build/`. The build-type
        # isolation therefore holds only while malf drives the build.
        build_dir = (os.environ.get("MALF_EDITABLE_BUILD_DIR")
                     or self.conf.get("user.malf:editable_build_dir", default="build"))
        self.cpp.build.libdirs = [build_dir]
        # Editable: the build-tree export()'d insight_metalog-config.cmake (carrying the
        # FILE_SET CXX_MODULES) lives in the build dir → consumers find it there (§10.9).
        self.cpp.build.builddirs = [build_dir]

    def requirements(self):
        # insight_canon provides logging and types; transitive headers needed.
        # Don't use transitive_libs since it pulls in spdlog which is header-only.
        self.requires("insight_canon/1.10.7", transitive_headers=True)
        # glaze is the JSON serializer, used only in metalog_serialize.cpp and never
        # in a public header — a private, non-propagated build dependency.
        self.requires("glaze/7.4.0", visible=False)

    def build_requirements(self):
        # Test-only dialect package. The cube's ROLE dimension is fed by package-supplied
        # StructuralRole rows (ADR-17.D2: canon ships no default composition), so the
        # emerging-border oracle cannot exercise `role: Terminator` without composing a
        # package that supplies one — measured, not assumed: under an EMPTY composition that
        # class recovers 1 of 2 antichain members. metalog itself stays dialect-free.
        self.test_requires("insight_semantic_github/1.10.7")
        self.test_requires("gtest/1.17.0")
        self.test_requires("benchmark/1.9.5")
        # picosha2 — TEST-ONLY now: the lib's template_id SHA-256 moved to canon (the TemplateId POD),
        # but the determinism/cube tests still hash full doc JSON for their golden digests.
        self.test_requires("picosha2/1.0.0")

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
        runpy.run_path(self.conf.get("user.malf:recipe_tests"))["run_tests"](self)

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
            "insight_canon::insight_canon"
        ]
        # Cross-package C++ modules (§10.7): defer to the package's OWN cmake config
        # (it carries FILE_SET CXX_MODULES; conan's generator does not emit it).
        # Editable build-tree config dir + create install path both listed.
        self.cpp_info.set_property("cmake_find_mode", "none")
        malf_editable_build_dir = os.environ.get("MALF_EDITABLE_BUILD_DIR")
        if malf_editable_build_dir:
            self.cpp_info.builddirs = [malf_editable_build_dir, "lib/cmake/insight_metalog"]
        else:
            self.cpp_info.builddirs = ["lib/cmake/insight_metalog"]
