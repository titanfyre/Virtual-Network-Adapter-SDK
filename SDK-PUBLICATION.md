# SDK publication status

Current source layout and registry coverage. A GitHub download is distinct from publication on a central registry.

| Target | Status | Package / documentation |
| --- | --- | --- |
| CPP/vcpkg | published_custom_registry | virtual-network-adapter-sdk |
| CPP/conan | published_custom_source_index | [virtual-network-adapter-sdk](https://github.com/TitanCSA/sdk-packages/tree/089bc73095aa8eda89bacbe10d0b9e58394b0207/conan-index) [Source](packaging/native/) |
| CPP/nuget | paused_pending_owner_email_confirmation | Virtual-Network-Adapter-SDK.Native |
| NET/nuget | built_verified_publication_on_hold_by_owner | VirtualNetworkAdapterSDK.SDK [Source](Virtual-Network-Adapter-SDK-NET-nuget/) |
| Python/pypi | built_fixture_and_production_verified | virtual-network-adapter-sdk [Source](Virtual-Network-Adapter-SDK-Python-pypi/) |
| Python/conda-forge | custom_channel_install_verified_requires_conda_forge_review | [virtual-network-adapter-sdk](https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel) [Source](packaging/conda/) |
| Rust/cargo | published_install_verified | [virtual-network-adapter-sdk](https://crates.io/crates/virtual-network-adapter-sdk) [Source](Virtual-Network-Adapter-SDK-Rust-cargo/) |
| JavaScript/npm | awaiting_browser_publication_authentication | [virtual-network-adapter-sdk](https://www.npmjs.com/package/virtual-network-adapter-sdk) [Source](Virtual-Network-Adapter-SDK-JavaScript-npm/) |
| Java/maven | published_install_verified | [virtual-network-adapter-sdk](https://central.sonatype.com/artifact/io.github.titancsa/virtual-network-adapter-sdk/1.0.0) [Source](Virtual-Network-Adapter-SDK-Java-maven/) |
| Java/jitpack | published_install_verified | [virtual-network-adapter-sdk](https://jitpack.io/#titanfyre/Virtual-Network-Adapter-SDK/java-sdk-v1.0.0) [Source](Virtual-Network-Adapter-SDK-Java-maven/) |
| Go/modules | published_install_verified | [virtual-network-adapter-sdk](https://pkg.go.dev/github.com/titanfyre/Virtual-Network-Adapter-SDK/Virtual-Network-Adapter-SDK-Go-modules@v1.0.0) [Source](Virtual-Network-Adapter-SDK-Go-modules/) |
| Go/pkg-go-dev | indexed_verified | [Documentation](https://pkg.go.dev/github.com/titanfyre/Virtual-Network-Adapter-SDK/Virtual-Network-Adapter-SDK-Go-modules@v1.0.0) |
| PowerShell/gallery | published_install_verified | [VirtualNetworkAdapterSDK](https://www.powershellgallery.com/packages/VirtualNetworkAdapterSDK/1.0.0) [Source](Virtual-Network-Adapter-SDK-PowerShell-gallery/) |
| Dart/pub-dev | built_fixture_and_production_verified | virtual_network_adapter_sdk [Source](Virtual-Network-Adapter-SDK-Dart-pub-dev/) |
| Ruby/rubygems | published_install_verified | [virtual-network-adapter-sdk](https://rubygems.org/gems/virtual-network-adapter-sdk) [Source](Virtual-Network-Adapter-SDK-Ruby-rubygems/) |
| PHP/packagist | composer_source_and_tag_published | [titancsa/virtual-network-adapter-sdk](https://packagist.org/packages/titancsa/virtual-network-adapter-sdk) |

Native SDK source and CMake/Conan/vcpkg/NuGet recipes share [`packaging/native`](packaging/native/LAYOUT.md). Previous release archives and tags remain unchanged.

Application and driver prerequisites and original licensing remain those documented by each SDK. No driver installer is bundled with these source packages.
