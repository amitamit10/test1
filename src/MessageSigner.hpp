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
 *       "cert_id":    "<hex-sha256-of-cert-der>",
 *       "payload_len": <byte-length-of-serialised-print-object>,
 *       "sign_alg":   "RSA_SHA256",
 *       "sign_string": "<base64-rsa-sha256-signature>",
 *       "sign_ver":   "v1.0"
 *     },
 *     "print": { … }
 *   }
 *
 * The signing key is the per-install private key bundled inside the
 * Bambu Connect Electron application.  Users must extract it themselves
 * (see tools/extract_bambu_key.py) and place it at:
 *   <config_dir>/bambu_connect_private.pem   (PKCS8 PEM private key)
 *   <config_dir>/bambu_connect_cert.pem      (PEM X.509 certificate)
 *
 * Without these files the signer stays disabled and only Developer Mode
 * printing is available.
 */
class MessageSigner {
public:
    MessageSigner();
    ~MessageSigner();

    MessageSigner(const MessageSigner&)            = delete;
    MessageSigner& operator=(const MessageSigner&) = delete;

    /**
     * Load key + cert from the supplied config directory.
     * Returns true if both files were found and are valid.
     */
    bool load(const std::string& config_dir);

    /** True once load() has succeeded. */
    bool is_loaded() const { return m_loaded; }

    /**
     * Wrap a plain (unsigned) MQTT JSON string in the signed header envelope.
     *
     * @param plain_json  Full MQTT message as produced by build_print_command(),
     *                    e.g. {"print":{...}}
     * @returns           Signed JSON string with "header" + original fields,
     *                    or the original string unchanged if the signer is not
     *                    loaded.
     */
    std::string sign_message(const std::string& plain_json) const;

private:
    static std::string base64_encode(const std::vector<uint8_t>& data);
    static std::string sha256_hex(const std::vector<uint8_t>& data);

    bool        m_loaded{false};
    std::string m_private_key_pem;
    std::string m_cert_pem;
    std::string m_cert_id;           // hex SHA-256 of the DER-encoded cert
};
