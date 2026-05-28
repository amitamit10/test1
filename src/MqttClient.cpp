#include "MqttClient.hpp"
#include <mosquitto.h>
#include <cstring>
#include <stdexcept>

// ---- mosquitto library init/cleanup (process-level) -------------------------
namespace {
struct MosquittoLib {
    MosquittoLib()  { mosquitto_lib_init(); }
    ~MosquittoLib() { mosquitto_lib_cleanup(); }
};
static MosquittoLib g_mosq_lib;
} // namespace

// ---- MqttClient ------------------------------------------------------------

MqttClient::MqttClient() {}

MqttClient::~MqttClient()
{
    disconnect();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mosq) {
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
    }
}

void MqttClient::set_credentials(const std::string& username, const std::string& password)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_username = username;
    m_password = password;
}

void MqttClient::set_client_id(const std::string& client_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_client_id = client_id;
}

void MqttClient::set_tls(bool enable, bool insecure,
                         const std::string& ca_file,
                         const std::string& cert_file,
                         const std::string& key_file)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tls_enabled  = enable;
    m_tls_insecure = insecure;
    m_ca_file      = ca_file;
    m_cert_file    = cert_file;
    m_key_file     = key_file;
}

void MqttClient::set_on_connect(ConnectCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_connect = std::move(cb);
}

void MqttClient::set_on_disconnect(DisconnectCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_disconnect = std::move(cb);
}

void MqttClient::set_on_message(MessageCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_on_message = std::move(cb);
}

bool MqttClient::connect(const std::string& host, int port, int keepalive)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Create mosquitto instance
    const char* cid = m_client_id.empty() ? nullptr : m_client_id.c_str();
    m_mosq = mosquitto_new(cid, true, this);
    if (!m_mosq) {
        LOG_ERR("mosquitto_new() failed");
        return false;
    }

    // Register callbacks
    mosquitto_connect_callback_set(m_mosq, s_on_connect);
    mosquitto_disconnect_callback_set(m_mosq, s_on_disconnect);
    mosquitto_message_callback_set(m_mosq, s_on_message);
    mosquitto_log_callback_set(m_mosq, s_on_log);

    // Credentials
    if (!m_username.empty()) {
        int rc = mosquitto_username_pw_set(m_mosq,
                                           m_username.c_str(),
                                           m_password.c_str());
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG_ERR("mosquitto_username_pw_set failed: %s", mosquitto_strerror(rc));
            mosquitto_destroy(m_mosq);
            m_mosq = nullptr;
            return false;
        }
    }

    // TLS
    if (m_tls_enabled) {
        const char* ca   = m_ca_file.empty()   ? nullptr : m_ca_file.c_str();
        const char* cert = m_cert_file.empty() ? nullptr : m_cert_file.c_str();
        const char* key  = m_key_file.empty()  ? nullptr : m_key_file.c_str();
        // ca can be nullptr when insecure mode is used
        int rc = mosquitto_tls_set(m_mosq, ca, nullptr, cert, key, nullptr);
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG_ERR("mosquitto_tls_set failed: %s", mosquitto_strerror(rc));
            mosquitto_destroy(m_mosq);
            m_mosq = nullptr;
            return false;
        }
        if (m_tls_insecure) {
            mosquitto_tls_insecure_set(m_mosq, true);
        }
        // Disable TLS version restriction
        mosquitto_tls_opts_set(m_mosq, 0, "tlsv1.2", nullptr);
    }

    // Async connect + start network loop thread
    int rc = mosquitto_connect_async(m_mosq, host.c_str(), port, keepalive);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERR("mosquitto_connect_async(%s:%d) failed: %s",
                host.c_str(), port, mosquitto_strerror(rc));
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
        return false;
    }

    rc = mosquitto_loop_start(m_mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERR("mosquitto_loop_start failed: %s", mosquitto_strerror(rc));
        mosquitto_disconnect(m_mosq);
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
        return false;
    }

    m_initialized = true;
    LOG_INFO("MQTT connecting to %s:%d", host.c_str(), port);
    return true;
}

void MqttClient::disconnect()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mosq && m_initialized) {
        mosquitto_disconnect(m_mosq);
        mosquitto_loop_stop(m_mosq, false);
        m_initialized = false;
    }
    m_connected = false;
}

bool MqttClient::is_connected() const
{
    return m_connected.load();
}

bool MqttClient::subscribe(const std::string& topic, int qos)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_mosq) {
        LOG_ERR("subscribe() called but no mosquitto instance");
        return false;
    }
    int rc = mosquitto_subscribe(m_mosq, nullptr, topic.c_str(), qos);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERR("mosquitto_subscribe(%s) failed: %s", topic.c_str(), mosquitto_strerror(rc));
        return false;
    }
    LOG_DBG("Subscribed to %s", topic.c_str());
    return true;
}

bool MqttClient::publish(const std::string& topic, const std::string& payload, int qos, bool retain)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_mosq) {
        LOG_ERR("publish() called but no mosquitto instance");
        return false;
    }
    int rc = mosquitto_publish(m_mosq, nullptr,
                               topic.c_str(),
                               (int)payload.size(),
                               payload.data(),
                               qos,
                               retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERR("mosquitto_publish(%s) failed: %s", topic.c_str(), mosquitto_strerror(rc));
        return false;
    }
    return true;
}

// ---- Static callbacks -------------------------------------------------------

void MqttClient::s_on_connect(struct mosquitto* /*mosq*/, void* userdata, int rc)
{
    auto* self = static_cast<MqttClient*>(userdata);
    self->on_connect(rc);
}

void MqttClient::s_on_disconnect(struct mosquitto* /*mosq*/, void* userdata, int rc)
{
    auto* self = static_cast<MqttClient*>(userdata);
    self->on_disconnect(rc);
}

void MqttClient::s_on_message(struct mosquitto* /*mosq*/, void* userdata,
                               const struct mosquitto_message* msg)
{
    auto* self = static_cast<MqttClient*>(userdata);
    self->on_message(msg);
}

void MqttClient::s_on_log(struct mosquitto* /*mosq*/, void* /*userdata*/, int level, const char* str)
{
    if (level == MOSQ_LOG_ERR) {
        LOG_ERR("[mosquitto] %s", str);
    } else if (level == MOSQ_LOG_WARNING) {
        LOG_INFO("[mosquitto] %s", str);
    }
    // Suppress DEBUG/INFO noise in production
}

// ---- Instance callbacks -------------------------------------------------------

void MqttClient::on_connect(int rc)
{
    if (rc == 0) {
        m_connected = true;
        LOG_INFO("MQTT connected (rc=%d)", rc);
    } else {
        m_connected = false;
        LOG_ERR("MQTT connect failed (rc=%d)", rc);
    }
    ConnectCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_on_connect;
    }
    if (cb) cb(rc);
}

void MqttClient::on_disconnect(int rc)
{
    m_connected = false;
    LOG_INFO("MQTT disconnected (rc=%d)", rc);
    DisconnectCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_on_disconnect;
    }
    if (cb) cb(rc);
}

void MqttClient::on_message(const struct mosquitto_message* msg)
{
    if (!msg) return;
    std::string topic(msg->topic ? msg->topic : "");
    std::string payload(msg->payload ? static_cast<const char*>(msg->payload) : "",
                        msg->payloadlen);
    MessageCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_on_message;
    }
    if (cb) cb(topic, payload);
}
