#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

SCHEME="${FACEKIT_SCHEME:-FaceKit}"
PROJECT_PATH="${FACEKIT_PROJECT_PATH:-$REPO_ROOT/build/osx/FFGLPlugins.xcodeproj}"
CONFIGURATION="${FACEKIT_CONFIGURATION:-Release}"
DERIVED_DATA_PATH="${FACEKIT_DERIVED_DATA_PATH:-/tmp/ffgl-facekit-xcode-task-dd}"
XCODEBUILD_HOME="${XCODEBUILD_HOME:-$HOME}"

PLUGIN_NAME="FaceKit.bundle"
BUILT_PLUGIN_PATH="${FACEKIT_BUILT_PLUGIN_PATH:-$REPO_ROOT/binaries/${CONFIGURATION:l}/$PLUGIN_NAME}"
RESOLUME_PLUGIN_DIR="${RESOLUME_PLUGIN_DIR:-$HOME/Documents/Resolume Arena/Extra Effects}"
INSTALLED_PLUGIN_PATH="$RESOLUME_PLUGIN_DIR/$PLUGIN_NAME"
ARCHIVE_DIR="${FACEKIT_ARCHIVE_DIR:-$REPO_ROOT/plugin-archives/FaceKit}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"

echo "Building $SCHEME..."
HOME="$XCODEBUILD_HOME" xcodebuild \
  -project "$PROJECT_PATH" \
  -scheme "$SCHEME" \
  -configuration "$CONFIGURATION" \
  -destination 'generic/platform=macOS' \
  -derivedDataPath "$DERIVED_DATA_PATH" \
  build

if [[ ! -d "$BUILT_PLUGIN_PATH" ]]; then
  echo "Built plugin not found: $BUILT_PLUGIN_PATH" >&2
  exit 1
fi

mkdir -p "$ARCHIVE_DIR"
mkdir -p "$RESOLUME_PLUGIN_DIR"

if [[ -d "$INSTALLED_PLUGIN_PATH" ]]; then
  ARCHIVED_PLUGIN_PATH="$ARCHIVE_DIR/FaceKit-$TIMESTAMP.bundle"
  echo "Archiving existing installed plugin to $ARCHIVED_PLUGIN_PATH"
  mv "$INSTALLED_PLUGIN_PATH" "$ARCHIVED_PLUGIN_PATH"
fi

mkdir -p "$INSTALLED_PLUGIN_PATH"
echo "Installing new plugin to $INSTALLED_PLUGIN_PATH"
rsync -a --delete "$BUILT_PLUGIN_PATH/" "$INSTALLED_PLUGIN_PATH/"

echo "Installed FaceKit."
echo "Archive directory: $ARCHIVE_DIR"
