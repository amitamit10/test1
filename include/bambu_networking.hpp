#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstdint>

enum SendingPrintJobStage {
    PrintJobInit      = 0,
    PrintJobUploadFile = 1,
    PrintJobWaiting   = 2,
    PrintJobSending   = 3,
    PrintJobSent      = 4,
    PrintJobFinished  = 5,
    PrintJobCancel    = 6,
    PrintJobFailed    = 7,
};

enum ConnectStatus {
    ConnectStatusOk     = 0,
    ConnectStatusFailed = 1,
    ConnectStatusLost   = 2,
};

enum MessageFlag {
    MessageFlagNone    = 0,
    MessageFlagSign    = 1,
    MessageFlagEncrypt = 2,
};

enum BindJobStage {
    BindJobInit       = 0,
    BindJobConnecting = 1,
    BindJobGotPing    = 2,
    BindJobLogin      = 3,
    BindJobBound      = 4,
    BindJobFailed     = 5,
};

struct PrintParams {
    std::string dev_id;
    std::string task_name;
    std::string project_name;
    std::string preset_name;
    std::string filename;
    std::string config_filename;
    std::string plate_index;
    std::string ftp_folder;
    std::string ftp_file;
    std::string ftp_file_md5;
    std::string nozzle_mapping;
    std::string ams_mapping;
    std::string ams_mapping2;
    std::string ams_mapping_info;
    std::string nozzles_info;
    std::string connection_type;
    std::string comments;
    std::string dev_name;
    std::string dev_ip;
    std::string username;
    std::string password;
    std::string extra_options;
    std::string task_bed_type;
    std::string task_timelapse_use_internal;

    bool use_ssl_for_ftp{true};
    bool use_ssl_for_mqtt{true};
    bool task_bed_leveling{false};
    bool task_flow_cali{false};
    bool task_vibration_cali{false};
    bool task_layer_inspect{false};
    bool task_record_timelapse{false};
    bool task_use_ams{false};
    bool auto_bed_leveling{false};
    bool auto_flow_cali{false};
    bool auto_offset_cali{false};
    bool extruder_cali_manual_mode{false};
    bool task_ext_change_assist{false};
    bool try_emmc_print{false};
};

struct detectResult {
    std::string result_msg;
    std::string command;
    std::string dev_id;
    std::string model_id;
    std::string dev_name;
    std::string version;
    std::string bind_state;
    std::string connect_type;
};

// Visibility macro for exported symbols
#if defined(_WIN32) || defined(__CYGWIN__)
#  define BAMBU_EXPORT __declspec(dllexport)
#else
#  define BAMBU_EXPORT __attribute__((visibility("default")))
#endif
