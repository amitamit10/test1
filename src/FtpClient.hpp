#pragma once
#include <string>
#include <functional>
#include <curl/curl.h>
#include "Logger.hpp"

class FtpClient {
public:
    using ProgressCallback = std::function<void(double progress_pct)>;

    FtpClient()  = default;
    ~FtpClient() = default;

    FtpClient(const FtpClient&)            = delete;
    FtpClient& operator=(const FtpClient&) = delete;

    /**
     * Upload a local file to the printer via FTPS.
     *
     * @param host             Printer IP address
     * @param port             FTP port (typically 990 for implicit TLS)
     * @param username         FTP username ("bblp")
     * @param password         Printer access code
     * @param local_path       Full path to local .3mf file
     * @param remote_filename  Destination filename inside /cache/ (e.g. "job.3mf")
     * @param use_tls          If true, use implicit FTPS (ftps:// scheme)
     * @param progress_cb      Called with progress [0..100] during upload
     * @return true on success
     */
    bool upload(const std::string& host,
                int                port,
                const std::string& username,
                const std::string& password,
                const std::string& local_path,
                const std::string& remote_filename,
                bool               use_tls     = true,
                ProgressCallback   progress_cb = nullptr);

private:
    struct ProgressData {
        ProgressCallback cb;
        curl_off_t       total_size;
    };

    static int s_progress_callback(void* clientp,
                                   curl_off_t dltotal, curl_off_t dlnow,
                                   curl_off_t ultotal, curl_off_t ulnow);
};
