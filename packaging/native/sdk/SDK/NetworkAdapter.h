#pragma once
#include "NetworkProtocol.h"
#include "AuthorizationProtocol.h"
#include <string>
#include <string_view>
#include <vector>
bool ParseEthernetFrame(std::string_view hex, std::vector<unsigned char>& frame);

class NetworkAdapter {
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD Call(DWORD code, const void* input, DWORD inputBytes, void* output, DWORD outputBytes) noexcept;
public:
    NetworkAdapter() = default;
    ~NetworkAdapter() { Close(); }
    NetworkAdapter(const NetworkAdapter&) = delete;
    NetworkAdapter& operator=(const NetworkAdapter&) = delete;
    DWORD Open() noexcept;
    void Close() noexcept;
    DWORD Query(VN_STATE& state) noexcept;
    std::string Authorize(std::string_view key);
    DWORD Connect() noexcept;
    DWORD Disconnect() noexcept;
    DWORD Receive(VN_FRAME& frame) noexcept;
    DWORD Send(const std::vector<unsigned char>& frame) noexcept;
    static std::string Error(DWORD error);
};
