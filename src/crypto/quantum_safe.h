// Copyright (c) 2024, Byze Project
// Adapted from an earlier quantum-safe prototype implementation
// Distributed under the MIT software license

#ifndef BITCOIN_CRYPTO_QUANTUM_SAFE_H
#define BITCOIN_CRYPTO_QUANTUM_SAFE_H

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <uint256.h>

namespace crypto
{
  // Forward declarations
  class xmss_private_key;
  class xmss_public_key;
  class xmss_signature;
  class sphincs_private_key;
  class sphincs_public_key;
  class sphincs_signature;

  // Quantum-safe signature algorithm types
  enum class quantum_algorithm : uint8_t
  {
    XMSS = 0,
    SPHINCS_PLUS = 1,
    DUAL = 2
  };

  // XMSS (eXtended Merkle Signature Scheme) implementation
  class xmss_private_key
  {
  public:
    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t SIGNATURE_SIZE = 1024;
    static constexpr size_t TREE_HEIGHT = 10;

    xmss_private_key();
    ~xmss_private_key();

    // Generate new XMSS key pair
    bool generate();
    
    // Load from bytes
    bool load(const std::vector<uint8_t>& data);
    
    // Save to bytes
    std::vector<uint8_t> save() const;
    
    // Get public key
    xmss_public_key get_public_key() const;
    
    // Sign message
    xmss_signature sign(const uint256& message) const;
    
    // Get remaining signatures
    uint32_t get_remaining_signatures() const;
    
    // Get tree height
    uint32_t get_tree_height() const;

    // Expose/restore XMSS state for crash-safe index management.
    uint32_t get_index() const;
    bool set_index(uint32_t index);

  private:
    std::vector<uint8_t> m_seed;
    std::vector<uint8_t> m_private_key;
    mutable uint32_t m_index;
    uint32_t m_max_signatures;
  };

  class xmss_public_key
  {
  public:
    static constexpr size_t KEY_SIZE = 32;

    xmss_public_key();
    ~xmss_public_key();

    // Load from bytes
    bool load(const std::vector<uint8_t>& data);
    
    // Save to bytes
    std::vector<uint8_t> save() const;
    
    // Verify signature
    bool verify(const uint256& message, const xmss_signature& signature) const;
    
    // Get public key bytes
    std::vector<uint8_t> get_public_key() const;

  private:
    std::vector<uint8_t> m_public_key;
  };

  class xmss_signature
  {
  public:
    static constexpr size_t SIGNATURE_SIZE = 1024;

    xmss_signature();
    ~xmss_signature();

    // Load from bytes
    bool load(const std::vector<uint8_t>& data);
    
    // Save to bytes
    std::vector<uint8_t> save() const;

  private:
    std::vector<uint8_t> m_signature;
    uint32_t m_index;
  };

  // SPHINCS+ implementation
  class sphincs_private_key
  {
  public:
    static constexpr size_t KEY_SIZE = 64;
    static constexpr size_t SIGNATURE_SIZE = 1024;
    static constexpr size_t TREE_LEVEL = 5;

    sphincs_private_key();
    ~sphincs_private_key();

    // Generate new SPHINCS+ key pair
    bool generate();
    
    // Load from bytes
    bool load(const std::vector<uint8_t>& data);
    
    // Save to bytes
    std::vector<uint8_t> save() const;
    
    // Get public key
    sphincs_public_key get_public_key() const;
    
    // Sign message
    sphincs_signature sign(const uint256& message) const;
    
    // Get tree level
    uint32_t get_level() const;

  private:
    std::vector<uint8_t> m_seed;
    std::vector<uint8_t> m_private_key;
  };

  class sphincs_public_key
  {
  public:
    static constexpr size_t KEY_SIZE = 32;

    sphincs_public_key();
    ~sphincs_public_key();

    // Load from bytes
    bool load(const std::vector<uint8_t>& data);
    
    // Save to bytes
    std::vector<uint8_t> save() const;
    
    // Verify signature
    bool verify(const uint256& message, const sphincs_signature& signature) const;
    
    // Get public key bytes
    std::vector<uint8_t> get_public_key() const;

  private:
    std::vector<uint8_t> m_public_key;
  };

  class sphincs_signature
  {
  public:
    static constexpr size_t SIGNATURE_SIZE = 1024;

    sphincs_signature();
    ~sphincs_signature();

    // Load from bytes
    bool load(const std::vector<uint8_t>& data);
    
    // Save to bytes
    std::vector<uint8_t> save() const;

  private:
    std::vector<uint8_t> m_signature;
  };

  // Quantum-safe signature manager
  class quantum_safe_manager
  {
  public:
    quantum_safe_manager();
    ~quantum_safe_manager();

    // Generate new key pair
    bool generate_keys(quantum_algorithm algo);
    
    // Load keys from file
    bool load_keys(const std::string& filename);
    
    // Save keys to file
    bool save_keys(const std::string& filename) const;
    
    // Sign message
    std::vector<uint8_t> sign(const uint256& message, quantum_algorithm algo) const;
    
    // Verify signature
    bool verify(const uint256& message, const std::vector<uint8_t>& signature, quantum_algorithm algo) const;
    
    // Get public key
    std::vector<uint8_t> get_public_key(quantum_algorithm algo) const;
    
    // Get current algorithm
    quantum_algorithm get_current_algorithm() const;
    
    // Set algorithm
    void set_algorithm(quantum_algorithm algo);

    // Dual algorithm enforcement methods
    bool generate_dual_keys(uint32_t xmss_tree_height = 10, uint32_t sphincs_level = 5);
    bool has_dual_keys() const;
    bool has_old_format_keys() const; // Check if keys are in old vulnerable format
    bool ensure_modern_keys(uint32_t xmss_tree_height = 10, uint32_t sphincs_level = 5); // Auto-migrate old keys
    bool validate_dual_signature(const std::vector<uint8_t>& message, 
                                const std::vector<uint8_t>& xmss_signature,
                                const std::vector<uint8_t>& sphincs_signature) const;
    std::vector<uint8_t> create_dual_signature(const std::vector<uint8_t>& message) const;
    bool verify_dual_signature(const std::vector<uint8_t>& message, 
                              const std::vector<uint8_t>& dual_signature) const;
    
    // Key management for dual enforcement
    bool save_dual_keys(const std::string& filename) const;
    bool load_dual_keys(const std::string& filename);
    /** Serialize dual XMSS/SPHINCS+ keys (same binary layout as save_dual_keys file body). */
    bool serialize_dual_keys(std::vector<uint8_t>& out) const;
    /** Load from buffer produced by serialize_dual_keys or a legacy quantum_wallet.keys file. */
    bool deserialize_dual_keys(const std::vector<uint8_t>& in);
    std::vector<uint8_t> get_dual_public_key() const;
    std::vector<uint8_t> get_dual_public_key_bundle() const;
    std::string get_dual_algorithm_info() const;

    // Helpers for XMSS index reservation/recovery.
    std::optional<uint32_t> get_xmss_index() const;
    bool set_xmss_index(uint32_t index);

  private:
    std::unique_ptr<xmss_private_key> m_xmss_private;
    std::unique_ptr<xmss_public_key> m_xmss_public;
    std::unique_ptr<sphincs_private_key> m_sphincs_private;
    std::unique_ptr<sphincs_public_key> m_sphincs_public;
    quantum_algorithm m_current_algo;
  };

  // Utility functions
  std::string algorithm_to_string(quantum_algorithm algo);
  quantum_algorithm string_to_algorithm(const std::string& str);
  bool is_quantum_safe_enabled();
} // namespace crypto

#endif // BITCOIN_CRYPTO_QUANTUM_SAFE_H

