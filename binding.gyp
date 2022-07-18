{
  "targets": [
    {
      "target_name": "FileSystem",
      "sources": [
        "FileSystem.cc"
      ],
      "cflags!": [ "-O3" ],
      "cflags": [ "-Os", "-fno-stack-protector" ]
    }
  ]
}