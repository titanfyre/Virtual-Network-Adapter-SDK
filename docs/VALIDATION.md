# Validation and deployment boundaries

This document records bounded checks for the first Virtual Network Adapter SDK preview. The private driver requires a verified Microsoft-signed return before the public bundle includes installable driver executables.

| Check | Status |
| --- | --- |
| Native application, SDK, console example and UI/console installer source | Built with MSVC `/W4 /WX`, CFG and static runtime |
| Frame parser bounds, malformed hex, minimum/maximum frame size and 10,000 deterministic randomized round trips | 40,014 assertions passed |
| Same user-mode assertions under AddressSanitizer | Passed with MSVC 14.44 runtime |
| Private NDIS miniport PREfast | Zero reported defects |
| INF Windows Driver / universal validation | `InfVerif /w` and `/u` passed |
| Windows 10/11 x64 catalog generation | Passed |
| CAB expansion and payload hash comparison | Passed |
| Windows 10 build 19045 x64, current driver candidate, normal VM campaign | Passed: actual installation, at least 2,141 checks, real UDP round trip, 1,024 receive indications and own-package uninstallation |
| Live owner removal and replacement with four concurrent observer threads | Passed normally and under Driver Verifier: at least 2,141 checks, replacement PnP code 0, fresh endpoint query, old endpoint STATUS/CONNECT refusal and handle cleanup |
| Restricted non-administrator control-device open | Denied as expected in the VM |
| Current candidate with targeted Driver Verifier | Passed: normal packet/control run and live-owner removal/replacement run, with real UDP delivery and full own-package unload |
| Microsoft-signed package installation and broad deployment | Pending signed return |

Both final VM campaigns used the candidate with SHA-256 `13A59130D7397FEFB36CDC752944181086DBBB46ECAC7EEFA47C320B6CA80352`. They used isolated lab-signed copies, not a returned Microsoft signature. The normal packet/control run completed 2,141 checks and its live-owner run completed 2,145; with targeted Driver Verifier, both runs completed 2,142 checks. Counts vary slightly with Windows background frames and ARP timing. The baseline and all nine historical checkpoints were restored after each campaign; the VM ended off. The own driver package unloaded without a restart after all old handles closed. No driver was installed on the host and no host networking or security policy was changed. The UI's 200 ms refresh rate is not a driver throughput or latency measurement. Advertised 1 Gbit/s link speed is a software interface property.

## Runtime checks

The isolated VM runtime opens only the administrator control endpoint, verifies that an unsigned permission cannot connect the link, obtains a fresh handle-bound challenge and accepts only a correctly signed permission. Replay, foreign-owner queue reads and foreign disconnects must fail. The driver starts disconnected.

For actual data delivery, the runtime assigns a temporary RFC 5737 address to the one synthetic MAC, sends a UDP datagram from Windows to a synthetic peer, responds to ARP inside the SDK, verifies the outbound UDP payload and supplies a constructed Ethernet/IPv4/UDP response. A Windows UDP socket receives the exact response. The runtime then performs 1,024 bounded private EtherType receive indications, disconnects, reconnects and verifies owner-handle close cleanup. It verifies removal of its temporary address and leaves physical interfaces, DNS and gateways unchanged.

The live-removal campaign keeps the authorized owner and an observer handle open while four threads query the observer endpoint. It removes the own PnP node, closes the old owner and re-enumerates the adapter while the old observer remains open. The replacement reaches PnP problem code 0, and the console SDK queries the new disconnected endpoint before and after the old observer closes. STATUS and CONNECT requests on the old observer are refused; closing it allows the old control object to finish deletion. The driver uses unique kernel device names with a stable public link and compares each request's device object with the current generation. The test then removes only its own package and verifies driver unload.

Windows exposes NDIS filter interfaces in `GetIfTable2`, often sharing the base adapter's MAC address. The test selects an exact current/permanent synthetic MAC with `InterfaceAndOperStatusFlags.FilterInterface == FALSE`; it never selects a physical interface or chooses an interface by its display name alone. Enumeration is bounded and interface-selection metadata is retained privately for diagnostics.

## Remaining limits

Initial qualification targets Windows 10 build 19045 x64. Windows 11, suspend/resume, many-instance churn, long-duration workloads, Hyper-V/third-party network-filter combinations, installer update/rollback, hardware performance and Microsoft-signed-package installation need separate qualification. No claim of zero latency, zero defects, unlimited throughput or unbreakable licensing is made.

Only Ethernet frames are accepted. The interface exposes no arbitrary memory access, kernel address operations or physical adapter capture. One owner controls one fixed adapter; application-side scheduling and protocol implementation remain the integrator's responsibility. Server authorization is a bounded lease: administrative key revocation prevents new grants, while an existing grant remains effective until its lifetime ends. Sharing an access key remains possible without machine binding.

Lease expiration blocks packet exchange immediately. The next passive control operation clears ownership, flushes stale queued frames and reports link departure before a renewal is accepted. An otherwise idle adapter's asynchronous Windows link notification is deferred until that control operation; there is no periodic expiry polling timer.
