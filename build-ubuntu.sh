#!/bin/bash
# Byze Coin Build Script for Ubuntu/Debian
# This script installs dependencies and builds Byze with GUI wallet support

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="build"
ENABLE_WALLET=true
ENABLE_GUI=true
ENABLE_IPC=false
ENABLE_ZMQ=false
PARALLEL_JOBS=$(nproc)

# Function to print colored output
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running on Ubuntu/Debian
if [ ! -f /etc/debian_version ]; then
    print_error "This script is designed for Ubuntu/Debian systems"
    exit 1
fi

# Detect Ubuntu version
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    VER=$VERSION_ID
    print_info "Detected: $OS $VER"
else
    print_warn "Cannot detect OS version, proceeding anyway..."
fi

# Check if running as root (needed for apt-get)
if [ "$EUID" -eq 0 ]; then
    print_warn "Running as root. Dependencies will be installed system-wide."
    SUDO=""
else
    print_info "Not running as root. Will use sudo for package installation."
    SUDO="sudo"
    # Check if sudo is available
    if ! command -v sudo &> /dev/null; then
        print_error "sudo is not available. Please run as root or install sudo."
        exit 1
    fi
fi

print_info "Byze Coin Build Script"
print_info "========================"
echo ""

# Update package lists
print_info "Updating package lists..."
$SUDO apt-get update

# Install build essentials
print_info "Installing build essentials..."
$SUDO apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    python3 \
    git

# Install core dependencies
print_info "Installing core dependencies..."
$SUDO apt-get install -y \
    libevent-dev \
    libboost-dev

# Install SQLite (required for wallet)
if [ "$ENABLE_WALLET" = true ]; then
    print_info "Installing SQLite (wallet support)..."
    $SUDO apt-get install -y libsqlite3-dev
fi

# Install Qt6 and GUI dependencies
if [ "$ENABLE_GUI" = true ]; then
    print_info "Installing Qt6 and GUI dependencies..."
    $SUDO apt-get install -y \
        qt6-base-dev \
        qt6-tools-dev \
        qt6-l10n-tools \
        qt6-tools-dev-tools \
        libgl-dev \
        libqrencode-dev
    
    # Optional: Wayland support
    print_info "Installing Wayland support (optional)..."
    $SUDO apt-get install -y qt6-wayland || print_warn "Wayland support installation failed (non-critical)"
fi

# Install ZMQ (optional)
if [ "$ENABLE_ZMQ" = true ]; then
    print_info "Installing ZeroMQ..."
    $SUDO apt-get install -y libzmq3-dev
fi

# Install Cap'n Proto (optional, for IPC)
if [ "$ENABLE_IPC" = true ]; then
    print_info "Installing Cap'n Proto (IPC support)..."
    $SUDO apt-get install -y libcapnp-dev capnproto
fi

# Install USDT tracing (optional)
print_info "Installing USDT tracing support (optional)..."
$SUDO apt-get install -y systemtap-sdt-dev || print_warn "USDT support installation failed (non-critical)"

echo ""
print_info "All dependencies installed successfully!"
echo ""

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    print_error "CMakeLists.txt not found. Please run this script from the Byze source directory."
    exit 1
fi

# Initialize and update git submodules (required for RandomX)
if [ -d ".git" ] || [ -f ".git" ]; then
    print_info "Initializing git submodules..."
    if command -v git &> /dev/null; then
        git submodule update --init --recursive || print_warn "Git submodule update failed. Continuing anyway..."
    else
        print_warn "Git not found. Skipping submodule initialization."
        print_warn "If RandomX build fails, run: git submodule update --init --recursive"
    fi
else
    print_warn "Not a git repository. Skipping submodule initialization."
    print_warn "If RandomX build fails, ensure src/randomx/ directory exists with RandomX source code."
fi

# Clean previous build if it exists
if [ -d "$BUILD_DIR" ]; then
    print_warn "Previous build directory found. Cleaning..."
    rm -rf "$BUILD_DIR"
fi

# Configure build
print_info "Configuring build with CMake..."
CMAKE_ARGS=(
    "-B" "$BUILD_DIR"
    "-DENABLE_WALLET=$ENABLE_WALLET"
    "-DENABLE_IPC=$ENABLE_IPC"
)

if [ "$ENABLE_GUI" = true ]; then
    CMAKE_ARGS+=("-DBUILD_GUI=ON")
fi

if [ "$ENABLE_ZMQ" = true ]; then
    CMAKE_ARGS+=("-DWITH_ZMQ=ON")
fi

cmake "${CMAKE_ARGS[@]}"

# Build
print_info "Building Byze (using $PARALLEL_JOBS parallel jobs)..."
print_info "This may take a while..."
cmake --build "$BUILD_DIR" -j"$PARALLEL_JOBS"

# Check if build was successful
if [ $? -eq 0 ]; then
    echo ""
    print_info "Build completed successfully!"
    echo ""
    print_info "Binaries are located in: $BUILD_DIR/bin/"
    echo ""
    
    # List built binaries
    if [ -d "$BUILD_DIR/bin" ]; then
        print_info "Built executables:"
        ls -lh "$BUILD_DIR/bin/" | grep -E "^-" | awk '{print "  " $9 " (" $5 ")"}'
    fi
    
    echo ""
    print_info "You can now run:"
    print_info "  ./$BUILD_DIR/bin/byzed    # Start the daemon"
    print_info "  ./$BUILD_DIR/bin/byze-qt  # Start the GUI wallet"
    print_info "  ./$BUILD_DIR/bin/byze-cli # Use the CLI"
    echo ""

    print_info "To run tests (optional):"
    print_info "  (cd $BUILD_DIR && ctest -j$PARALLEL_JOBS)"
else
    print_error "Build failed!"
    exit 1
fi

