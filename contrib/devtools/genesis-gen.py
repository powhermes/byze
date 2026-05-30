#!/usr/bin/env python3
"""
Byze Genesis Block Generator
Generates a genesis block for Byze Coin with RandomX support
"""

import hashlib
import struct
import time
from binascii import hexlify, unhexlify

def uint256_from_str(s):
    """Convert hex string to uint256 (little-endian)"""
    return int.from_bytes(unhexlify(s), byteorder='little')

def uint256_to_str(val):
    """Convert uint256 to hex string (little-endian)"""
    return hexlify(val.to_bytes(32, byteorder='little')).decode('ascii')

def uint256_from_compact(c):
    """Convert compact representation to uint256"""
    nbytes = (c >> 24) & 0xFF
    if nbytes <= 3:
        return c & 0xFFFFFF
    return (c & 0xFFFFFF) << (8 * (nbytes - 3))

def compact_from_uint256(val):
    """Convert uint256 to compact representation"""
    if val == 0:
        return 0
    nbytes = (val.bit_length() + 7) // 8
    if nbytes <= 3:
        return (nbytes << 24) | val
    return (nbytes << 24) | (val >> (8 * (nbytes - 3)))

def sha256d(data):
    """Double SHA256 hash"""
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def create_genesis_block(pszTimestamp, genesisReward, nTime, nBits, nNonce, nVersion=1):
    """
    Create a genesis block
    
    Args:
        pszTimestamp: Timestamp message
        genesisReward: Initial coin reward (in satoshis)
        nTime: Block timestamp (Unix time)
        nBits: Difficulty target (compact format)
        nNonce: Nonce value
        nVersion: Block version
    """
    # Create coinbase transaction
    txNew = {}
    txNew['version'] = nVersion
    txNew['vin'] = [{
        'prevout': {'hash': '0' * 64, 'n': 0xFFFFFFFF},
        'scriptSig': {
            'length': len(pszTimestamp) + 4,
            'data': struct.pack('<I', 486604799) + pszTimestamp.encode('utf-8')
        },
        'sequence': 0xFFFFFFFF
    }]
    txNew['vout'] = [{
        'value': genesisReward,
        'scriptPubKey': {
            'length': 25,
            'data': unhexlify('04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f41')
        }
    }]
    txNew['nLockTime'] = 0
    
    # Serialize transaction (simplified)
    tx_serialized = struct.pack('<I', txNew['version'])
    # Add inputs
    tx_serialized += b'\x01'  # vin count
    tx_serialized += unhexlify('0' * 64)  # prevout hash
    tx_serialized += struct.pack('<I', 0xFFFFFFFF)  # prevout n
    script_sig = txNew['vin'][0]['scriptSig']['data']
    tx_serialized += struct.pack('<B', len(script_sig))
    tx_serialized += script_sig
    tx_serialized += struct.pack('<I', 0xFFFFFFFF)  # sequence
    # Add outputs
    tx_serialized += b'\x01'  # vout count
    tx_serialized += struct.pack('<Q', genesisReward)  # value
    script_pubkey = txNew['vout'][0]['scriptPubKey']['data']
    tx_serialized += struct.pack('<B', len(script_pubkey))
    tx_serialized += script_pubkey
    tx_serialized += struct.pack('<I', 0)  # nLockTime
    
    # Calculate merkle root (for genesis, it's just the tx hash)
    merkle_root = sha256d(tx_serialized)
    
    # Create block header
    block_header = struct.pack('<I', nVersion)  # version
    block_header += b'\x00' * 32  # hashPrevBlock (all zeros for genesis)
    block_header += merkle_root  # hashMerkleRoot
    block_header += struct.pack('<I', nTime)  # nTime
    block_header += struct.pack('<I', nBits)  # nBits
    block_header += struct.pack('<I', nNonce)  # nNonce
    
    # Calculate block hash
    block_hash = sha256d(block_header)
    
    return {
        'block_header': block_header,
        'block_hash': block_hash,
        'merkle_root': merkle_root,
        'tx': tx_serialized
    }

def mine_genesis(pszTimestamp, genesisReward, nTime, nBits, target_hash):
    """
    Mine a genesis block to find a nonce that produces a hash below target
    """
    print(f"Mining genesis block...")
    print(f"Timestamp: {pszTimestamp}")
    print(f"Target: {target_hash}")
    print(f"Bits: 0x{nBits:08x}")
    
    nNonce = 0
    max_nonce = 0xFFFFFFFF
    
    while nNonce < max_nonce:
        result = create_genesis_block(pszTimestamp, genesisReward, nTime, nBits, nNonce)
        block_hash_hex = hexlify(result['block_hash']).decode('ascii')
        
        # Check if hash is below target
        if int.from_bytes(result['block_hash'], byteorder='little') < target_hash:
            print(f"\nFound genesis block!")
            print(f"Nonce: {nNonce}")
            print(f"Block Hash: {block_hash_hex}")
            print(f"Merkle Root: {hexlify(result['merkle_root']).decode('ascii')}")
            return result, nNonce
        
        if nNonce % 100000 == 0:
            print(f"Trying nonce {nNonce}... (hash: {block_hash_hex})")
        
        nNonce += 1
    
    return None, None

if __name__ == '__main__':
    # Byze Coin Genesis Parameters
    pszTimestamp = "Byze Coin - RandomX CPU Mining Network Launched 2025"
    genesisReward = 50 * 100000000  # 50 coins in satoshis
    nTime = int(time.time())  # Current timestamp
    nBits = 0x1e0ffff0  # Initial difficulty (easier than Bitcoin for faster mining)
    
    # Target hash (easier than Bitcoin)
    target_hash = uint256_from_compact(nBits)
    
    print("=" * 60)
    print("Byze Coin Genesis Block Generator")
    print("=" * 60)
    print(f"\nParameters:")
    print(f"  Timestamp Message: {pszTimestamp}")
    print(f"  Genesis Reward: {genesisReward / 100000000} coins")
    print(f"  Block Time: {nTime} ({time.ctime(nTime)})")
    print(f"  Difficulty Bits: 0x{nBits:08x}")
    print(f"  Target: {hex(target_hash)[2:].zfill(64)}")
    print()
    
    result, nNonce = mine_genesis(pszTimestamp, genesisReward, nTime, nBits, target_hash)
    
    if result:
        print("\n" + "=" * 60)
        print("Genesis Block Generated Successfully!")
        print("=" * 60)
        print(f"\nUse these values in chainparams.cpp:")
        print(f"  nTime: {nTime}")
        print(f"  nNonce: {nNonce}")
        print(f"  nBits: 0x{nBits:08x}")
        print(f"  hashGenesisBlock: 0x{hexlify(result['block_hash']).decode('ascii')}")
        print(f"  hashMerkleRoot: 0x{hexlify(result['merkle_root']).decode('ascii')}")
    else:
        print("\nFailed to find genesis block. Try adjusting difficulty or timestamp.")

