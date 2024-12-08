// Copyright (c) 2024 DaisyDogs07
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
// NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include <stdint.h>

#if !(defined(__linux__) || defined(_WIN32))
#error FileSystem is only available on Linux and Windows
#elif INTPTR_MAX == INT32_MAX
#error FileSystem is not available on 32-bit platforms
#else

#ifdef __linux__
#define FS_LONG long
#else
#define FS_LONG long long
#endif

#define FS_AT_EMPTY_PATH 0x1000
#define FS_AT_FDCWD -100
#define FS_AT_REMOVEDIR 0x200
#define FS_AT_SYMLINK_FOLLOW 0x400
#define FS_AT_SYMLINK_NOFOLLOW 0x100
#define FS_DT_REG 8
#define FS_DT_DIR 4
#define FS_DT_LNK 10
#define FS_FALLOC_FL_COLLAPSE_RANGE 0x08
#define FS_FALLOC_FL_INSERT_RANGE 0x20
#define FS_FALLOC_FL_KEEP_SIZE 0x01
#define FS_FALLOC_FL_PUNCH_HOLE 0x02
#define FS_FALLOC_FL_ZERO_RANGE 0x10
#define FS_O_ACCMODE 0003
#define FS_O_APPEND 02000
#define FS_O_CREAT 0100
#define FS_O_DIRECTORY 0200000
#define FS_O_EXCL 0200
#define FS_O_NOATIME 01000000
#define FS_O_NOFOLLOW 0400000
#define FS_O_RDONLY 00
#define FS_O_RDWR 02
#define FS_O_TMPFILE (020000000 | FS_O_DIRECTORY)
#define FS_O_TRUNC 01000
#define FS_O_WRONLY 01
#define FS_F_OK 0
#define FS_R_OK 4
#define FS_W_OK 2
#define FS_X_OK 1
#define FS_NAME_MAX 255
#define FS_PATH_MAX 4096
#define FS_RENAME_EXCHANGE (1 << 1)
#define FS_RENAME_NOREPLACE (1 << 0)
#define FS_SEEK_CUR 1
#define FS_SEEK_DATA 3
#define FS_SEEK_END 2
#define FS_SEEK_HOLE 4
#define FS_SEEK_SET 0
#define FS_STATX_ATIME 0x00000020U
#define FS_STATX_BTIME 0x00000800U
#define FS_STATX_CTIME 0x00000080U
#define FS_STATX_INO 0x00000100U
#define FS_STATX_MODE 0x00000002U
#define FS_STATX_MTIME 0x00000040U
#define FS_STATX_NLINK 0x00000004U
#define FS_STATX_SIZE 0x00000200U
#define FS_STATX_TYPE 0x00000001U
#define FS_STATX_BASIC_STATS (FS_STATX_INO | \
  FS_STATX_MODE  | FS_STATX_NLINK | FS_STATX_SIZE | \
  FS_STATX_ATIME | FS_STATX_MTIME | FS_STATX_CTIME)
#define FS_STATX_ALL (FS_STATX_BASIC_STATS | FS_STATX_BTIME)
#define FS_S_IFDIR 0040000
#define FS_S_IFLNK 0120000
#define FS_S_IFMT 0170000
#define FS_S_IFREG 0100000
#define FS_UTIME_NOW (((FS_LONG)1 << 30) - (FS_LONG)1)
#define FS_UTIME_OMIT (((FS_LONG)1 << 30) - (FS_LONG)2)
#define FS_XATTR_CREATE 0x1
#define FS_XATTR_LIST_MAX 65536
#define FS_XATTR_NAME_MAX 255
#define FS_XATTR_REPLACE 0x2
#define FS_XATTR_SIZE_MAX 65536

#define FS_EACCES 13
#define FS_EBADF 9
#define FS_EBUSY 16
#define FS_EEXIST 17
#define FS_EFBIG 27
#define FS_EINVAL 22
#define FS_EISDIR 21
#define FS_ELOOP 40
#define FS_ENAMETOOLONG 36
#define FS_ENODATA 61
#define FS_ENODEV 19
#define FS_ENOENT 2
#define FS_ENOMEM 12
#define FS_ENOTDIR 20
#define FS_ENOTEMPTY 39
#define FS_EOPNOTSUPP 95
#define FS_EOVERFLOW 75
#define FS_EPERM 1
#define FS_ERANGE 34

#define FS_S_ISREG(mode) ((mode & FS_S_IFMT) == FS_S_IFREG)
#define FS_S_ISDIR(mode) ((mode & FS_S_IFMT) == FS_S_IFDIR)
#define FS_S_ISLNK(mode) ((mode & FS_S_IFMT) == FS_S_IFLNK)

#define FS_IFTODT(mode) ((mode & FS_S_IFMT) >> 12)

typedef unsigned FS_LONG fs_dev_t;
typedef unsigned FS_LONG fs_ino_t;
typedef unsigned int fs_mode_t;
typedef unsigned FS_LONG fs_nlink_t;
typedef FS_LONG fs_off_t;
typedef unsigned FS_LONG fs_size_t;
typedef FS_LONG fs_ssize_t;
typedef FS_LONG fs_time_t;

struct fs_dirent {
  unsigned FS_LONG d_ino;
  unsigned FS_LONG d_off;
  unsigned short d_reclen;
  char d_name[];
};

struct fs_iovec {
  void* iov_base;
  fs_size_t iov_len;
};

struct fs_timespec {
  fs_time_t tv_sec;
  FS_LONG tv_nsec;
};

struct fs_stat {
  fs_ino_t st_ino;
  fs_mode_t st_mode;
  fs_nlink_t st_nlink;
  fs_off_t st_size;
  struct fs_timespec st_atim;
  struct fs_timespec st_mtim;
  struct fs_timespec st_ctim;
};

struct fs_statx {
  int stx_mask;
  fs_ino_t stx_ino;
  fs_mode_t stx_mode;
  fs_nlink_t stx_nlink;
  fs_off_t stx_size;
  struct fs_timespec stx_atime;
  struct fs_timespec stx_mtime;
  struct fs_timespec stx_ctime;
  struct fs_timespec stx_btime;
};

struct fs_timeval {
  fs_time_t tv_sec;
  FS_LONG tv_usec;
};

struct fs_utimbuf {
  fs_time_t actime;
  fs_time_t modtime;
};

#undef FS_LONG

#endif