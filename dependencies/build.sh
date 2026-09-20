#!/usr/bin/env bash
set -euo pipefail
set -x

# helpers

tempDir="${RUNNER_TEMP:-${TMPDIR:-${TEMP:-${TMP:-/tmp}}}}"
tempDir="${tempDir//\\//}" # for Windows

scriptDir=$(cd "$(dirname "$0")" && pwd)
cciRecipePathQt='recipes/qt'
cciRepoName='conan-center-index'
conanOptionOrdinaryLuaLib='&:lua_lib=lua'
conanOptionTargetPreWindows10='&:target_pre_windows10=True'
if command -v python3 >/dev/null ; then
	python=python3
else
	python=python
fi

colored_echo() {
	reset='\033[0m'
	printf "\\033[0;${1}m%s${reset}\n" "$2"
}

error() {
	red=31
	colored_echo $red "$1"
	exit 1
}

print_current_step() {
	green=32
	[ "${BUILD_ACTION:-}" ] || colored_echo $green "executing step: ${FUNCNAME[1]}"
}

# performs `pushd` inside the cloned repo
clone_repo() {
	repoUrl="$1"
	repoPath="$tempDir/$2"
	branch="$3"
	sparseCheckoutPaths=("${@:4}")

	[ ${#sparseCheckoutPaths[@]} -eq 0 ] || hasSparseCheckout=1

	git clone \
		--branch "$branch" \
		--depth 1 \
		--no-tags \
		--single-branch \
		${hasSparseCheckout:+ --no-checkout --sparse} \
		"$repoUrl.git" "$repoPath"
	pushd "$repoPath"

	if [ "${hasSparseCheckout:-}" ] ; then
		git sparse-checkout set "${sparseCheckoutPaths[@]}"
		git checkout
	fi
}

delete_current_dir_and_popd() {
	clonedRepo=$(pwd)
	popd
	rm -rf "$clonedRepo"
}

build_recipes() {
	for p in "$@" ; do
		IFS_OLD="$IFS"
		IFS=/
		read -r package version <<<"$p"
		IFS="$IFS_OLD"

		if [[ $package == qt ]] ; then
			packagePath="$cciRecipePathQt/5.x.x"
		else
			packagePath="recipes/$package/all"
		fi

		conan create "$packagePath" \
			--version="$version" \
			"${CONAN_PROFILES_ARRAY[@]}" \
			--build=missing \
			--test-folder= \
			--core-conf core.sources.patch:extra_path="$scriptDir/conan_patches" \
			--options "qt/*:android_sdk=${ANDROID_HOME:-}"
	done
}

# build steps

# required: CONAN_PROFILES_JSON_ARRAY env var
prepare() {
	print_current_step
	if [ -z "${CONAN_PROFILES_JSON_ARRAY:-}" ] ; then
		error "CONAN_PROFILES_JSON_ARRAY env var not set"
	fi

	digitRE='[[:digit:]]'
	cmake="cmake/$(cmake --version | grep -E --max-count=1 --only-matching "$digitRE+\\.$digitRE+\\.$digitRE+")"
	ninja="ninja/$(ninja --version)"

	platformTools="
[platform_tool_requires]
$cmake
$ninja

[replace_tool_requires]
cmake/*: $cmake
ninja/*: $ninja

[conf]
tools.cmake.cmaketoolchain:generator=Ninja"

	platformToolsProfile="$tempDir/platformTools"
	echo "$platformTools" > "$platformToolsProfile"

	# builds a list of profile parameters
	profiles=$($python -c '
import json, sys
profilePaths = [f"--profile=\"{sys.argv[1]}/{profile}\"" for profile in json.loads(sys.argv[2])]
print(" ".join(profilePaths))
	' "$scriptDir/conan_profiles" "$CONAN_PROFILES_JSON_ARRAY")

	eval CONAN_PROFILES_ARRAY="($profiles --profile='$platformToolsProfile' \
		--profile:build=default --profile:build='$platformToolsProfile')"

	# CI captures output to place the variable in GitHub env
	echo CONAN_PROFILES="${CONAN_PROFILES_ARRAY[*]}"
}

install_system_libs() {
	print_current_step
	if [ -z "${CONAN_SYSTEM_LIBS:-}" ] ; then
		echo 'no system libs defined, skip'
		return
	fi

	clone_repo 'https://github.com/kambala-decapitator/conan-system-libs' conan-system-libs main

	for p in $CONAN_SYSTEM_LIBS ; do
		conan create "$p" --user system
	done

	delete_current_dir_and_popd
}

build_recipes_with_patches() {
	print_current_step
	clone_repo "https://github.com/conan-io/$cciRepoName" "$cciRepoName" master \
		recipes/minizip \
		recipes/onnx \
		recipes/onnxruntime \

	# versions must be synced with: conan_patches/<package>/conandata.yml
	# if no custom patches are required for a package, it should be removed from here
	build_recipes \
		minizip/1.3.1 \

	# not deleting the cloned repo because it's still used in the next step
	popd
}

build_onnx_recipes_with_patches() {
	print_current_step
	pushd "$tempDir/$cciRepoName"

	# parent of d6cf51e85d5c869bc794a6f68efc5a55834c806e where ONNX* recipes changed
	git fetch --no-tags origin 2d65e6a1500a8be291ddd16ef6360b6edbafd803
	git -c advice.detachedHead=false checkout FETCH_HEAD

	# Patch conan recipe to support msvc version 192
	git apply --ignore-whitespace "$scriptDir/conan_patches/onnxruntime/recipe.diff"

	# order matters! onnxruntime depends on onnx
	build_recipes \
		onnx/1.16.2 \
		onnxruntime/1.18.1 \

	delete_current_dir_and_popd
}

build_recipes_from_cci_pull_requests() {
	print_current_step
    # TODO: remove LuaJIT when https://github.com/conan-io/conan-center-index/pull/26577 is merged
    # TODO: remove Qt5 when the following are merged:
	# - https://github.com/conan-io/conan-center-index/pull/28251
	# - https://github.com/conan-io/conan-center-index/pull/29299

	[[ "${CONAN_OPTIONS:-}" == *"$conanOptionOrdinaryLuaLib"* ]] || buildLuaJit=1

	clone_repo "https://github.com/kambala-decapitator/$cciRepoName" cci-fork vcmi \
		${buildLuaJit:+ recipes/luajit} \
		$cciRecipePathQt \

	build_recipes \
		${buildLuaJit:+ luajit/2.1.0-beta3} \
		qt/5.15.18 \

	delete_current_dir_and_popd
}

build_normal_recipes() {
	print_current_step
	conan install "$scriptDir" \
		--output-folder=conan-generated \
		--build=missing \
		"${CONAN_PROFILES_ARRAY[@]}" \
		${CONAN_OPTIONS_ARRAY:+ "${CONAN_OPTIONS_ARRAY[@]}"}
}

# only for CI
remove_build_requirements_binaries() {
	graphFile="$tempDir/graph.json"
	packageListFile="$tempDir/pkglist.json"

	conan graph info "$scriptDir" \
		"${CONAN_PROFILES_ARRAY[@]}" \
		${CONAN_OPTIONS_ARRAY:+ "${CONAN_OPTIONS_ARRAY[@]}"} \
		--format=json \
		--build=never \
		--no-remote \
		> "$graphFile"
	conan list \
		--graph "$graphFile" \
		--graph-context=build-only \
		--format=json \
		> "$packageListFile"
	conan remove --list "$packageListFile" --confirm

	rm -f "$graphFile" "$packageListFile"
}


# actual script

platform="${1:-${BUILD_PLATFORM:-}}"
case "$platform" in
	android-32|android-armv7|android-armeabi-v7a)
		CONAN_PROFILES_JSON_ARRAY='["android-32-ndk", "base/android-system"]'
		CONAN_SYSTEM_LIBS='zlib'
		;;
	android-64|android-arm64|android-arm64-v8a)
		CONAN_PROFILES_JSON_ARRAY='["android-64-ndk", "base/android-system"]'
		CONAN_SYSTEM_LIBS='zlib'
		;;
	android-x64)
		CONAN_PROFILES_JSON_ARRAY='["android-x64-ndk", "base/android-system"]'
		CONAN_SYSTEM_LIBS='zlib'
		;;
	ios|ios-arm64)
		CONAN_PROFILES_JSON_ARRAY='["ios-arm64", "base/apple-system"]'
		CONAN_SYSTEM_LIBS='bzip2 libiconv sqlite3 zlib'
		;;
	linux-arm64)
		CONAN_PROFILES_JSON_ARRAY='["linux-arm64"]'
		;;
	linux-x64)
		CONAN_PROFILES_JSON_ARRAY='["linux-x64"]'
		;;
	mac-arm|macos-arm64)
		CONAN_PROFILES_JSON_ARRAY='["macos-arm", "base/apple-system"]'
		CONAN_SYSTEM_LIBS='bzip2 libiconv sqlite3 zlib'
		;;
	mac-intel|macos-intel)
		CONAN_PROFILES_JSON_ARRAY='["macos-intel", "base/apple-system"]'
		CONAN_SYSTEM_LIBS='bzip2 libiconv sqlite3 zlib'
		;;
	windows-arm64)
		CONAN_PROFILES_JSON_ARRAY='["msvc-arm64"]'
		CONAN_OPTIONS="--options '$conanOptionOrdinaryLuaLib'"
		;;
	windows-x64)
		CONAN_PROFILES_JSON_ARRAY='["msvc-x64"]'
		CONAN_OPTIONS="--options '$conanOptionTargetPreWindows10'"
		;;
	windows-x86)
		CONAN_PROFILES_JSON_ARRAY='["msvc-x86"]'
		CONAN_OPTIONS="--options '$conanOptionTargetPreWindows10'"
		;;
	*)
		[ -z "$platform" ] || error "unknown platform: $platform"
		;;
esac
eval CONAN_OPTIONS_ARRAY="(${CONAN_OPTIONS:-})"

if $python -c 'import platform; exit(0 if platform.system() == "Windows" else 1)' ; then
	# WINDOWS_PERL_DIR is a workaround for https://bugreports.qt.io/browse/QTBUG-84543
	[ "${WINDOWS_PERL_DIR:-}" ] || error "WINDOWS_PERL_DIR env var not set"
	export PATH="$WINDOWS_PERL_DIR:$PATH"
fi

if [ "${BUILD_ACTION:-}" ] ; then
	# CI stores string variable, but we need an actual array
	[ -z "${CONAN_PROFILES:-}" ] || eval CONAN_PROFILES_ARRAY="($CONAN_PROFILES)"
	"$BUILD_ACTION"
	exit 0
fi

# execute all steps
prepare
install_system_libs
build_recipes_with_patches
build_onnx_recipes_with_patches # TODO: remove after fixing build against new recipes
build_recipes_from_cci_pull_requests
build_normal_recipes
