#!/bin/bash
# Fetch VCV Rack SDK for the appropriate platform
# Uses the stellare-modular mirror which provides pre-built SDKs

set -e

SDK_VERSION="${1:-v2.6.4}"
SDK_REPO="stellare-modular/vcv-rack-sdk"
SDK_DIR="Rack-SDK"

# Detect platform
detect_platform() {
    case "$(uname -s)" in
        Darwin)
            if [ "$(uname -m)" = "arm64" ]; then
                echo "mac-arm64"
            else
                echo "mac-x64"
            fi
            ;;
        Linux)
            echo "lin"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            echo "win"
            ;;
        *)
            echo "Unsupported platform: $(uname -s)" >&2
            exit 1
            ;;
    esac
}

PLATFORM=$(detect_platform)

echo "Fetching VCV Rack SDK ${SDK_VERSION} for ${PLATFORM}..."

# Remove existing SDK if present
if [ -d "$SDK_DIR" ]; then
    echo "Removing existing SDK directory..."
    rm -rf "$SDK_DIR"
fi

# Clone the repo with sparse checkout for only the platform directory
echo "Cloning SDK repository (sparse checkout)..."
git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/${SDK_REPO}.git \
    -b ${SDK_VERSION} \
    ${SDK_DIR}.tmp

cd ${SDK_DIR}.tmp
git sparse-checkout set ${PLATFORM}

# Move platform contents to SDK_DIR
cd ..
mv ${SDK_DIR}.tmp/${PLATFORM} ${SDK_DIR}
rm -rf ${SDK_DIR}.tmp

echo "SDK installed to ${SDK_DIR}/"
echo "Platform: ${PLATFORM}"
echo "Version: ${SDK_VERSION}"
