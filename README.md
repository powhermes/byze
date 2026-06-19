# Byze — Quantum-hardened RandomX blockchain

**Ecosystem hub:** explorer, browser wallet, mining pool, downloads, and open-source development.

| | |
|---|---|
| **Website** | https://byze.org/ |
| **Explorer** | https://explorer.byze.org/ |
| **Pool** | https://pool.byze.org/pool/ |
| **GitHub** | https://github.com/powhermes/byze |
| **Downloads** | https://github.com/powhermes/byze/releases/tag/v0.2.0-mainnet |
| **Miner** | https://github.com/powhermes/byze-miner |
| **Web wallet** | https://wallet.byze.org/ |

> **Official org only:** `github.com/powhermes`. We are **not** affiliated with `github.com/byze-chain` or [this copycat Bitcointalk ANN](https://bitcointalk.org/index.php?topic=5586169.0). Build and download only from the links above.

## About

Byze (BYZ) is a Bitcoin Core-derived chain with:
- **RandomX** proof-of-work (CPU-friendly; replaces SHA256d)
- **Dual post-quantum block signatures**: **XMSS + SPHINCS+** enforced on mainnet blocks

## Status

Mainnet genesis has been mined and `chainparams` updated. `byze-qt` loads and the full Byze binary set is produced (e.g. `byzed`, `byze-cli`, `byze-qt`, `byze-wallet`, `byze-tx`, `byze-util`).

## What is Byze?

Byze is an experimental blockchain project that explores:
- CPU-friendly mining via RandomX algorithm
- Post-quantum cryptography with XMSS and SPHINCS+ signatures
- Bitcoin-compatible transaction and block structures

While Byze is a Bitcoin Core fork, it is **not Bitcoin**. It has Byze-specific consensus and address/network parameters.

## Tokenomics (Mainnet)

- **Ticker / unit**: BYZ
- **Target block time**: 10 minutes
- **Initial block subsidy**: 50 BYZ
- **Halving interval**: 210,000 blocks
- **Supply (Bitcoin-style schedule)**: approaches ~21,000,000 BYZ over time (subsidy halves until it reaches 0)

Source of truth:
- Subsidy schedule: `src/validation.cpp` (`GetBlockSubsidy`)
- Consensus parameters (spacing/halving): `src/kernel/chainparams.cpp`

## Build (Ubuntu)

```bash
./build-ubuntu.sh
```

## Run

```bash
# Start node (mainnet)
./src/byzed -daemon

# Basic RPC sanity check
./src/byze-cli getblockchaininfo
```

## Mining notes (RandomX + quantum block signatures)

- **RandomX is consensus PoW**: block headers must satisfy `CheckProofOfWork()` using `RandomXHash()`.
- **Mainnet requires XMSS + SPHINCS+ block signatures**: blocks without both signatures are rejected by consensus (`CheckBlock()`).
- Built-in CPU mining paths (`startmining`, `generatetoaddress`, `generatetodescriptor`) add the required quantum signatures automatically after finding a valid nonce.
- External miners using `getblocktemplate` must ensure the submitted block includes the required quantum signature tail before calling `submitblock`.

Further information about Bitcoin Core is available in the [doc folder](/doc).

License
-------

Byze is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/license/MIT.

Byze is based on [Bitcoin Core](https://github.com/bitcoin/bitcoin), which is also MIT-licensed.

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
