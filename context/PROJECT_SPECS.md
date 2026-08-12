# Secure Home Server Architecture: Remote Wake-on-LAN & WireGuard VPN

This document outlines the architecture for a highly secure, power-efficient home server setup. It utilizes an ESP microcontroller to trigger a Wake-on-LAN (WoL) event from a sleeping state (S3 RAM), followed by a secure data connection via a WireGuard VPN tunnel.

## 1. System Components

*   **Client / Frontend:** The remote application (e.g., a web dashboard or mobile app) that requires access to the server's data.
*   **Router (Gateway):** The home network's entry point, configured with minimal port forwarding to ensure maximum security.
*   **ESP Microcontroller (ESP32/ESP8266):** A low-power, always-on device acting as the WoL endpoint.
*   **Home Server (Ubuntu 24.04):** The main machine storing the data. It remains in an **S3 (Suspend to RAM)** state to save power when not in use.

---

## 2. Network & Port Forwarding Configuration

To keep the network secure against automated scanners and bots, only two specific ports are exposed to the internet.

### Router Rules
| External Port | Protocol | Internal IP | Internal Port | Destination | Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `38429` | TCP | `192.168.1.X` (ESP) | `38429` | ESP Microcontroller | HTTPS WoL API (TLS) |
| `51820` | UDP | `192.168.1.Y` (Server)| `51820` | Ubuntu Server | WireGuard VPN Tunnel |

> **Security Note:** The Home Server has **no open TCP ports**. It is completely invisible to standard web scanners. WireGuard operates on UDP and silently drops unauthenticated packets, meaning port `51820` will appear "closed" to attackers.

---

## 3. Workflow & Connection Sequence

The connection process is completely programmatic and requires zero human intervention once initiated by the frontend.

### Step 1: The Wake Request (Frontend -> ESP)
When the client needs data, the frontend sends an HTTPS `POST` request to the router's public IP on port `38429`. The router forwards this to the ESP's `/api/wol` endpoint.
*   The request **must** include an authentication header (e.g., `X-API-Key: <Secret_API_Key>`) to prevent unauthorized wakeups.
*   The ESP validates the header using constant-time comparison. If invalid, it returns `401 Unauthorized`.

### Step 2: Wake-on-LAN Execution
Upon successful validation, the ESP broadcasts a Magic Packet (WoL) across the local network targeting the Home Server's MAC address. 

### Step 3: Server Wake Up (S3 RAM)
Because the server is in an **S3 Suspend to RAM** state, it powers up and restores the operating system instantly.
*   The frontend is programmed to wait exactly **4 seconds** after the `POST` request before proceeding. This gives the server enough time to wake up, re-initialize its network interfaces, and start the WireGuard service.

### Step 4: Secure VPN Connection & Data Transfer
After the 4-second delay, the client application initiates the WireGuard handshake through UDP port `51820`. 
*   Once the cryptographic keys are validated, a secure peer-to-peer tunnel is established.
*   The client can now securely request data from the Home Server using its private local IP (e.g., `10.x.x.x`), operating as if it were physically on the home network.

---

## 4. Architecture Diagram (Mermaid)

```mermaid
sequenceDiagram
    participant C as Client / Frontend
    participant R as Home Router
    participant E as ESP (Local Web Server)
    participant S as Home Server (Ubuntu)

    C->>R: POST [Public_IP]:38429/api/wol
    Note over C,R: Header: X-API-Key: <Key>
    R->>E: Forward HTTPS to ESP (Port 38429)
    E->>E: Validate API Key
    E->>S: Send WoL Magic Packet
    E-->>C: 200 OK (Trigger Confirmed)
    
    Note over S: Server wakes from S3 (RAM).<br/>Network & WireGuard start.
    
    C->>C: Frontend waits 4 seconds
    
    C->>R: WireGuard Handshake (UDP 51820)
    R->>S: Forward UDP to Server
    S-->>C: VPN Tunnel Established
    
    Note over C,S: Encrypted Data Transfer via Private IP
    C->>S: Request Data (e.g., SMB, HTTP, DB)
    S-->>C: Return Data

---

## 5. Security Summary

1. **TLS Encryption:** All traffic encrypted in transit via HTTPS with self-signed certificate.
2. **Zero Trust for the Server:** The server itself is never exposed to raw HTTP traffic.
3. **API Key Protection:** The ESP requires strict header validation with constant-time comparison (`mbedtls_ct_memcmp`). Unauthorized requests receive `401`.
4. **Resource Caps:** Socket limits, LRU purge, and timeouts prevent exhaustion attacks.
5. **Heap Guard:** Requests rejected with `503` when free heap drops below 30 KB.
6. **Silent VPN:** WireGuard does not respond to unauthenticated scans. The home network remains invisible to unauthorized users.