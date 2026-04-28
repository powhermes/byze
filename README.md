Byze - Bitcoin-Derived Experimental Chain with RandomX + Quantum-Safe Signatures
========================================================================================

**⚠️ WARNING: This is an experimental project. Current release (v0.1.0-regtest-stable) is REGTEST-ONLY and NOT FOR MAINNET USE. ⚠️**

Byze is a Bitcoin Core fork that integrates:
- **RandomX** CPU-based proof-of-work mining (replacing SHA256)
- **XMSS/SPHINCS+** quantum-safe signature schemes
- Experimental consensus modifications for quantum-resistant blockchain

## Current Status: v0.1.0-regtest-stable

This release represents a **stable regtest baseline** with working RandomX mining and quantum-safe signatures. It is intended **ONLY for development and testing** in regtest mode.

### ⚠️ Important Limitations

- **Regtest-only**: No testnet or mainnet support yet
- **Experimental**: Consensus rules not finalized for production
- **Not audited**: Security review pending
- **Do not use with real funds**

## What is Byze?

Byze is an experimental blockchain project that explores:
- CPU-friendly mining via RandomX algorithm
- Post-quantum cryptography with XMSS and SPHINCS+ signatures
- Bitcoin-compatible transaction and block structures

The current release (v0.1.0-regtest-stable) provides a working foundation for:
- Regtest development and testing
- Mining infrastructure validation
- Quantum-safe signature integration verification
- Future testnet preparation

## Quick Start (Regtest Only)

```bash
# Build (see BUILD.md for details)
mkdir build && cd build
cmake ..
make -j$(nproc)

# Start regtest daemon
./build/bin/byzed -regtest -daemon

# Create wallet and mine blocks
./build/bin/byze-cli -regtest createwallet test
ADDR=$(./build/bin/byze-cli -regtest -rpcwallet=test getnewaddress)
./build/bin/byze-cli -regtest -rpcwallet=test generatetoaddress 1 "$ADDR"
```

See [MINING_GUIDE.md](MINING_GUIDE.md) and [RELEASE_NOTES_v0.1.0.md](RELEASE_NOTES_v0.1.0.md) for detailed information.

Further information about Bitcoin Core is available in the [doc folder](/doc).

License
-------

Bitcoin Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/license/MIT.

Development Process
-------------------

The `master` branch is regularly built (see `doc/build-*.md` for instructions) and tested, but it is not guaranteed to be
completely stable. [Tags](https://github.com/bitcoin/bitcoin/tags) are created
regularly from release branches to indicate new official, stable release versions of Bitcoin Core.

The https://github.com/bitcoin-core/gui repository is used exclusively for the
development of the GUI. Its master branch is identical in all monotree
repositories. Release branches and tags do not exist, so please do not fork
that repository unless it is for development reasons.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md)
and useful hints for developers can be found in [doc/developer-notes.md](doc/developer-notes.md).

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled during the generation of the build system) with: `ctest`. Further details on running
and extending unit tests can be found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python.
These tests can be run (if the [test dependencies](/test) are installed) with: `build/test/functional/test_runner.py`
(assuming `build` is your build directory).

The CI (Continuous Integration) systems make sure that every pull request is tested on Windows, Linux, and macOS.
The CI must pass on all commits before merge to avoid unrelated CI failures on new pull requests.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.

Translations
------------

Changes to translations as well as new translations can be submitted to
[Bitcoin Core's Transifex page](https://explore.transifex.com/bitcoin/bitcoin/).

Translations are periodically pulled from Transifex and merged into the git repository. See the
[translation process](doc/translation_process.md) for details on how this works.

**Important**: We do not accept translation changes as GitHub pull requests because the next
pull from Transifex would automatically overwrite them again.
