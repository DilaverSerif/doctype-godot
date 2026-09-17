#!/usr/bin/env python
# Doctype for Godot: builds the GDExtension (litehtml + the Doctype quad
# recorder + the Godot glue) into addons/doctype/bin.
#
#   git submodule update --init      (godot-cpp)
#   scons platform=macos arch=arm64 target=template_debug
#   scons platform=macos arch=universal target=template_release
#   scons platform=android arch=arm64 target=template_release
#   scons platform=ios arch=arm64 target=template_release
#
# litehtml needs C++ exceptions, so godot-cpp is built with them enabled.
import os
import sys

ARGUMENTS.setdefault("disable_exceptions", "no")
ARGUMENTS.setdefault("api_version", "4.7")

env = SConscript("godot-cpp/SConstruct")

lite = "native/third_party/litehtml"

env.Append(CPPPATH=[
    "src/",
    "native/src/",
    "native/third_party/",
    lite + "/include",
    lite + "/include/litehtml",
    lite + "/src",
    lite + "/src/gumbo/include",
    lite + "/src/gumbo/include/gumbo",
])

# gumbo is C99; godot-cpp only configures the C++ standard.
env.Append(CFLAGS=["-std=c99"])

# litehtml is noisy under -Wall; it is vendored, not ours to fix.
if env["platform"] != "windows":
    env.Append(CCFLAGS=["-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-sign-compare",
                        "-Wno-unused-but-set-variable", "-Wno-deprecated-declarations"])

sources = (
    Glob("src/*.cpp")
    + Glob("native/src/*.cpp")
    + Glob(lite + "/src/*.cpp")
    + Glob(lite + "/src/gumbo/*.c")
)

lib_name = "libdoctype{}{}".format(env["suffix"], env["SHLIBSUFFIX"])
out_dir = "addons/doctype/bin/"

if env["platform"] == "macos":
    framework = "libdoctype.{}.{}.framework".format(env["platform"], env["target"])
    library = env.SharedLibrary(
        out_dir + framework + "/libdoctype.{}.{}".format(env["platform"], env["target"]),
        source=sources,
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            out_dir + "libdoctype.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            out_dir + "libdoctype.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
        )
else:
    library = env.SharedLibrary(out_dir + lib_name, source=sources)

Default(library)
