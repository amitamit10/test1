#include "NetworkAgent.hpp"

#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <random>
#include <thread>
#include <chrono>

using json = nlohmann::json;

// ---- helpers ----------------------------------------------------------------

namespace {

std::string random_hex8()
{
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist;
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", dist(rng));
    return std::string(buf);
}

// Very small JSON value extractor — avoid needing nlohmann just for simple strings
// Uses nlohmann where it's already available in this TU.

} // namespace

// ---- NetworkAgent -----------------------------------------------------------

NetworkAgent::NetworkAgent(const std::string& log_dir)
    : m_log_dir(log_dir)
    , m_cloud_client(std::make_unique<CloudClient>())
{
    LOG_INFO("NetworkAgent created, log_dir=%s", log_dir.c_str());
}

NetworkAgent::~NetworkAgent()
{
    stop_ssdp();
    if (m_lan_mqtt)   m_lan_mqtt->disconnect();
    if (m_cloud_mqtt) m_cloud_mqtt->disconnect();
    LOG_INFO("NetworkAgent destroyed");
}

// ---- Configuration ----------------------------------------------------------

int NetworkAgent::init_log()
{
    LOG_INFO("init_log: log_dir=%s", m_log_dir.c_str());
    return 0;
}

int NetworkAgent::set_config_dir(const std::string& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config_dir = path;
    LOG_INFO("config_dir=%s", path.c_str());
    return 0;
}

int NetworkAgent::set_cert_file(const std::string& folder, const std::string& filename)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cert_folder   = folder;
    m_cert_filename = filename;
    LOG_INFO("cert_file=%s/%s", folder.c_str(), filename.c_str());
    return 0;
}

int NetworkAgent::set_country_code(const std::string& code)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_country_code = code;
    // Switch cloud base URL for China
    if (code == "CN" || code == "cn") {
        m_cloud_client->set_base_url("https://api.bambulab.cn");
        LOG_INFO("Switched to China cloud endpoint");
    }
    return 0;
}

int NetworkAgent::start()
{
    LOG_INFO("NetworkAgent::start()");
    return 0;
}

void NetworkAgent::set_extra_http_header(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cloud_client->set_extra_header(key, value);
}

// ---- Callback setters -------------------------------------------------------

void NetworkAgent::set_on_ssdp_msg(std::function<void(std::string)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_ssdp_msg = std::move(fn);
}

void NetworkAgent::set_on_user_login(std::function<void(int, bool)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_user_login = std::move(fn);
}

void NetworkAgent::set_on_printer_connected(std::function<void(std::string)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_printer_connected = std::move(fn);
}

void NetworkAgent::set_on_server_connected(std::function<void(int, int)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_server_connected = std::move(fn);
}

void NetworkAgent::set_on_http_error(std::function<void(unsigned, std::string)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_http_error = std::move(fn);
}

void NetworkAgent::set_get_country_code(std::function<std::string()> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_get_country_code = std::move(fn);
}

void NetworkAgent::set_on_subscribe_failure(std::function<void(std::string)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_subscribe_failure = std::move(fn);
}

void NetworkAgent::set_on_message(std::function<void(std::string, std::string)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_message = std::move(fn);
}

void NetworkAgent::set_on_user_message(std::function<void(std::string, std::string)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_user_message = std::move(fn);
}

void NetworkAgent::set_on_local_connect(std::function<void(int, std::string, std::string)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_local_connect = std::move(fn);
}

void NetworkAgent::set_on_local_message(std::function<void(std::string, std::string)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_local_message = std::move(fn);
}

void NetworkAgent::set_queue_on_main(std::function<void(std::function<void()>)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue_on_main = std::move(fn);
}

void NetworkAgent::set_server_callback(std::function<void(std::string, int)> fn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_server_callback = std::move(fn);
}

// ---- LAN connection ---------------------------------------------------------

int NetworkAgent::connect_printer(const std::string& dev_id,
                                   const std::string& dev_ip,
                                   const std::string& username,
                                   const std::string& password,
                                   bool               use_ssl)
{
    LOG_INFO("connect_printer: dev_id=%s ip=%s ssl=%d",
             dev_id.c_str(), dev_ip.c_str(), (int)use_ssl);

    // Capture callbacks before creating the client
    std::function<void(std::string)> on_printer_connected;
    std::function<void(std::string, std::string)> on_local_message;
    std::function<void(int, std::string, std::string)> on_local_connect;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        on_printer_connected = m_on_printer_connected;
        on_local_message     = m_on_local_message;
        on_local_connect     = m_on_local_connect;
        m_lan_dev_id = dev_id;
        m_lan_dev_ip = dev_ip;
    }

    auto mqtt = std::make_unique<MqttClient>();
    mqtt->set_credentials(username, password);
    mqtt->set_client_id("tango_" + random_hex8());
    mqtt->set_tls(use_ssl, /*insecure=*/true);

    std::string report_topic = "device/" + dev_id + "/report";
    std::string dev_id_copy  = dev_id;

    mqtt->set_on_connect([this, dev_id_copy, report_topic, on_printer_connected, on_local_connect](int rc) {
        if (rc == 0) {
            LOG_INFO("LAN MQTT connected to printer %s", dev_id_copy.c_str());
            // Subscribe to the report topic
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_lan_mqtt) {
                    m_lan_mqtt->subscribe(report_topic, 1);
                }
            }
            if (on_printer_connected) on_printer_connected(dev_id_copy);
            if (on_local_connect)     on_local_connect(0, dev_id_copy, "");
        } else {
            LOG_ERR("LAN MQTT connect failed rc=%d", rc);
            if (on_local_connect) on_local_connect(ConnectStatusFailed, dev_id_copy, "");
        }
    });

    mqtt->set_on_disconnect([this, dev_id_copy, on_local_connect](int rc) {
        LOG_INFO("LAN MQTT disconnected from printer %s (rc=%d)", dev_id_copy.c_str(), rc);
        if (on_local_connect) on_local_connect(ConnectStatusLost, dev_id_copy, "");
    });

    mqtt->set_on_message([this, on_local_message](const std::string& topic,
                                                    const std::string& payload) {
        if (on_local_message) on_local_message(topic, payload);
    });

    bool ok = mqtt->connect(dev_ip, 8883, 60);
    if (!ok) {
        LOG_ERR("connect_printer: MqttClient::connect() failed");
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lan_mqtt = std::move(mqtt);
    }
    return 0;
}

int NetworkAgent::disconnect_printer()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_lan_mqtt) {
        m_lan_mqtt->disconnect();
        m_lan_mqtt.reset();
    }
    return 0;
}

int NetworkAgent::send_message_to_printer(const std::string& dev_id,
                                           const std::string& json_str,
                                           int /*qos*/, int /*flag*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_lan_mqtt) {
        LOG_ERR("send_message_to_printer: no LAN connection");
        return -1;
    }
    std::string topic = "device/" + dev_id + "/request";
    bool ok = m_lan_mqtt->publish(topic, json_str, 1);
    return ok ? 0 : -1;
}

// ---- SSDP -------------------------------------------------------------------

static const char* SSDP_MCAST_ADDR = "239.255.255.250";
static const int   SSDP_PORT       = 1990;
static const char* SSDP_ST         = "urn:bambulab-com:device:3dprinter:1";

bool NetworkAgent::start_discovery(bool start_flag, bool /*sending*/)
{
    if (start_flag) {
        if (m_ssdp_running.load()) {
            LOG_INFO("SSDP already running");
            return true;
        }
        m_ssdp_running = true;
        m_ssdp_thread  = std::thread(&NetworkAgent::ssdp_worker, this);
        LOG_INFO("SSDP discovery started");
    } else {
        stop_ssdp();
        LOG_INFO("SSDP discovery stopped");
    }
    return true;
}

void NetworkAgent::stop_ssdp()
{
    m_ssdp_running = false;
    // Close the socket to unblock recvfrom
    if (m_ssdp_sock >= 0) {
        close(m_ssdp_sock);
        m_ssdp_sock = -1;
    }
    if (m_ssdp_thread.joinable()) {
        m_ssdp_thread.join();
    }
}

void NetworkAgent::ssdp_worker()
{
    LOG_INFO("SSDP worker started");

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("SSDP: socket() failed: %s", strerror(errno));
        m_ssdp_running = false;
        return;
    }

    m_ssdp_sock = sock;

    // Allow address reuse
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    // Bind to the SSDP port
    struct sockaddr_in local_addr{};
    local_addr.sin_family      = AF_INET;
    local_addr.sin_port        = htons(SSDP_PORT);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        LOG_ERR("SSDP: bind() failed: %s", strerror(errno));
        close(sock);
        m_ssdp_sock = -1;
        m_ssdp_running = false;
        return;
    }

    // Join multicast group
    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(SSDP_MCAST_ADDR);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        LOG_ERR("SSDP: IP_ADD_MEMBERSHIP failed: %s", strerror(errno));
        // Non-fatal — we can still send M-SEARCH and receive unicast replies
    }

    // Non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    // M-SEARCH template
    char msearch[512];
    snprintf(msearch, sizeof(msearch),
             "M-SEARCH * HTTP/1.1\r\n"
             "HOST: %s:%d\r\n"
             "MAN: \"ssdp:discover\"\r\n"
             "MX: 3\r\n"
             "ST: %s\r\n"
             "\r\n",
             SSDP_MCAST_ADDR, SSDP_PORT, SSDP_ST);
    size_t msearch_len = strlen(msearch);

    struct sockaddr_in mcast_addr{};
    mcast_addr.sin_family      = AF_INET;
    mcast_addr.sin_port        = htons(SSDP_PORT);
    mcast_addr.sin_addr.s_addr = inet_addr(SSDP_MCAST_ADDR);

    auto last_search = std::chrono::steady_clock::now() - std::chrono::seconds(31);

    while (m_ssdp_running.load()) {
        // Send M-SEARCH every 30 s
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_search).count() >= 30) {
            sendto(sock, msearch, msearch_len, 0,
                   (struct sockaddr*)&mcast_addr, sizeof(mcast_addr));
            LOG_DBG("SSDP: sent M-SEARCH");
            last_search = now;
        }

        // Wait for response (select with 1 s timeout)
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        struct timeval tv{1, 0};
        int ret = select(sock + 1, &rfds, nullptr, nullptr, &tv);
        if (ret <= 0) continue;

        char buf[2048];
        struct sockaddr_in src_addr{};
        socklen_t src_len = sizeof(src_addr);
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr*)&src_addr, &src_len);
        if (n <= 0) continue;
        buf[n] = '\0';

        // Parse headers — look for BAMBU-SN, BAMBU-NAME, LOCATION
        std::string response(buf, n);
        std::string sn, name, location;

        // Walk line by line
        std::istringstream ss(response);
        std::string line;
        while (std::getline(ss, line)) {
            // Strip CR
            if (!line.empty() && line.back() == '\r') line.pop_back();

            auto colon = line.find(':');
            if (colon == std::string::npos) continue;

            std::string hdr_name  = line.substr(0, colon);
            std::string hdr_value = line.substr(colon + 1);
            // Trim leading spaces from value
            size_t vstart = hdr_value.find_first_not_of(' ');
            if (vstart != std::string::npos) hdr_value = hdr_value.substr(vstart);

            // Case-insensitive comparison (headers are ASCII)
            auto to_upper = [](std::string s) {
                for (auto& c : s) c = (char)toupper((unsigned char)c);
                return s;
            };
            std::string upper_name = to_upper(hdr_name);

            if (upper_name == "BAMBU-SN")   sn       = hdr_value;
            else if (upper_name == "BAMBU-NAME") name = hdr_value;
            else if (upper_name == "LOCATION")   location = hdr_value;
        }

        // Extract IP from LOCATION or from sender address
        std::string dev_ip;
        if (!location.empty()) {
            // Location may be "192.168.1.10" or "http://192.168.1.10/..."
            // Strip scheme
            size_t start = location.find("://");
            if (start != std::string::npos) {
                start += 3;
                size_t end = location.find('/', start);
                dev_ip = location.substr(start, end == std::string::npos ? std::string::npos : end - start);
            } else {
                dev_ip = location;
            }
        } else {
            char ip_buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src_addr.sin_addr, ip_buf, sizeof(ip_buf));
            dev_ip = ip_buf;
        }

        if (sn.empty()) {
            LOG_DBG("SSDP: response missing BAMBU-SN, ignoring");
            continue;
        }

        // Build JSON and invoke callback
        std::string json_str;
        {
            json j;
            j["dev_id"]       = sn;
            j["dev_name"]     = name;
            j["dev_ip"]       = dev_ip;
            j["connect_type"] = "lan";
            json_str = j.dump();
        }

        LOG_INFO("SSDP: discovered dev_id=%s name=%s ip=%s",
                 sn.c_str(), name.c_str(), dev_ip.c_str());

        std::function<void(std::string)> cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cb = m_on_ssdp_msg;
        }
        if (cb) cb(json_str);
    }

    close(sock);
    m_ssdp_sock = -1;
    LOG_INFO("SSDP worker exited");
}

// ---- Cloud ------------------------------------------------------------------

int NetworkAgent::connect_server()
{
    std::string user_id, access_token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        user_id      = m_user_id;
        access_token = m_access_token;
    }

    if (user_id.empty() || access_token.empty()) {
        LOG_ERR("connect_server: no user credentials (call change_user first)");
        return -1;
    }

    LOG_INFO("connect_server: user_id=%s", user_id.c_str());

    // Capture callbacks
    std::function<void(int, int)> on_server_connected;
    std::function<void(std::string, std::string)> on_message;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        on_server_connected = m_on_server_connected;
        on_message          = m_on_message;
    }

    auto mqtt = std::make_unique<MqttClient>();
    mqtt->set_credentials("u_" + user_id, access_token);
    mqtt->set_client_id("tango_" + random_hex8());
    mqtt->set_tls(true, false);  // Verify peer for cloud

    mqtt->set_on_connect([this, on_server_connected](int rc) {
        if (rc == 0) {
            m_server_connected = true;
            LOG_INFO("Cloud MQTT connected");
            if (on_server_connected) on_server_connected(ConnectStatusOk, 0);
        } else {
            m_server_connected = false;
            LOG_ERR("Cloud MQTT connect failed rc=%d", rc);
            if (on_server_connected) on_server_connected(ConnectStatusFailed, rc);
        }
    });

    mqtt->set_on_disconnect([this, on_server_connected](int rc) {
        m_server_connected = false;
        LOG_INFO("Cloud MQTT disconnected (rc=%d)", rc);
        if (on_server_connected) on_server_connected(ConnectStatusLost, rc);
    });

    mqtt->set_on_message([this, on_message](const std::string& topic,
                                            const std::string& payload) {
        if (on_message) on_message(topic, payload);
    });

    bool ok = mqtt->connect("us.mqtt.bambulab.com", 8883, 60);
    if (!ok) {
        LOG_ERR("connect_server: MqttClient::connect() failed");
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cloud_mqtt = std::move(mqtt);
    }
    return 0;
}

bool NetworkAgent::is_server_connected() const
{
    return m_server_connected.load();
}

int NetworkAgent::refresh_connection()
{
    LOG_INFO("refresh_connection");
    // Disconnect and reconnect cloud MQTT
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_cloud_mqtt) {
            m_cloud_mqtt->disconnect();
            m_cloud_mqtt.reset();
        }
    }
    m_server_connected = false;
    return connect_server();
}

int NetworkAgent::start_subscribe(const std::string& module)
{
    LOG_INFO("start_subscribe module=%s", module.c_str());
    // Subscribe to user topic on cloud MQTT
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_cloud_mqtt) return -1;
    std::string topic = "device/+/report";
    m_cloud_mqtt->subscribe(topic, 1);
    return 0;
}

int NetworkAgent::stop_subscribe(const std::string& /*module*/)
{
    LOG_INFO("stop_subscribe");
    return 0;
}

int NetworkAgent::add_subscribe(const std::vector<std::string>& dev_list)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& dev_id : dev_list) {
        m_subscribed_devs.push_back(dev_id);
        if (m_cloud_mqtt) {
            std::string topic = "device/" + dev_id + "/report";
            m_cloud_mqtt->subscribe(topic, 1);
            LOG_INFO("add_subscribe: %s", dev_id.c_str());
        }
    }
    return 0;
}

int NetworkAgent::del_subscribe(const std::vector<std::string>& dev_list)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& dev_id : dev_list) {
        m_subscribed_devs.erase(
            std::remove(m_subscribed_devs.begin(), m_subscribed_devs.end(), dev_id),
            m_subscribed_devs.end());
    }
    return 0;
}

void NetworkAgent::enable_multi_machine(bool enable)
{
    m_multi_machine = enable;
}

int NetworkAgent::send_message(const std::string& dev_id,
                                const std::string& json_str,
                                int /*qos*/, int /*flag*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_cloud_mqtt) {
        LOG_ERR("send_message: no cloud MQTT connection");
        return -1;
    }
    std::string topic = "device/" + dev_id + "/request";
    bool ok = m_cloud_mqtt->publish(topic, json_str, 1);
    return ok ? 0 : -1;
}

// ---- User / auth ------------------------------------------------------------

int NetworkAgent::change_user(const std::string& user_info_json)
{
    try {
        auto j = json::parse(user_info_json);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_user_id      = j.value("user_id",      "");
        m_access_token = j.value("access_token", "");
        m_user_name    = j.value("user_name",    "");
        m_user_avatar  = j.value("avatar",       "");
        m_user_nickname= j.value("nickname",     "");
        LOG_INFO("change_user: user_id=%s", m_user_id.c_str());
    } catch (const std::exception& e) {
        LOG_ERR("change_user: JSON parse error: %s", e.what());
        return -1;
    }
    // Fire login callback
    std::function<void(int, bool)> cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_on_user_login;
    }
    if (cb) cb(0, true);
    return 0;
}

bool NetworkAgent::is_user_login() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_access_token.empty();
}

int NetworkAgent::user_logout(bool /*request*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_user_id.clear();
    m_access_token.clear();
    m_user_name.clear();
    m_user_avatar.clear();
    m_user_nickname.clear();
    LOG_INFO("user_logout");
    return 0;
}

std::string NetworkAgent::get_user_id()       const { std::lock_guard<std::mutex> lk(m_mutex); return m_user_id; }
std::string NetworkAgent::get_user_name()     const { std::lock_guard<std::mutex> lk(m_mutex); return m_user_name; }
std::string NetworkAgent::get_user_avatar()   const { std::lock_guard<std::mutex> lk(m_mutex); return m_user_avatar; }
std::string NetworkAgent::get_user_nickname() const { std::lock_guard<std::mutex> lk(m_mutex); return m_user_nickname; }

int NetworkAgent::get_my_profile(const std::string& token,
                                  unsigned int*      http_code,
                                  std::string*       http_body)
{
    std::string body = m_cloud_client->get_my_profile(token, http_code);
    if (http_body) *http_body = body;
    return (http_code && *http_code == 200) ? 0 : -1;
}

int NetworkAgent::get_my_token(const std::string& ticket,
                                unsigned int*      http_code,
                                std::string*       http_body)
{
    std::string body = m_cloud_client->get_my_token(ticket, http_code);
    if (http_body) *http_body = body;
    return (http_code && *http_code == 200) ? 0 : -1;
}

// ---- Binding ----------------------------------------------------------------

int NetworkAgent::ping_bind(const std::string& /*ping_code*/) { return 0; }

int NetworkAgent::bind_detect(const std::string& /*dev_ip*/,
                               const std::string& /*sec_link*/,
                               detectResult&      result)
{
    result.result_msg   = "not_implemented";
    result.bind_state   = "unknown";
    result.connect_type = "lan";
    return 0;
}

int NetworkAgent::bind(const std::string& /*dev_ip*/,
                       const std::string& /*dev_id*/,
                       const std::string& /*sec_link*/,
                       const std::string& /*timezone*/,
                       bool /*improved*/,
                       std::function<void(int,int,std::string)> fn)
{
    if (fn) fn(BindJobFailed, -1, "not_implemented");
    return -1;
}

int NetworkAgent::unbind(const std::string& /*dev_id*/) { return 0; }

int NetworkAgent::request_bind_ticket(std::string* ticket)
{
    if (ticket) *ticket = "";
    return 0;
}

int NetworkAgent::query_bind_status(const std::vector<std::string>& query_list,
                                     unsigned int* http_code,
                                     std::string*  http_body)
{
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_access_token;
    }
    std::string body = m_cloud_client->query_bind_status(token, query_list, http_code);
    if (http_body) *http_body = body;
    return (http_code && *http_code == 200) ? 0 : -1;
}

std::string NetworkAgent::get_bambulab_host() const
{
    // Return cloud MQTT host
    std::lock_guard<std::mutex> lock(m_mutex);
    return "us.mqtt.bambulab.com";
}

std::string NetworkAgent::get_user_selected_machine() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_selected_dev_id;
}

int NetworkAgent::set_user_selected_machine(const std::string& dev_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_selected_dev_id = dev_id;
    return 0;
}

int NetworkAgent::modify_printer_name(const std::string& /*dev_id*/,
                                       const std::string& /*dev_name*/)
{
    return 0;
}

int NetworkAgent::get_printer_firmware(const std::string& dev_id,
                                        unsigned*          http_code,
                                        std::string*       http_body)
{
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_access_token;
    }
    std::string body = m_cloud_client->get_device_firmware(token, dev_id, http_code);
    if (http_body) *http_body = body;
    return (http_code && *http_code == 200) ? 0 : -1;
}

// ---- Print helpers ----------------------------------------------------------

std::string NetworkAgent::next_seq_id()
{
    uint32_t id = m_seq_id.fetch_add(1);
    char buf[5];
    snprintf(buf, sizeof(buf), "%04x", id & 0xFFFF);
    return std::string(buf);
}

std::string NetworkAgent::build_print_command(const PrintParams& params,
                                               const std::string& ftp_url) const
{
    json j;
    j["print"]["sequence_id"]    = "0001";
    j["print"]["command"]        = "project_file";
    j["print"]["param"]          = "Metadata/plate_1.gcode";
    j["print"]["url"]            = ftp_url;
    j["print"]["timelapse"]      = params.task_record_timelapse;
    j["print"]["bed_leveling"]   = params.task_bed_leveling;
    j["print"]["flow_cali"]      = params.task_flow_cali;
    j["print"]["vibration_cali"] = params.task_vibration_cali;
    j["print"]["layer_inspect"]  = params.task_layer_inspect;
    j["print"]["use_ams"]        = params.task_use_ams;
    if (!params.ams_mapping_info.empty()) {
        j["print"]["ams_mapping"] = params.ams_mapping_info;
    }
    if (!params.task_bed_type.empty()) {
        j["print"]["bed_type"] = params.task_bed_type;
    }
    return j.dump();
}

int NetworkAgent::do_local_print_flow(const PrintParams& params,
                                       std::function<void(int,int,std::string)> update_fn,
                                       std::function<bool()>                    cancel_fn,
                                       bool send_start_cmd)
{
    if (update_fn) update_fn(PrintJobInit, 0, "Starting print");

    // Determine FTP target filename
    std::string ftp_filename = params.ftp_file;
    if (ftp_filename.empty()) {
        // Derive from local filename
        ftp_filename = params.filename;
        size_t slash = ftp_filename.rfind('/');
        if (slash != std::string::npos) ftp_filename = ftp_filename.substr(slash + 1);
    }
    if (ftp_filename.empty()) ftp_filename = "job.3mf";

    // ---- FTP upload ----------------------------------------------------------
    if (update_fn) update_fn(PrintJobUploadFile, 0, "Uploading file");

    if (cancel_fn && cancel_fn()) {
        if (update_fn) update_fn(PrintJobCancel, 0, "Cancelled");
        return -1;
    }

    FtpClient ftp;
    bool upload_ok = ftp.upload(
        params.dev_ip,
        990,
        params.username.empty() ? "bblp" : params.username,
        params.password,
        params.filename,
        ftp_filename,
        params.use_ssl_for_ftp,
        [&update_fn, &cancel_fn](double pct) {
            if (update_fn) update_fn(PrintJobUploadFile, (int)pct, "Uploading");
            // progress callback can't cancel here easily; cancel handled above
        }
    );

    if (!upload_ok) {
        LOG_ERR("do_local_print_flow: FTP upload failed");
        if (update_fn) update_fn(PrintJobFailed, 0, "FTP upload failed");
        return -1;
    }

    if (cancel_fn && cancel_fn()) {
        if (update_fn) update_fn(PrintJobCancel, 0, "Cancelled");
        return -1;
    }

    if (update_fn) update_fn(PrintJobWaiting, 100, "Upload complete, waiting");

    // Wait 2 seconds for printer to index the file
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    if (cancel_fn && cancel_fn()) {
        if (update_fn) update_fn(PrintJobCancel, 0, "Cancelled");
        return -1;
    }

    if (!send_start_cmd) {
        if (update_fn) update_fn(PrintJobFinished, 100, "File sent to SD card");
        return 0;
    }

    // ---- MQTT print command --------------------------------------------------
    if (update_fn) update_fn(PrintJobSending, 0, "Sending print command");

    // ftp:/// (3 slashes, no host) — printer resolves locally
    std::string ftp_url = "ftp:///cache/" + ftp_filename;
    std::string print_cmd = build_print_command(params, ftp_url);

    // Build the sequence_id properly
    {
        json j = json::parse(print_cmd);
        j["print"]["sequence_id"] = next_seq_id();
        print_cmd = j.dump();
    }

    int rc = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lan_mqtt) {
            std::string topic = "device/" + params.dev_id + "/request";
            bool ok = m_lan_mqtt->publish(topic, print_cmd, 1);
            rc = ok ? 0 : -1;
        } else {
            LOG_ERR("do_local_print_flow: no LAN MQTT connection for dev %s",
                    params.dev_id.c_str());
        }
    }

    if (rc != 0) {
        if (update_fn) update_fn(PrintJobFailed, 0, "MQTT send failed");
        return -1;
    }

    if (update_fn) update_fn(PrintJobSent, 100, "Print command sent");
    if (update_fn) update_fn(PrintJobFinished, 100, "Done");
    return 0;
}

// ---- Print jobs -------------------------------------------------------------

int NetworkAgent::start_local_print(const PrintParams& params,
                                     std::function<void(int,int,std::string)> update_fn,
                                     std::function<bool()>                    cancel_fn)
{
    LOG_INFO("start_local_print: dev=%s file=%s",
             params.dev_id.c_str(), params.filename.c_str());
    return do_local_print_flow(params, update_fn, cancel_fn, /*send_start_cmd=*/true);
}

int NetworkAgent::start_local_print_with_record(const PrintParams& params,
                                                 std::function<void(int,int,std::string)> update_fn,
                                                 std::function<bool()>                    cancel_fn,
                                                 std::function<bool(int,std::string)>     wait_fn)
{
    LOG_INFO("start_local_print_with_record: dev=%s", params.dev_id.c_str());
    int rc = do_local_print_flow(params, update_fn, cancel_fn, /*send_start_cmd=*/true);
    if (rc == 0 && wait_fn) {
        // wait_fn polls the print status; call it until it returns false
        while (wait_fn(0, "")) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    return rc;
}

int NetworkAgent::start_send_gcode_to_sdcard(const PrintParams& params,
                                              std::function<void(int,int,std::string)> update_fn,
                                              std::function<bool()>                    cancel_fn,
                                              std::function<bool(int,std::string)>     /*wait_fn*/)
{
    LOG_INFO("start_send_gcode_to_sdcard: dev=%s", params.dev_id.c_str());
    // Upload only, no print command
    return do_local_print_flow(params, update_fn, cancel_fn, /*send_start_cmd=*/false);
}

int NetworkAgent::start_sdcard_print(const PrintParams& params,
                                      std::function<void(int,int,std::string)> update_fn,
                                      std::function<bool()>                    cancel_fn)
{
    LOG_INFO("start_sdcard_print: dev=%s", params.dev_id.c_str());
    // Send print command for a file already on the SD card
    if (update_fn) update_fn(PrintJobSending, 0, "Sending print command");

    std::string ftp_filename = params.ftp_file.empty() ? params.filename : params.ftp_file;
    size_t slash = ftp_filename.rfind('/');
    if (slash != std::string::npos) ftp_filename = ftp_filename.substr(slash + 1);

    std::string ftp_url = "ftp:///sdcard/" + ftp_filename;
    std::string print_cmd = build_print_command(params, ftp_url);
    {
        json j = json::parse(print_cmd);
        j["print"]["sequence_id"] = next_seq_id();
        print_cmd = j.dump();
    }

    if (cancel_fn && cancel_fn()) {
        if (update_fn) update_fn(PrintJobCancel, 0, "Cancelled");
        return -1;
    }

    int rc = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lan_mqtt) {
            std::string topic = "device/" + params.dev_id + "/request";
            bool ok = m_lan_mqtt->publish(topic, print_cmd, 1);
            rc = ok ? 0 : -1;
        }
    }

    if (rc == 0) {
        if (update_fn) update_fn(PrintJobFinished, 100, "Print started");
    } else {
        if (update_fn) update_fn(PrintJobFailed, 0, "MQTT send failed");
    }
    return rc;
}

int NetworkAgent::start_print(const PrintParams& params,
                               std::function<void(int,int,std::string)> update_fn,
                               std::function<bool()>                    cancel_fn,
                               std::function<bool(int,std::string)>     wait_fn)
{
    LOG_INFO("start_print (cloud): dev=%s", params.dev_id.c_str());

    if (update_fn) update_fn(PrintJobInit, 0, "Starting cloud print");

    // Determine if we have a direct IP for hybrid mode
    bool has_ip = !params.dev_ip.empty();

    std::string ftp_filename = params.ftp_file;
    if (ftp_filename.empty()) {
        ftp_filename = params.filename;
        size_t sl = ftp_filename.rfind('/');
        if (sl != std::string::npos) ftp_filename = ftp_filename.substr(sl + 1);
    }
    if (ftp_filename.empty()) ftp_filename = "job.3mf";

    if (has_ip) {
        // Hybrid: FTP directly to printer, MQTT via cloud
        if (update_fn) update_fn(PrintJobUploadFile, 0, "Uploading file via FTP");
        if (cancel_fn && cancel_fn()) {
            if (update_fn) update_fn(PrintJobCancel, 0, "Cancelled");
            return -1;
        }

        FtpClient ftp;
        bool ok = ftp.upload(
            params.dev_ip,
            990,
            params.username.empty() ? "bblp" : params.username,
            params.password,
            params.filename,
            ftp_filename,
            params.use_ssl_for_ftp,
            [&update_fn](double pct) {
                if (update_fn) update_fn(PrintJobUploadFile, (int)pct, "Uploading");
            }
        );
        if (!ok) {
            if (update_fn) update_fn(PrintJobFailed, 0, "FTP failed");
            return -1;
        }
    } else {
        // Upload to cloud
        if (update_fn) update_fn(PrintJobUploadFile, 0, "Uploading to cloud");
        std::string token;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            token = m_access_token;
        }
        unsigned int code = 0;
        m_cloud_client->upload_file(token, params.filename, ftp_filename, &code);
        if (code != 200) {
            if (update_fn) update_fn(PrintJobFailed, 0, "Cloud upload failed");
            return -1;
        }
    }

    if (cancel_fn && cancel_fn()) {
        if (update_fn) update_fn(PrintJobCancel, 0, "Cancelled");
        return -1;
    }

    // Wait for printer to index
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    if (update_fn) update_fn(PrintJobSending, 0, "Sending print command via cloud");

    std::string ftp_url  = "ftp:///cache/" + ftp_filename;
    std::string print_cmd = build_print_command(params, ftp_url);
    {
        json j = json::parse(print_cmd);
        j["print"]["sequence_id"] = next_seq_id();
        print_cmd = j.dump();
    }

    int rc = send_message(params.dev_id, print_cmd, 1, 0);
    if (rc != 0) {
        if (update_fn) update_fn(PrintJobFailed, 0, "Cloud MQTT send failed");
        return -1;
    }

    if (update_fn) update_fn(PrintJobSent, 100, "Print command sent");

    if (wait_fn) {
        while (wait_fn(0, "")) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    if (update_fn) update_fn(PrintJobFinished, 100, "Done");
    return 0;
}

// ---- User presets -----------------------------------------------------------

int NetworkAgent::get_user_presets(std::map<std::string,std::map<std::string,std::string>>* presets)
{
    if (presets) presets->clear();
    return 0;
}

std::string NetworkAgent::request_setting_id(const std::string& name,
                                              std::map<std::string,std::string>* values,
                                              unsigned int* http_code)
{
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_access_token;
    }
    static std::map<std::string, std::string> empty;
    auto& v = values ? *values : empty;
    return m_cloud_client->request_setting_id(token, name, v, http_code);
}

int NetworkAgent::put_setting(const std::string& setting_id,
                               const std::string& name,
                               std::map<std::string,std::string>* values,
                               unsigned int* http_code)
{
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_access_token;
    }
    static std::map<std::string, std::string> empty;
    auto& v = values ? *values : empty;
    m_cloud_client->put_setting(token, setting_id, name, v, http_code);
    return (http_code && *http_code == 200) ? 0 : -1;
}

int NetworkAgent::get_setting_list(const std::string& bundle_version,
                                    std::function<void(int)>  progress_fn,
                                    std::function<bool()>     cancel_fn)
{
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_access_token;
    }
    if (cancel_fn && cancel_fn()) return -1;
    if (progress_fn) progress_fn(0);
    unsigned int code = 0;
    m_cloud_client->get_setting_list(token, bundle_version, &code);
    if (progress_fn) progress_fn(100);
    return (code == 200) ? 0 : -1;
}

int NetworkAgent::get_setting_list2(const std::string& bundle_version,
                                     std::function<bool(std::map<std::string,std::string>)> check_fn,
                                     std::function<void(int)>  progress_fn,
                                     std::function<bool()>     cancel_fn)
{
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_access_token;
    }
    if (cancel_fn && cancel_fn()) return -1;
    if (progress_fn) progress_fn(0);
    unsigned int code = 0;
    std::string body = m_cloud_client->get_setting_list(token, bundle_version, &code);
    if (progress_fn) progress_fn(50);

    if (check_fn) {
        // Pass raw body as a single-entry map for the caller to inspect
        std::map<std::string, std::string> result{{"body", body}};
        check_fn(result);
    }
    if (progress_fn) progress_fn(100);
    return (code == 200) ? 0 : -1;
}

int NetworkAgent::delete_setting(const std::string& setting_id)
{
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_access_token;
    }
    unsigned int code = 0;
    return m_cloud_client->delete_setting(token, setting_id, &code);
}

// ---- Misc -------------------------------------------------------------------

int NetworkAgent::get_user_info(int* identifier)
{
    if (identifier) *identifier = 0;
    return 0;
}

std::string NetworkAgent::build_login_cmd() const
{
    json j;
    j["command"]  = "login";
    j["user_id"]  = get_user_id();
    return j.dump();
}

std::string NetworkAgent::build_logout_cmd() const
{
    json j;
    j["command"] = "logout";
    return j.dump();
}

std::string NetworkAgent::build_login_info() const
{
    json j;
    j["user_id"]   = get_user_id();
    j["user_name"] = get_user_name();
    return j.dump();
}

int NetworkAgent::update_cert()
{
    LOG_INFO("update_cert");
    return 0;
}

void NetworkAgent::install_device_cert(const std::string& dev_id, bool lan_only)
{
    LOG_INFO("install_device_cert: dev_id=%s lan_only=%d",
             dev_id.c_str(), (int)lan_only);
}
