#include "../SDK/NetworkAdapter.h"
#include <iostream>
int main() {
    NetworkAdapter adapter;
    std::string key; std::cout << "Application access key: "; std::getline(std::cin, key);
    auto error = adapter.Authorize(key); SecureZeroMemory(key.data(), key.size());
    if (!error.empty()) { std::cerr << error << '\n'; return 1; }
    if (auto code = adapter.Connect()) { std::cerr << NetworkAdapter::Error(code) << '\n'; return 1; }
    // Windows -> your application. An empty queue is normal; do not busy-poll.
    VN_FRAME frame{};
    if (adapter.Receive(frame) == ERROR_SUCCESS) std::cout << "Received " << frame.length << " Ethernet bytes.\n";
    // Your application -> Windows: adapter.Send(ethernetBytes).
    // Keep the adapter alive while connected. Its destructor disconnects.
}
