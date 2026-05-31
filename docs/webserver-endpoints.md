# Webserver Endpoints

This document describes all HTTP and WebSocket endpoints available on the Biscuit Reader webserver.

- [Webserver Endpoints](#webserver-endpoints)
  - [Overview](#overview)
  - [Session Authorization](#session-authorization)
  - [HTTP Endpoints](#http-endpoints)
    - [GET `/` - Home Page](#get----home-page)
    - [GET `/files` - File Browser Page](#get-files---file-browser-page)
    - [GET `/api/device` - Device Identity](#get-apidevice---device-identity)
    - [GET `/api/status` - Device Status](#get-apistatus---device-status)
    - [GET `/api/events` - Event Snapshot](#get-apievents---event-snapshot)
    - [GET `/api/files` - List Files](#get-apifiles---list-files)
    - [POST `/upload` - Upload File](#post-upload---upload-file)
    - [POST `/mkdir` - Create Folder](#post-mkdir---create-folder)
    - [POST `/delete` - Delete File or Folder](#post-delete---delete-file-or-folder)
  - [WebSocket Endpoint](#websocket-endpoint)
    - [Port 81 - Fast Binary Upload](#port-81---fast-binary-upload)
  - [Network Modes](#network-modes)
    - [Station Mode (STA)](#station-mode-sta)
    - [Access Point Mode (AP)](#access-point-mode-ap)
  - [Notes](#notes)


## Overview

The Biscuit Reader exposes a webserver for file management and device monitoring:

- **HTTP Server**: Port 80
- **WebSocket Server**: Port 81 (for fast binary uploads)

---

## Session Authorization

When the File Transfer or Calibre/WebDAV activity starts, Biscuit generates a short-lived session
token and keeps it in RAM only. The token expires when the activity exits, the device enters deep
sleep, or the device reboots.

Only `GET /api/device` is public. All file, status, settings, WiFi, WebDAV, and WebSocket upload
operations require the current session token.

Supported carriers:

- Browser pairing URL: `http://<ip>/?token=<token>`, which stores a temporary `biscuit_token`
  cookie for same-origin browser requests.
- HTTP API and file requests: `Authorization: Bearer <token>`.
- WebDAV clients: Basic auth with user `biscuit` and password equal to the session token.
- WebSocket upload: send `AUTH:<token>` before `START:<filename>:<size>:<path>`.

In the examples below, set `TOKEN` to the token shown on the device:

```bash
TOKEN=<session-token>
curl -H "Authorization: Bearer $TOKEN" http://biscuit.local/api/status
```

---

## HTTP Endpoints

### GET `/` - Home Page

Serves the home page HTML interface.

**Request:**
```bash
curl "http://biscuit.local/?token=$TOKEN"
```

**Response:** HTML page (200 OK)

---

### GET `/files` - File Browser Page

Serves the file browser HTML interface.

**Request:**
```bash
curl -H "Authorization: Bearer $TOKEN" http://biscuit.local/files
```

**Response:** HTML page (200 OK)

---

### GET `/api/device` - Device Identity

Returns stable Biscuit Link v0 identity, hardware, capability, and endpoint metadata.

**Request:**
```bash
curl http://biscuit.local/api/device
```

**Response (200 OK):**
```json
{
  "v": 0,
  "device_id": "m5paper-example",
  "device_type": "m5paper",
  "name": "M5Paper",
  "firmware": "0.1.0-m5paper+master",
  "board": "M5Paper",
  "auth_required": true,
  "auth_user": "biscuit",
  "chip": {
    "model": "ESP32-D0WDQ6-V3",
    "revision": 3,
    "cpuMHz": 240,
    "flashBytes": 16777216
  },
  "display": {
    "width": 960,
    "height": 540,
    "bufferBytes": 64800,
    "color": "monochrome"
  },
  "capabilities": ["reader", "sd", "wifi", "web_transfer", "biscuit_link_v0"],
  "endpoints": {
    "status": "/api/status",
    "events": "/api/events",
    "files": "/api/files",
    "wifi": "/api/wifi",
    "settings": "/api/settings"
  },
  "transport": {
    "httpPort": 80,
    "wsPort": 81,
    "udpDiscoveryPort": 8134
  }
}
```

`device_id` is derived from the board type and WiFi MAC address, and is intended to be stable enough
for local discovery and controller pairing.

---

### GET `/api/status` - Device Status

Returns JSON with device status information.

**Request:**
```bash
curl -H "Authorization: Bearer $TOKEN" http://biscuit.local/api/status
```

**Response (200 OK):**
```json
{
  "v": 0,
  "device_id": "m5paper-example",
  "device_type": "m5paper",
  "version": "1.0.0",
  "ip": "192.168.1.100",
  "mode": "STA",
  "rssi": -45,
  "freeHeap": 123456,
  "uptime": 3600,
  "ssid": "HomeWiFi",
  "heap": {
    "free": 123456,
    "total": 281944,
    "minFree": 110000,
    "maxAlloc": 90000
  },
  "display": {
    "width": 960,
    "height": 540,
    "bufferBytes": 64800,
    "color": "monochrome"
  },
  "upload": {
    "inProgress": false,
    "received": 0,
    "total": 0,
    "filename": "",
    "lastCompleteName": "book.epub",
    "lastCompleteSize": 123456,
    "lastCompleteAt": 1234567
  }
}
```

The original flat fields (`version`, `ip`, `mode`, `rssi`, `freeHeap`, `uptime`) remain for
backward compatibility. New code should prefer the explicit `device_id`, `device_type`, `heap`,
`display`, and `upload` objects.

---

### GET `/api/events` - Event Snapshot

Returns a lightweight Biscuit Link v0 event snapshot. This is not a streaming endpoint yet; it is a
pollable envelope that can later map to WebSocket, ESP-NOW, or a local hub.

**Request:**
```bash
curl -H "Authorization: Bearer $TOKEN" http://biscuit.local/api/events
```

**Response (200 OK):**
```json
{
  "v": 0,
  "device_id": "m5paper-example",
  "snapshot": true,
  "events": [
    {
      "type": "status",
      "ts": 1234567,
      "body": {
        "uptime": 3600,
        "ip": "192.168.1.100",
        "mode": "STA",
        "freeHeap": 123456
      }
    }
  ]
}
```

---

### GET `/api/files` - List Files

Returns a JSON array of files and folders in the specified directory.

**Request:**
```bash
# List root directory
curl -H "Authorization: Bearer $TOKEN" http://biscuit.local/api/files

# List specific directory
curl -H "Authorization: Bearer $TOKEN" "http://biscuit.local/api/files?path=/Books"
```

**Query Parameters:**

| Parameter | Required | Default | Description            |
| --------- | -------- | ------- | ---------------------- |
| `path`    | No       | `/`     | Directory path to list |

**Response (200 OK):**
```json
[
  {"name": "MyBook.epub", "size": 1234567, "isDirectory": false, "isEpub": true},
  {"name": "Notes", "size": 0, "isDirectory": true, "isEpub": false},
  {"name": "document.pdf", "size": 54321, "isDirectory": false, "isEpub": false}
]
```

| Field         | Type    | Description                              |
| ------------- | ------- | ---------------------------------------- |
| `name`        | string  | File or folder name                      |
| `size`        | number  | Size in bytes (0 for directories)        |
| `isDirectory` | boolean | `true` if the item is a folder           |
| `isEpub`      | boolean | `true` if the file has `.epub` extension |

**Notes:**
- Hidden files (starting with `.`) are automatically filtered out
- System folders (`System Volume Information`, `XTCache`) are hidden

---

### POST `/upload` - Upload File

Uploads a file to the SD card via multipart form data.

**Request:**
```bash
# Upload to root directory
curl -H "Authorization: Bearer $TOKEN" -X POST -F "file=@mybook.epub" http://biscuit.local/upload

# Upload to specific directory
curl -H "Authorization: Bearer $TOKEN" -X POST -F "file=@mybook.epub" "http://biscuit.local/upload?path=/Books"
```

**Query Parameters:**

| Parameter | Required | Default | Description                     |
| --------- | -------- | ------- | ------------------------------- |
| `path`    | No       | `/`     | Target directory for the upload |

**Response (200 OK):**
```
File uploaded successfully: mybook.epub
```

**Error Responses:**

| Status | Body                                            | Cause                       |
| ------ | ----------------------------------------------- | --------------------------- |
| 400    | `Failed to create file on SD card`              | Cannot create file          |
| 400    | `Failed to write to SD card - disk may be full` | Write error during upload   |
| 400    | `Failed to write final data to SD card`         | Error flushing final buffer |
| 400    | `Upload aborted`                                | Client aborted the upload   |
| 400    | `Unknown error during upload`                   | Unspecified error           |
| 401    | `{"error":"unauthorized"}`                      | Missing or invalid token    |

**Notes:**
- Existing files with the same name will be overwritten
- Uses a 4KB buffer for efficient SD card writes

---

### POST `/mkdir` - Create Folder

Creates a new folder on the SD card.

**Request:**
```bash
curl -H "Authorization: Bearer $TOKEN" -X POST -d "name=NewFolder&path=/" http://biscuit.local/mkdir
```

**Form Parameters:**

| Parameter | Required | Default | Description                  |
| --------- | -------- | ------- | ---------------------------- |
| `name`    | Yes      | -       | Name of the folder to create |
| `path`    | No       | `/`     | Parent directory path        |

**Response (200 OK):**
```
Folder created: NewFolder
```

**Error Responses:**

| Status | Body                          | Cause                         |
| ------ | ----------------------------- | ----------------------------- |
| 400    | `Missing folder name`         | `name` parameter not provided |
| 400    | `Folder name cannot be empty` | Empty folder name             |
| 400    | `Folder already exists`       | Folder with same name exists  |
| 500    | `Failed to create folder`     | SD card error                 |

---

### POST `/delete` - Delete File or Folder

Deletes a file or folder from the SD card.

**Request:**
```bash
# Delete a file
curl -H "Authorization: Bearer $TOKEN" -X POST -d "path=/Books/mybook.epub&type=file" http://biscuit.local/delete

# Delete an empty folder
curl -H "Authorization: Bearer $TOKEN" -X POST -d "path=/OldFolder&type=folder" http://biscuit.local/delete
```

**Form Parameters:**

| Parameter | Required | Default | Description                      |
| --------- | -------- | ------- | -------------------------------- |
| `path`    | Yes      | -       | Path to the item to delete       |
| `type`    | No       | `file`  | Type of item: `file` or `folder` |

**Response (200 OK):**
```
Deleted successfully
```

**Error Responses:**

| Status | Body                                          | Cause                         |
| ------ | --------------------------------------------- | ----------------------------- |
| 400    | `Missing path`                                | `path` parameter not provided |
| 400    | `Cannot delete root directory`                | Attempted to delete `/`       |
| 400    | `Folder is not empty. Delete contents first.` | Non-empty folder              |
| 403    | `Cannot delete system files`                  | Hidden file (starts with `.`) |
| 403    | `Cannot delete protected items`               | Protected system folder       |
| 404    | `Item not found`                              | Path does not exist           |
| 500    | `Failed to delete item`                       | SD card error                 |

**Protected Items:**
- Files/folders starting with `.`
- `System Volume Information`
- `XTCache`

---

## WebSocket Endpoint

### Port 81 - Fast Binary Upload

A WebSocket endpoint for high-speed binary file uploads. More efficient than HTTP multipart for large files.

**Connection:**
```
ws://biscuit.local:81/
```

**Protocol:**

1. **Client** sends TEXT message: `AUTH:<token>`
2. **Server** responds with TEXT: `AUTH_OK`
3. **Client** sends TEXT message: `START:<filename>:<size>:<path>`
4. **Server** responds with TEXT: `READY`
5. **Client** sends BINARY messages with file data chunks
6. **Server** sends TEXT progress updates: `PROGRESS:<received>:<total>`
7. **Server** sends TEXT when complete: `DONE` or `ERROR:<message>`

**Example Session:**

```
Client -> "AUTH:<token>"
Server -> "AUTH_OK"
Client -> "START:mybook.epub:1234567:/Books"
Server -> "READY"
Client -> [binary chunk 1]
Client -> [binary chunk 2]
Server -> "PROGRESS:65536:1234567"
Client -> [binary chunk 3]
...
Server -> "PROGRESS:1234567:1234567"
Server -> "DONE"
```

**Error Messages:**

| Message                           | Cause                              |
| --------------------------------- | ---------------------------------- |
| `ERROR:Unauthorized`              | Missing or invalid session token   |
| `ERROR:Failed to create file`     | Cannot create file on SD card      |
| `ERROR:Invalid START format`      | Malformed START message            |
| `ERROR:No upload in progress`     | Binary data received without START |
| `ERROR:Write failed - disk full?` | SD card write error                |

**Example with `websocat`:**
```bash
# Interactive session
websocat ws://biscuit.local:81

# Then type:
AUTH:<token>
START:mybook.epub:1234567:/Books
# Wait for READY, then send binary data
```

**Notes:**
- Progress updates are sent every 64KB or at completion
- Disconnection during upload will delete the incomplete file
- Existing files with the same name will be overwritten

---

## Network Modes

The device can operate in two network modes:

### Station Mode (STA)
- Device connects to an existing WiFi network
- IP address assigned by router/DHCP
- `mode` field in `/api/status` returns `"STA"`
- `rssi` field shows signal strength

### Access Point Mode (AP)
- Device creates its own WiFi hotspot
- Hotspot is WPA2-protected with a generated password shown on the device and encoded in the QR code
- Default IP is typically `192.168.4.1`
- `mode` field in `/api/status` returns `"AP"`
- `rssi` field returns `0`

---

## Notes

- These examples use `biscuit.local`. If your network does not support mDNS or the address does not resolve, replace it with the specific **IP Address** displayed on your device screen (e.g., `http://192.168.1.102/`).
- All paths on the SD card start with `/`
- Trailing slashes are automatically stripped (except for root `/`)
- The webserver uses chunked transfer encoding for file listings
