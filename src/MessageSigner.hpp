#pragma once

#include <string>
#include <vector>
#include <cstdint>

/**
 * MessageSigner — RSA-SHA256 signing for Bambu Lab MQTT print commands.
 *
 * Non-Developer-Mode firmware (post-firmware 01.08.03.00) requires that
 * privileged commands (project_file, gcode_file, etc.) carry a cryptographic
 * header:
 *
 *   {
 *     "header": {
 *       "cert_id":     "<serial_hex>…<SubjectDN>",  e.g. "a4e8faaa…CN=GLOF38.bambulab.com"
 *       "payload_len": <byte-length-of-serialised-print-object>,
 *       "sign_alg":    "RSA_SHA256",
 *       "sign_string": "<base64-rsa-sha256-signature>",
 *       "sign_ver":    "v1.0"
 *     },
 *     "print": { … }
 *   }
 *
 * cert_id is derived from the X.509 certificate:
 *   hex(cert_serial_number) + "…" + X509_subject_oneline
 *
 * The certificate must have CN=<device_serial>.bambulab.com in its Subject.
 * The plugin looks for certificates in this order:
 *   1. <config_dir>/certs/<device_serial>.pem      (device-specific cert)
 *   2. <config_dir>/bambu_connect_cert.pem          (fallback global cert)
 *
 * The private key is extracted from Bambu Connect (same key for all devices):
 *   <config_dir>/bambu_connect_private.pem
 *
 * Use tools/extract_bambu_key.py to extract both.
 */
class MessageSigner {
public:
    MessageSigner();
    ~MessageSigner();

    MessageSigner(const MessageSigner&)            = delete;
    MessageSigner& operator=(const MessageSigner&) = delete;

    /**
     * Load key + cert for a specific device serial.
     * Tries <config_dir>/certs/<device_serial>.pem first,
     * then falls back to bambu_connect_cert.pem.
     */
    bool load(const std::string& config_dir,
              const std::string& device_serial = "");

    /** True once load() has succeeded. */
    bool is_loaded() const { return m_loaded; }

    /** The cert_id that will appear in the header (for logging/debugging). */
    const std::string& cert_id() const { return m_cert_id; }

    /**
     * Wrap a plain MQTT JSON string in the signed header envelope.
     * Returns the original string unchanged if the signer is not loaded.
     */
    std::string sign_message(const std::string& plain_json) const;

private:
    static std::string base64_encode(const std::vector<uint8_t>& data);
    static std::string sha256_hex(const std::vector<uint8_t>& data);

    bool        m_load_cert(const std::string& cert_path);

    bool        m_loaded{false};
    std::string m_private_key_pem;
    std::string m_cert_pem;
    std::string m_cert_id;    // hex(serial)…SubjectDN, matches firmware expectation
};
