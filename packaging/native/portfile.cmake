vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/titanfyre/Virtual-Network-Adapter-SDK/releases/download/sdk-v0.1.1/virtual-network-adapter-sdk-sdk-0.1.1.tar.gz"
    FILENAME "virtual-network-adapter-sdk-sdk-0.1.1.tar.gz"
    SHA512 f5272280501f8e71f5b2d1a3babf1ab66ff2be221cea221934e3bd168b2ea4e5eab858685f57443b6f66aa0d1e88790c9243123c3ce543cb7ff5e5fedbce7557
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME VirtualNetworkAdapterSDK CONFIG_PATH lib/cmake/VirtualNetworkAdapterSDK)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
