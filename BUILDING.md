# Building the remote client

Validation on 2026-09-20: a new out-of-tree Windows x64 build compiled and linked
vcmiclient, vcmi, BattleAI, MMAI, StupidAI and EmptyAI from this exported source.
It reused already installed Conan third-party binaries; dependency downloads and
builds from an empty Conan cache were not repeated. The generated projects' 652
source entries referenced no private server/adventure-AI source directories.

## Windows x64

Install Git, Python, Conan 2, CMake 3.24 or newer, and Visual Studio 2022 with
Desktop development with C++, including the v142 (14.29) build tools required by
the supplied Conan profile. Use a Developer PowerShell for VS 2022.

```powershell
conan profile detect
conan install . --output-folder=conan-public --profile:host=dependencies/conan_profiles/msvc-x64 --profile:build=default -s build_type=RelWithDebInfo --build=missing -o '&:with_onnxruntime=True'
cmake -S . -B build-public -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=conan-public/conan_toolchain.cmake -DENABLE_LAUNCHER=OFF
cmake --build build-public --config RelWithDebInfo --target vcmiclient BattleAI MMAI StupidAI EmptyAI --parallel 4
```

The exported CMake profile forces remote-client mode and disables omitted server
and adventure-AI targets. Required upstream dependency recipes, FuzzyLite and
innoextract sources are vendored, with revisions in SOURCE_PROVENANCE.json.
Conan downloads/builds third-party dependencies; initial builds can take time.
This does not mean players need GitHub access to start or play the game.

Runtime requires legally obtained Heroes III data and compatible game resources.
Original Data/Maps/Mp3 and third-party HotA/translation bundles are not supplied
here. This source checkout is not a ready-to-play game distribution.
`config/arenaServers.json` contains a localhost example; configure your own endpoint.

## Android

Android Java/Gradle, Qt launcher, resource-import code and CMake integration are
included. Use Qt 5.15, Conan's Android ARM64 profile, Android SDK/NDK, JDK and Ninja;
enable the launcher and set `HAB_ANDROID_APPLICATION_ID` to your own application ID.
The development build used NDK 29.0.14206865, Qt 5.15.18, JDK 21, SDK 35, min API 21.
Signing keys are intentionally not included; use your own development key.

The bundled-mod startup code expects `assets/hab-bundle/manifest.json` (a JSON
mapping of relative `Mods/...` paths to SHA-256), `bundle-id.txt` (SHA-256 of the
UTF-8 manifest bytes), and the corresponding files. External asset redistribution
permissions remain to be finalized. With legally obtained mod folders, run
`python tools/stage_bundled_mods.py --mods YOUR_MODS_FOLDER --assets build-android/android-package/assets`
after CMake configuration and before building `android_deploy`. The helper requires
a new `hab-bundle` destination and never deletes existing user files.
An independent APK rebuild from this public checkout has not yet been validated.
Do not treat this first source publication as a verified reproducible APK release.
