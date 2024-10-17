# The Linux File System in C++ for Linux/Windows
## Library Install

### Windows
You must have Visual Studio 2022 with the "Desktop Development with C++" component installed.

Once it is installed,
Open "x64 Native Tools Command Prompt for VS 2022",
And run the following command:

```
cl /O2 /std:c++17 /LD /Zi FileSystem.cc
```

This will make `FileSystem.dll` and `FileSystem.lib` for you to use with your applications.
It will also make `FileSystem.pdb` for debugging the library.

If you wish to leave out debug info, Remove the `/Zi` flag.

### Linux
You must have any compiler installed. For this example, I'll assume you have `g++` installed.

Once you have installed the compiler, Run the following command:

```
g++ -O2 -g -fPIC -shared -o FileSystem.so FileSystem.cc
```

This will make `FileSystem.so` for you to use with your applications.

If you wish to leave out debug info, Remove the `-g` flag.

## Node.js Install
You must follow the [Library Install](#library-install) steps first
before installing the Node.js Module.

Once you have completed the [Library Install](#library-install),
Simply run the following command:

```
npm i
```