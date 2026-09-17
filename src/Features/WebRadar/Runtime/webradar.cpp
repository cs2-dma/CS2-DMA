#include <winsock2.h>
#include <ws2tcpip.h>
#include "Features/WebRadar/webradar.h"
#include "Features/WebRadar/embedded_assets.h"
#include "Features/WebRadar/web_remote.h"
#include "Features/WebRadar/runtime_utils.h"
#include "Features/Radar/map_registry.h"
#include "Features/ESP/weapon_catalog.h"
#include "app/Core/globals.h"
#include "app/Core/fallback_log.h"
#include "app/Core/text_utils.h"
#include "app/Config/project_paths.h"
#include "app/Localization/localization.h"
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <json/json.hpp>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Ws2_32.lib")

namespace
{
    using webradar::runtime::Base64Encode;
    using webradar::runtime::SendAll;
    constexpr size_t kMaxHttpRequestSize = size_t{8} * 1024;

    using MapDefinition = radar::MapDefinition;

    struct HttpRequest
    {
        std::string method;
        std::string path;
        std::unordered_map<std::string, std::string> query;
        std::unordered_map<std::string, std::string> headers;

        HttpRequest() = default;
        HttpRequest(const HttpRequest&) = delete;
        HttpRequest& operator=(const HttpRequest&) = delete;
        HttpRequest(HttpRequest&&) noexcept(false) = default;
        HttpRequest& operator=(HttpRequest&&) noexcept(false) = default;
    };

    bool IsValidWorldVec(const Vector3& v)
    {
        return IsFiniteVec(v) && (std::abs(v.x) > 1.0f || std::abs(v.y) > 1.0f);
    }

    struct LegacyBombJsonState
    {
        uint64_t sequence = 0;
        uint64_t timestampMs = 0;
        Vector3 position = {};
        bool planted = false;
        bool dropped = false;
        bool ticking = false;
        bool defusing = false;
        bool defused = false;
        float blowTime = 0.0f;
        float timerLength = 40.0f;
        float defuseTime = 0.0f;
        float defuseLength = 10.0f;
    };

    nlohmann::json BuildLegacyBombJson(const LegacyBombJsonState& state)
    {
        return {
            {"m_is_planted", state.planted},
            {"m_is_dropped", state.dropped},
            {"m_is_ticking", state.ticking},
            {"m_is_defusing", state.defusing},
            {"m_is_defused", state.defused},
            {"m_blow_time", state.blowTime},
            {"m_timer_length", state.timerLength},
            {"m_defuse_time", state.defuseTime},
            {"m_defuse_length", state.defuseLength},
            {"m_seq", state.sequence},
            {"m_ts", state.timestampMs},
            {"m_position", IsValidWorldVec(state.position) ? nlohmann::json{
                {"x", state.position.x},
                {"y", state.position.y},
                {"z", state.position.z}
            } : nlohmann::json(nullptr)}
        };
    }

    void AppendJsonString(std::string& out, std::string_view value)
    {
        out.push_back('"');
        for (const unsigned char c : value) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(c >> 4) & 0x0F]);
                    out.push_back(kHex[c & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
            }
        }
        out.push_back('"');
    }

    template <typename T>
    void AppendJsonInt(std::string& out, T value)
    {
        char buffer[32] = {};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec == std::errc{})
            out.append(buffer, result.ptr);
    }

    void AppendJsonFloat(std::string& out, float value)
    {
        if (!std::isfinite(value))
            value = 0.0f;

        char buffer[48] = {};
        auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 2);
        if (result.ec != std::errc{}) {
            out += "0";
            return;
        }

        size_t len = static_cast<size_t>(result.ptr - buffer);
        while (len > 1 && buffer[len - 1] == '0')
            --len;
        if (len > 1 && buffer[len - 1] == '.')
            --len;
        out.append(buffer, len);
    }

    void AppendJsonVec3(std::string& out, const Vector3& v)
    {
        out.push_back('[');
        AppendJsonFloat(out, v.x);
        out.push_back(',');
        AppendJsonFloat(out, v.y);
        out.push_back(',');
        AppendJsonFloat(out, v.z);
        out.push_back(']');
    }

    void AppendJsonVec2(std::string& out, float x, float y)
    {
        out.push_back('[');
        AppendJsonFloat(out, x);
        out.push_back(',');
        AppendJsonFloat(out, y);
        out.push_back(']');
    }

    const char* TeamDefaultModelName(int team)
    {
        if (team == 3)
            return "ctm_sas";
        if (team == 2)
            return "tm_phoenix";
        return "tm_phoenix";
    }

    const char* WeaponVisualKeyOrEmpty(uint16_t id) noexcept
    {
        const char* visualKey = esp::weapons::WeaponVisualKeyFromItemId(id);
        return visualKey ? visualKey : "";
    }

    std::string SafePlayerName(const char* value, size_t maxLen)
    {
        if (!value || maxLen == 0)
            return {};

        size_t n = 0;
        while (n < maxLen && value[n] != '\0')
            ++n;

        std::string out(value, n);
        for (char& c : out) {
            if (static_cast<unsigned char>(c) < 0x20)
                c = ' ';
        }
        return out;
    }

    uint16_t NormalizePort(int port)
    {
        if (port < 1025 || port > 65535)
            return webradar::cfg::kDefaultListenPort;
        return static_cast<uint16_t>(port);
    }

    int RequestedProtocolVersion(const std::unordered_map<std::string, std::string>& query)
    {
        const auto it = query.find("pv");
        if (it == query.end())
            return webradar::cfg::kDefaultProtocolVersion;
        const std::string& v = it->second;
        if (v.empty())
            return webradar::cfg::kDefaultProtocolVersion;
        int parsed = 0;
        for (char c : v) {
            if (c < '0' || c > '9')
                return webradar::cfg::kDefaultProtocolVersion;
            parsed = parsed * 10 + (c - '0');
            if (parsed > 99)
                return webradar::cfg::kDefaultProtocolVersion;
        }
        if (parsed < webradar::cfg::kMinProtocolVersion ||
            parsed > webradar::cfg::kMaxProtocolVersion)
            return webradar::cfg::kDefaultProtocolVersion;
        return parsed;
    }

    std::string UrlDecode(std::string_view encoded)
    {
        std::string out;
        out.reserve(encoded.size());

        for (size_t i = 0; i < encoded.size(); ++i) {
            const char c = encoded[i];
            if (c == '+') {
                out.push_back(' ');
                continue;
            }

            if (c == '%' && (i + 2) < encoded.size()) {
                auto hexToInt = [](char x) -> int {
                    if (x >= '0' && x <= '9')
                        return x - '0';
                    if (x >= 'a' && x <= 'f')
                        return 10 + (x - 'a');
                    if (x >= 'A' && x <= 'F')
                        return 10 + (x - 'A');
                    return -1;
                };

                const int hiValue = hexToInt(encoded[i + 1]);
                const int loValue = hexToInt(encoded[i + 2]);
                if (hiValue >= 0 && loValue >= 0) {
                    out.push_back(static_cast<char>((hiValue << 4) | loValue));
                    i += 2;
                    continue;
                }
            }

            out.push_back(c);
        }

        return out;
    }

    std::unordered_map<std::string, std::string> ParseQuery(std::string_view queryText)
    {
        std::unordered_map<std::string, std::string> result;
        size_t start = 0;
        while (start <= queryText.size()) {
            const size_t end = queryText.find('&', start);
            const std::string_view part = queryText.substr(start, end == std::string::npos ? std::string::npos : (end - start));
            if (!part.empty()) {
                const size_t sep = part.find('=');
                const std::string key = UrlDecode(part.substr(0, sep));
                const std::string value = sep == std::string::npos ? std::string() : UrlDecode(part.substr(sep + 1));
                result[key] = value;
            }

            if (end == std::string::npos)
                break;
            start = end + 1;
        }

        return result;
    }

    bool ReadHttpRequest(SOCKET client, std::string* outRequest)
    {
        if (!outRequest)
            return false;

        
        DWORD recvTimeout = 5000; 
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeout), sizeof(recvTimeout));

        std::string request;
        request.reserve(2048);
        std::array<char, 2048> buffer = {};

        while (request.find("\r\n\r\n") == std::string::npos && request.size() < kMaxHttpRequestSize) {
            const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received <= 0)
                return false;
            request.append(buffer.data(), static_cast<size_t>(received));
        }

        if (request.find("\r\n\r\n") == std::string::npos)
            return false;

        *outRequest = std::move(request);
        return true;
    }

    HttpRequest ParseHttpRequest(const std::string& rawRequest)
    {
        HttpRequest request;
        const size_t firstLineEnd = rawRequest.find("\r\n");
        const std::string_view requestView(rawRequest);
        const std::string_view firstLine = requestView.substr(0, firstLineEnd);
        const size_t methodSep = firstLine.find(' ');
        if (methodSep == std::string::npos)
            return request;

        const size_t pathSep = firstLine.find(' ', methodSep + 1);
        if (pathSep == std::string::npos)
            return request;

        request.method.assign(firstLine.substr(0, methodSep));
        const std::string_view target = firstLine.substr(methodSep + 1, pathSep - methodSep - 1);
        const size_t querySep = target.find('?');
        request.path.assign(querySep == std::string::npos ? target : target.substr(0, querySep));
        if (request.path.empty())
            request.path = "/";
        if (querySep != std::string::npos)
            request.query = ParseQuery(target.substr(querySep + 1));

        size_t lineStart = (firstLineEnd == std::string::npos) ? std::string::npos : firstLineEnd + 2;
        while (lineStart != std::string::npos && lineStart < rawRequest.size()) {
            const size_t lineEnd = rawRequest.find("\r\n", lineStart);
            if (lineEnd == std::string::npos || lineEnd == lineStart)
                break;

            const std::string_view line = requestView.substr(lineStart, lineEnd - lineStart);
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                const std::string key = app::text::ToLowerAscii(app::text::Trim(line.substr(0, colon)));
                const std::string value = app::text::Trim(line.substr(colon + 1));
                if (!key.empty())
                    request.headers[key] = value;
            }

            lineStart = lineEnd + 2;
        }

        return request;
    }
}

namespace webradar
{

namespace
{
    constexpr int kWebRadarRealtimeIntervalMs = cfg::kMinRealtimeIntervalMs;

    double Ema(double current, double sample, double alpha)
    {
        if (!std::isfinite(current) || current <= 0.0)
            return sample;
        return current + (sample - current) * alpha;
    }

    std::string HttpStatusText(int statusCode)
    {
        switch (statusCode) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "OK";
        }
    }

    bool SendHttpResponseEx(
        SOCKET socketHandle,
        int statusCode,
        const char* contentType,
        const std::string& body,
        const char* cacheControl,
        bool keepAlive,
        const std::string& accessControlOrigin = std::string("*"))
    {
        const std::string cacheControlValue = cacheControl ? cacheControl : "no-store";
        const std::string noCacheCompatHeaders =
            cacheControlValue.find("no-store") != std::string::npos
                ? "Pragma: no-cache\r\nExpires: 0\r\n"
                : "";
        const std::string corsHeader = accessControlOrigin.empty()
            ? std::string()
            : std::format(
                "Access-Control-Allow-Origin: {}\r\n"
                "Vary: Origin\r\n",
                accessControlOrigin);
        const std::string header = std::format(
            "HTTP/1.1 {} {}\r\n"
            "Connection: {}\r\n"
            "{}"
            "X-Content-Type-Options: nosniff\r\n"
            "Cache-Control: {}\r\n"
            "{}"
            "Content-Type: {}\r\n"
            "Content-Length: {}\r\n\r\n",
            statusCode,
            HttpStatusText(statusCode),
            keepAlive ? "keep-alive" : "close",
            corsHeader,
            cacheControlValue,
            noCacheCompatHeaders,
            contentType ? contentType : "application/octet-stream",
            body.size());

        return SendAll(socketHandle, header.data(), header.size()) &&
            SendAll(socketHandle, body.data(), body.size());
    }

    bool SendHttpResponse(SOCKET socketHandle, int statusCode, const char* contentType, const std::string& body)
    {
        return SendHttpResponseEx(socketHandle, statusCode, contentType, body, "no-store", false);
    }

    bool SendSseHeaders(SOCKET socketHandle, const std::string& accessControlOrigin = std::string("*"))
    {
        const std::string corsHeader = accessControlOrigin.empty()
            ? std::string()
            : std::format(
                "Access-Control-Allow-Origin: {}\r\n"
                "Vary: Origin\r\n",
                accessControlOrigin);
        const std::string header = std::format(
            "HTTP/1.1 200 OK\r\n"
            "Connection: keep-alive\r\n"
            "Cache-Control: no-store\r\n"
            "{}"
            "X-Content-Type-Options: nosniff\r\n"
            "Content-Type: text/event-stream\r\n"
            "X-Accel-Buffering: no\r\n\r\n",
            corsHeader);
        return SendAll(socketHandle, header.data(), header.size());
    }

    bool SendSseEvent(SOCKET socketHandle, const char* eventName, const std::string& body)
    {
        std::string payload;
        payload.reserve(body.size() + 48);
        if (eventName && *eventName != '\0')
            payload += std::string("event: ") + eventName + "\n";
        payload += "data: ";
        payload += body;
        payload += "\n\n";
        return SendAll(socketHandle, payload.data(), payload.size());
    }

    std::string ResolveAllowedOrigin(
        const std::vector<std::string>& allowlist,
        const std::unordered_map<std::string, std::string>& headers)
    {
        if (allowlist.empty())
            return std::string("*");

        const auto it = headers.find("origin");
        if (it == headers.end())
            return std::string();

        const std::string& origin = it->second;
        if (origin.empty())
            return std::string();

        for (const auto& allowed : allowlist) {
            if (allowed.empty())
                continue;
            if (allowed == "*")
                return origin;
            if (_stricmp(allowed.c_str(), origin.c_str()) == 0)
                return origin;
        }
        return std::string();
    }

    bool HeaderContainsValue(
        const std::unordered_map<std::string, std::string>& headers,
        const char* headerName,
        const char* value)
    {
        if (!headerName || !value)
            return false;

        const auto it = headers.find(headerName);
        if (it == headers.end())
            return false;

        const std::string haystack = app::text::ToLowerAscii(it->second);
        const std::string needle = app::text::ToLowerAscii(value);
        size_t start = 0;
        while (start < haystack.size()) {
            size_t end = haystack.find(',', start);
            std::string part = app::text::Trim(
                haystack.substr(start, end == std::string::npos ? std::string::npos : (end - start)));
            if (part == needle)
                return true;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }

        return false;
    }

    std::string BuildWebSocketAcceptKey(const std::string& clientKey)
    {
        if (clientKey.empty())
            return {};

        static constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        const std::string input = clientKey + kWebSocketGuid;

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD objectSize = 0;
        DWORD resultSize = 0;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
        if (status < 0)
            return {};

        status = BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize),
            sizeof(objectSize),
            &resultSize,
            0);
        if (status < 0 || objectSize == 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return {};
        }

        std::vector<unsigned char> objectBuffer(objectSize);
        std::array<unsigned char, 20> digest = {};

        status = BCryptCreateHash(
            algorithm,
            &hash,
            objectBuffer.data(),
            static_cast<ULONG>(objectBuffer.size()),
            nullptr,
            0,
            0);
        if (status >= 0) {
            status = BCryptHashData(
                hash,
                reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                static_cast<ULONG>(input.size()),
                0);
        }
        if (status >= 0) {
            status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        }

        if (hash)
            BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);

        if (status < 0)
            return {};

        return Base64Encode(digest.data(), digest.size());
    }

    bool SendWebSocketHandshakeResponse(
        SOCKET socketHandle,
        const std::string& acceptKey,
        const std::string& accessControlOrigin = std::string("*"))
    {
        if (acceptKey.empty())
            return false;

        const std::string corsHeader = accessControlOrigin.empty()
            ? std::string()
            : std::format(
                "Access-Control-Allow-Origin: {}\r\n",
                accessControlOrigin);
        const std::string response = std::format(
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "{}"
            "Sec-WebSocket-Accept: {}\r\n\r\n",
            corsHeader,
            acceptKey);
        return SendAll(socketHandle, response.data(), response.size());
    }

    bool SendWebSocketFrame(SOCKET socketHandle, unsigned char opcode, const void* payload, size_t payloadSize)
    {
        std::array<unsigned char, 10> header = {};
        size_t headerSize = 0;
        header[headerSize++] = static_cast<unsigned char>(0x80 | (opcode & 0x0F));

        if (payloadSize < 126) {
            header[headerSize++] = static_cast<unsigned char>(payloadSize);
        } else if (payloadSize <= 0xFFFFu) {
            header[headerSize++] = 126;
            header[headerSize++] = static_cast<unsigned char>((payloadSize >> 8) & 0xFF);
            header[headerSize++] = static_cast<unsigned char>(payloadSize & 0xFF);
        } else {
            header[headerSize++] = 127;
            for (int shift = 56; shift >= 0; shift -= 8) {
                header[headerSize++] = static_cast<unsigned char>((payloadSize >> shift) & 0xFF);
            }
        }

        if (!SendAll(socketHandle, reinterpret_cast<const char*>(header.data()), headerSize))
            return false;
        if (payloadSize == 0)
            return true;
        return SendAll(socketHandle, static_cast<const char*>(payload), payloadSize);
    }

    bool SendWebSocketTextFrame(SOCKET socketHandle, const std::string& payload)
    {
        return SendWebSocketFrame(socketHandle, 0x1, payload.data(), payload.size());
    }

    bool SendWebSocketPing(SOCKET socketHandle)
    {
        return SendWebSocketFrame(socketHandle, 0x9, nullptr, 0);
    }

    bool SendWebSocketPong(SOCKET socketHandle, const void* payload, size_t payloadSize)
    {
        return SendWebSocketFrame(socketHandle, 0xA, payload, payloadSize);
    }

    bool SendWebSocketCloseFrame(SOCKET socketHandle, uint16_t code = 1000)
    {
        unsigned char payload[2];
        payload[0] = static_cast<unsigned char>((code >> 8) & 0xFF);
        payload[1] = static_cast<unsigned char>(code & 0xFF);
        return SendWebSocketFrame(socketHandle, 0x8, payload, sizeof(payload));
    }

    int DrainWebSocketIncoming(SOCKET socketHandle, bool* closeRequested)
    {
        if (closeRequested)
            *closeRequested = false;

        thread_local std::vector<unsigned char> s_drainBuf;
        thread_local size_t s_drainHead = 0;

        if (s_drainHead > 0 && s_drainHead >= s_drainBuf.size()) {
            s_drainBuf.clear();
            s_drainHead = 0;
        }
        
        if (s_drainBuf.capacity() > 65536 && s_drainBuf.size() < 4096)
            s_drainBuf.shrink_to_fit();

        int totalConsumed = 0;
        int processedFrames = 0;
        for (int iter = 0; iter < 8; ++iter) {
            char buf[1024];
            const int r = recv(socketHandle, buf, sizeof(buf), 0);
            if (r > 0) {
                totalConsumed += r;
                s_drainBuf.insert(s_drainBuf.end(), buf, buf + r);
                if (r < static_cast<int>(sizeof(buf)))
                    break;
                continue;
            }
            if (r == 0) {
                if (closeRequested)
                    *closeRequested = true;
                break;
            }
            const int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAETIMEDOUT)
                break;
            if (closeRequested)
                *closeRequested = true;
            break;
        }

        while (s_drainHead < s_drainBuf.size()) {
            const size_t avail = s_drainBuf.size() - s_drainHead;
            if (avail < 2)
                break;
            const unsigned char b0 = s_drainBuf[s_drainHead];
            const unsigned char b1 = s_drainBuf[s_drainHead + 1];
            const bool fin = (b0 & 0x80) != 0;
            const unsigned char rsv = b0 & 0x70;
            const unsigned char opcode = b0 & 0x0F;
            const bool masked = (b1 & 0x80) != 0;
            const unsigned char len7 = b1 & 0x7F;

            size_t headerLen = 2;
            uint64_t payloadLen = len7;

            if (len7 == 126) {
                if (avail < 4) break;
                payloadLen = (static_cast<uint64_t>(s_drainBuf[s_drainHead + 2]) << 8) |
                              static_cast<uint64_t>(s_drainBuf[s_drainHead + 3]);
                headerLen = 4;
            } else if (len7 == 127) {
                if (avail < 10) break;
                payloadLen = 0;
                for (int k = 0; k < 8; ++k) {
                    payloadLen = (payloadLen << 8) |
                                  static_cast<uint64_t>(s_drainBuf[s_drainHead + 2 + k]);
                }
                headerLen = 10;
            }

            if (masked) headerLen += 4;

            const uint64_t totalFrameLen64 = headerLen + payloadLen;
            const bool isControlFrame = opcode >= 0x8;
            const bool invalidFrame =
                rsv != 0 ||
                opcode > 0xA ||
                (opcode >= 0x3 && opcode <= 0x7) ||
                (isControlFrame && (!fin || payloadLen > 125));

            if (invalidFrame || totalFrameLen64 > (1ull << 24)) {
                if (closeRequested)
                    *closeRequested = true;
                s_drainBuf.clear();
                s_drainHead = 0;
                break;
            }
            const size_t totalFrameLen = static_cast<size_t>(totalFrameLen64);
            if (avail < totalFrameLen) break;

            if (opcode == 0x8) {
                if (closeRequested)
                    *closeRequested = true;
                s_drainBuf.clear();
                s_drainHead = 0;
                ++processedFrames;
                break;
            }

            if (opcode == 0x9) {
                std::array<unsigned char, 125> pongPayload = {};
                const size_t maskOffset = masked ? (headerLen - 4) : 0;
                const size_t payloadOffset = headerLen;
                for (size_t i = 0; i < static_cast<size_t>(payloadLen); ++i) {
                    unsigned char value = s_drainBuf[s_drainHead + payloadOffset + i];
                    if (masked)
                        value ^= s_drainBuf[s_drainHead + maskOffset + (i & 3)];
                    pongPayload[i] = value;
                }
                if (!SendWebSocketPong(socketHandle, pongPayload.data(), static_cast<size_t>(payloadLen))) {
                    if (closeRequested)
                        *closeRequested = true;
                    s_drainBuf.clear();
                    s_drainHead = 0;
                    ++processedFrames;
                    break;
                }
            }

            s_drainHead += totalFrameLen;
            ++processedFrames;
        }

        if (s_drainHead > 0 &&
            s_drainHead < s_drainBuf.size() &&
            s_drainHead >= s_drainBuf.size() / 2) {
            using DrainDifference = std::vector<unsigned char>::difference_type;
            s_drainBuf.erase(
                s_drainBuf.begin(),
                s_drainBuf.begin() + static_cast<DrainDifference>(s_drainHead));
            s_drainHead = 0;
        } else if (s_drainHead >= s_drainBuf.size()) {
            s_drainBuf.clear();
            s_drainHead = 0;
        }

        return totalConsumed > 0 ? totalConsumed : processedFrames;
    }

    std::string GuessContentTypeByExtension(const std::string& extension)
    {
        if (extension == ".html" || extension == ".htm")
            return "text/html; charset=utf-8";
        if (extension == ".json")
            return "application/json; charset=utf-8";
        if (extension == ".js")
            return "application/javascript; charset=utf-8";
        if (extension == ".css")
            return "text/css; charset=utf-8";
        if (extension == ".png")
            return "image/png";
        if (extension == ".webp")
            return "image/webp";
        if (extension == ".jpg" || extension == ".jpeg")
            return "image/jpeg";
        if (extension == ".svg")
            return "image/svg+xml";
        if (extension == ".ico")
            return "image/x-icon";
        if (extension == ".woff2")
            return "font/woff2";
        return "application/octet-stream";
    }

    std::string GuessContentType(const std::filesystem::path& path)
    {
        return GuessContentTypeByExtension(app::text::ToLowerAscii(path.extension().string()));
    }

    std::string GuessContentTypeFromUrl(const std::string& urlPath)
    {
        const auto dot = urlPath.rfind('.');
        if (dot == std::string::npos)
            return "application/octet-stream";
        return GuessContentTypeByExtension(app::text::ToLowerAscii(urlPath.substr(dot)));
    }

    const char* CacheControlForRequestPath(const std::string& requestPath, const std::filesystem::path& assetPath)
    {
        const std::string extension = app::text::ToLowerAscii(assetPath.extension().string());
        if (requestPath == "/" ||
            extension == ".html" ||
            extension == ".htm" ||
            extension == ".js" ||
            extension == ".css")
            return "no-store";

        if (requestPath.rfind("/assets/", 0) == 0 ||
            requestPath.rfind("/data/", 0) == 0 ||
            extension == ".png" ||
            extension == ".jpg" ||
            extension == ".jpeg" ||
            extension == ".svg" ||
            extension == ".woff2") {
            return "public, max-age=86400, stale-while-revalidate=43200";
        }

        return "public, max-age=300, stale-while-revalidate=60";
    }

    bool ReadEntireFile(const std::filesystem::path& path, std::string* outContents)
    {
        if (!outContents)
            return false;

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        file.seekg(0, std::ios::end);
        const std::streamoff size = file.tellg();
        if (size < 0)
            return false;
        file.seekg(0, std::ios::beg);

        std::string contents;
        contents.resize(static_cast<size_t>(size));
        if (size > 0)
            file.read(contents.data(), static_cast<std::streamsize>(size));
        if (!file.good() && !file.eof())
            return false;

        *outContents = std::move(contents);
        return true;
    }

    std::string BuildFallbackMapsJson()
    {
        return radar::BuildMapsJson();
    }

    std::filesystem::path ResolveStaticAssetPath(const std::string& requestPath)
    {
        const auto assetRoot = app::paths::ResolveWebRadarAssetDirectory();
        if (assetRoot.empty())
            return {};

        std::string relative = requestPath;
        if (relative.empty() || relative == "/")
            relative = "/index.html";

        if (!relative.empty() && relative.front() == '/')
            relative.erase(relative.begin());

        relative = UrlDecode(relative);
        std::replace(relative.begin(), relative.end(), '\\', '/');
        if (relative.find("..") != std::string::npos || relative.find(':') != std::string::npos)
            return {};

        auto candidate = (assetRoot / relative).lexically_normal();
        const auto rootNormalized = assetRoot.lexically_normal();

        const std::string rootString = app::text::ToLowerAscii(rootNormalized.generic_string());
        const std::string candidateString = app::text::ToLowerAscii(candidate.generic_string());
        if (candidateString.size() < rootString.size())
            return {};
        if (candidateString.compare(0, rootString.size(), rootString) != 0)
            return {};
        if (candidateString.size() > rootString.size()) {
            const char next = candidateString[rootString.size()];
            if (next != '/' && next != '\\')
                return {};
        }

        return candidate;
    }
}

WEBRadar::WEBRadar()
{
    settings_.listenPort = cfg::kDefaultListenPort;
    requestedPort_.store(cfg::kDefaultListenPort, std::memory_order_relaxed);
    stats_.listenPort = cfg::kDefaultListenPort;
    stats_.statusText = "WEBRadar ready";
    latestPayloadJson_ = BuildFallbackLiveJson("unknown", UnixNowMs());
    latestPayloadJsonV2_ = BuildFallbackLiveJsonV2("unknown", UnixNowMs());
}

WEBRadar::~WEBRadar()
{
    try {
        Stop();
    } catch (...) {
        app::diagnostics::WriteFallbackError(
            "WEBRadar shutdown failed during destruction");
    }
}

void WEBRadar::Start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
        return;

    auto runGuarded = [this](const char* loopName, auto loop, auto cleanup) noexcept {
        while (running_.load(std::memory_order_relaxed)) {
            try {
                (this->*loop)();
            } catch (const std::exception& ex) {
                try {
                    std::lock_guard<std::mutex> lock(mutex_);
                    stats_.serverListening = false;
                    stats_.statusText =
                        std::string("WEBRadar ") + loopName + " loop recovered: " + ex.what();
                    ++stats_.failedPackets;
                } catch (...) {
                    app::diagnostics::WriteFallbackError(
                        "WEBRadar could not record a loop exception",
                        ex.what());
                }
                try {
                    cleanup();
                } catch (...) {
                    app::diagnostics::WriteFallbackError(
                        "WEBRadar loop cleanup failed");
                }
            } catch (...) {
                try {
                    std::lock_guard<std::mutex> lock(mutex_);
                    stats_.serverListening = false;
                    stats_.statusText =
                        std::string("WEBRadar ") + loopName + " loop recovered";
                    ++stats_.failedPackets;
                } catch (...) {
                    app::diagnostics::WriteFallbackError(
                        "WEBRadar could not record an unknown loop exception");
                }
                try {
                    cleanup();
                } catch (...) {
                    app::diagnostics::WriteFallbackError(
                        "WEBRadar loop cleanup failed");
                }
            }

            if (!running_.load(std::memory_order_relaxed))
                break;
            Sleep(500);
        }
    };

    try {
        worker_ = std::jthread([runGuarded]() noexcept {
            runGuarded("worker", &WEBRadar::WorkerLoop, [] {});
        });
        serverThread_ = std::jthread([this, runGuarded]() noexcept {
            runGuarded("server", &WEBRadar::ServerLoop, [this] {
                const uintptr_t handle = listenSocket_.exchange(0);
                if (handle != 0) {
                    const SOCKET socketHandle = static_cast<SOCKET>(handle);
                    shutdown(socketHandle, SD_BOTH);
                    closesocket(socketHandle);
                }
                WSACleanup();
            });
        });
    } catch (...) {
        running_.store(false, std::memory_order_relaxed);
        cv_.notify_all();
        const uintptr_t handle = listenSocket_.exchange(0);
        if (handle != 0) {
            const SOCKET socketHandle = static_cast<SOCKET>(handle);
            shutdown(socketHandle, SD_BOTH);
            closesocket(socketHandle);
        }
        if (worker_.joinable())
            worker_.join();
        if (serverThread_.joinable())
            serverThread_.join();
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_.serverListening = false;
            stats_.statusText = "WEBRadar worker startup failed";
            ++stats_.failedPackets;
        } catch (...) {
            app::diagnostics::WriteFallbackError(
                "WEBRadar could not record a worker startup failure");
        }
    }
}

void WEBRadar::Stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false))
        return;

    cv_.notify_all();

    const uintptr_t handle = listenSocket_.exchange(0);
    if (handle != 0) {
        const SOCKET socketHandle = static_cast<SOCKET>(handle);
        shutdown(socketHandle, SD_BOTH);
        closesocket(socketHandle);
    }

    CloseStreamClients();
    CloseWebSocketClients();

    if (worker_.joinable())
        worker_.join();
    if (serverThread_.joinable())
        serverThread_.join();
}

void WEBRadar::Configure(bool enabled, int intervalMs, uint16_t listenPort, const std::string& mapOverride,
                         bool bindLan, std::vector<std::string> originAllowlist)
{
    bool portChanged = false;
    bool disableLocal = false;
    bool bindChanged = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool prevEnabled = settings_.enabled;
        const int prevInterval = settings_.intervalMs;
        const uint16_t prevPort = settings_.listenPort;
        const std::string prevMapOverride = settings_.mapOverride;
        const bool prevBindLan = settings_.bindLan;
        const auto prevAllowlist = settings_.originAllowlist;

        settings_.enabled = enabled;
        settings_.intervalMs = std::clamp(intervalMs, cfg::kMinRealtimeIntervalMs, cfg::kMaxRealtimeIntervalMs);
        settings_.listenPort = NormalizePort(listenPort);
        settings_.mapOverride = NormalizeMapName(mapOverride);
        settings_.bindLan = bindLan;
        settings_.originAllowlist = std::move(originAllowlist);
        stats_.enabled = settings_.enabled;
        stats_.listenPort = settings_.listenPort;
        requestedPort_.store(settings_.listenPort, std::memory_order_relaxed);
        portChanged = (prevPort != settings_.listenPort);
        bindChanged = (prevBindLan != settings_.bindLan);
        disableLocal = prevEnabled && !settings_.enabled;

        const bool changed =
            prevEnabled != settings_.enabled ||
            prevInterval != settings_.intervalMs ||
            portChanged ||
            bindChanged ||
            prevMapOverride != settings_.mapOverride ||
            prevAllowlist != settings_.originAllowlist;

        if (changed) {
            ++settingsVersion_;
            cv_.notify_one();
        }
    }

    if (!portChanged && !bindChanged && !disableLocal)
        return;

    const uintptr_t handle = listenSocket_.exchange(0);
    if (handle != 0) {
        const SOCKET socketHandle = static_cast<SOCKET>(handle);
        shutdown(socketHandle, SD_BOTH);
        closesocket(socketHandle);
    }

    CloseStreamClients();
    CloseWebSocketClients();
}

void WEBRadar::UpdateSnapshot(const esp::WebRadarSnapshot& snapshot)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latestSnapshot_ = snapshot;
        hasSnapshot_ = true;
        ++snapshotVersion_;
    }
    cv_.notify_one();
}

RuntimeStats WEBRadar::GetStats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

bool WEBRadar::HasActiveConsumers() const
{
    if (!IsLocalEnabled())
        return false;

    {
        std::lock_guard<std::mutex> streamLock(streamMutex_);
        if (!streamClients_.empty())
            return true;
    }
    {
        std::lock_guard<std::mutex> wsLock(wsMutex_);
        for (const auto& client : wsClients_) {
            if (client && client->active.load(std::memory_order_relaxed))
                return true;
        }
    }
    return false;
}

bool WEBRadar::IsLocalEnabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.enabled;
}

void WEBRadar::CloseStreamClients()
{
    std::vector<StreamClient> clients;
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        streamGeneration_.fetch_add(1, std::memory_order_relaxed);
        clients.swap(streamClients_);
    }

    for (const StreamClient& client : clients) {
        const uintptr_t handleValue = client.socketHandle;
        if (handleValue == 0)
            continue;
        const SOCKET socketHandle = static_cast<SOCKET>(handleValue);
        shutdown(socketHandle, SD_BOTH);
        closesocket(socketHandle);
    }
}

void WEBRadar::CloseWebSocketClients()
{
    std::vector<std::unique_ptr<WebSocketClient>> clients;
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        clients.swap(wsClients_);
    }

    for (auto& client : clients) {
        if (!client)
            continue;

        client->active.store(false, std::memory_order_relaxed);
        const uintptr_t handleValue = client->socketHandle.exchange(0, std::memory_order_relaxed);
        if (handleValue != 0) {
            const SOCKET socketHandle = static_cast<SOCKET>(handleValue);
            shutdown(socketHandle, SD_BOTH);
            closesocket(socketHandle);
        }
    }

    for (auto& client : clients) {
        if (client && client->thread.joinable())
            client->thread.join();
    }
}

void WEBRadar::PruneWebSocketClients()
{
    std::vector<std::unique_ptr<WebSocketClient>> finished;
    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        for (size_t i = 0; i < wsClients_.size();) {
            WebSocketClient* client = wsClients_[i].get();
            if (client && !client->active.load(std::memory_order_relaxed)) {
                finished.push_back(std::move(wsClients_[i]));
                wsClients_.erase(wsClients_.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }
    }

    for (auto& client : finished) {
        if (client && client->thread.joinable())
            client->thread.join();
    }
}

bool WEBRadar::RegisterWebSocketClient(uintptr_t socketHandleValue, int protocolVersion)
{
    if (socketHandleValue == 0)
        return false;

    PruneWebSocketClients();

    const SOCKET socketHandle = static_cast<SOCKET>(socketHandleValue);
    u_long nonBlocking = 1;
    ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
    const BOOL noDelay = TRUE;
    setsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    const DWORD wsSendTimeoutMs = 1500;
    setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&wsSendTimeoutMs), sizeof(wsSendTimeoutMs));

    auto client = std::make_unique<WebSocketClient>();
    client->socketHandle.store(socketHandleValue, std::memory_order_relaxed);
    client->protocolVersion = protocolVersion >= 2 ? 2 : 1;

    uint64_t currentVersion = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (client->protocolVersion < 2)
            legacyPayloadDemandUntilMs_ = UnixNowMs() + 30000;
        currentVersion = latestPayloadVersion_;
    }
    if (client->protocolVersion < 2)
        cv_.notify_one();

    const uint64_t firstSeenVersion =
        client->protocolVersion >= 2 && currentVersion > 0 ? (currentVersion - 1) : currentVersion;
    client->thread = std::jthread(&WEBRadar::WebSocketClientLoop, this, client.get(), firstSeenVersion);

    {
        std::lock_guard<std::mutex> lock(wsMutex_);
        wsClients_.push_back(std::move(client));
    }

    return true;
}

void WEBRadar::WebSocketClientLoop(WebSocketClient* client, uint64_t lastSeenVersion)
{
    if (!client)
        return;

    constexpr uint64_t kIdlePingIntervalMs = 25000;
    constexpr uint64_t kIdleDropMs = 75000;
    const uint64_t startMs = UnixNowMs();
    uint64_t lastPingMs = startMs;
    uint64_t lastActivityMs = startMs;

    while (running_.load(std::memory_order_relaxed) && client->active.load(std::memory_order_relaxed)) {
        std::string payloadJson;
        uint64_t payloadVersion = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(250), [this, client, lastSeenVersion] {
                return !running_.load(std::memory_order_relaxed) ||
                    !client->active.load(std::memory_order_relaxed) ||
                    latestPayloadVersion_ != lastSeenVersion;
            });

            if (!running_.load(std::memory_order_relaxed) || !client->active.load(std::memory_order_relaxed))
                break;

            payloadJson = client->protocolVersion >= 2 ? latestPayloadJsonV2_ : latestPayloadJson_;
            payloadVersion = latestPayloadVersion_;
        }

        const uintptr_t handleValue = client->socketHandle.load(std::memory_order_relaxed);
        if (handleValue == 0)
            break;
        const SOCKET socketHandle = static_cast<SOCKET>(handleValue);

        bool peerClose = false;
        const int drained = DrainWebSocketIncoming(socketHandle, &peerClose);
        if (drained > 0)
            lastActivityMs = UnixNowMs();
        if (peerClose) {
            client->active.store(false, std::memory_order_relaxed);
            break;
        }

        const uint64_t nowMs = UnixNowMs();
        if (nowMs - lastActivityMs > kIdleDropMs) {
            client->active.store(false, std::memory_order_relaxed);
            break;
        }

        bool sentSomething = false;
        if (payloadVersion != lastSeenVersion && !payloadJson.empty()) {
            if (!SendWebSocketTextFrame(socketHandle, payloadJson)) {
                client->active.store(false, std::memory_order_relaxed);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++stats_.droppedFramesSlowClient;
                }
                break;
            }
            RecordBytesOut(payloadJson.size());
            lastSeenVersion = payloadVersion;
            sentSomething = true;
        }

        if (!sentSomething && (nowMs - lastPingMs) >= kIdlePingIntervalMs) {
            if (!SendWebSocketPing(socketHandle)) {
                client->active.store(false, std::memory_order_relaxed);
                break;
            }
            lastPingMs = nowMs;
        }
    }

    const uintptr_t handleValue = client->socketHandle.exchange(0, std::memory_order_relaxed);
    if (handleValue != 0) {
        const SOCKET socketHandle = static_cast<SOCKET>(handleValue);
        SendWebSocketCloseFrame(socketHandle, 1000);
        shutdown(socketHandle, SD_BOTH);
        closesocket(socketHandle);
    }
    client->active.store(false, std::memory_order_relaxed);
}

void WEBRadar::BroadcastLivePayload(const std::string& payloadJsonV1, const std::string& payloadJsonV2)
{
    size_t bytesSent = 0;
    if (remote::HasActiveConsumerDemand())
        remote::Publish(payloadJsonV2);

    if (!IsLocalEnabled())
        return;

    std::vector<StreamClient> clients;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        generation = streamGeneration_.load(std::memory_order_relaxed);
        clients.swap(streamClients_);
    }

    std::vector<StreamClient> survivors;
    survivors.reserve(clients.size());

    for (const StreamClient& client : clients) {
        const SOCKET socketHandle = static_cast<SOCKET>(client.socketHandle);
        if (socketHandle == INVALID_SOCKET)
            continue;

        bool connected = true;
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketHandle, &readSet);
        timeval tv = { 0, 0 };
        if (select(0, &readSet, nullptr, nullptr, &tv) > 0) {
            char buf;
            const int r = recv(socketHandle, &buf, 1, MSG_PEEK);
            connected = (r > 0);
        }

        if (!connected) {
            shutdown(socketHandle, SD_BOTH);
            closesocket(socketHandle);
            continue;
        }

        const std::string& payloadJson = client.protocolVersion >= 2 ? payloadJsonV2 : payloadJsonV1;
        if (!payloadJson.empty()) {
            if (!SendSseEvent(socketHandle, "snapshot", payloadJson)) {
                shutdown(socketHandle, SD_BOTH);
                closesocket(socketHandle);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++stats_.droppedFramesSlowClient;
                }
                continue;
            }
            bytesSent += payloadJson.size();
        }

        survivors.push_back(client);
    }

    if (!survivors.empty()) {
        bool keepSurvivors = false;
        if (running_.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(streamMutex_);
            keepSurvivors = (streamGeneration_.load(std::memory_order_relaxed) == generation);
            if (keepSurvivors)
                streamClients_.insert(streamClients_.end(), survivors.begin(), survivors.end());
        }

        if (!keepSurvivors) {
            for (const StreamClient& client : survivors) {
                const SOCKET socketHandle = static_cast<SOCKET>(client.socketHandle);
                if (socketHandle == INVALID_SOCKET)
                    continue;
                shutdown(socketHandle, SD_BOTH);
                closesocket(socketHandle);
            }
        }
    }

    if (bytesSent > 0)
        RecordBytesOut(bytesSent);
}

void WEBRadar::RecordBytesOut(size_t bytes)
{
    if (bytes == 0)
        return;

    const uint64_t nowMs = UnixNowMs();
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.totalBytesOut += static_cast<uint64_t>(bytes);

    if (bytesOutWindowUnixMs_ == 0)
        bytesOutWindowUnixMs_ = nowMs;
    bytesOutWindowBytes_ += static_cast<uint64_t>(bytes);

    const uint64_t elapsedMs = nowMs > bytesOutWindowUnixMs_ ? (nowMs - bytesOutWindowUnixMs_) : 0;
    if (elapsedMs >= 250) {
        const double sampleBytesPerSec =
            static_cast<double>(bytesOutWindowBytes_) * 1000.0 / static_cast<double>(elapsedMs);
        stats_.bytesOutPerSec = Ema(stats_.bytesOutPerSec, sampleBytesPerSec, 0.25);
        bytesOutWindowUnixMs_ = nowMs;
        bytesOutWindowBytes_ = 0;
    }
}

// The analyzer loses the RAII lock state across condition_variable::wait_for
// and later lock scopes in this long-running worker. The lock scopes are local.
#pragma warning(push)
#pragma warning(disable: 26110 26117)
void WEBRadar::WorkerLoop()
{
    #include "webradar_parts/webradar_worker_loop_body.inl"
}
#pragma warning(pop)

void WEBRadar::ServerLoop()
{
    #include "webradar_parts/webradar_server_loop_body.inl"
}

bool WEBRadar::HandleHttpClient(uintptr_t socketHandleValue, bool* keepOpen)
{
    #include "webradar_parts/webradar_handle_http_client_body.inl"
}

std::string WEBRadar::BuildStatusJson() const
{
    #include "webradar_parts/webradar_build_status_json_body.inl"
}

std::string WEBRadar::BuildFallbackLiveJson(const std::string& mapName, uint64_t nowMs) const
{
    #include "webradar_parts/webradar_build_fallback_live_json_body.inl"
}

std::string WEBRadar::BuildFallbackLiveJsonV2(const std::string& mapName, uint64_t nowMs) const
{
    nlohmann::json payload = {
        {"v", 2},
        {"seq", nowMs},
        {"ts", nowMs},
        {"lang", app::localization::LanguageCode(app::localization::GetLanguage())},
        {"map", mapName.empty() ? "unknown" : mapName},
        {"lt", 0},
        {"p", nlohmann::json::array()},
        {"b", nlohmann::json::array({0, nlohmann::json::array({0.0f, 0.0f, 0.0f}), 0.0f, 40.0f, 0.0f, 10.0f})},
        {"w", nlohmann::json::array()}
    };
    return payload.dump();
}

uint64_t WEBRadar::UnixNowMs()
{
    const auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

std::string WEBRadar::BuildPlayerSteamId(int slot)
{
    return "player:" + std::to_string(slot + 1);
}

std::string WEBRadar::NormalizeMapName(const std::string& rawName)
{
    const std::string trimmed = app::text::Trim(rawName);
    if (trimmed.empty() || app::text::ToLowerAscii(trimmed) == "auto")
        return {};

    return radar::NormalizeMapName(trimmed);
}

std::string WEBRadar::ResolveMapName(const esp::WebRadarSnapshot& snapshot)
{
    #include "webradar_parts/webradar_resolve_map_name_body.inl"
}

WEBRadar& Instance()
{
    static WEBRadar instance;
    return instance;
}

void Initialize()
{
    remote::Start();
    Instance().Start();
}

void Shutdown()
{
    Instance().Stop();
    remote::Stop();
}

void ApplySettingsFromGlobals()
{
    struct LocalSettingsSnapshot {
        bool enabled = false;
        int intervalMs = 0;
        int port = 0;
        std::string mapOverride;
        bool bindLan = false;
        std::vector<std::string> originAllowlist;

        bool operator==(const LocalSettingsSnapshot&) const = default;
    };
    struct RemoteSettingsSnapshot {
        bool enabled = false;
        std::string host;
        int webPort = 0;
        int sshPort = 0;
        std::string login;
        std::string password;
        std::string remotePath;

        bool operator==(const RemoteSettingsSnapshot&) const = default;
    };

    static bool initialized = false;
    static LocalSettingsSnapshot lastLocal;
    static RemoteSettingsSnapshot lastRemote;
    static auto nextCheck = std::chrono::steady_clock::time_point{};

    const auto now = std::chrono::steady_clock::now();
    if (initialized && now < nextCheck)
        return;
    nextCheck = now + std::chrono::milliseconds(16);

    LocalSettingsSnapshot local;
    RemoteSettingsSnapshot remoteSnapshot;
    {
        std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
        local.enabled = g::webRadarEnabled;
        local.intervalMs = g::webRadarIntervalMs;
        local.port = g::webRadarPort;
        local.mapOverride = g::webRadarMapOverride;
        local.bindLan = g::webRadarBindLan;
        local.originAllowlist = g::webRadarOriginAllowlist;

        remoteSnapshot.enabled = g::webRadarRemoteEnabled;
        remoteSnapshot.host = g::webRadarRemoteHost;
        remoteSnapshot.webPort = g::webRadarRemoteWebPort;
        remoteSnapshot.sshPort = g::webRadarRemoteSshPort;
        remoteSnapshot.login = g::webRadarRemoteLogin;
        remoteSnapshot.password = g::webRadarRemotePassword;
        remoteSnapshot.remotePath = g::webRadarRemotePath;
    }

    if (!initialized || local != lastLocal) {
        Instance().Configure(
            local.enabled,
            local.intervalMs,
            static_cast<uint16_t>(std::clamp(local.port, 1, 65535)),
            local.mapOverride,
            local.bindLan,
            local.originAllowlist);
        lastLocal = std::move(local);
    }

    if (!initialized || remoteSnapshot != lastRemote) {
        remote::Settings remoteSettings;
        remoteSettings.enabled = remoteSnapshot.enabled;
        remoteSettings.host = remoteSnapshot.host;
        remoteSettings.webPort = remoteSnapshot.webPort;
        remoteSettings.sshPort = remoteSnapshot.sshPort;
        remoteSettings.login = remoteSnapshot.login;
        remoteSettings.password = remoteSnapshot.password;
        remoteSettings.remotePath = remoteSnapshot.remotePath;
        remote::Configure(remoteSettings);
        lastRemote = std::move(remoteSnapshot);
    }

    initialized = true;
}

void CaptureFromEsp()
{
    static uint64_t s_lastCapturedPublishCount = 0;
    const uint64_t publishCount = esp::GetPublishCount();
    if (publishCount == 0 || publishCount == s_lastCapturedPublishCount)
        return;

    static thread_local esp::WebRadarSnapshot snapshot;
    if (!esp::GetWebRadarSnapshot(&snapshot))
        return;

    s_lastCapturedPublishCount = publishCount;
    Instance().UpdateSnapshot(snapshot);
}

RuntimeStats GetRuntimeStats()
{
    return Instance().GetStats();
}

bool HasActiveConsumers()
{
    return Instance().HasActiveConsumers() || remote::HasActiveConsumerDemand();
}

} 
