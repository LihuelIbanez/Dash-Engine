#!/bin/bash
# ─────────────────────────────────────────────────────────────────────────────
# install_app.sh – Build DashEngine, create a .app bundle, install to
#                  /Applications so it appears in Launchpad.
# ─────────────────────────────────────────────────────────────────────────────
set -e

PROJECT_DIR="/Users/lihuelibanez/Development/proyects/Dash-Engine"
BUILD_DIR="$PROJECT_DIR/build"
PKG_DIR="$PROJECT_DIR/packaging"
APP_NAME="DashEngine"
APP_BUNDLE="/Applications/$APP_NAME.app"

echo "=== Building $APP_NAME ==="
cd "$BUILD_DIR"
/opt/homebrew/bin/cmake "$PROJECT_DIR"
/usr/bin/make -j4 DashEngine

echo ""
echo "=== Creating $APP_NAME.app bundle ==="

# Clean previous bundle
/bin/rm -rf "$APP_BUNDLE"

# Create bundle structure
/bin/mkdir -p "$APP_BUNDLE/Contents/MacOS"
/bin/mkdir -p "$APP_BUNDLE/Contents/Resources"

# Copy executable
/bin/cp "$BUILD_DIR/DashEngine" "$APP_BUNDLE/Contents/MacOS/DashEngine"

# Copy Info.plist
/bin/cp "$PKG_DIR/Info.plist" "$APP_BUNDLE/Contents/Info.plist"

# Copy icon
if [ -f "$PKG_DIR/DashEngine.icns" ]; then
    /bin/cp "$PKG_DIR/DashEngine.icns" "$APP_BUNDLE/Contents/Resources/AppIcon.icns"
fi

# PkgInfo
/bin/echo -n "APPL????" > "$APP_BUNDLE/Contents/PkgInfo"

# Reset Launchpad icon cache so it picks up the new app
echo ""
echo "=== Refreshing Launchpad ==="
/usr/bin/defaults write com.apple.dock ResetLaunchPad -bool true 2>/dev/null || true
/usr/bin/killall Dock 2>/dev/null || true

echo ""
echo "✅  $APP_NAME.app installed to /Applications/"
echo "    Open Launchpad to find it!"
