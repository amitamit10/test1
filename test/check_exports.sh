#!/usr/bin/env bash
# check_exports.sh
# Build bambu_networking.so and verify all critical C ABI symbols are exported.
#
# Usage:
#   cd /path/to/repo
#   ./test/check_exports.sh [build_dir]
#
# The optional argument specifies an existing CMake build directory.
# If not given, the script creates "build" under the repo root.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-${REPO_DIR}/build}"

echo "=== Bambu Networking export check ==="
echo "Repo   : ${REPO_DIR}"
echo "Build  : ${BUILD_DIR}"
echo

# ---- Build -------------------------------------------------------------------
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    echo "[1/2] Configuring CMake..."
    cmake -S "${REPO_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
fi

echo "[2/2] Building..."
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

SO_PATH="${BUILD_DIR}/libbambu_networking.so"
if [ ! -f "${SO_PATH}" ]; then
    echo "ERROR: shared library not found at ${SO_PATH}"
    exit 1
fi

echo
echo "=== Checking exports in ${SO_PATH} ==="
echo

# ---- List of required symbols -----------------------------------------------
REQUIRED_SYMBOLS=(
    "bambu_network_check_debug_consistent"
    "bambu_network_get_version"
    "bambu_network_create_agent"
    "bambu_network_destroy_agent"
    "bambu_network_init_log"
    "bambu_network_set_config_dir"
    "bambu_network_set_cert_file"
    "bambu_network_set_country_code"
    "bambu_network_start"
    "bambu_network_set_on_ssdp_msg_fn"
    "bambu_network_set_on_user_login_fn"
    "bambu_network_set_on_printer_connected_fn"
    "bambu_network_set_on_server_connected_fn"
    "bambu_network_set_on_http_error_fn"
    "bambu_network_set_get_country_code_fn"
    "bambu_network_set_on_subscribe_failure_fn"
    "bambu_network_set_on_message_fn"
    "bambu_network_set_on_user_message_fn"
    "bambu_network_set_on_local_connect_fn"
    "bambu_network_set_on_local_message_fn"
    "bambu_network_set_queue_on_main_fn"
    "bambu_network_set_server_callback"
    "bambu_network_connect_printer"
    "bambu_network_disconnect_printer"
    "bambu_network_send_message_to_printer"
    "bambu_network_start_discovery"
    "bambu_network_connect_server"
    "bambu_network_is_server_connected"
    "bambu_network_refresh_connection"
    "bambu_network_start_subscribe"
    "bambu_network_stop_subscribe"
    "bambu_network_add_subscribe"
    "bambu_network_del_subscribe"
    "bambu_network_enable_multi_machine"
    "bambu_network_send_message"
    "bambu_network_change_user"
    "bambu_network_is_user_login"
    "bambu_network_user_logout"
    "bambu_network_get_user_id"
    "bambu_network_get_user_name"
    "bambu_network_get_user_avatar"
    "bambu_network_get_user_nickanme"
    "bambu_network_get_my_profile"
    "bambu_network_get_my_token"
    "bambu_network_ping_bind"
    "bambu_network_bind_detect"
    "bambu_network_bind"
    "bambu_network_unbind"
    "bambu_network_request_bind_ticket"
    "bambu_network_query_bind_status"
    "bambu_network_get_bambulab_host"
    "bambu_network_get_user_selected_machine"
    "bambu_network_set_user_selected_machine"
    "bambu_network_modify_printer_name"
    "bambu_network_get_printer_firmware"
    "bambu_network_start_print"
    "bambu_network_start_local_print"
    "bambu_network_start_local_print_with_record"
    "bambu_network_start_send_gcode_to_sdcard"
    "bambu_network_start_sdcard_print"
    "bambu_network_get_user_presets"
    "bambu_network_request_setting_id"
    "bambu_network_put_setting"
    "bambu_network_get_setting_list"
    "bambu_network_get_setting_list2"
    "bambu_network_delete_setting"
    "bambu_network_set_extra_http_header"
    "bambu_network_get_user_info"
    "bambu_network_build_login_cmd"
    "bambu_network_build_logout_cmd"
    "bambu_network_build_login_info"
    "bambu_network_update_cert"
    "bambu_network_install_device_cert"
)

# Get dynamic symbol table
NM_OUTPUT=$(nm -D --defined-only "${SO_PATH}" 2>/dev/null || nm -D "${SO_PATH}")

PASS=0
FAIL=0
MISSING=()

for sym in "${REQUIRED_SYMBOLS[@]}"; do
    if echo "${NM_OUTPUT}" | grep -q " ${sym}$\| ${sym} "; then
        echo "  [OK] ${sym}"
        PASS=$(( PASS + 1 ))
    else
        echo "  [MISSING] ${sym}"
        MISSING+=("${sym}")
        FAIL=$(( FAIL + 1 ))
    fi
done

echo
echo "=== Results: ${PASS} passed, ${FAIL} missing ==="

if [ ${FAIL} -gt 0 ]; then
    echo
    echo "Missing symbols:"
    for sym in "${MISSING[@]}"; do
        echo "  - ${sym}"
    done
    exit 1
fi

echo "All required exports are present."
exit 0
