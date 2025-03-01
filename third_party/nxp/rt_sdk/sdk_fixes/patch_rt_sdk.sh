#!/bin/bash

patch_sdk()
{
    # copy/replace additional/modified SDK files
    if [ -d "fixes" ]; then
        cp -r fixes/* $1
    fi

    # apply Wi-Fi patches
    patch -N $1/middleware/wifi_nxp/wlcmgr/wlan.c <wlan_reconnect_fix.patch || :

    echo "MCUXpresso SDK folder \"$1\" has been patched!"
}

main()
{
    if [ $# != 0 ]; then
        echo >&2 "Trailing arguments: $@"
        # 128 for Invalid arguments
        exit 128
    fi

    patch_sdk ../repo
}

main "$@"
