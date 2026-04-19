from conan import ConanFile
from conan.tools.env import VirtualBuildEnv
from conan.tools.gnu import PkgConfigDeps
from conan.tools.meson import MesonToolchain


class DcatConan(ConanFile):
    name = "dcat"
    version = "0.1.0"
    package_type = "application"
    settings = "os", "arch", "compiler", "build_type"

    def requirements(self):
        self.requires("assimp/5.4.3")
        self.requires("cglm/0.9.1")
        self.requires("libvips/8.16.0")
        self.requires("vulkan-loader/1.4.313.0")
        if self.settings.os != "Windows":
            self.requires("libsixel/1.10.3")

    def build_requirements(self):
        self.tool_requires("pkgconf/2.5.1")

    def generate(self):
        MesonToolchain(self).generate()
        PkgConfigDeps(self).generate()
        VirtualBuildEnv(self).generate()
