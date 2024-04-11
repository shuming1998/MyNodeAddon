{
  "targets": [
    {
      "target_name": "mynodeaddon",
      "sources": [ "src/myaddon.cpp" ],
      "include_dirs": [
        "<!(llvm-config --includedir)",
        "<!(node -p \"require('node-addon-api').include\")"
      ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "cflags": [ "<!(llvm-config --cxxflags)" ],
      "ldflags": [
        "<!(llvm-config --ldflags)",
        "<!(llvm-config --system-libs)",
        "<!(llvm-config --libs core orcjit native)"
      ],
      'conditions': [
        ['OS=="mac"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [
              '<!(llvm-config --ldflags)',
              '<!(llvm-config --system-libs)',
              '<!(llvm-config --libs core orcjit native)'
            ]
          }
        }],
        ['OS=="win"', {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'AdditionalOptions': [
                '<!(llvm-config --cxxflags)'
              ]
            },
            'VCLinkerTool': {
              'AdditionalDependencies': [
                '<!(llvm-config --ldflags)',
                '<!(llvm-config --system-libs)',
                '<!(llvm-config --libs core orcjit native)'
              ]
            }
          }
        }],
        ['OS=="linux"', {
          'ldflags': [
            '<!(llvm-config --ldflags)',
            '<!(llvm-config --system-libs)',
            '<!(llvm-config --libs core orcjit native)'
          ],
          'cflags': [ '<!(llvm-config --cxxflags)' ]
        }]
      ]
    }
  ]
}
