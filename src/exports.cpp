/**
 * exports.cpp — extern "C" ABI surface for bambu_networking.so
 *
 * Every function here casts the opaque void* agent handle to NetworkAgent*,
 * null-checks it, and delegates to the real implementation.
 */

#include "../include/bambu_networking.hpp"
#include "NetworkAgent.hpp"
#include "Logger.hpp"

#include <string>
#include <vector>
#include <map>
#include <functional>

// Version string is defined at compile time via -DBAMBU_PLUGIN_VERSION="..."
#ifndef BAMBU_PLUGIN_VERSION
#  define BAMBU_PLUGIN_VERSION "02.06.00.00"
#endif

#define AGENT_CAST(ptr)  static_cast<NetworkAgent*>(ptr)
#define CHECK_AGENT(ptr) do { if (!(ptr)) { LOG_ERR("%s: null agent", __func__); return -1; } } while(0)

extern "C" {

// ============================================================
// Core lifecycle
// ============================================================

BAMBU_EXPORT bool bambu_network_check_debug_consistent(bool is_debug)
{
#ifdef NDEBUG
    return !is_debug;
#else
    return is_debug;
#endif
}

BAMBU_EXPORT std::string bambu_network_get_version()
{
    return BAMBU_PLUGIN_VERSION;
}

BAMBU_EXPORT void* bambu_network_create_agent(std::string log_dir)
{
    try {
        auto* agent = new NetworkAgent(log_dir);
        LOG_INFO("bambu_network_create_agent -> %p", (void*)agent);
        return static_cast<void*>(agent);
    } catch (const std::exception& e) {
        LOG_ERR("bambu_network_create_agent: exception: %s", e.what());
        return nullptr;
    }
}

BAMBU_EXPORT int bambu_network_destroy_agent(void* agent)
{
    CHECK_AGENT(agent);
    delete AGENT_CAST(agent);
    return 0;
}

BAMBU_EXPORT int bambu_network_init_log(void* agent)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->init_log();
}

BAMBU_EXPORT int bambu_network_set_config_dir(void* agent, std::string path)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->set_config_dir(path);
}

BAMBU_EXPORT int bambu_network_set_cert_file(void* agent, std::string folder, std::string filename)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->set_cert_file(folder, filename);
}

BAMBU_EXPORT int bambu_network_set_country_code(void* agent, std::string code)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->set_country_code(code);
}

BAMBU_EXPORT int bambu_network_start(void* agent)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->start();
}

// ============================================================
// Callback registration
// ============================================================

BAMBU_EXPORT int bambu_network_set_on_ssdp_msg_fn(void* agent, std::function<void(std::string)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_ssdp_msg(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_on_user_login_fn(void* agent, std::function<void(int, bool)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_user_login(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_on_printer_connected_fn(void* agent, std::function<void(std::string)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_printer_connected(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_on_server_connected_fn(void* agent, std::function<void(int, int)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_server_connected(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_on_http_error_fn(void* agent, std::function<void(unsigned, std::string)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_http_error(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_get_country_code_fn(void* agent, std::function<std::string()> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_get_country_code(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_on_subscribe_failure_fn(void* agent, std::function<void(std::string)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_subscribe_failure(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_on_message_fn(void* agent, std::function<void(std::string, std::string)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_message(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_on_user_message_fn(void* agent, std::function<void(std::string, std::string)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_user_message(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_on_local_connect_fn(void* agent,
                                                        std::function<void(int, std::string, std::string)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_local_connect(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_on_local_message_fn(void* agent, std::function<void(std::string, std::string)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_on_local_message(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_queue_on_main_fn(void* agent, std::function<void(std::function<void()>)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_queue_on_main(std::move(fn));
    return 0;
}

BAMBU_EXPORT int bambu_network_set_server_callback(void* agent, std::function<void(std::string, int)> fn)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_server_callback(std::move(fn));
    return 0;
}

// ============================================================
// LAN printer connection
// ============================================================

BAMBU_EXPORT int bambu_network_connect_printer(void*        agent,
                                                std::string  dev_id,
                                                std::string  dev_ip,
                                                std::string  username,
                                                std::string  password,
                                                bool         use_ssl)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->connect_printer(dev_id, dev_ip, username, password, use_ssl);
}

BAMBU_EXPORT int bambu_network_disconnect_printer(void* agent)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->disconnect_printer();
}

BAMBU_EXPORT int bambu_network_send_message_to_printer(void*       agent,
                                                        std::string dev_id,
                                                        std::string json,
                                                        int         qos,
                                                        int         flag)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->send_message_to_printer(dev_id, json, qos, flag);
}

BAMBU_EXPORT bool bambu_network_start_discovery(void* agent, bool start, bool sending)
{
    if (!agent) return false;
    return AGENT_CAST(agent)->start_discovery(start, sending);
}

// ============================================================
// Cloud connection
// ============================================================

BAMBU_EXPORT int bambu_network_connect_server(void* agent)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->connect_server();
}

BAMBU_EXPORT bool bambu_network_is_server_connected(void* agent)
{
    if (!agent) return false;
    return AGENT_CAST(agent)->is_server_connected();
}

BAMBU_EXPORT int bambu_network_refresh_connection(void* agent)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->refresh_connection();
}

BAMBU_EXPORT int bambu_network_start_subscribe(void* agent, std::string module)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->start_subscribe(module);
}

BAMBU_EXPORT int bambu_network_stop_subscribe(void* agent, std::string module)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->stop_subscribe(module);
}

BAMBU_EXPORT int bambu_network_add_subscribe(void* agent, std::vector<std::string> dev_list)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->add_subscribe(dev_list);
}

BAMBU_EXPORT int bambu_network_del_subscribe(void* agent, std::vector<std::string> dev_list)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->del_subscribe(dev_list);
}

BAMBU_EXPORT void bambu_network_enable_multi_machine(void* agent, bool enable)
{
    if (!agent) return;
    AGENT_CAST(agent)->enable_multi_machine(enable);
}

BAMBU_EXPORT int bambu_network_send_message(void*       agent,
                                             std::string dev_id,
                                             std::string json,
                                             int         qos,
                                             int         flag)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->send_message(dev_id, json, qos, flag);
}

// ============================================================
// User / auth
// ============================================================

BAMBU_EXPORT int bambu_network_change_user(void* agent, std::string user_info_json)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->change_user(user_info_json);
}

BAMBU_EXPORT bool bambu_network_is_user_login(void* agent)
{
    if (!agent) return false;
    return AGENT_CAST(agent)->is_user_login();
}

BAMBU_EXPORT int bambu_network_user_logout(void* agent, bool request)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->user_logout(request);
}

BAMBU_EXPORT std::string bambu_network_get_user_id(void* agent)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->get_user_id();
}

BAMBU_EXPORT std::string bambu_network_get_user_name(void* agent)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->get_user_name();
}

BAMBU_EXPORT std::string bambu_network_get_user_avatar(void* agent)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->get_user_avatar();
}

// Intentional typo — matches OrcaSlicer's expected symbol name
BAMBU_EXPORT std::string bambu_network_get_user_nickanme(void* agent)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->get_user_nickname();
}

BAMBU_EXPORT int bambu_network_get_my_profile(void*         agent,
                                               std::string   token,
                                               unsigned int* http_code,
                                               std::string*  http_body)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->get_my_profile(token, http_code, http_body);
}

BAMBU_EXPORT int bambu_network_get_my_token(void*         agent,
                                             std::string   ticket,
                                             unsigned int* http_code,
                                             std::string*  http_body)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->get_my_token(ticket, http_code, http_body);
}

// ============================================================
// Binding
// ============================================================

BAMBU_EXPORT int bambu_network_ping_bind(void* agent, std::string ping_code)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->ping_bind(ping_code);
}

BAMBU_EXPORT int bambu_network_bind_detect(void*         agent,
                                            std::string   dev_ip,
                                            std::string   sec_link,
                                            detectResult& result)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->bind_detect(dev_ip, sec_link, result);
}

BAMBU_EXPORT int bambu_network_bind(void*       agent,
                                     std::string dev_ip,
                                     std::string dev_id,
                                     std::string sec_link,
                                     std::string timezone,
                                     bool        improved,
                                     std::function<void(int,int,std::string)> fn)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->bind(dev_ip, dev_id, sec_link, timezone, improved, std::move(fn));
}

BAMBU_EXPORT int bambu_network_unbind(void* agent, std::string dev_id)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->unbind(dev_id);
}

BAMBU_EXPORT int bambu_network_request_bind_ticket(void* agent, std::string* ticket)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->request_bind_ticket(ticket);
}

BAMBU_EXPORT int bambu_network_query_bind_status(void*                    agent,
                                                  std::vector<std::string> query_list,
                                                  unsigned int*            http_code,
                                                  std::string*             http_body)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->query_bind_status(query_list, http_code, http_body);
}

BAMBU_EXPORT std::string bambu_network_get_bambulab_host(void* agent)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->get_bambulab_host();
}

BAMBU_EXPORT std::string bambu_network_get_user_selected_machine(void* agent)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->get_user_selected_machine();
}

BAMBU_EXPORT int bambu_network_set_user_selected_machine(void* agent, std::string dev_id)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->set_user_selected_machine(dev_id);
}

BAMBU_EXPORT int bambu_network_modify_printer_name(void* agent, std::string dev_id, std::string dev_name)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->modify_printer_name(dev_id, dev_name);
}

BAMBU_EXPORT int bambu_network_get_printer_firmware(void*       agent,
                                                     std::string dev_id,
                                                     unsigned*   http_code,
                                                     std::string* http_body)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->get_printer_firmware(dev_id, http_code, http_body);
}

// ============================================================
// Print jobs (critical path)
// ============================================================

BAMBU_EXPORT int bambu_network_start_print(void*       agent,
                                            PrintParams params,
                                            std::function<void(int,int,std::string)> update_fn,
                                            std::function<bool()>                    cancel_fn,
                                            std::function<bool(int,std::string)>     wait_fn)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->start_print(params, std::move(update_fn),
                                          std::move(cancel_fn), std::move(wait_fn));
}

BAMBU_EXPORT int bambu_network_start_local_print(void*       agent,
                                                  PrintParams params,
                                                  std::function<void(int,int,std::string)> update_fn,
                                                  std::function<bool()>                    cancel_fn)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->start_local_print(params, std::move(update_fn), std::move(cancel_fn));
}

BAMBU_EXPORT int bambu_network_start_local_print_with_record(void*       agent,
                                                              PrintParams params,
                                                              std::function<void(int,int,std::string)> update_fn,
                                                              std::function<bool()>                    cancel_fn,
                                                              std::function<bool(int,std::string)>     wait_fn)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->start_local_print_with_record(params, std::move(update_fn),
                                                            std::move(cancel_fn), std::move(wait_fn));
}

BAMBU_EXPORT int bambu_network_start_send_gcode_to_sdcard(void*       agent,
                                                           PrintParams params,
                                                           std::function<void(int,int,std::string)> update_fn,
                                                           std::function<bool()>                    cancel_fn,
                                                           std::function<bool(int,std::string)>     wait_fn)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->start_send_gcode_to_sdcard(params, std::move(update_fn),
                                                         std::move(cancel_fn), std::move(wait_fn));
}

BAMBU_EXPORT int bambu_network_start_sdcard_print(void*       agent,
                                                   PrintParams params,
                                                   std::function<void(int,int,std::string)> update_fn,
                                                   std::function<bool()>                    cancel_fn)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->start_sdcard_print(params, std::move(update_fn), std::move(cancel_fn));
}

// ============================================================
// User presets
// ============================================================

BAMBU_EXPORT int bambu_network_get_user_presets(
    void* agent,
    std::map<std::string,std::map<std::string,std::string>>* presets)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->get_user_presets(presets);
}

BAMBU_EXPORT std::string bambu_network_request_setting_id(void*       agent,
                                                           std::string name,
                                                           std::map<std::string,std::string>* values,
                                                           unsigned int* http_code)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->request_setting_id(name, values, http_code);
}

BAMBU_EXPORT int bambu_network_put_setting(void*       agent,
                                            std::string setting_id,
                                            std::string name,
                                            std::map<std::string,std::string>* values,
                                            unsigned int* http_code)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->put_setting(setting_id, name, values, http_code);
}

BAMBU_EXPORT int bambu_network_get_setting_list(void*       agent,
                                                 std::string bundle_version,
                                                 std::function<void(int)>  progress_fn,
                                                 std::function<bool()>     cancel_fn)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->get_setting_list(bundle_version,
                                               std::move(progress_fn), std::move(cancel_fn));
}

BAMBU_EXPORT int bambu_network_get_setting_list2(
    void*       agent,
    std::string bundle_version,
    std::function<bool(std::map<std::string,std::string>)> check_fn,
    std::function<void(int)>  progress_fn,
    std::function<bool()>     cancel_fn)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->get_setting_list2(bundle_version,
                                                std::move(check_fn),
                                                std::move(progress_fn),
                                                std::move(cancel_fn));
}

BAMBU_EXPORT int bambu_network_delete_setting(void* agent, std::string setting_id)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->delete_setting(setting_id);
}

// ============================================================
// Misc
// ============================================================

BAMBU_EXPORT int bambu_network_set_extra_http_header(void* agent, std::string key, std::string value)
{
    CHECK_AGENT(agent);
    AGENT_CAST(agent)->set_extra_http_header(key, value);
    return 0;
}

BAMBU_EXPORT int bambu_network_get_user_info(void* agent, int* identifier)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->get_user_info(identifier);
}

BAMBU_EXPORT std::string bambu_network_build_login_cmd(void* agent)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->build_login_cmd();
}

BAMBU_EXPORT std::string bambu_network_build_logout_cmd(void* agent)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->build_logout_cmd();
}

BAMBU_EXPORT std::string bambu_network_build_login_info(void* agent)
{
    if (!agent) return "";
    return AGENT_CAST(agent)->build_login_info();
}

BAMBU_EXPORT int bambu_network_update_cert(void* agent)
{
    CHECK_AGENT(agent);
    return AGENT_CAST(agent)->update_cert();
}

BAMBU_EXPORT void bambu_network_install_device_cert(void* agent, std::string dev_id, bool lan_only)
{
    if (!agent) return;
    AGENT_CAST(agent)->install_device_cert(dev_id, lan_only);
}

} // extern "C"
