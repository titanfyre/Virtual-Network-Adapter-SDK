#include "../SDK/NetworkAdapter.h"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    NetworkAdapter adapter;
    const std::string action = argc == 2 ? argv[1] : "status";
    if (action != "status" && action != "connect") { std::cout << "Usage: NetworkAdapterCLI.exe [status | connect]\n"; return 2; }
    VN_STATE state{};
    if (auto error = adapter.Query(state)) {
        std::cerr << "Open an administrator console and install the matching signed driver.\n" << NetworkAdapter::Error(error) << '\n'; return 1;
    }
    if (action == "status") {
        std::cout << "Virtual Ethernet: " << (state.connected ? "connected" : "disconnected") << "; queued " << state.queued << "/" << state.capacity << "; sent " << state.transmitted << "; received " << state.received << "; dropped " << state.dropped << '\n'; return 0;
    }
    std::cout << "Application access key (paste with Ctrl+V): ";
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE); DWORD mode = 0;
    const bool console = GetConsoleMode(input, &mode) != FALSE;
    if (console) SetConsoleMode(input, mode & ~ENABLE_ECHO_INPUT);
    std::string key; std::getline(std::cin, key);
    if (console) SetConsoleMode(input, mode);
    std::cout << '\n';
    auto result = adapter.Authorize(key); SecureZeroMemory(key.data(), key.size());
    if (!result.empty()) { std::cerr << result << '\n'; return 1; }
    if (auto error = adapter.Connect()) { std::cerr << NetworkAdapter::Error(error) << '\n'; return 1; }
    std::cout << "Virtual link connected. No routes, DNS, bridge or NAT were changed.\nPress Enter to disconnect.\n";
    std::string line; std::getline(std::cin, line);
}
