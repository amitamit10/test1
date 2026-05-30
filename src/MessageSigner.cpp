#include "MessageSigner.hpp"
#include "Logger.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

static std::string read_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ── helpers ───────────────────────────────────────────────────────────────────

std::string MessageSigner::base64_encode(const std::vector<uint8_t>& data)
{
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data.data(), (int)data.size());
    BIO_flush(b64);
    char* buf = nullptr;
    long  len = BIO_get_mem_data(mem, &buf);
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
    for (auto b : digest) oss << std::setw(2) << (int)b;
    return oss.str();
}

// ── cert loading ──────────────────────────────────────────────────────────────

bool MessageSigner::m_load_cert(const std::string& cert_path)
{
    const std::string pem = read_file(cert_path);
    if (pem.empty()) return false;

    BIO* bio = BIO_new_mem_buf(pem.data(), (int)pem.size());
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!cert) {
        LOG_ERR("MessageSigner: cannot parse cert: %s", cert_path.c_str());
        return false;
    }

    // ── cert_id = hex(serial_number) + "…" + SubjectDN ───────────────────────
    // Observed real format: "a4e8faaa…CN=GLOF3813734089.bambulab.com"
    // The "…" is U+2026 HORIZONTAL ELLIPSIS (3 UTF-8 bytes: e2 80 a6)

    // Serial number as lowercase hex
    ASN1_INTEGER* serial_asn1 = X509_get_serialNumber(cert);
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial_asn1, nullptr);
    char* serial_hex_cstr = BN_bn2hex(bn);
    std::string serial_hex(serial_hex_cstr);
    OPENSSL_free(serial_hex_cstr);
    BN_free(bn);
    // Lowercase to match observed example
    for (auto& c : serial_hex) c = (char)tolower((unsigned char)c);

    // Subject DN as one-line string (e.g. "/CN=GLOF3813734089.bambulab.com")
    X509_NAME* subj = X509_get_subject_name(cert);
    char* subj_cstr = X509_NAME_oneline(subj, nullptr, 0);
    std::string subject_dn(subj_cstr);
    OPENSSL_free(subj_cstr);

    // Remove leading "/" that OpenSSL sometimes adds
    if (!subject_dn.empty() && subject_dn[0] == '/')
        subject_dn = subject_dn.substr(1);

    // U+2026 HORIZONTAL ELLIPSIS
    const std::string ellipsis = "\xe2\x80\xa6";
    m_cert_id  = serial_hex + ellipsis + subject_dn;
    m_cert_pem = pem;

    // Log CN for verification
    char cn_buf[256] = {};
    X509_NAME_get_text_by_NID(subj, NID_commonName, cn_buf, sizeof(cn_buf));
    LOG_INFO("MessageSigner: cert loaded CN=%s", cn_buf);
    LOG_INFO("MessageSigner: cert_id=%s…", m_cert_id.substr(0, 24).c_str());

    X509_free(cert);
    return true;
}

// ── public API ────────────────────────────────────────────────────────────────

MessageSigner::MessageSigner()  = default;
MessageSigner::~MessageSigner() = default;

bool MessageSigner::load(const std::string& config_dir,
                          const std::string& device_serial)
{
    m_loaded = false;

    // Private key (global — same for all Bambu Connect installations)
    const std::string key_path = config_dir + "/bambu_connect_private.pem";
    m_private_key_pem = read_file(key_path);
    if (m_private_key_pem.empty()) {
        LOG_INFO("MessageSigner: %s not found — signing disabled", key_path.c_str());
        return false;
    }

    // Validate private key
    {
        BIO* bio = BIO_new_mem_buf(m_private_key_pem.data(),
                                   (int)m_private_key_pem.size());
        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);
        if (!pkey) {
            LOG_ERR("MessageSigner: invalid private key in %s", key_path.c_str());
            return false;
        }
        EVP_PKEY_free(pkey);
    }

    // Certificate — try device-specific first, then global fallback
    bool cert_loaded = false;

    if (!device_serial.empty()) {
        // <config_dir>/certs/<serial>.pem
        const std::string dev_cert = config_dir + "/certs/" + device_serial + ".pem";
        cert_loaded = m_load_cert(dev_cert);
        if (cert_loaded)
            LOG_INFO("MessageSigner: using device cert for %s", device_serial.c_str());
    }

    if (!cert_loaded) {
        // Fallback: global Bambu Connect cert
        const std::string global_cert = config_dir + "/bambu_connect_cert.pem";
        cert_loaded = m_load_cert(global_cert);
        if (cert_loaded)
            LOG_INFO("MessageSigner: using global Bambu Connect cert (fallback)");
    }

    if (!cert_loaded) {
        LOG_INFO("MessageSigner: no cert found — signing disabled");
        LOG_INFO("  Run: python3 tools/extract_bambu_key.py --help");
        return false;
    }

    m_loaded = true;
    LOG_INFO("MessageSigner: RSA-SHA256 signing enabled");
    return true;
}

// ── sign_message ──────────────────────────────────────────────────────────────

std::string MessageSigner::sign_message(const std::string& plain_json) const
{
    if (!m_loaded)
        return plain_json;

    json msg;
    try {
        msg = json::parse(plain_json);
    } catch (...) {
        LOG_ERR("MessageSigner::sign_message: JSON parse failed");
        return plain_json;
    }

    if (!msg.contains("print"))
        return plain_json;  // only sign print commands

    // Serialise the "print" sub-object — these exact bytes are what's signed
    const std::string print_payload = msg["print"].dump();

    // ── RSA-SHA256 sign ───────────────────────────────────────────────────────
    BIO* bio = BIO_new_mem_buf(m_private_key_pem.data(),
                               (int)m_private_key_pem.size());
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey) {
        LOG_ERR("MessageSigner::sign_message: failed to load key");
        return plain_json;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1 ||
        EVP_DigestSignUpdate(ctx,
            reinterpret_cast<const uint8_t*>(print_payload.data()),
            print_payload.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        LOG_ERR("MessageSigner::sign_message: DigestSign failed");
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

    // ── Build signed envelope ─────────────────────────────────────────────────
    json out;
    out["header"]["cert_id"]     = m_cert_id;
    out["header"]["payload_len"] = (int)print_payload.size();
    out["header"]["sign_alg"]    = "RSA_SHA256";
    out["header"]["sign_string"] = base64_encode(sig);
    out["header"]["sign_ver"]    = "v1.0";

    for (auto& [k, v] : msg.items())
        out[k] = v;

    return out.dump();
}
