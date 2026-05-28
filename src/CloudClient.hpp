#pragma once
#include <string>
#include <functional>
#include <map>
#include "Logger.hpp"

/**
 * CloudClient – wraps the Bambu Lab cloud REST API.
 *
 * All methods are synchronous and return the raw HTTP response body as a string.
 * http_code is set to the HTTP status code (200, 401, …) on return.
 */
class CloudClient {
public:
    CloudClient();
    ~CloudClient();

    CloudClient(const CloudClient&)            = delete;
    CloudClient& operator=(const CloudClient&) = delete;

    // ---- Configuration -------------------------------------------------------

    /** Override the default base URL (for China region, etc.) */
    void set_base_url(const std::string& url);

    /** Add / override an HTTP request header sent with every call. */
    void set_extra_header(const std::string& key, const std::string& value);

    // ---- Auth ----------------------------------------------------------------

    /**
     * Login with email + password.
     * Returns JSON body.  Check for loginType == "verifyCode" to call
     * login_with_code() next.
     */
    std::string login(const std::string& email,
                      const std::string& password,
                      unsigned int*      http_code);

    /**
     * Second-factor login (2FA / email code).
     */
    std::string login_with_code(const std::string& email,
                                const std::string& code,
                                unsigned int*      http_code);

    // ---- User info -----------------------------------------------------------

    /**
     * GET /v1/design-user-service/my/preference
     * Returns JSON containing the user's uid field.
     */
    std::string get_user_preferences(const std::string& access_token,
                                     unsigned int*      http_code);

    /**
     * GET /v1/user-service/my/profile
     */
    std::string get_my_profile(const std::string& access_token,
                               unsigned int*      http_code);

    /**
     * Exchange a ticket for an access token.
     * POST /v1/user-service/my/token
     */
    std::string get_my_token(const std::string& ticket,
                             unsigned int*      http_code);

    // ---- Devices -------------------------------------------------------------

    /**
     * GET /v1/iot-service/api/user/bind
     * Returns JSON array of devices bound to the account.
     */
    std::string list_devices(const std::string& access_token,
                             unsigned int*      http_code);

    // ---- File upload ---------------------------------------------------------

    /**
     * Upload a local file via multipart POST to the cloud storage.
     * POST /v1/user-service/my/upload
     *
     * Returns the URL of the uploaded file, or "" on failure.
     */
    std::string upload_file(const std::string& access_token,
                            const std::string& local_path,
                            const std::string& remote_filename,
                            unsigned int*      http_code);

    // ---- Binding -------------------------------------------------------------

    std::string query_bind_status(const std::string& access_token,
                                  const std::vector<std::string>& dev_list,
                                  unsigned int* http_code);

    std::string get_device_firmware(const std::string& access_token,
                                    const std::string& dev_id,
                                    unsigned int*      http_code);

    // ---- Settings ------------------------------------------------------------

    std::string get_setting_list(const std::string& access_token,
                                 const std::string& bundle_version,
                                 unsigned int*      http_code);

    std::string request_setting_id(const std::string& access_token,
                                   const std::string& name,
                                   const std::map<std::string, std::string>& values,
                                   unsigned int* http_code);

    std::string put_setting(const std::string& access_token,
                            const std::string& setting_id,
                            const std::string& name,
                            const std::map<std::string, std::string>& values,
                            unsigned int* http_code);

    int delete_setting(const std::string& access_token,
                       const std::string& setting_id,
                       unsigned int*      http_code);

private:
    // Execute an HTTP request.  method = "GET" | "POST" | "PUT" | "DELETE"
    std::string do_request(const std::string& method,
                           const std::string& path,
                           const std::string& body,          // may be empty
                           const std::string& content_type,  // may be empty
                           const std::string& access_token,  // may be empty
                           unsigned int*      http_code);

    // Build the standard BBL client headers as a curl slist
    void* build_headers(const std::string& access_token,
                        const std::string& content_type,
                        const std::string& extra_key   = "",
                        const std::string& extra_val   = "");

    std::string m_base_url{"https://api.bambulab.com"};
    std::map<std::string, std::string> m_extra_headers;
};
