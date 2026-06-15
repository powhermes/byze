// Copyright (c) 2024, Byze Project
// Distributed under the MIT software license

#ifndef BITCOIN_CRYPTO_QUANTUM_SAFE_H
#define BITCOIN_CRYPTO_QUANTUM_SAFE_H

#include <crypto/quantum_safe_config.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <uint256.h>

namespace crypto
{
  class xmss_private_key;
  class xmss_public_key;
  class xmss_signature;
  class sphincs_private_key;
  class sphincs_public_key;
  class sphincs_signature;

  enum class quantum_algorithm : uint8_t
  {
    XMSS = 0,
    SPHINCS_PLUS = 1,
    DUAL = 2
  };

  class xmss_private_key
  {
  public:
    static constexpr size_t PUBKEY_SIZE = BYZE_XMSS_PUBKEY_SIZE;
    static constexpr size_t SIGNATURE_SIZE = BYZE_XMSS_SIGNATURE_SIZE;
    static constexpr size_t TREE_HEIGHT = BYZE_XMSS_HEIGHT;

    xmss_private_key();
    ~xmss_private_key();

    bool generate();
    bool initialize_from_entropy(const unsigned char seed32[32]);
    bool load(const std::vector<uint8_t>& data);
    std::vector<uint8_t> save() const;
    xmss_public_key get_public_key() const;
    xmss_signature sign(const uint256& message) const;
    uint32_t get_remaining_signatures() const;
    uint32_t get_tree_height() const;
    uint32_t get_index() const;
    bool set_index(uint32_t index);
    void attach_public_key(std::vector<uint8_t> pubkey);

  private:
    void free_stfl_secret() const;
    bool ensure_stfl_secret() const;

    std::vector<uint8_t> m_secret_key;
    std::vector<uint8_t> m_public_key;
    /** Live liboqs STFL handle; must not round-trip serialize per signature (crashes xmss_sign). */
    mutable void* m_stfl_secret{nullptr};
  };

  class xmss_public_key
  {
  public:
    static constexpr size_t KEY_SIZE = BYZE_XMSS_PUBKEY_SIZE;

    xmss_public_key();
    ~xmss_public_key();

    bool load(const std::vector<uint8_t>& data);
    std::vector<uint8_t> save() const;
    bool verify(const uint256& message, const xmss_signature& signature) const;
    std::vector<uint8_t> get_public_key() const;

  private:
    std::vector<uint8_t> m_public_key;
  };

  class xmss_signature
  {
  public:
    static constexpr size_t SIGNATURE_SIZE = BYZE_XMSS_SIGNATURE_SIZE;

    xmss_signature();
    ~xmss_signature();

    bool load(const std::vector<uint8_t>& data);
    std::vector<uint8_t> save() const;

  private:
    std::vector<uint8_t> m_signature;
  };

  class sphincs_private_key
  {
  public:
    static constexpr size_t PUBKEY_SIZE = BYZE_SPHINCS_PUBKEY_SIZE;
    static constexpr size_t SECRET_KEY_SIZE = 64;
    static constexpr size_t SIGNATURE_SIZE = BYZE_SPHINCS_SIGNATURE_SIZE;

    sphincs_private_key();
    ~sphincs_private_key();

    bool generate();
    bool initialize_from_entropy128(const unsigned char seed64[64], const unsigned char priv64[64]);
    bool load(const std::vector<uint8_t>& data);
    std::vector<uint8_t> save() const;
    sphincs_public_key get_public_key() const;
    sphincs_signature sign(const uint256& message) const;
    uint32_t get_level() const;
    void attach_public_key(std::vector<uint8_t> pubkey);

  private:
    std::vector<uint8_t> m_secret_key;
    std::vector<uint8_t> m_public_key;
  };

  class sphincs_public_key
  {
  public:
    static constexpr size_t KEY_SIZE = BYZE_SPHINCS_PUBKEY_SIZE;

    sphincs_public_key();
    ~sphincs_public_key();

    bool load(const std::vector<uint8_t>& data);
    std::vector<uint8_t> save() const;
    bool verify(const uint256& message, const sphincs_signature& signature) const;
    std::vector<uint8_t> get_public_key() const;

  private:
    std::vector<uint8_t> m_public_key;
  };

  class sphincs_signature
  {
  public:
    static constexpr size_t SIGNATURE_SIZE = BYZE_SPHINCS_SIGNATURE_SIZE;

    sphincs_signature();
    ~sphincs_signature();

    bool load(const std::vector<uint8_t>& data);
    std::vector<uint8_t> save() const;

  private:
    std::vector<uint8_t> m_signature;
  };

  class quantum_safe_manager
  {
  public:
    quantum_safe_manager();
    ~quantum_safe_manager();

    bool generate_keys(quantum_algorithm algo);
    bool load_keys(const std::string& filename);
    bool save_keys(const std::string& filename) const;
    std::vector<uint8_t> sign(const uint256& message, quantum_algorithm algo) const;
    bool verify(const uint256& message, const std::vector<uint8_t>& signature, quantum_algorithm algo) const;
    std::vector<uint8_t> get_public_key(quantum_algorithm algo) const;
    quantum_algorithm get_current_algorithm() const;
    void set_algorithm(quantum_algorithm algo);

    bool generate_dual_keys(uint32_t xmss_tree_height = 10, uint32_t sphincs_level = 1);
    bool generate_dual_keys_from_entropy_ikm(const unsigned char* ikm, size_t ikm_len, uint32_t xmss_tree_height = 10, uint32_t sphincs_level = 1);
    bool has_dual_keys() const;
    bool has_old_format_keys() const;
    bool ensure_modern_keys(uint32_t xmss_tree_height = 10, uint32_t sphincs_level = 1);
    bool validate_dual_signature(const std::vector<uint8_t>& message,
                                const std::vector<uint8_t>& xmss_signature,
                                const std::vector<uint8_t>& sphincs_signature) const;
    std::vector<uint8_t> create_dual_signature(const std::vector<uint8_t>& message) const;
    bool verify_dual_signature(const std::vector<uint8_t>& message,
                              const std::vector<uint8_t>& dual_signature) const;
    bool save_dual_keys(const std::string& filename) const;
    bool load_dual_keys(const std::string& filename);
    bool serialize_dual_keys(std::vector<uint8_t>& out) const;
    bool deserialize_dual_keys(const std::vector<uint8_t>& in);
    std::vector<uint8_t> get_dual_public_key() const;
    std::vector<uint8_t> get_dual_public_key_bundle() const;
    std::string get_dual_algorithm_info() const;
    std::optional<uint32_t> get_xmss_index() const;
    bool set_xmss_index(uint32_t index);

  private:
    std::unique_ptr<xmss_private_key> m_xmss_private;
    std::unique_ptr<xmss_public_key> m_xmss_public;
    std::unique_ptr<sphincs_private_key> m_sphincs_private;
    std::unique_ptr<sphincs_public_key> m_sphincs_public;
    quantum_algorithm m_current_algo;
  };

  std::string algorithm_to_string(quantum_algorithm algo);
  quantum_algorithm string_to_algorithm(const std::string& str);
  bool is_quantum_safe_enabled();
} // namespace crypto

#endif // BITCOIN_CRYPTO_QUANTUM_SAFE_H
