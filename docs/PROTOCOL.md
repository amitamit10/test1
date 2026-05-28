# Bambu Lab Network Plugin — Protocol Reference

This document describes every network protocol used by the `bambu_networking` shared library.

---

## 1. LAN MQTT

### Connection parameters

| Parameter   | Value                       |
|-------------|-----------------------------|
| Host        | `{printer_ip}`              |
| Port        | `8883`                      |
| TLS         | Required (implicit)         |
| Username    | `bblp`                      |
| Password    | `{8-character access_code}` |
| Client ID   | `tango_{random_hex8}`       |
| Keep-alive  | 60 s                        |

### TLS behaviour

Development mode (default): `mosquitto_tls_insecure_set(true)` — peer certificate is **not** verified.  
Production mode: supply the Bambu CA certificate bundle in `set_cert_file()`.

### Topics

| Direction | Topic                       | Description              |
|-----------|-----------------------------|--------------------------|
| Subscribe | `device/{serial}/report`    | Status/telemetry from printer |
| Publish   | `device/{serial}/request`   | Commands sent to printer |

### Example: subscribe

```
SUBSCRIBE device/01P00A123456789/report  QoS=1
```

---

## 2. FTPS File Upload

Files are uploaded to the printer's local cache before a print job starts.

| Parameter   | Value                            |
|-------------|----------------------------------|
| Scheme      | `ftps://` (implicit TLS, port 990) |
| Host        | `{printer_ip}`                   |
| Port        | `990`                            |
| Username    | `bblp`                           |
| Password    | `{access_code}`                  |
| Remote path | `/cache/{filename}.3mf`          |

### libcurl options used

```c
CURLOPT_URL          = "ftps://{ip}:990/cache/job.3mf"
CURLOPT_USE_SSL      = CURLUSESSL_ALL
CURLOPT_SSL_VERIFYPEER = 0
CURLOPT_SSL_VERIFYHOST = 0
CURLOPT_CONNECTTIMEOUT = 30     // seconds
CURLOPT_TIMEOUT        = 300    // seconds (5 min for large files)
```

### Example URL

```
ftps://192.168.1.25:990/cache/my_print_job.3mf
```

---

## 3. MQTT Print Command

After the FTP upload completes, wait **~2 seconds** (printer needs time to index the file), then send this JSON payload to `device/{serial}/request`:

```json
{
  "print": {
    "sequence_id": "0001",
    "command": "project_file",
    "param": "Metadata/plate_1.gcode",
    "url": "ftp:///cache/my_print_job.3mf",
    "timelapse": false,
    "bed_leveling": true,
    "flow_cali": false,
    "vibration_cali": true,
    "layer_inspect": false,
    "use_ams": false
  }
}
```

**Important:** The `url` field uses `ftp:///` (three slashes, no host).  The printer resolves the path against its own local FTP server.

### Field reference

| Field           | Type    | Description                                          |
|-----------------|---------|------------------------------------------------------|
| `sequence_id`   | string  | 4-digit zero-padded hex counter, e.g. `"000a"`       |
| `command`       | string  | Always `"project_file"` for .3mf prints             |
| `param`         | string  | Internal G-code path inside the .3mf archive        |
| `url`           | string  | `ftp:///cache/{filename}` (3 slashes)               |
| `timelapse`     | bool    | Record timelapse video                               |
| `bed_leveling`  | bool    | Run auto bed levelling before print                  |
| `flow_cali`     | bool    | Run flow calibration                                 |
| `vibration_cali`| bool    | Run vibration/resonance calibration                  |
| `layer_inspect` | bool    | Enable layer-by-layer inspection                     |
| `use_ams`       | bool    | Use AMS filament system                              |

---

## 4. SSDP Discovery

Bambu printers announce themselves via a custom SSDP variant on a non-standard port.

| Parameter           | Value                                    |
|---------------------|------------------------------------------|
| Protocol            | UDP multicast                            |
| Multicast group     | `239.255.255.250`                        |
| Port                | `1990` (**not** the standard SSDP 1900) |
| Search target (ST)  | `urn:bambulab-com:device:3dprinter:1`   |

### M-SEARCH packet

```
M-SEARCH * HTTP/1.1\r\n
HOST: 239.255.255.250:1990\r\n
MAN: "ssdp:discover"\r\n
MX: 3\r\n
ST: urn:bambulab-com:device:3dprinter:1\r\n
\r\n
```

Sent every 30 seconds from the discovery background thread.

### Response headers of interest

| Header      | Content                    |
|-------------|----------------------------|
| `BAMBU-SN`  | Printer serial number      |
| `BAMBU-NAME`| Printer display name       |
| `LOCATION`  | Printer IP address (or URL)|

### Callback JSON payload

When a printer is discovered, `m_on_ssdp_msg` is called with:

```json
{
  "dev_id":       "01P00A123456789",
  "dev_name":     "My X1C",
  "dev_ip":       "192.168.1.25",
  "connect_type": "lan"
}
```

---

## 5. Cloud REST API

Base URL: `https://api.bambulab.com` (China: `https://api.bambulab.cn`)

### Required headers (all requests)

```
X-BBL-Client-Name: OrcaSlicer
X-BBL-Client-Type: slicer
X-BBL-Client-Version: 02.06.00.00
Authorization: Bearer {access_token}   (omit for unauthenticated endpoints)
```

### 5.1 Login

**POST** `/v1/user-service/user/login`

Request body:
```json
{"account": "user@example.com", "password": "s3cr3t"}
```

Success response:
```json
{
  "accessToken":  "eyJ...",
  "refreshToken": "eyJ...",
  "loginType":    "account"
}
```

If `loginType == "verifyCode"`, a verification code was emailed. Proceed to 5.2.

### 5.2 Two-factor verification

**POST** `/v1/user-service/user/login`

Request body:
```json
{"account": "user@example.com", "code": "123456"}
```

### 5.3 Get user profile / UID

**GET** `/v1/design-user-service/my/preference`  
Header: `Authorization: Bearer {access_token}`

Response contains the `uid` field (used as username prefix for cloud MQTT).

### 5.4 List bound devices

**GET** `/v1/iot-service/api/user/bind`  
Header: `Authorization: Bearer {access_token}`

Returns JSON array of device objects.

### 5.5 Upload file to cloud

**POST** `/v1/user-service/my/upload`  
`Content-Type: multipart/form-data`

Multipart field: `file` — the .3mf binary.

---

## 6. Cloud MQTT

Used when LAN connection is not available (or for multi-machine monitoring).

| Parameter   | Value                               |
|-------------|-------------------------------------|
| Host        | `us.mqtt.bambulab.com`              |
| Port        | `8883`                              |
| TLS         | Required, verify peer               |
| Username    | `u_{user_id}` (note `u_` prefix)    |
| Password    | `{access_token}` (full JWT)         |
| Client ID   | `tango_{random_hex8}`               |
| Topics      | Same as LAN (`device/{sn}/report` / `device/{sn}/request`) |

### Hybrid print flow (cloud MQTT + LAN FTP)

When the printer IP is known even in cloud mode:
1. Upload the .3mf via FTPS directly to the printer IP (same as LAN flow).
2. Send the print command via **cloud** MQTT.

This is the fastest path when on the same LAN but the cloud session is active.

---

## 7. Print job stage codes

These correspond to the `SendingPrintJobStage` enum and are reported via the `update_fn` callback during `start_local_print` / `start_print`.

| Value | Name              | Description                       |
|-------|-------------------|-----------------------------------|
| 0     | `PrintJobInit`    | Job initialising                  |
| 1     | `PrintJobUploadFile` | FTP / cloud upload in progress |
| 2     | `PrintJobWaiting` | Upload done, waiting for printer  |
| 3     | `PrintJobSending` | Sending MQTT print command        |
| 4     | `PrintJobSent`    | Command delivered                 |
| 5     | `PrintJobFinished`| Job complete                      |
| 6     | `PrintJobCancel`  | Job cancelled by user             |
| 7     | `PrintJobFailed`  | Job failed                        |

---

## 8. Agent lifecycle

```
bambu_network_create_agent(log_dir)
  → bambu_network_set_config_dir
  → bambu_network_set_cert_file        (optional, for verified TLS)
  → bambu_network_set_country_code     (optional, default = global)
  → bambu_network_set_on_*_fn         (register callbacks)
  → bambu_network_start

# For LAN printing:
  → bambu_network_start_discovery(agent, true, true)
  → bambu_network_connect_printer(agent, dev_id, ip, "bblp", access_code, true)
  → bambu_network_start_local_print(agent, params, update_fn, cancel_fn)

# For cloud printing:
  → bambu_network_change_user(agent, user_json)   // sets user_id + access_token
  → bambu_network_connect_server(agent)
  → bambu_network_start_subscribe(agent, "device")
  → bambu_network_start_print(agent, params, update_fn, cancel_fn, wait_fn)

# Teardown:
  → bambu_network_disconnect_printer(agent)
  → bambu_network_start_discovery(agent, false, false)
  → bambu_network_destroy_agent(agent)
```
