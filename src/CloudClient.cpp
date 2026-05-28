#include "CloudClient.hpp"
#include <curl/curl.h>
#include <sstream>
#include <vector>
#include <stdexcept>
#include <cstring>

// ---- static write callback --------------------------------------------------

namespace {

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

} // namespace

// ---- CloudClient ------------------------------------------------------------

CloudClient::CloudClient() {}

CloudClient::~CloudClient() {}

void CloudClient::set_base_url(const std::string& url)
{
    m_base_url = url;
}

void CloudClient::set_extra_header(const std::string& key, const std::string& value)
{
    m_extra_headers[key] = value;
}

// ---- Private helpers --------------------------------------------------------

// Returns a curl_slist* that must be freed by the caller via curl_slist_free_all.
// Cast from void* is used to avoid pulling in curl.h from the header.
void* CloudClient::build_headers(const std::string& access_token,
                                  const std::string& content_type,
                                  const std::string& extra_key,
                                  const std::string& extra_val)
{
    struct curl_slist* headers = nullptr;

    auto append = [&](const std::string& h) {
        headers = curl_slist_append(headers, h.c_str());
    };

    append("X-BBL-Client-Name: OrcaSlicer");
    append("X-BBL-Client-Type: slicer");
    append("X-BBL-Client-Version: 02.06.00.00");

    if (!access_token.empty()) {
        append("Authorization: Bearer " + access_token);
    }
    if (!content_type.empty()) {
        append("Content-Type: " + content_type);
    }
    // Extra per-instance headers
    for (const auto& kv : m_extra_headers) {
        append(kv.first + ": " + kv.second);
    }
    if (!extra_key.empty()) {
        append(extra_key + ": " + extra_val);
    }
    return static_cast<void*>(headers);
}

std::string CloudClient::do_request(const std::string& method,
                                     const std::string& path,
                                     const std::string& body,
                                     const std::string& content_type,
                                     const std::string& access_token,
                                     unsigned int*      http_code)
{
    std::string url = m_base_url + path;
    std::string response_body;
    if (http_code) *http_code = 0;

    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERR("CloudClient: curl_easy_init() failed");
        return "";
    }

    auto* headers = static_cast<struct curl_slist*>(
        build_headers(access_token, content_type));

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    // else GET (default)

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_ERR("CloudClient: %s %s failed: %s",
                method.c_str(), url.c_str(), curl_easy_strerror(res));
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (http_code) *http_code = (unsigned int)code;
        LOG_DBG("CloudClient: %s %s -> %ld", method.c_str(), url.c_str(), code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response_body;
}

// ---- Auth -------------------------------------------------------------------

std::string CloudClient::login(const std::string& email,
                                const std::string& password,
                                unsigned int*      http_code)
{
    // Build JSON body manually (no nlohmann yet at this layer — keep it simple)
    std::string body = "{\"account\":\"" + email + "\",\"password\":\"" + password + "\"}";
    return do_request("POST",
                      "/v1/user-service/user/login",
                      body,
                      "application/json",
                      "",
                      http_code);
}

std::string CloudClient::login_with_code(const std::string& email,
                                          const std::string& code,
                                          unsigned int*      http_code)
{
    std::string body = "{\"account\":\"" + email + "\",\"code\":\"" + code + "\"}";
    return do_request("POST",
                      "/v1/user-service/user/login",
                      body,
                      "application/json",
                      "",
                      http_code);
}

// ---- User info --------------------------------------------------------------

std::string CloudClient::get_user_preferences(const std::string& access_token,
                                               unsigned int*      http_code)
{
    return do_request("GET",
                      "/v1/design-user-service/my/preference",
                      "",
                      "",
                      access_token,
                      http_code);
}

std::string CloudClient::get_my_profile(const std::string& access_token,
                                         unsigned int*      http_code)
{
    return do_request("GET",
                      "/v1/user-service/my/profile",
                      "",
                      "",
                      access_token,
                      http_code);
}

std::string CloudClient::get_my_token(const std::string& ticket,
                                       unsigned int*      http_code)
{
    std::string body = "{\"ticket\":\"" + ticket + "\"}";
    return do_request("POST",
                      "/v1/user-service/my/token",
                      body,
                      "application/json",
                      "",
                      http_code);
}

// ---- Devices ----------------------------------------------------------------

std::string CloudClient::list_devices(const std::string& access_token,
                                       unsigned int*      http_code)
{
    return do_request("GET",
                      "/v1/iot-service/api/user/bind",
                      "",
                      "",
                      access_token,
                      http_code);
}

// ---- File upload ------------------------------------------------------------

std::string CloudClient::upload_file(const std::string& access_token,
                                      const std::string& local_path,
                                      const std::string& remote_filename,
                                      unsigned int*      http_code)
{
    if (http_code) *http_code = 0;

    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERR("CloudClient: curl_easy_init() failed for upload");
        return "";
    }

    std::string url = m_base_url + "/v1/user-service/my/upload";
    std::string response_body;

    // Build multipart form
    curl_mime* form   = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(form);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, local_path.c_str());
    curl_mime_filename(part, remote_filename.c_str());

    auto* headers = static_cast<struct curl_slist*>(
        build_headers(access_token, "", "", ""));

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_ERR("CloudClient: upload failed: %s", curl_easy_strerror(res));
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (http_code) *http_code = (unsigned int)code;
        LOG_INFO("CloudClient: upload -> HTTP %ld", code);
    }

    curl_slist_free_all(headers);
    curl_mime_free(form);
    curl_easy_cleanup(curl);

    // The response JSON should contain a "url" field; return the raw JSON
    // and let the caller parse it.
    return response_body;
}

// ---- Binding ----------------------------------------------------------------

std::string CloudClient::query_bind_status(const std::string& access_token,
                                            const std::vector<std::string>& dev_list,
                                            unsigned int* http_code)
{
    // Build JSON array of device IDs
    std::string body = "[";
    for (size_t i = 0; i < dev_list.size(); ++i) {
        if (i > 0) body += ",";
        body += "\"" + dev_list[i] + "\"";
    }
    body += "]";
    return do_request("POST",
                      "/v1/iot-service/api/user/device/bind/status",
                      body,
                      "application/json",
                      access_token,
                      http_code);
}

std::string CloudClient::get_device_firmware(const std::string& access_token,
                                              const std::string& dev_id,
                                              unsigned int*      http_code)
{
    return do_request("GET",
                      "/v1/iot-service/api/firmware?dev_id=" + dev_id,
                      "",
                      "",
                      access_token,
                      http_code);
}

// ---- Settings ---------------------------------------------------------------

std::string CloudClient::get_setting_list(const std::string& access_token,
                                           const std::string& bundle_version,
                                           unsigned int*      http_code)
{
    std::string path = "/v1/user-service/my/setting";
    if (!bundle_version.empty()) {
        path += "?bundle_version=" + bundle_version;
    }
    return do_request("GET", path, "", "", access_token, http_code);
}

std::string CloudClient::request_setting_id(const std::string& access_token,
                                             const std::string& name,
                                             const std::map<std::string, std::string>& values,
                                             unsigned int* http_code)
{
    // Serialize values map as JSON object
    std::string vals = "{";
    bool first = true;
    for (const auto& kv : values) {
        if (!first) vals += ",";
        // Escape the value string (basic escaping of backslashes and quotes)
        std::string v = kv.second;
        std::string escaped;
        for (char c : v) {
            if (c == '"')  escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else escaped += c;
        }
        vals += "\"" + kv.first + "\":\"" + escaped + "\"";
        first = false;
    }
    vals += "}";

    std::string body = "{\"name\":\"" + name + "\",\"content\":" + vals + "}";
    return do_request("POST",
                      "/v1/user-service/my/setting",
                      body,
                      "application/json",
                      access_token,
                      http_code);
}

std::string CloudClient::put_setting(const std::string& access_token,
                                      const std::string& setting_id,
                                      const std::string& name,
                                      const std::map<std::string, std::string>& values,
                                      unsigned int* http_code)
{
    std::string vals = "{";
    bool first = true;
    for (const auto& kv : values) {
        if (!first) vals += ",";
        std::string v = kv.second;
        std::string escaped;
        for (char c : v) {
            if (c == '"')  escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else escaped += c;
        }
        vals += "\"" + kv.first + "\":\"" + escaped + "\"";
        first = false;
    }
    vals += "}";

    std::string body = "{\"setting_id\":\"" + setting_id + "\",\"name\":\"" + name
                     + "\",\"content\":" + vals + "}";
    return do_request("PUT",
                      "/v1/user-service/my/setting/" + setting_id,
                      body,
                      "application/json",
                      access_token,
                      http_code);
}

int CloudClient::delete_setting(const std::string& access_token,
                                 const std::string& setting_id,
                                 unsigned int*      http_code)
{
    std::string resp = do_request("DELETE",
                                   "/v1/user-service/my/setting/" + setting_id,
                                   "",
                                   "",
                                   access_token,
                                   http_code);
    if (http_code && (*http_code == 200 || *http_code == 204)) return 0;
    return -1;
}
