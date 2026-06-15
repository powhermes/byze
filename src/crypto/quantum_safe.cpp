// Copyright (c) 2024-2026 Byze Project
// Real post-quantum signatures via liboqs (XMSS-SHA2_10_256 + SPHINCS+-SHA2-128s-simple)

#include <crypto/quantum_safe.h>
#include <crypto/quantum_safe_config.h>
#include <crypto/hkdf_sha256_32.h>
#include <crypto/sha3.h>

#include <logging.h>
#include <random.h>
#include <span.h>
#include <uint256.h>

#include <oqs/oqs.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

namespace crypto
{
namespace {

constexpr const char* XMSS_ALG = OQS_SIG_STFL_alg_xmss_sha256_h10;
constexpr const char* SPHINCS_ALG = OQS_SIG_alg_sphincs_sha2_128s_simple;

struct DetRngState {
  uint8_t seed[64]{};
  size_t seed_len{0};
  uint64_t counter{0};
};

std::mutex g_det_rng_mutex;
DetRngState g_det_rng;

uint256 hash_bytes(const unsigned char* data, size_t len)
{
  uint256 result;
  SHA3_256 hash;
  hash.Write(std::span<const unsigned char>(data, len));
  unsigned char out[32];
  hash.Finalize(std::span<unsigned char>(out, 32));
  std::memcpy(result.begin(), out, 32);
  return result;
}

void det_randombytes(uint8_t* random_array, size_t bytes_to_read)
{
  size_t offset = 0;
  while (offset < bytes_to_read) {
    std::vector<uint8_t> input(g_det_rng.seed, g_det_rng.seed + g_det_rng.seed_len);
    for (int i = 0; i < 8; ++i) {
      input.push_back(static_cast<uint8_t>((g_det_rng.counter >> (8 * i)) & 0xff));
    }
    const uint256 block = hash_bytes(input.data(), input.size());
    const size_t n = std::min(bytes_to_read - offset, sizeof(uint256));
    std::memcpy(random_array + offset, block.begin(), n);
    offset += n;
    ++g_det_rng.counter;
  }
}

class DetRngScope
{
public:
  explicit DetRngScope(std::span<const uint8_t> seed)
  {
    std::lock_guard<std::mutex> lock(g_det_rng_mutex);
    g_det_rng.seed_len = std::min(seed.size(), sizeof(g_det_rng.seed));
    std::memcpy(g_det_rng.seed, seed.data(), g_det_rng.seed_len);
    g_det_rng.counter = 0;
    OQS_randombytes_custom_algorithm(det_randombytes);
  }

  ~DetRngScope()
  {
    std::lock_guard<std::mutex> lock(g_det_rng_mutex);
    OQS_randombytes_custom_algorithm(nullptr);
  }
};

struct XmssStoreCtx {
  std::vector<uint8_t>* blob;
};

static OQS_STATUS xmss_secure_store(uint8_t* sk_buf, size_t buf_len, void* context)
{
  auto* ctx = static_cast<XmssStoreCtx*>(context);
  if (!ctx || !ctx->blob) return OQS_ERROR;
  ctx->blob->assign(sk_buf, sk_buf + buf_len);
  return OQS_SUCCESS;
}

static bool SerializeXmssSecret(const OQS_SIG_STFL_SECRET_KEY* sk, std::vector<uint8_t>& out)
{
  uint8_t* buf = nullptr;
  size_t len = 0;
  if (OQS_SIG_STFL_SECRET_KEY_serialize(&buf, &len, sk) != OQS_SUCCESS || !buf) return false;
  out.assign(buf, buf + len);
  OQS_MEM_secure_free(buf, len);
  return true;
}

static OQS_SIG_STFL_SECRET_KEY* DeserializeXmssSecret(const std::vector<uint8_t>& blob)
{
  OQS_SIG_STFL_SECRET_KEY* sk = OQS_SIG_STFL_SECRET_KEY_new(XMSS_ALG);
  if (!sk) return nullptr;
  if (OQS_SIG_STFL_SECRET_KEY_deserialize(sk, blob.data(), blob.size(), nullptr) != OQS_SUCCESS) {
    OQS_SIG_STFL_SECRET_KEY_free(sk);
    return nullptr;
  }
  return sk;
}

static std::vector<uint8_t> MessageBytes(const uint256& message)
{
  return std::vector<uint8_t>(message.begin(), message.end());
}

static uint256 HashMessage(const std::vector<uint8_t>& message)
{
  if (message.size() == sizeof(uint256)) {
    uint256 h;
    std::memcpy(h.begin(), message.data(), sizeof(uint256));
    return h;
  }
  return hash_bytes(message.data(), message.size());
}

struct OqsInit {
  OqsInit() { OQS_init(); }
  ~OqsInit() { OQS_destroy(); }
};
static OqsInit g_oqs_init;

} // namespace

  xmss_private_key::xmss_private_key() = default;
  xmss_private_key::~xmss_private_key() = default;

  bool xmss_private_key::generate()
  {
    if (!OQS_SIG_STFL_alg_is_enabled(XMSS_ALG)) {
      return false;
    }
    OQS_SIG_STFL* sig = OQS_SIG_STFL_new(XMSS_ALG);
    if (!sig) return false;

    OQS_SIG_STFL_SECRET_KEY* sk = OQS_SIG_STFL_SECRET_KEY_new(XMSS_ALG);
    std::vector<uint8_t> pk(PUBKEY_SIZE);
    XmssStoreCtx store_ctx{&m_secret_key};
    bool ok = false;
    if (sk) {
      OQS_SIG_STFL_SECRET_KEY_SET_store_cb(sk, xmss_secure_store, &store_ctx);
      if (OQS_SIG_STFL_keypair(sig, pk.data(), sk) == OQS_SUCCESS) {
        ok = SerializeXmssSecret(sk, m_secret_key);
        m_public_key = std::move(pk);
      }
      OQS_SIG_STFL_SECRET_KEY_free(sk);
    }
    OQS_SIG_STFL_free(sig);
    return ok && m_public_key.size() == PUBKEY_SIZE && !m_secret_key.empty();
  }

  bool xmss_private_key::initialize_from_entropy(const unsigned char seed32[32])
  {
    DetRngScope rng(std::span<const uint8_t>(seed32, 32));
    return generate();
  }

  bool xmss_private_key::load(const std::vector<uint8_t>& data)
  {
    if (data.empty()) return false;
    OQS_SIG_STFL_SECRET_KEY* sk = DeserializeXmssSecret(data);
    if (!sk) return false;
    OQS_SIG_STFL_SECRET_KEY_free(sk);
    m_secret_key = data;
    return true;
  }

  std::vector<uint8_t> xmss_private_key::save() const { return m_secret_key; }

  void xmss_private_key::attach_public_key(std::vector<uint8_t> pubkey)
  {
    if (pubkey.size() == PUBKEY_SIZE) m_public_key = std::move(pubkey);
  }

  xmss_public_key xmss_private_key::get_public_key() const
  {
    xmss_public_key pub;
    if (!m_public_key.empty()) pub.load(m_public_key);
    return pub;
  }

  xmss_signature xmss_private_key::sign(const uint256& message) const
  {
    xmss_signature sig;
    if (m_secret_key.empty() || m_public_key.size() != PUBKEY_SIZE) return sig;

    OQS_SIG_STFL* scheme = OQS_SIG_STFL_new(XMSS_ALG);
    OQS_SIG_STFL_SECRET_KEY* sk = DeserializeXmssSecret(m_secret_key);
    if (!scheme || !sk) {
      OQS_SIG_STFL_free(scheme);
      return sig;
    }

    std::vector<uint8_t> updated_secret = m_secret_key;
    XmssStoreCtx store_ctx{&updated_secret};
    OQS_SIG_STFL_SECRET_KEY_SET_store_cb(sk, xmss_secure_store, &store_ctx);

    std::vector<uint8_t> sig_bytes(SIGNATURE_SIZE, 0);
    size_t sig_len = 0;
    const auto msg = MessageBytes(message);
    if (OQS_SIG_STFL_sign(scheme, sig_bytes.data(), &sig_len, msg.data(), msg.size(), sk) == OQS_SUCCESS
        && sig_len > 0 && sig_len <= SIGNATURE_SIZE) {
      sig.load(sig_bytes);
      const_cast<xmss_private_key*>(this)->m_secret_key = std::move(updated_secret);
    }

    OQS_SIG_STFL_SECRET_KEY_free(sk);
    OQS_SIG_STFL_free(scheme);
    return sig;
  }

  uint32_t xmss_private_key::get_remaining_signatures() const
  {
    OQS_SIG_STFL_SECRET_KEY* sk = DeserializeXmssSecret(m_secret_key);
    if (!sk) return 0;
    OQS_SIG_STFL* scheme = OQS_SIG_STFL_new(XMSS_ALG);
    unsigned long long remain = 0;
    if (scheme && scheme->sigs_remaining) scheme->sigs_remaining(&remain, sk);
    OQS_SIG_STFL_SECRET_KEY_free(sk);
    OQS_SIG_STFL_free(scheme);
    return remain > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(remain);
  }

  uint32_t xmss_private_key::get_tree_height() const { return TREE_HEIGHT; }

  uint32_t xmss_private_key::get_index() const
  {
    OQS_SIG_STFL_SECRET_KEY* sk = DeserializeXmssSecret(m_secret_key);
    if (!sk) return 0;
    OQS_SIG_STFL* scheme = OQS_SIG_STFL_new(XMSS_ALG);
    unsigned long long total = 0, remain = 0;
    if (scheme) {
      if (scheme->sigs_total) scheme->sigs_total(&total, sk);
      if (scheme->sigs_remaining) scheme->sigs_remaining(&remain, sk);
    }
    OQS_SIG_STFL_SECRET_KEY_free(sk);
    OQS_SIG_STFL_free(scheme);
    if (total < remain) return 0;
    return static_cast<uint32_t>(total - remain);
  }

  bool xmss_private_key::set_index(uint32_t index) { (void)index; return false; }

  xmss_public_key::xmss_public_key() { m_public_key.resize(KEY_SIZE, 0); }
  xmss_public_key::~xmss_public_key() = default;

  bool xmss_public_key::load(const std::vector<uint8_t>& data)
  {
    if (data.size() != KEY_SIZE) return false;
    m_public_key = data;
    return true;
  }

  std::vector<uint8_t> xmss_public_key::save() const { return m_public_key; }

  bool xmss_public_key::verify(const uint256& message, const xmss_signature& signature) const
  {
    if (m_public_key.size() != KEY_SIZE) return false;
    OQS_SIG_STFL* scheme = OQS_SIG_STFL_new(XMSS_ALG);
    if (!scheme) return false;
    const auto sig_bytes = signature.save();
    const auto msg = MessageBytes(message);
    const bool ok = OQS_SIG_STFL_verify(scheme, msg.data(), msg.size(), sig_bytes.data(), sig_bytes.size(), m_public_key.data()) == OQS_SUCCESS;
    OQS_SIG_STFL_free(scheme);
    return ok;
  }

  std::vector<uint8_t> xmss_public_key::get_public_key() const { return m_public_key; }

  xmss_signature::xmss_signature() { m_signature.resize(SIGNATURE_SIZE, 0); }
  xmss_signature::~xmss_signature() = default;

  bool xmss_signature::load(const std::vector<uint8_t>& data)
  {
    if (data.size() != SIGNATURE_SIZE) return false;
    m_signature = data;
    return true;
  }

  std::vector<uint8_t> xmss_signature::save() const { return m_signature; }

  sphincs_private_key::sphincs_private_key()
  {
    m_secret_key.resize(SECRET_KEY_SIZE, 0);
    m_public_key.resize(PUBKEY_SIZE, 0);
  }
  sphincs_private_key::~sphincs_private_key() = default;

  bool sphincs_private_key::generate()
  {
    if (!OQS_SIG_alg_is_enabled(SPHINCS_ALG)) return false;
    OQS_SIG* sig = OQS_SIG_new(SPHINCS_ALG);
    if (!sig) return false;
    m_public_key.resize(PUBKEY_SIZE);
    m_secret_key.resize(SECRET_KEY_SIZE);
    const bool ok = OQS_SIG_keypair(sig, m_public_key.data(), m_secret_key.data()) == OQS_SUCCESS;
    OQS_SIG_free(sig);
    return ok;
  }

  bool sphincs_private_key::initialize_from_entropy128(const unsigned char seed64[64], const unsigned char priv64[64])
  {
    (void)priv64;
    DetRngScope rng(std::span<const uint8_t>(seed64, 64));
    return generate();
  }

  bool sphincs_private_key::load(const std::vector<uint8_t>& data)
  {
    if (data.size() != SECRET_KEY_SIZE) return false;
    m_secret_key = data;
    return true;
  }

  std::vector<uint8_t> sphincs_private_key::save() const { return m_secret_key; }

  void sphincs_private_key::attach_public_key(std::vector<uint8_t> pubkey)
  {
    if (pubkey.size() == PUBKEY_SIZE) m_public_key = std::move(pubkey);
  }

  sphincs_public_key sphincs_private_key::get_public_key() const
  {
    sphincs_public_key pub;
    pub.load(m_public_key);
    return pub;
  }

  sphincs_signature sphincs_private_key::sign(const uint256& message) const
  {
    sphincs_signature sig;
    if (m_secret_key.size() != SECRET_KEY_SIZE) return sig;
    OQS_SIG* scheme = OQS_SIG_new(SPHINCS_ALG);
    if (!scheme) return sig;
    std::vector<uint8_t> sig_bytes(SIGNATURE_SIZE, 0);
    size_t sig_len = 0;
    const auto msg = MessageBytes(message);
    if (OQS_SIG_sign(scheme, sig_bytes.data(), &sig_len, msg.data(), msg.size(), m_secret_key.data()) == OQS_SUCCESS
        && sig_len > 0 && sig_len <= SIGNATURE_SIZE) {
      sig.load(sig_bytes);
    }
    OQS_SIG_free(scheme);
    return sig;
  }

  uint32_t sphincs_private_key::get_level() const { return BYZE_SPHINCS_LEVEL; }

  sphincs_public_key::sphincs_public_key() { m_public_key.resize(KEY_SIZE, 0); }
  sphincs_public_key::~sphincs_public_key() = default;

  bool sphincs_public_key::load(const std::vector<uint8_t>& data)
  {
    if (data.size() != KEY_SIZE) return false;
    m_public_key = data;
    return true;
  }

  std::vector<uint8_t> sphincs_public_key::save() const { return m_public_key; }

  bool sphincs_public_key::verify(const uint256& message, const sphincs_signature& signature) const
  {
    if (m_public_key.size() != KEY_SIZE) return false;
    OQS_SIG* scheme = OQS_SIG_new(SPHINCS_ALG);
    if (!scheme) return false;
    const auto sig_bytes = signature.save();
    const auto msg = MessageBytes(message);
    const bool ok = OQS_SIG_verify(scheme, msg.data(), msg.size(), sig_bytes.data(), sig_bytes.size(), m_public_key.data()) == OQS_SUCCESS;
    OQS_SIG_free(scheme);
    return ok;
  }

  std::vector<uint8_t> sphincs_public_key::get_public_key() const { return m_public_key; }

  sphincs_signature::sphincs_signature() { m_signature.resize(SIGNATURE_SIZE, 0); }
  sphincs_signature::~sphincs_signature() = default;

  bool sphincs_signature::load(const std::vector<uint8_t>& data)
  {
    if (data.size() != SIGNATURE_SIZE) return false;
    m_signature = data;
    return true;
  }

  std::vector<uint8_t> sphincs_signature::save() const { return m_signature; }

  quantum_safe_manager::quantum_safe_manager() : m_current_algo(quantum_algorithm::XMSS) {}
  quantum_safe_manager::~quantum_safe_manager() = default;

  bool quantum_safe_manager::generate_keys(quantum_algorithm algo)
  {
    switch (algo) {
    case quantum_algorithm::XMSS: {
      m_xmss_private = std::make_unique<xmss_private_key>();
      if (!m_xmss_private->generate()) return false;
      m_xmss_public = std::make_unique<xmss_public_key>(m_xmss_private->get_public_key());
      m_current_algo = algo;
      return true;
    }
    case quantum_algorithm::SPHINCS_PLUS: {
      m_sphincs_private = std::make_unique<sphincs_private_key>();
      if (!m_sphincs_private->generate()) return false;
      m_sphincs_public = std::make_unique<sphincs_public_key>(m_sphincs_private->get_public_key());
      m_current_algo = algo;
      return true;
    }
    case quantum_algorithm::DUAL:
      return generate_dual_keys();
    }
    return false;
  }

  bool quantum_safe_manager::load_keys(const std::string& filename) { return load_dual_keys(filename); }
  bool quantum_safe_manager::save_keys(const std::string& filename) const { return save_dual_keys(filename); }

  std::vector<uint8_t> quantum_safe_manager::sign(const uint256& message, quantum_algorithm algo) const
  {
    if (algo == quantum_algorithm::XMSS && m_xmss_private) return m_xmss_private->sign(message).save();
    if (algo == quantum_algorithm::SPHINCS_PLUS && m_sphincs_private) return m_sphincs_private->sign(message).save();
    return {};
  }

  bool quantum_safe_manager::verify(const uint256& message, const std::vector<uint8_t>& signature, quantum_algorithm algo) const
  {
    if (algo == quantum_algorithm::XMSS && m_xmss_public) {
      xmss_signature sig;
      return sig.load(signature) && m_xmss_public->verify(message, sig);
    }
    if (algo == quantum_algorithm::SPHINCS_PLUS && m_sphincs_public) {
      sphincs_signature sig;
      return sig.load(signature) && m_sphincs_public->verify(message, sig);
    }
    return false;
  }

  std::vector<uint8_t> quantum_safe_manager::get_public_key(quantum_algorithm algo) const
  {
    if (algo == quantum_algorithm::XMSS && m_xmss_public) return m_xmss_public->save();
    if (algo == quantum_algorithm::SPHINCS_PLUS && m_sphincs_public) return m_sphincs_public->save();
    return {};
  }

  quantum_algorithm quantum_safe_manager::get_current_algorithm() const { return m_current_algo; }
  void quantum_safe_manager::set_algorithm(quantum_algorithm algo) { m_current_algo = algo; }

  bool quantum_safe_manager::has_dual_keys() const
  {
    return m_xmss_private && m_xmss_public && m_sphincs_private && m_sphincs_public;
  }

  bool quantum_safe_manager::has_old_format_keys() const
  {
    if (!has_dual_keys()) return false;
    return m_xmss_public->save().size() != BYZE_XMSS_PUBKEY_SIZE
        || m_sphincs_public->save().size() != BYZE_SPHINCS_PUBKEY_SIZE;
  }

  bool quantum_safe_manager::ensure_modern_keys(uint32_t, uint32_t)
  {
    if (!has_dual_keys() || has_old_format_keys()) return generate_dual_keys();
    return true;
  }

  bool quantum_safe_manager::generate_dual_keys(uint32_t, uint32_t)
  {
    m_xmss_private = std::make_unique<xmss_private_key>();
    m_sphincs_private = std::make_unique<sphincs_private_key>();
    if (!m_xmss_private->generate() || !m_sphincs_private->generate()) return false;
    m_xmss_public = std::make_unique<xmss_public_key>(m_xmss_private->get_public_key());
    m_sphincs_public = std::make_unique<sphincs_public_key>(m_sphincs_private->get_public_key());
    m_current_algo = quantum_algorithm::DUAL;
    return true;
  }

  bool quantum_safe_manager::generate_dual_keys_from_entropy_ikm(const unsigned char* ikm, size_t ikm_len, uint32_t, uint32_t)
  {
    CHKDF_HMAC_SHA256_L32 hkdf(ikm, ikm_len, std::string{"byze_quantum_hd_v2"});
    unsigned char xmss_seed[32];
    unsigned char sph_seed[64];
    hkdf.Expand32(std::string{"byze/q/xmss_seed"}, xmss_seed);
    hkdf.Expand32(std::string{"byze/q/sph_seed_a"}, sph_seed);
    hkdf.Expand32(std::string{"byze/q/sph_seed_b"}, sph_seed + 32);

    m_xmss_private = std::make_unique<xmss_private_key>();
    m_sphincs_private = std::make_unique<sphincs_private_key>();
    if (!m_xmss_private->initialize_from_entropy(xmss_seed)) return false;
    if (!m_sphincs_private->initialize_from_entropy128(sph_seed, sph_seed)) return false;
    m_xmss_public = std::make_unique<xmss_public_key>(m_xmss_private->get_public_key());
    m_sphincs_public = std::make_unique<sphincs_public_key>(m_sphincs_private->get_public_key());
    m_current_algo = quantum_algorithm::DUAL;
    return true;
  }

  bool quantum_safe_manager::validate_dual_signature(const std::vector<uint8_t>& message,
      const std::vector<uint8_t>& xmss_signature_bytes,
      const std::vector<uint8_t>& sphincs_signature_bytes) const
  {
    const uint256 hash = HashMessage(message);
    xmss_signature xs;
    sphincs_signature ss;
    if (!xs.load(xmss_signature_bytes) || !ss.load(sphincs_signature_bytes)) return false;
    return m_xmss_public && m_sphincs_public
        && m_xmss_public->verify(hash, xs)
        && m_sphincs_public->verify(hash, ss);
  }

  std::vector<uint8_t> quantum_safe_manager::create_dual_signature(const std::vector<uint8_t>& message) const
  {
    const uint256 hash = HashMessage(message);
    std::vector<uint8_t> out;
    if (!m_xmss_private || !m_sphincs_private) return out;
    const auto xmss_sig = m_xmss_private->sign(hash).save();
    const auto sphincs_sig = m_sphincs_private->sign(hash).save();
    uint32_t xlen = static_cast<uint32_t>(xmss_sig.size());
    uint32_t slen = static_cast<uint32_t>(sphincs_sig.size());
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&xlen), reinterpret_cast<uint8_t*>(&xlen) + 4);
    out.insert(out.end(), xmss_sig.begin(), xmss_sig.end());
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&slen), reinterpret_cast<uint8_t*>(&slen) + 4);
    out.insert(out.end(), sphincs_sig.begin(), sphincs_sig.end());
    return out;
  }

  bool quantum_safe_manager::verify_dual_signature(const std::vector<uint8_t>& message, const std::vector<uint8_t>& dual_signature) const
  {
    if (dual_signature.size() < 8) return false;
    uint32_t xlen = 0, slen = 0;
    std::memcpy(&xlen, dual_signature.data(), 4);
    if (4 + xlen + 4 > dual_signature.size()) return false;
    std::memcpy(&slen, dual_signature.data() + 4 + xlen, 4);
    if (4 + xlen + 4 + slen != dual_signature.size()) return false;
    return validate_dual_signature(message,
        std::vector<uint8_t>(dual_signature.begin() + 4, dual_signature.begin() + 4 + xlen),
        std::vector<uint8_t>(dual_signature.begin() + 4 + xlen + 4, dual_signature.end()));
  }

  static bool WriteBlob(std::ostream& out, const std::vector<uint8_t>& blob)
  {
    const uint32_t sz = static_cast<uint32_t>(blob.size());
    out.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    out.write(reinterpret_cast<const char*>(blob.data()), sz);
    return static_cast<bool>(out);
  }

  static bool ReadBlob(std::istream& in, std::vector<uint8_t>& blob)
  {
    uint32_t sz = 0;
    in.read(reinterpret_cast<char*>(&sz), sizeof(sz));
    if (!in || sz == 0 || sz > 1'000'000) return false;
    blob.resize(sz);
    in.read(reinterpret_cast<char*>(blob.data()), sz);
    return static_cast<bool>(in);
  }

  bool quantum_safe_manager::save_dual_keys(const std::string& filename) const
  {
    if (!has_dual_keys()) return false;
    const std::string temp = filename + ".tmp";
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    const uint32_t magic = 0x5146534B;
    const uint8_t version = BYZE_QUANTUM_KEY_FORMAT_VERSION;
    const uint8_t algo_byte = static_cast<uint8_t>(quantum_algorithm::DUAL);
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&algo_byte), sizeof(algo_byte));
    if (!WriteBlob(file, m_xmss_private->save()) || !WriteBlob(file, m_xmss_public->save())
        || !WriteBlob(file, m_sphincs_private->save()) || !WriteBlob(file, m_sphincs_public->save())) {
      return false;
    }
    file.close();
    return std::rename(temp.c_str(), filename.c_str()) == 0;
  }

  bool quantum_safe_manager::load_dual_keys(const std::string& filename)
  {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    uint32_t magic = 0;
    uint8_t version = 0, algo_byte = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&algo_byte), sizeof(algo_byte));
    if (magic != 0x5146534B || version != BYZE_QUANTUM_KEY_FORMAT_VERSION) return false;
    if (static_cast<quantum_algorithm>(algo_byte) != quantum_algorithm::DUAL) return false;

    std::vector<uint8_t> xmss_priv, xmss_pub, sph_priv, sph_pub;
    if (!ReadBlob(file, xmss_priv) || !ReadBlob(file, xmss_pub) || !ReadBlob(file, sph_priv) || !ReadBlob(file, sph_pub)) {
      return false;
    }
    m_xmss_private = std::make_unique<xmss_private_key>();
    m_xmss_public = std::make_unique<xmss_public_key>();
    m_sphincs_private = std::make_unique<sphincs_private_key>();
    m_sphincs_public = std::make_unique<sphincs_public_key>();
    if (!m_xmss_private->load(xmss_priv) || !m_xmss_public->load(xmss_pub)) return false;
    if (!m_sphincs_private->load(sph_priv) || !m_sphincs_public->load(sph_pub)) return false;
    m_xmss_private->attach_public_key(xmss_pub);
    m_sphincs_private->attach_public_key(sph_pub);
    m_current_algo = quantum_algorithm::DUAL;
    return true;
  }

  bool quantum_safe_manager::serialize_dual_keys(std::vector<uint8_t>& out) const
  {
    if (!has_dual_keys()) return false;
    std::ostringstream oss(std::ios::binary);
    const uint32_t magic = 0x5146534B;
    const uint8_t version = BYZE_QUANTUM_KEY_FORMAT_VERSION;
    const uint8_t algo_byte = static_cast<uint8_t>(quantum_algorithm::DUAL);
    oss.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    oss.write(reinterpret_cast<const char*>(&version), sizeof(version));
    oss.write(reinterpret_cast<const char*>(&algo_byte), sizeof(algo_byte));
    if (!WriteBlob(oss, m_xmss_private->save()) || !WriteBlob(oss, m_xmss_public->save())
        || !WriteBlob(oss, m_sphincs_private->save()) || !WriteBlob(oss, m_sphincs_public->save())) {
      return false;
    }
    const std::string s = oss.str();
    out.assign(s.begin(), s.end());
    return true;
  }

  bool quantum_safe_manager::deserialize_dual_keys(const std::vector<uint8_t>& in)
  {
    std::istringstream iss(std::string(reinterpret_cast<const char*>(in.data()), in.size()), std::ios::binary);
    uint32_t magic = 0;
    uint8_t version = 0, algo_byte = 0;
    iss.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    iss.read(reinterpret_cast<char*>(&version), sizeof(version));
    iss.read(reinterpret_cast<char*>(&algo_byte), sizeof(algo_byte));
    if (magic != 0x5146534B || version != BYZE_QUANTUM_KEY_FORMAT_VERSION) return false;
    if (static_cast<quantum_algorithm>(algo_byte) != quantum_algorithm::DUAL) return false;
    std::vector<uint8_t> xmss_priv, xmss_pub, sph_priv, sph_pub;
    if (!ReadBlob(iss, xmss_priv) || !ReadBlob(iss, xmss_pub) || !ReadBlob(iss, sph_priv) || !ReadBlob(iss, sph_pub)) {
      return false;
    }
    m_xmss_private = std::make_unique<xmss_private_key>();
    m_xmss_public = std::make_unique<xmss_public_key>();
    m_sphincs_private = std::make_unique<sphincs_private_key>();
    m_sphincs_public = std::make_unique<sphincs_public_key>();
    if (!m_xmss_private->load(xmss_priv) || !m_xmss_public->load(xmss_pub)) return false;
    if (!m_sphincs_private->load(sph_priv) || !m_sphincs_public->load(sph_pub)) return false;
    m_xmss_private->attach_public_key(xmss_pub);
    m_sphincs_private->attach_public_key(sph_pub);
    m_current_algo = quantum_algorithm::DUAL;
    return true;
  }

  std::vector<uint8_t> quantum_safe_manager::get_dual_public_key() const
  {
    const auto bundle = get_dual_public_key_bundle();
    if (bundle.empty()) return {};
    std::vector<uint8_t> out(32);
    const uint256 h = hash_bytes(bundle.data(), bundle.size());
    std::memcpy(out.data(), h.begin(), 32);
    return out;
  }

  std::vector<uint8_t> quantum_safe_manager::get_dual_public_key_bundle() const
  {
    if (!has_dual_keys()) return {};
    std::vector<uint8_t> bundle;
    const auto xp = m_xmss_public->save();
    const auto sp = m_sphincs_public->save();
    bundle.reserve(xp.size() + sp.size());
    bundle.insert(bundle.end(), xp.begin(), xp.end());
    bundle.insert(bundle.end(), sp.begin(), sp.end());
    return bundle;
  }

  std::string quantum_safe_manager::get_dual_algorithm_info() const
  {
    return "DUAL: XMSS-SHA2_10_256 + SPHINCS+-SHA2-128s-simple (liboqs)";
  }

  std::optional<uint32_t> quantum_safe_manager::get_xmss_index() const
  {
    if (!m_xmss_private) return std::nullopt;
    return m_xmss_private->get_index();
  }

  bool quantum_safe_manager::set_xmss_index(uint32_t index)
  {
    if (!m_xmss_private) return false;
    return m_xmss_private->set_index(index);
  }

  std::string algorithm_to_string(quantum_algorithm algo)
  {
    switch (algo) {
    case quantum_algorithm::XMSS: return "XMSS";
    case quantum_algorithm::SPHINCS_PLUS: return "SPHINCS+";
    case quantum_algorithm::DUAL: return "DUAL";
    }
    return "UNKNOWN";
  }

  quantum_algorithm string_to_algorithm(const std::string& str)
  {
    if (str == "XMSS") return quantum_algorithm::XMSS;
    if (str == "SPHINCS+" || str == "SPHINCS_PLUS") return quantum_algorithm::SPHINCS_PLUS;
    if (str == "DUAL") return quantum_algorithm::DUAL;
    return quantum_algorithm::XMSS;
  }

  bool is_quantum_safe_enabled() { return BYZE_QUANTUM_SAFE_ENABLED != 0; }

} // namespace crypto
