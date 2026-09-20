from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.apple import is_apple_os
from conan.tools.microsoft import is_msvc

from os import getenv

required_conan_version = ">=2.13.0"

class VCMI(ConanFile):
    settings = "os", "compiler", "build_type", "arch"

    _libRequires = [
        "zlib/[^1.2.12]",
    ]
    _clientRequires = [
        "libsquish/[^1.15]",
        "onetbb/[^2021.7]",
        "sdl_image/[^2.8.2]",
        "sdl_mixer/[^2.8.0]",
        "sdl_ttf/[^2.0.18]",
    ]
    _launcherRequires = [
        "xz_utils/[^5.2.5]", # innoextract
    ]
    requires = _libRequires + _clientRequires + _launcherRequires

    options = {
        "target_pre_windows10": [True, False],
        "with_ffmpeg": [True, False],
        "with_onnxruntime": [True, False],
        "with_discord_presence": [True, False],
        "lua_lib": ["None", "luajit", "lua"],
    }
    default_options = {
        "target_pre_windows10": False,
        "with_ffmpeg": True,
        "with_onnxruntime": True,
        "with_discord_presence": True,
        "lua_lib": "luajit",
    }

    def config_options(self):
        isMobile = self.settings.os == "iOS" or self.settings.os == "Android"

        # shared options should be synced with conan_profiles/base/common (and possibly others)
        self.options["boost"].shared = True
        self.options["bzip2"].shared = True
        self.options["libiconv"].shared = True
        self.options["libpng"].shared = True
        self.options["minizip"].shared = True
        self.options["ogg"].shared = True
        self.options["opus"].shared = True
        self.options["qt"].shared = self.settings.os != "iOS"
        self.options["xz_utils"].shared = True
        self.options["zlib"].shared = True

        # static on "single app" platforms
        isSdlShared = not isMobile
        self.options["sdl"].shared = isSdlShared
        self.options["sdl_image"].shared = isSdlShared
        self.options["sdl_mixer"].shared = isSdlShared
        self.options["sdl_ttf"].shared = isSdlShared

        self.options["qt"].openssl = not is_apple_os(self)
        self.options["qt"].qtsvg = True
        if self.settings.os == "Android":
            self.options["qt"].android_sdk = getenv("ANDROID_HOME")
        if isMobile:
            self.options["qt"].opengl = "es2"

        if self.settings.os != "Windows":
            del self.options.target_pre_windows10

        if isMobile:
            del self.options.with_discord_presence

    # hard requirements on dependencies' options
    def configure(self):
        self.options["sdl"].sdl2main = self.settings.os != "iOS"

        self.options["qt"].qttools = True
        self.options["qt"].with_md4c = True
        if self.settings.os == "Android":
            # TODO: in Qt 6 this option doesn't exist
            self.options["qt"].qtandroidextras = True

        if is_msvc(self):
            # required because VCMI uses dynamic runtime
            self.options["boost"].shared = True
            self.options["ffmpeg"].shared = True

    def requirements(self):
        # onnxruntime depends on exact boost version
        # placing it before our boost requirement ensures that this version will be in the graph to prevent conflicts like:
        # Conflict between boost/1.83.0 and boost/1.90.0 in the graph.
        # see https://docs.conan.io/2/knowledge/faq.html#getting-version-conflicts-even-when-using-version-ranges
        if self.options.with_onnxruntime:
            self.requires("onnxruntime/1.18.1")

        # lib
        # boost::filesystem removed support for Windows < 10 in v1.87
        boostMinVersion = "1.74"
        if self.options.get_safe("target_pre_windows10", False):
            self.requires(f"boost/[>={boostMinVersion} <1.87]")
        else:
            self.requires(f"boost/[^{boostMinVersion}]")

        # v1.3.2 is broken for Android, see https://github.com/madler/zlib/pull/1188
        minizipMinVersion = "1.2.12"
        if self.settings.os == "Android":
            self.requires(f"minizip/[>={minizipMinVersion} <1.3.2]")
        else:
            self.requires(f"minizip/[^{minizipMinVersion}]")

        if self.options.lua_lib != "None":
            lib = str(self.options.lua_lib)
            libVersion = {
                "lua": "[^5.4.7]",
                "luajit": "2.1.0-beta3",
            }.get(lib)
            self.requires(f"{lib}/{libVersion}")

        # client
        if self.options.with_ffmpeg:
            self.requires("ffmpeg/[>=4.4]")

        if self.options.get_safe("with_discord_presence", False):
            self.requires("fmt/[>=12.1.0]")
            self.requires("glaze/[>=5.5.4]")

        # upcoming SDL version 3.0+ is not supported at the moment due to API breakage
        # SDL versions between 2.22-2.26.1 have broken sound
        # versions before 2.30.7 don't build for Android with NDK 27: https://github.com/libsdl-org/SDL/issues/9792
        self.requires("sdl/[^2.30.7]")

        # launcher
        if self.settings.os == "Android":
            self.requires("qt/[~5.15.14]") # earlier versions have serious bugs
        else:
            self.requires("qt/[~5.15.2]")

    def validate(self):
        if not is_apple_os(self) and self.dependencies["qt"].options.openssl != True:
            self.output.warning("qt:openssl option for non-Apple OS should be set to True, otherwise mods can't be downloaded")
