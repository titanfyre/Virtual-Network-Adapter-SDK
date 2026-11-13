# Virtual-Network-Adapter-SDK Python SDK

`pip install virtual-network-adapter-sdk`

Windows x64 Python3.10+. No driver is installed by this package.

```python
from virtual_network_adapter_sdk import Client
with Client() as client:
    info = client.call("status")
```

[Operation schema and API](docs/API.md). Check errors and the product licensing state before device creation.
