{
  'targets': [
    {
      'target_name': 'module',
      'sources': ['module.cc'],
      'conditions': [
        ['OS=="win"', {
          'libraries': ['<(module_root_dir)/FileSystem.lib'],
          'copies': [{
            'destination': '<(module_root_dir)/build/Release/',
            'files': [
              '<(module_root_dir)/FileSystem.dll'
            ]
          }]
        }, {
          'cflags!': ['-O3'],
          'cflags': ['-O2', '-g'],
          'libraries': ['<(module_root_dir)/FileSystem.so'],
          'copies': [{
            'destination': '<(module_root_dir)/build/Release/',
            'files': [
              '<(module_root_dir)/FileSystem.so'
            ]
          }]
        }]
      ]
    }
  ]
}