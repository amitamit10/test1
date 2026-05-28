#include "FtpClient.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <cstring>
#include <string>

// ---- libcurl global init -----------------------------------------------------
namespace {
struct CurlLib {
    CurlLib()  { curl_global_init(CURL_GLOBAL_ALL); }
    ~CurlLib() { curl_global_cleanup(); }
};
static CurlLib g_curl_lib;
} // namespace

// ---- Progress callback -------------------------------------------------------

int FtpClient::s_progress_callback(void* clientp,
                                    curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                                    curl_off_t ultotal, curl_off_t ulnow)
{
    auto* pd = static_cast<ProgressData*>(clientp);
    if (pd && pd->cb && ultotal > 0) {
        double pct = (double)ulnow / (double)ultotal * 100.0;
        pd->cb(pct);
    }
    return 0; // returning non-zero aborts the transfer
}

// ---- FtpClient::upload -------------------------------------------------------

bool FtpClient::upload(const std::string& host,
                       int                port,
                       const std::string& username,
                       const std::string& password,
                       const std::string& local_path,
                       const std::string& remote_filename,
                       bool               use_tls,
                       ProgressCallback   progress_cb)
{
    // Open local file
    FILE* fp = fopen(local_path.c_str(), "rb");
    if (!fp) {
        LOG_ERR("FTP: cannot open local file: %s", local_path.c_str());
        return false;
    }

    // Determine file size
    fseek(fp, 0, SEEK_END);
    curl_off_t file_size = (curl_off_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Build URL
    // ftps:// uses implicit TLS (connects to port 990 directly over TLS)
    std::string scheme = use_tls ? "ftps" : "ftp";
    std::string url    = scheme + "://" + host + ":" + std::to_string(port)
                       + "/cache/" + remote_filename;

    LOG_INFO("FTP: uploading %s -> %s (size=%lld bytes)",
             local_path.c_str(), url.c_str(), (long long)file_size);

    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERR("FTP: curl_easy_init() failed");
        fclose(fp);
        return false;
    }

    ProgressData pd{progress_cb, file_size};

    // URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Credentials
    std::string userpwd = username + ":" + password;
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());

    // Upload
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READDATA, fp);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, file_size);

    // TLS options
    if (use_tls) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        // FTP over implicit TLS — do NOT send AUTH TLS command
        curl_easy_setopt(curl, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_DEFAULT);
    }

    // Timeouts
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);   // 5 min total

    // Progress
    if (progress_cb) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, s_progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pd);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    // Verbose logging in debug builds
#ifdef BAMBU_DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        LOG_ERR("FTP: upload failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return false;
    }

    long ftp_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ftp_code);
    curl_easy_cleanup(curl);

    LOG_INFO("FTP: upload complete (ftp_code=%ld)", ftp_code);
    // FTP 226 = transfer complete
    return true;
}
