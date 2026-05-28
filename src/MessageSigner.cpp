#include "MessageSigner.hpp"
#include "Logger.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>

using json = nlohmann::json;

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string read_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string MessageSigner::base64_encode(const std::vector<uint8_t>& data)
{
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data.data(), (int)data.size());
    BIO_flush(b64);

    char*  buf  = nullptr;
    long   len  = BIO_get_mem_data(mem, &buf);
    std::string result(buf, (size_t)len);
    BIO_free_all(b64);
    return result;
}

std::string MessageSigner::sha256_hex(const std::vector<uint8_t>& data)
{
    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(data.data(), data.size(), digest);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : digest)
        oss << std::setw(2) << (int)b;
    return oss.str();
}

// ── MessageSigner ─────────────────────────────────────────────────────────────

MessageSigner::MessageSigner()  = default;
MessageSigner::~MessageSigner() = default;

bool MessageSigner::load(const std::string& config_dir)
{
    m_loaded = false;

    const std::string key_path  = config_dir + "/bambu_connect_private.pem";
    const std::string cert_path = config_dir + "/bambu_connect_cert.pem";

    m_private_key_pem = read_file(key_path);
    m_cert_pem        = read_file(cert_path);

    if (m_private_key_pem.empty()) {
        LOG_INFO("MessageSigner: %s not found — non-Dev-Mode signing disabled", key_path.c_str());
        return false;
    }
    if (m_cert_pem.empty()) {
        LOG_INFO("MessageSigner: %s not found — non-Dev-Mode signing disabled", cert_path.c_str());
        return false;
    }

    // Validate private key
    {
        BIO* bio = BIO_new_mem_buf(m_private_key_pem.data(), (int)m_private_key_pem.size());
        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!pkey) {
            LOG_ERR("MessageSigner: failed to parse private key from %s", key_path.c_str());
            return false;
        }
        EVP_PKEY_free(pkey);
    }

    // Validate certificate and compute cert_id (hex SHA-256 of DER encoding)
    {
        BIO* bio = BIO_new_mem_buf(m_cert_pem.data(), (int)m_cert_pem.size());
        X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!cert) {
            LOG_ERR("MessageSigner: failed to parse certificate from %s", cert_path.c_str());
            return false;
        }

        // DER-encode the cert to compute its fingerprint
        int der_len = i2d_X509(cert, nullptr);
        if (der_len <= 0) {
            X509_free(cert);
            LOG_ERR("MessageSigner: failed to DER-encode certificate");
            return false;
        }
        std::vector<uint8_t> der(der_len);
        uint8_t* p = der.data();
        i2d_X509(cert, &p);

        m_cert_id = sha256_hex(der);

        // Log the Subject CN so the user can verify it's the right cert
        X509_NAME* subj = X509_get_subject_name(cert);
        char cn_buf[256] = {};
        X509_NAME_get_text_by_NID(subj, NID_commonName, cn_buf, sizeof(cn_buf));
        LOG_INFO("MessageSigner: loaded cert CN=%s, cert_id=%s…", cn_buf,
                 m_cert_id.substr(0, 16).c_str());

        X509_free(cert);
    }

    m_loaded = true;
    LOG_INFO("MessageSigner: signing enabled (RSA-SHA256)");
    return true;
}

std::string MessageSigner::sign_message(const std::string& plain_json) const
{
    if (!m_loaded)
        return plain_json;  // pass-through unchanged (Developer Mode path)

    // Parse the original message so we can extract the "print" object
    json msg;
    try {
        msg = json::parse(plain_json);
    } catch (...) {
        LOG_ERR("MessageSigner::sign_message: failed to parse input JSON");
        return plain_json;
    }

    // Serialise only the "print" sub-object (compact, deterministic)
    // The firmware verifies exactly these bytes.
    if (!msg.contains("print")) {
        // Not a print command — return unchanged (no signing needed)
        return plain_json;
    }

    const std::string print_payload = msg["print"].dump();

    // ── RSA-SHA256 sign ───────────────────────────────────────────────────────
    BIO* bio = BIO_new_mem_buf(m_private_key_pem.data(), (int)m_private_key_pem.size());
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey) {
        LOG_ERR("MessageSigner::sign_message: failed to load private key");
        return plain_json;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        LOG_ERR("MessageSigner::sign_message: DigestSignInit failed");
        return plain_json;
    }

    if (EVP_DigestSignUpdate(ctx,
                             reinterpret_cast<const uint8_t*>(print_payload.data()),
                             print_payload.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        LOG_ERR("MessageSigner::sign_message: DigestSignUpdate failed");
        return plain_json;
    }

    size_t sig_len = 0;
    EVP_DigestSignFinal(ctx, nullptr, &sig_len);

    std::vector<uint8_t> sig(sig_len);
    if (EVP_DigestSignFinal(ctx, sig.data(), &sig_len) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        LOG_ERR("MessageSigner::sign_message: DigestSignFinal failed");
        return plain_json;
    }
    sig.resize(sig_len);

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    const std::string sign_b64 = base64_encode(sig);

    // ── Assemble signed message ───────────────────────────────────────────────
    json signed_msg;
    signed_msg["header"]["cert_id"]     = m_cert_id;
    signed_msg["header"]["payload_len"] = (int)print_payload.size();
    signed_msg["header"]["sign_alg"]    = "RSA_SHA256";
    signed_msg["header"]["sign_string"] = sign_b64;
    signed_msg["header"]["sign_ver"]    = "v1.0";

    // Copy all top-level keys from the original message (print, and any others)
    for (auto& [k, v] : msg.items())
        signed_msg[k] = v;

    return signed_msg.dump();
}
