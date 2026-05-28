#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include "../include/bambu_networking.hpp"
#include "MqttClient.hpp"
#include "FtpClient.hpp"
#include "CloudClient.hpp"
#include "Logger.hpp"

/**
 * NetworkAgent – central coordinator for Bambu Lab printer communication.
 *
 * Owns:
 *   - MqttClient for LAN printer (direct IP connection)
 *   - MqttClient for cloud (us.mqtt.bambulab.com)
 *   - FtpClient for file upload
 *   - CloudClient for REST API
 *   - SSDP discovery thread
 */
class NetworkAgent {
public:
    NetworkAgent(const std::string& log_dir);
    ~NetworkAgent();

    NetworkAgent(const NetworkAgent&)            = delete;
    NetworkAgent& operator=(const NetworkAgent&) = delete;

    // ---- Configuration -------------------------------------------------------
    int  init_log();
    int  set_config_dir(const std::string& path);
    int  set_cert_file(const std::string& folder, const std::string& filename);
    int  set_country_code(const std::string& code);
    int  start();
    void set_extra_http_header(const std::string& key, const std::string& value);

    // ---- Callback setters ----------------------------------------------------
    void set_on_ssdp_msg(std::function<void(std::string)> fn);
    void set_on_user_login(std::function<void(int, bool)> fn);
    void set_on_printer_connected(std::function<void(std::string)> fn);
    void set_on_server_connected(std::function<void(int, int)> fn);
    void set_on_http_error(std::function<void(unsigned, std::string)> fn);
    void set_get_country_code(std::function<std::string()> fn);
    void set_on_subscribe_failure(std::function<void(std::string)> fn);
    void set_on_message(std::function<void(std::string, std::string)> fn);
    void set_on_user_message(std::function<void(std::string, std::string)> fn);
    void set_on_local_connect(std::function<void(int, std::string, std::string)> fn);
    void set_on_local_message(std::function<void(std::string, std::string)> fn);
    void set_queue_on_main(std::function<void(std::function<void()>)> fn);
    void set_server_callback(std::function<void(std::string, int)> fn);

    // ---- LAN -----------------------------------------------------------------
    int  connect_printer(const std::string& dev_id,
                         const std::string& dev_ip,
                         const std::string& username,
                         const std::string& password,
                         bool               use_ssl);
    int  disconnect_printer();
    int  send_message_to_printer(const std::string& dev_id,
                                  const std::string& json,
                                  int qos, int flag);
    bool start_discovery(bool start, bool sending);

    // ---- Cloud ---------------------------------------------------------------
    int  connect_server();
    bool is_server_connected() const;
    int  refresh_connection();
    int  start_subscribe(const std::string& module);
    int  stop_subscribe(const std::string& module);
    int  add_subscribe(const std::vector<std::string>& dev_list);
    int  del_subscribe(const std::vector<std::string>& dev_list);
    void enable_multi_machine(bool enable);
    int  send_message(const std::string& dev_id,
                      const std::string& json,
                      int qos, int flag);

    // ---- User / auth ---------------------------------------------------------
    int  change_user(const std::string& user_info_json);
    bool is_user_login() const;
    int  user_logout(bool request);
    std::string get_user_id()       const;
    std::string get_user_name()     const;
    std::string get_user_avatar()   const;
    std::string get_user_nickname() const;
    int  get_my_profile(const std::string& token,
                        unsigned int*      http_code,
                        std::string*       http_body);
    int  get_my_token(const std::string& ticket,
                      unsigned int*      http_code,
                      std::string*       http_body);

    // ---- Binding (stubs) -----------------------------------------------------
    int  ping_bind(const std::string& ping_code);
    int  bind_detect(const std::string& dev_ip,
                     const std::string& sec_link,
                     detectResult&      result);
    int  bind(const std::string& dev_ip,
              const std::string& dev_id,
              const std::string& sec_link,
              const std::string& timezone,
              bool improved,
              std::function<void(int,int,std::string)> fn);
    int  unbind(const std::string& dev_id);
    int  request_bind_ticket(std::string* ticket);
    int  query_bind_status(const std::vector<std::string>& query_list,
                           unsigned int* http_code,
                           std::string*  http_body);
    std::string get_bambulab_host()            const;
    std::string get_user_selected_machine()    const;
    int  set_user_selected_machine(const std::string& dev_id);
    int  modify_printer_name(const std::string& dev_id,
                              const std::string& dev_name);
    int  get_printer_firmware(const std::string& dev_id,
                               unsigned*          http_code,
                               std::string*       http_body);

    // ---- Print jobs ----------------------------------------------------------
    int start_print(const PrintParams& params,
                    std::function<void(int,int,std::string)> update_fn,
                    std::function<bool()>                    cancel_fn,
                    std::function<bool(int,std::string)>     wait_fn);

    int start_local_print(const PrintParams& params,
                          std::function<void(int,int,std::string)> update_fn,
                          std::function<bool()>                    cancel_fn);

    int start_local_print_with_record(const PrintParams& params,
                                      std::function<void(int,int,std::string)> update_fn,
                                      std::function<bool()>                    cancel_fn,
                                      std::function<bool(int,std::string)>     wait_fn);

    int start_send_gcode_to_sdcard(const PrintParams& params,
                                   std::function<void(int,int,std::string)> update_fn,
                                   std::function<bool()>                    cancel_fn,
                                   std::function<bool(int,std::string)>     wait_fn);

    int start_sdcard_print(const PrintParams& params,
                           std::function<void(int,int,std::string)> update_fn,
                           std::function<bool()>                    cancel_fn);

    // ---- User presets --------------------------------------------------------
    int get_user_presets(std::map<std::string,std::map<std::string,std::string>>* presets);
    std::string request_setting_id(const std::string& name,
                                   std::map<std::string,std::string>* values,
                                   unsigned int* http_code);
    int put_setting(const std::string& setting_id,
                    const std::string& name,
                    std::map<std::string,std::string>* values,
                    unsigned int* http_code);
    int get_setting_list(const std::string& bundle_version,
                         std::function<void(int)>  progress_fn,
                         std::function<bool()>     cancel_fn);
    int get_setting_list2(const std::string& bundle_version,
                          std::function<bool(std::map<std::string,std::string>)> check_fn,
                          std::function<void(int)>  progress_fn,
                          std::function<bool()>     cancel_fn);
    int delete_setting(const std::string& setting_id);

    // ---- Misc ----------------------------------------------------------------
    int  get_user_info(int* identifier);
    std::string build_login_cmd()  const;
    std::string build_logout_cmd() const;
    std::string build_login_info() const;
    int  update_cert();
    void install_device_cert(const std::string& dev_id, bool lan_only);

private:
    // SSDP thread implementation
    void ssdp_worker();
    void stop_ssdp();

    // Build JSON print command payload
    std::string build_print_command(const PrintParams& params,
                                    const std::string& ftp_url) const;

    // Execute the FTP + MQTT print flow (shared by local print functions)
    int do_local_print_flow(const PrintParams& params,
                            std::function<void(int,int,std::string)> update_fn,
                            std::function<bool()>                    cancel_fn,
                            bool send_start_cmd);

    // next sequence_id
    std::string next_seq_id();

    // ---- State ---------------------------------------------------------------
    mutable std::mutex  m_mutex;

    std::string m_log_dir;
    std::string m_config_dir;
    std::string m_cert_folder;
    std::string m_cert_filename;
    std::string m_country_code;

    // User state
    std::string m_user_id;
    std::string m_access_token;
    std::string m_user_name;
    std::string m_user_avatar;
    std::string m_user_nickname;

    // LAN connection state
    std::string m_lan_dev_id;
    std::string m_lan_dev_ip;

    // Selected machine
    std::string m_selected_dev_id;

    std::unique_ptr<MqttClient>   m_lan_mqtt;
    std::unique_ptr<MqttClient>   m_cloud_mqtt;
    std::unique_ptr<CloudClient>  m_cloud_client;

    std::atomic<bool>   m_server_connected{false};
    std::atomic<bool>   m_multi_machine{false};

    // Subscribed device list (for cloud MQTT)
    std::vector<std::string> m_subscribed_devs;

    // SSDP
    std::thread         m_ssdp_thread;
    std::atomic<bool>   m_ssdp_running{false};
    int                 m_ssdp_sock{-1};

    // Sequence ID counter
    std::atomic<uint32_t> m_seq_id{1};

    // Callbacks
    std::function<void(std::string)>                 m_on_ssdp_msg;
    std::function<void(int, bool)>                   m_on_user_login;
    std::function<void(std::string)>                 m_on_printer_connected;
    std::function<void(int, int)>                    m_on_server_connected;
    std::function<void(unsigned, std::string)>       m_on_http_error;
    std::function<std::string()>                     m_get_country_code;
    std::function<void(std::string)>                 m_on_subscribe_failure;
    std::function<void(std::string, std::string)>    m_on_message;
    std::function<void(std::string, std::string)>    m_on_user_message;
    std::function<void(int, std::string, std::string)> m_on_local_connect;
    std::function<void(std::string, std::string)>    m_on_local_message;
    std::function<void(std::function<void()>)>       m_queue_on_main;
    std::function<void(std::string, int)>            m_server_callback;
};
