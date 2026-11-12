#include "NetworkAdapter.h"
#include <cstring>
#include <cctype>

bool ParseEthernetFrame(std::string_view hex, std::vector<unsigned char>& frame) {
    frame.clear(); if (hex.size() > VN_MAX_FRAME * 3u) return false;
    int high = -1;
    for (char raw : hex) {
        const auto c = static_cast<unsigned char>(raw);
        if (std::isspace(c) || c == '-' || c == ':') { if (high != -1) { frame.clear(); return false; } continue; }
        int value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (value < 0) { frame.clear(); return false; }
        if (high < 0) high = value;
        else { frame.push_back(static_cast<unsigned char>(high * 16 + value)); high = -1; if (frame.size() > VN_MAX_FRAME) { frame.clear(); return false; } }
    }
    if (high != -1 || frame.size() < 14) { frame.clear(); return false; } return true;
}

namespace { std::string RequestDriverAuthorization(std::string_view key,
    const TITAN_AUTHORIZATION_CHALLENGE& challenge, TITAN_AUTHORIZATION_TOKEN& token); }

DWORD NetworkAdapter::Open() noexcept {
    if (handle_ != INVALID_HANDLE_VALUE) return ERROR_SUCCESS;
    handle_ = CreateFileW(L"\\\\.\\TitanVirtualNetwork", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return handle_ == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
}
void NetworkAdapter::Close() noexcept {
    if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
}
DWORD NetworkAdapter::Call(DWORD code, const void* input, DWORD inputBytes, void* output, DWORD outputBytes) noexcept {
    if (const auto error = Open()) return error;
    DWORD returned = 0;
    if (!DeviceIoControl(handle_, code, const_cast<void*>(input), inputBytes, output, outputBytes, &returned, nullptr)) {
        const auto error = GetLastError();
        // Drop a stale generation. The next call opens the current endpoint;
        // never replay a frame automatically after a failed send.
        if (error == ERROR_NOT_READY || error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_INVALID_HANDLE || error == ERROR_DELETE_PENDING) Close();
        return error;
    }
    return returned == outputBytes ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}
DWORD NetworkAdapter::Query(VN_STATE& state) noexcept {
    state = {}; auto error = Call(VN_IOCTL_STATUS, nullptr, 0, &state, sizeof(state));
    if (!error && (state.version != VN_VERSION || state.size != sizeof(state) || state.queued > VN_RING_CAPACITY || state.max_frame != VN_MAX_FRAME || state.capacity != VN_RING_CAPACITY)) error = ERROR_INVALID_DATA;
    if (error) state = {}; return error;
}
std::string NetworkAdapter::Authorize(std::string_view key) {
    TITAN_AUTHORIZATION_CHALLENGE challenge{}; TITAN_AUTHORIZATION_TOKEN token{};
    if (auto error = Call(VN_IOCTL_CHALLENGE, nullptr, 0, &challenge, sizeof(challenge))) return "Driver authorization challenge: " + Error(error);
    auto result = RequestDriverAuthorization(key, challenge, token);
    if (!result.empty()) return result;
    auto error = Call(VN_IOCTL_AUTHORIZE, &token, sizeof(token), nullptr, 0);
    SecureZeroMemory(&token, sizeof(token));
    return error ? "Driver refused authorization: " + Error(error) : std::string{};
}
DWORD NetworkAdapter::Connect() noexcept { return Call(VN_IOCTL_CONNECT, nullptr, 0, nullptr, 0); }
DWORD NetworkAdapter::Disconnect() noexcept { return Call(VN_IOCTL_DISCONNECT, nullptr, 0, nullptr, 0); }
DWORD NetworkAdapter::Receive(VN_FRAME& frame) noexcept {
    frame = {}; auto error = Call(VN_IOCTL_RECEIVE, nullptr, 0, &frame, sizeof(frame));
    if (!error && (frame.length < 14 || frame.length > VN_MAX_FRAME)) error = ERROR_INVALID_DATA;
    if (error) frame = {}; return error;
}
DWORD NetworkAdapter::Send(const std::vector<unsigned char>& bytes) noexcept {
    if (bytes.size() < 14 || bytes.size() > VN_MAX_FRAME) return ERROR_BAD_LENGTH;
    VN_FRAME frame{}; frame.length = static_cast<ULONG>(bytes.size()); std::memcpy(frame.data, bytes.data(), bytes.size());
    return Call(VN_IOCTL_SEND, &frame, sizeof(frame), nullptr, 0);
}
std::string NetworkAdapter::Error(DWORD error) {
    char* text = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), reinterpret_cast<char*>(&text), 0, nullptr);
    std::string result = text ? text : "Windows operation failed"; if (text) LocalFree(text);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) result.pop_back();
    return result + " (Windows error " + std::to_string(error) + ")";
}

#include "NetworkAdapter.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <array>
#include <cstring>
#include <utility>
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"crypt32.lib")

namespace {
struct Http {
    HINTERNET value = nullptr;
    explicit Http(HINTERNET handle) : value(handle) {}
    ~Http() { if (value) WinHttpCloseHandle(value); }
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;
};
std::string Failure(const char* stage) {
    return std::string(stage) + " failed (Windows error " +
        std::to_string(GetLastError()) + ").";
}
bool ValidKey(std::string_view key) {
    if (key.size() != 81 || key.substr(0, 5) != "app1_" || key[37] != '_')
        return false;
    for (size_t i = 5; i != 37; ++i)
        if (!((key[i] >= '0' && key[i] <= '9') ||
              (key[i] >= 'a' && key[i] <= 'f'))) return false;
    for (size_t i = 38; i != key.size(); ++i)
        if (!((key[i] >= 'a' && key[i] <= 'z') ||
              (key[i] >= 'A' && key[i] <= 'Z') ||
              (key[i] >= '0' && key[i] <= '9') ||
               key[i] == '_' || key[i] == '-')) return false;
    return true;
}
}

namespace {
std::string RequestDriverAuthorization(std::string_view key,
    const TITAN_AUTHORIZATION_CHALLENGE& challenge,
    TITAN_AUTHORIZATION_TOKEN& token) {
    token = {};
    if (!ValidKey(key)) return "Enter a valid application access key (app1_...).";
    if (challenge.magic != TITAN_AUTHORIZATION_MAGIC ||
        challenge.version != TITAN_AUTHORIZATION_VERSION ||
        challenge.duration_seconds != 0 ||
        (challenge.product != TITAN_AUTHORIZATION_NETWORK &&
         challenge.product != TITAN_AUTHORIZATION_CAMERA))
        return "The driver returned an invalid authorization challenge.";
    const char* sku = challenge.product == TITAN_AUTHORIZATION_NETWORK ?
        "virtual-network-adapter-sdk" : "virtual-camera-sdk";
    std::string nonce;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char value : challenge.nonce) {
        nonce += hex[value >> 4]; nonce += hex[value & 15];
    }
    std::string body = "{\"key\":\"" + std::string(key) +
        "\",\"sku\":\"" + sku + "\",\"nonce\":\"" + nonce + "\"}";
    Http session(WinHttpOpen(L"Titan Driver SDK/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) { SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS initialization"); }
    WinHttpSetTimeouts(session.value, 5000, 5000, 10000, 10000);
    Http connection(WinHttpConnect(session.value, L"licences.configcsa.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.value) { SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS connection"); }
    Http request(WinHttpOpenRequest(connection.value, L"POST",
        L"/portfolio/api/v1/driver-authorize", nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) { SecureZeroMemory(body.data(), body.size()); return Failure("Authorization request"); }
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE,
        &disabled, sizeof(disabled))) {
        SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS policy");
    }
    const BOOL sent = WinHttpSendRequest(request.value,
        L"Content-Type: application/json\r\n", static_cast<DWORD>(-1),
        body.data(), static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);
    const DWORD sendError = sent ? ERROR_SUCCESS : GetLastError();
    SecureZeroMemory(body.data(), body.size());
    if (!sent) return "Authorization request failed (Windows error " + std::to_string(sendError) + ").";
    if (!WinHttpReceiveResponse(request.value, nullptr)) return Failure("Authorization response");
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE |
        WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
        &length, WINHTTP_NO_HEADER_INDEX)) return Failure("HTTP status");
    std::string response;
    std::array<char, 1024> chunk{};
    for (;;) {
        DWORD count = 0;
        if (!WinHttpReadData(request.value, chunk.data(),
            static_cast<DWORD>(chunk.size()), &count)) return Failure("Response read");
        if (!count) break;
        if (response.size() + count > 8192) return "Authorization response is too large.";
        response.append(chunk.data(), count);
    }
    if (status != 200) {
        for (char& c : response) if (static_cast<unsigned char>(c) < 32) c = ' ';
        return "License server refused authorization (HTTP " +
            std::to_string(status) + "): " + response;
    }
    // Only extract a bounded base64 field. The kernel verifies every byte of
    // the decoded permission and its signature before enabling any producer.
    const auto field = response.find("\"grant_base64\"");
    if (field == std::string::npos) return "Missing authorization grant in server response.";
    const auto colon = response.find(':', field + 14);
    const auto start = colon == std::string::npos ? std::string::npos : response.find('"', colon + 1);
    const auto end = start == std::string::npos ? std::string::npos : response.find('"', start + 1);
    if (end == std::string::npos || end - start - 1 != 152)
        return "Invalid authorization grant encoding.";
    const std::string grant = response.substr(start + 1, end - start - 1);
    DWORD bytes = sizeof(token);
    if (!CryptStringToBinaryA(grant.c_str(), static_cast<DWORD>(grant.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        reinterpret_cast<BYTE*>(&token), &bytes, nullptr, nullptr) ||
        bytes != sizeof(token)) {
        token = {}; return "Invalid authorization grant length or encoding.";
    }
    if (token.permission.magic != challenge.magic ||
        token.permission.version != challenge.version ||
        token.permission.product != challenge.product ||
        token.permission.duration_seconds == 0 ||
        token.permission.duration_seconds > TITAN_AUTHORIZATION_MAX_SECONDS ||
        std::memcmp(token.permission.nonce, challenge.nonce, sizeof(challenge.nonce))) {
        token = {}; return "Authorization grant does not match this driver session.";
    }
    return {};
}

} // private HTTPS implementation
