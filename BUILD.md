# Building Byze Coin

## Quick Build (Ubuntu/Debian)

The easiest way to build Byze on Ubuntu/Debian is using the provided build script:

```bash
./build-ubuntu.sh
```

This script will:
- Install all required dependencies
- Configure the build with GUI and wallet support
- Build the project using all available CPU cores
- Optionally run tests

### Requirements

- Ubuntu 22.04 LTS or later (or Debian equivalent)
- Internet connection (for downloading dependencies)
- sudo access (for installing packages)
- At least 4GB RAM (8GB+ recommended)
- At least 10GB free disk space

### Manual Build

If you prefer to build manually:

#### 1. Install Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake pkg-config python3 \
    libevent-dev libboost-dev libsqlite3-dev \
    qt6-base-dev qt6-tools-dev qt6-l10n-tools \
    qt6-tools-dev-tools libgl-dev libqrencode-dev
```

#### 2. Configure Build

```bash
cmake -B build -DENABLE_WALLET=ON -DENABLE_IPC=OFF -DBUILD_GUI=ON
```

#### 3. Build

```bash
cmake --build build -j$(nproc)
```

#### 4. Run Tests (Optional)

```bash
cd build
ctest -j$(nproc)
```

## Build Options

The build script supports several configuration options. Edit `build-ubuntu.sh` to modify:

- `ENABLE_WALLET=true` - Enable wallet support (requires SQLite)
- `ENABLE_GUI=true` - Enable Qt GUI wallet
- `ENABLE_IPC=false` - Disable multiprocess support (requires Cap'n Proto)
- `ENABLE_ZMQ=false` - Disable ZeroMQ notifications
- `PARALLEL_JOBS=$(nproc)` - Number of parallel build jobs

## Output

After a successful build, binaries will be in `build/bin/`:

- `bitcoind` - The daemon/node
- `bitcoin-qt` - GUI wallet
- `bitcoin-cli` - Command-line interface
- `bitcoin-tx` - Transaction utility
- `bitcoin-util` - Utility tool
- `bitcoin-wallet` - Wallet management tool
- `test_bitcoin` - Test suite

## Troubleshooting

### Out of Memory

If the build fails due to memory issues, reduce parallel jobs:

```bash
cmake --build build -j2  # Use only 2 parallel jobs
```

### Missing Dependencies

If you get dependency errors, ensure all packages are installed:

```bash
sudo apt-get update
sudo apt-get install -y $(cat build-ubuntu.sh | grep "apt-get install" | head -1)
```

### Qt6 Not Found

If Qt6 is not found, ensure you're on Ubuntu 22.04+ or install Qt6 manually:

```bash
sudo apt-get install -y qt6-base-dev qt6-tools-dev
```

## Cross-Compilation

For cross-compilation, see the `depends/` directory and use the depends system:

```bash
cd depends
make HOST=x86_64-pc-linux-gnu
cd ..
./configure --prefix=$PWD/depends/x86_64-pc-linux-gnu
make
```

## Development Build

For development with debugging symbols:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_WALLET=ON -DBUILD_GUI=ON
cmake --build build -j$(nproc)
```

