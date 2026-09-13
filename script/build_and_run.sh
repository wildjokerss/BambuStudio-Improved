#!/usr/bin/env bash
set -euo pipefail

mode="${1:-run}"
case "$mode" in
    run|--build-only|--debug|--logs|--verify) ;;
    *) echo "Usage: $0 [--build-only|--debug|--logs|--verify]" >&2; exit 2 ;;
esac

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$project_dir/build/$(uname -m)"
app_bundle="$build_dir/printer-fix/BambuStudio.app"

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    echo "Configure the macOS build first with ./BuildMac.sh -s -x." >&2
    exit 1
fi

# Do not terminate a slicer that may have an unsaved project or active transfer.
if [[ "$mode" != --build-only ]] && pgrep -x BambuStudio >/dev/null; then
    echo "Save your project and quit Bambu Studio before running this script." >&2
    echo "Use --build-only to prepare the app while keeping your session open." >&2
    exit 1
fi

cmake --build "$build_dir" --target BambuStudio --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
mkdir -p "$app_bundle/Contents"
ditto "$build_dir/src/BambuStudio.app/Contents/MacOS" "$app_bundle/Contents/MacOS"
cp "$build_dir/src/BambuStudio.app/Contents/Info.plist" "$app_bundle/Contents/Info.plist"
ditto "$project_dir/resources" "$app_bundle/Contents/Resources"
# Ad hoc signing makes the local bundle consistent; it does not grant Bambu
# cloud authorization. Use Bambu Connect for that workflow.
codesign --force --sign - "$app_bundle"
echo "Prepared $app_bundle"

case "$mode" in
    --build-only) ;;
    --debug) lldb -- "$app_bundle/Contents/MacOS/BambuStudio" ;;
    --logs)
        open -n "$app_bundle"
        /usr/bin/log stream --info --style compact --predicate 'process == "BambuStudio"'
        ;;
    --verify)
        open -n "$app_bundle"
        sleep 2
        pgrep -x BambuStudio >/dev/null
        ;;
    run) open -n "$app_bundle" ;;
esac
