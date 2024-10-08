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

declare module "@daisydogs07/filesystem" {
  type NumberLike = number | BigInt;
  type DataLike = string | Buffer;
  type Zero = 0 | 0n;

  interface Dirent {
    d_ino: BigInt;
    d_off: BigInt;
    d_name: string;
    d_type: number;
  }
  interface TimeSpec {
    tv_sec: number;
    tv_nsec: number;
  }
  interface Stats {
    st_ino: BigInt;
    st_mode: number;
    st_nlink: BigInt;
    st_size: BigInt;
    st_atim: TimeSpec;
    st_mtim: TimeSpec;
    st_ctim: TimeSpec;
  }
  interface Statx {
    stx_ino: BigInt;
    stx_mode: number;
    stx_nlink: BigInt;
    stx_size: BigInt;
    stx_atime: TimeSpec;
    stx_mtime: TimeSpec;
    stx_ctime: TimeSpec;
    stx_btime: TimeSpec;
  }

  class FileSystem {
    static AT_EMPTY_PATH: number;
    static AT_FDCWD: number;
    static AT_REMOVEDIR: number;
    static AT_SYMLINK_FOLLOW: number;
    static AT_SYMLINK_NOFOLLOW: number;
    static DT_REG: number;
    static DT_DIR: number;
    static DT_LNK: number;
    static FALLOC_FL_COLLAPSE_RANGE: number;
    static FALLOC_FL_INSERT_RANGE: number;
    static FALLOC_FL_KEEP_SIZE: number;
    static FALLOC_FL_PUNCH_HOLE: number;
    static FALLOC_FL_ZERO_RANGE: number;
    static O_ACCMODE: number;
    static O_APPEND: number;
    static O_CREAT: number;
    static O_DIRECTORY: number;
    static O_EXCL: number;
    static O_NOATIME: number;
    static O_NOFOLLOW: number;
    static O_RDONLY: number;
    static O_RDWR: number;
    static O_TMPFILE: number;
    static O_TRUNC: number;
    static O_WRONLY: number;
    static F_OK: number;
    static R_OK: number;
    static W_OK: number;
    static X_OK: number;
    static NAME_MAX: number;
    static PATH_MAX: number;
    static RENAME_EXCHANGE: number;
    static RENAME_NOREPLACE: number;
    static SEEK_CUR: number;
    static SEEK_DATA: number;
    static SEEK_END: number;
    static SEEK_HOLE: number;
    static SEEK_SET: number;
    static STATX_ATIME: number;
    static STATX_BTIME: number;
    static STATX_CTIME: number;
    static STATX_INO: number;
    static STATX_MODE: number;
    static STATX_MTIME: number;
    static STATX_NLINK: number;
    static STATX_SIZE: number;
    static STATX_TYPE: number;
    static STATX_BASIC_STATS: number;
    static STATX_ALL: number;
    static S_IFDIR: number;
    static S_IFLNK: number;
    static S_IFMT: number;
    static S_IFREG: number;
    static S_IRUSR: number;
    static S_IRGRP: number;
    static S_IROTH: number;
    static S_IWUSR: number;
    static S_IWGRP: number;
    static S_IWOTH: number;
    static S_IXUSR: number;
    static S_IXGRP: number;
    static S_IXOTH: number;
    static S_IRWXU: number;
    static S_IRWXG: number;
    static S_IRWXO: number;
    static UTIME_NOW: number;
    static UTIME_OMIT: number;
    static XATTR_CREATE: number;
    static XATTR_LIST_MAX: number;
    static XATTR_NAME_MAX: number;
    static XATTR_REPLACE: number;
    static XATTR_SIZE_MAX: number;

    static EACCES: number;
    static EBADF: number;
    static EBUSY: number;
    static EEXIST: number;
    static EFBIG: number;
    static EINVAL: number;
    static EISDIR: number;
    static ELOOP: number;
    static ENAMETOOLONG: number;
    static ENODATA: number;
    static ENODEV: number;
    static ENOENT: number;
    static ENOMEM: number;
    static ENOTDIR: number;
    static ENOTEMPTY: number;
    static EOPNOTSUPP: number;
    static EOVERFLOW: number;
    static EPERM: number;
    static ERANGE: number;

    constructor();
    faccessat2(dirFd: NumberLike, path: string, mode: NumberLike, flags: NumberLike): void;
    faccessat(dirFd: NumberLike, path: string, mode: NumberLike): void;
    access(path: string, mode: NumberLike): void;
    openat(dirFd: NumberLike, path: string, flags: NumberLike, mode: NumberLike): number;
    open(path: string, flags: NumberLike, mode: NumberLike): number;
    creat(path: string, mode: NumberLike): number;
    close(fd: NumberLike): void;
    close_range(fd: NumberLike, maxFd: NumberLike): void;
    mknodat(dirFd: NumberLike, path: string, mode: NumberLike): void;
    mknod(path: string, mode: NumberLike): void;
    mkdirat(dirFd: NumberLike, path: string, mode: NumberLike): void;
    mkdir(path: string, mode: NumberLike): void;
    symlinkat(target: string, dirFd: NumberLike, path: string): void;
    symlink(target: string, path: string): void;
    readlinkat(dirFd: NumberLike, path: string): string;
    readlink(path: string): string;
    getdents(fd: NumberLike, count?: NumberLike): Dirent[];
    linkat(
      oldDirFd: NumberLike,
      oldPath: string,
      newDirFd: NumberLike,
      newPath: string,
      flags: NumberLike
    ): void;
    link(oldPath: string, newPath: string): void;
    unlinkat(dirFd: NumberLike, path: string, flags: NumberLike): void;
    unlink(path: string): void;
    rmdir(path: string): void;
    renameat2(
      oldDirFd: NumberLike,
      oldPath: string,
      newDirFd: NumberLike,
      newPath: string,
      flags: NumberLike
    ): void;
    renameat(oldDirFd: NumberLike, oldPath: string, newDirFd: NumberLike, newPath: string): void;
    rename(oldPath: string, newPath: string): void;
    fallocate(fd: NumberLike, mode: NumberLike, offset: NumberLike, len: NumberLike): void;
    lseek(fd: NumberLike, offset: NumberLike, whence: NumberLike): BigInt;
    read(fd: NumberLike, count: NumberLike): Buffer;
    readv(fd: NumberLike, iov: Buffer[]): BigInt;
    pread(fd: NumberLike, count: NumberLike, offset: NumberLike): Buffer;
    preadv(fd: NumberLike, iov: Buffer[], offset: NumberLike): BigInt;
    write(fd: NumberLike, buffer: DataLike, count?: NumberLike): BigInt;
    writev(fd: NumberLike, iov: Buffer[]): BigInt;
    pwrite(fd: NumberLike, buffer: DataLike, offset: NumberLike, count?: NumberLike): BigInt;
    pwritev(fd: NumberLike, iov: Buffer[], offset: NumberLike): BigInt;
    sendfile(
      outFd: NumberLike,
      inFd: NumberLike,
      offset: NumberLike | null,
      count: NumberLike
    ): BigInt;
    ftruncate(fd: NumberLike, length: NumberLike): void;
    truncate(path: string, length: NumberLike): void;
    fchmodat(dirFd: NumberLike, path: string, mode: NumberLike): void;
    fchmod(fd: NumberLike, mode: NumberLike): void;
    chmod(path: string, mode: NumberLike): void;
    chdir(path: string): void;
    getcwd(): string;
    fstat(fd: NumberLike): Stats;
    stat(path: string): Stats;
    lstat(path: string): Stats;
    statx(dirFd: NumberLike, path: string, flags: NumberLike): Statx;
    getxattr(path: string, name: string, size: NumberLike): Buffer;
    getxattr(path: string, name: string, size: Zero): boolean;
    lgetxattr(path: string, name: string, size: NumberLike): Buffer;
    lgetxattr(path: string, name: string, size: Zero): boolean;
    fgetxattr(fd: NumberLike, name: string, size: NumberLike): Buffer;
    fgetxattr(fd: NumberLike, name: string, size: Zero): boolean;
    setxattr(path: string, name: string, data: DataLike, flags: NumberLike): void;
    setxattr(path: string, name: string, data: DataLike, size: NumberLike, flags: NumberLike): void;
    lsetxattr(path: string, name: string, data: DataLike, flags: NumberLike): void;
    lsetxattr(
      path: string,
      name: string,
      data: DataLike,
      size: NumberLike,
      flags: NumberLike
    ): void;
    fsetxattr(fd: NumberLike, name: string, data: DataLike, flags: NumberLike): void;
    fsetxattr(
      fd: NumberLike,
      name: string,
      data: DataLike,
      size: NumberLike,
      flags: NumberLike
    ): void;
    removexattr(path: string, name: string): void;
    lremovexattr(path: string, name: string): void;
    fremovexattr(fd: NumberLike, name: string): void;
    listxattr(path: string): string[];
    llistxattr(path: string): string[];
    flistxattr(fd: NumberLike): string[];
    utimensat(dirFd: NumberLike, path: string, times: NumberLike[], flags: NumberLike): void;
    futimesat(dirFd: NumberLike, path: string, times: NumberLike[]): void;
    utimes(path: string, times: NumberLike[]): void;
    utime(path: string, times: NumberLike[]): void;
    umask(mask: NumberLike): number;
    dumpTo(path: string): void;
    static loadFrom(path: string): FileSystem;
  }

  export = FileSystem;
}