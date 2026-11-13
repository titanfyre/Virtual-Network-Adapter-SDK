# Virtual-Network-Adapter-SDK Ruby SDK

`gem install virtual-network-adapter-sdk`

Windows x64 Ruby3.1+.

```ruby
require "virtual_network_adapter_sdk"
VirtualNetworkAdapterSDKSDK::Client.open { |client| p client.call("status") }
```

[API and operation schema](docs/API.md). Matching driver installed separately.
