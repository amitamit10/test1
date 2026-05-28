#pragma once
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include "Logger.hpp"

// Forward declare mosquitto struct to avoid requiring the header in dependents
struct mosquitto;

class MqttClient {
public:
    using MessageCallback    = std::function<void(const std::string& topic, const std::string& payload)>;
    using ConnectCallback    = std::function<void(int rc)>;
    using DisconnectCallback = std::function<void(int rc)>;

    MqttClient();
    ~MqttClient();

    // Disable copy
    MqttClient(const MqttClient&)            = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    // Configuration — call before connect()
    void set_credentials(const std::string& username, const std::string& password);
    void set_client_id(const std::string& client_id);
    void set_tls(bool enable, bool insecure = true,
                 const std::string& ca_file = "",
                 const std::string& cert_file = "",
                 const std::string& key_file  = "");
    void set_on_connect(ConnectCallback cb);
    void set_on_disconnect(DisconnectCallback cb);
    void set_on_message(MessageCallback cb);

    // Lifecycle
    bool connect(const std::string& host, int port, int keepalive = 60);
    void disconnect();
    bool is_connected() const;

    // Pub/sub
    bool subscribe(const std::string& topic, int qos = 1);
    bool publish(const std::string& topic, const std::string& payload, int qos = 1, bool retain = false);

private:
    // Static mosquitto callbacks that forward to instance methods
    static void s_on_connect(struct mosquitto* mosq, void* userdata, int rc);
    static void s_on_disconnect(struct mosquitto* mosq, void* userdata, int rc);
    static void s_on_message(struct mosquitto* mosq, void* userdata,
                             const struct mosquitto_message* msg);
    static void s_on_log(struct mosquitto* mosq, void* userdata, int level, const char* str);

    void on_connect(int rc);
    void on_disconnect(int rc);
    void on_message(const struct mosquitto_message* msg);

    mutable std::mutex   m_mutex;
    struct mosquitto*    m_mosq{nullptr};
    std::atomic<bool>    m_connected{false};
    std::atomic<bool>    m_initialized{false};

    std::string          m_username;
    std::string          m_password;
    std::string          m_client_id;

    bool                 m_tls_enabled{false};
    bool                 m_tls_insecure{true};
    std::string          m_ca_file;
    std::string          m_cert_file;
    std::string          m_key_file;

    ConnectCallback      m_on_connect;
    DisconnectCallback   m_on_disconnect;
    MessageCallback      m_on_message;
};
