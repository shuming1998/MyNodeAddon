{
    "include_dirs": [
        "<!(node -p \"require('node-addon-api').include\")"
    ],
    "targets": [
        {
            "target_name": "mynodeaddon",
            "sources": [ "src/myaddon.cpp" ]
        }
    ]
}
