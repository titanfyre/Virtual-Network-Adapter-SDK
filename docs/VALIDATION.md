# Validation and deployment boundaries

Application/SDK 0.1.1 includes Microsoft-signed driver 0.1.0.0. The returned INF and Authenticode-protected SYS image match the submission. Microsoft catalog trust, INF/SYS membership and the exact embedded installer resources pass. Signing and bounded VM tests do not establish universal deployment qualification.

| Check | Result |
| --- | --- |
| Native application, SDK, CLI, example and UI/console setup | Built with MSVC `/W4 /WX`, CFG, ASLR, NX and static runtime |
| Frame parser bounds, malformed hex, minimum/maximum frames and deterministic randomized round trips | 40,014 assertions passed |
| Installer class/provider/hardware/model/service/copy-file identity and unsigned-signature refusal | 12 checks passed |
| Both user-mode CTest cases under AddressSanitizer | Passed with MSVC 14.44 runtime |
| Returned signed package policy | 17 read-only checks: exact identity, Microsoft-root catalog and INF/SYS membership accepted; altered INF/SYS/catalog fixtures refused |
| Embedded UI/console installer payloads | Exact returned INF/CAT/SYS bytes, inspected as data without executing installer code on the host |
| Private NDIS miniport PREfast | Zero reported defects |
| INF Windows Driver / universal validation | `InfVerif /w` and `/u` passed |
| Windows 10/11 x64 catalog generation and CAB round trip | Passed before submission; this is not a Windows 11 runtime result |
| Microsoft-signed package on Windows 10 build 19045 x64, normal campaign | Passed: real console installation, 2,144 packet/control checks and 2,144 live-owner removal/replacement checks |
| Same signed package under standard Driver Verifier targeted at both new SDK drivers | Passed: 2,145 packet/control checks and 2,147 live-owner removal/replacement checks |
| Restricted non-administrator control-device open | Denied in both signed VM campaigns |
| Actual signed console uninstallation, package removal and driver unload | Passed in both signed VM campaigns |

Both final campaigns used the exact returned signed SYS with SHA-256 `21645fc20733f3ce5fb0b17bf55fc5dd2beb84437d167ec41637b20fd8dc896b`; its installed Driver Store hash was checked. Test-signing was disabled, no guest test certificate was used, and the bundled console installers performed installation and full own-package removal. The UI and console share the installation core, but UI resource/core checks are distinct from live console operation tests.

The normal and Driver Verifier campaigns both restored the original powered-off VM baseline and all nine historical checkpoints. Neither produced a new minidump. No driver was installed on the host, and no host networking or security policy was changed. Earlier pre-submission lab-signed candidate tests remain separate private evidence rather than being counted as signed-return tests.

## Runtime checks

The isolated runtime opens only the administrator control endpoint. It verifies that an unsigned permission cannot connect the link, obtains a fresh handle-bound challenge and accepts only a correctly signed permission. Replay, foreign-owner queue reads and foreign disconnects are refused. The driver starts disconnected.

For actual delivery, the runtime assigns a temporary RFC 5737 address to the one synthetic MAC, sends a UDP datagram from Windows to a synthetic peer, responds to ARP inside the SDK, verifies the outbound UDP payload and supplies a constructed Ethernet/IPv4/UDP response. A Windows UDP socket receives the exact response. Each packet/control run and each live-owner run includes 1,024 bounded private EtherType receive indications, disconnect/reconnect and owner-handle close cleanup. The runtime removes its temporary address and leaves physical interfaces, DNS and gateways unchanged. Check counts vary slightly with background frames and ARP timing.

The live-removal case keeps the authorized owner and an observer handle open while four threads query the observer endpoint. It removes the own PnP node, closes the old owner and reinstalls the signed adapter while the old observer remains open. The replacement reaches PnP problem code 0. The shipping console SDK queries its fresh disconnected endpoint before and after the old observer closes. STATUS and CONNECT requests on the old observer are refused; closing it allows the old control object to finish deletion. The test then removes the own package and verifies driver unload.

Windows exposes NDIS filter interfaces in `GetIfTable2`, often sharing the base adapter's MAC. The runtime selects the exact current/permanent synthetic MAC with `InterfaceAndOperStatusFlags.FilterInterface == FALSE`; it never selects a physical interface or chooses an interface by its display name alone. Enumeration is bounded.

## Signed installer boundary

Both setups embed the exact returned INF/CAT/SYS. Catalog verification requires a trusted chain to a Microsoft root, the Microsoft Windows Hardware Compatibility Publisher common name and Microsoft organization, followed by catalog membership for the actual INF and SYS. Identity checks require this product's class, provider, hardware ID, model, service and copy-file/directory identity. Tests of altered files do not install a driver on the host.

A source-only build contains no package by default and says so in setup. Supply a separately verified opaque signed package at build time to produce installable setups. Neither setup changes signature enforcement, enables test-signing, restarts automatically, changes a physical adapter or installs routes, gateways, bridges or DNS configuration. Exit code 3010 requests a manual restart; no restart was needed in the completed signed campaigns.

## Remaining limits

Qualification currently covers Windows 10 build 19045 x64 in an isolated Hyper-V guest. Windows 11, Secure Boot/HVCI, suspend/resume, long-duration workloads, many-instance churn, Hyper-V/third-party filter coexistence, installer update/rollback and hardware performance require separate testing. No claim of zero latency, zero defects, unlimited throughput or unbreakable licensing is made. The UI's 200 ms display refresh is not a data-path rate limit; the advertised 1 Gbit/s software link speed is not a benchmark.

Only Ethernet frames are accepted. The interface exposes no arbitrary memory access, kernel address operations or physical adapter capture. One owner controls one fixed adapter. Application scheduling, Ethernet protocols, address configuration and any tunnel transport remain the integrator's responsibility.

Authorization is a bounded lease: key revocation prevents new grants, while an issued grant remains effective until expiry. Keys can be shared; this build has no machine binding. Lease expiration blocks packet exchange immediately. The next passive control operation clears ownership, flushes queued frames and reports link departure before a renewal is accepted. An otherwise idle adapter's Windows link notification is deferred until that operation; there is no periodic expiry polling timer.
