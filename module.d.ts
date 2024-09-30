declare module "@daisydogs07/filesystem" {
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
    static FALLOC_FL_KEEP_SIZE: number;
    static FALLOC_FL_PUNCH_HOLE: number;
    static FALLOC_FL_COLLAPSE_RANGE: number;
    static FALLOC_FL_ZERO_RANGE: number;
    static FALLOC_FL_INSERT_RANGE: number;
    static O_APPEND: number;
    static O_CREAT: number;
    static O_DIRECTORY: number;
    static O_EXCL: number;
    static O_NOATIME: number;
    static O_NOFOLLOW: number;
    static O_TRUNC: number;
    static O_TMPFILE: number;
    static O_RDONLY: number;
    static O_WRONLY: number;
    static O_RDWR: number;
    static RENAME_EXCHANGE: number;
    static RENAME_NOREPLACE: number;
    static S_IFMT: number;
    static S_IFREG: number;
    static S_IFDIR: number;
    static S_IFLNK: number;
    static S_IRWXU: number;
    static S_IRUSR: number;
    static S_IWUSR: number;
    static S_IXUSR: number;
    static S_IRWXG: number;
    static S_IRGRP: number;
    static S_IWGRP: number;
    static S_IXGRP: number;
    static S_IRWXO: number;
    static S_IROTH: number;
    static S_IWOTH: number;
    static S_IXOTH: number;
    static SEEK_SET: number;
    static SEEK_CUR: number;
    static SEEK_END: number;
    static SEEK_DATA: number;
    static SEEK_HOLE: number;
    static STATX_ALL: number;
    static STATX_ATIME: number;
    static STATX_BASIC_STATS: number;
    static STATX_BLOCKS: number;
    static STATX_BTIME: number;
    static STATX_CTIME: number;
    static STATX_INO: number;
    static STATX_MODE: number;
    static STATX_MTIME: number;
    static STATX_NLINK: number;
    static STATX_SIZE: number;
    static STATX_TYPE: number;
    static UTIME_NOW: number;
    static UTIME_OMIT: number;
    static R_OK: number;
    static W_OK: number;
    static X_OK: number;
    static F_OK: number;

    static EPERM: number;
    static ENOENT: number;
    static EIO: number;
    static EBADF: number;
    static ENOMEM: number;
    static EACCES: number;
    static EBUSY: number;
    static EEXIST: number;
    static ENODEV: number;
    static ENOTDIR: number;
    static EISDIR: number;
    static EINVAL: number;
    static EFBIG: number;
    static ERANGE: number;
    static ENAMETOOLONG: number;
    static ENOTEMPTY: number;
    static ELOOP: number;
    static EOVERFLOW: number;
    static EOPNOTSUPP: number;

    constructor();
    faccessat2(
      dirFd: number | BigInt,
      path: string,
      mode: number | BigInt,
      flags: number | BigInt
    ): void;
    faccessat(dirFd: number | BigInt, path: string, mode: number | BigInt): void;
    access(path: string, mode: number | BigInt): void;
    openat(
      dirFd: number | BigInt,
      path: string,
      flags: number | BigInt,
      mode: number | BigInt
    ): number;
    open(path: string, flags: number | BigInt, mode: number | BigInt): number;
    creat(path: string, mode: number | BigInt): number;
    close(fd: number | BigInt): void;
    close_range(fd: number | BigInt, maxFd: number | BigInt): void;
    mknodat(dirFd: number | BigInt, path: string, mode: number | BigInt): void;
    mknod(path: string, mode: number | BigInt): void;
    mkdirat(dirFd: number | BigInt, path: string, mode: number | BigInt): void;
    mkdir(path: string, mode: number | BigInt): void;
    symlinkat(target: string, dirFd: number | BigInt, path: string): void;
    symlink(target: string, path: string): void;
    readlinkat(dirFd: number | BigInt, path: string): string;
    readlink(path: string): string;
    getdents(fd: number | BigInt, count?: number | BigInt): Dirent[];
    linkat(
      oldDirFd: number | BigInt,
      oldPath: string,
      newDirFd: number | BigInt,
      newPath: string,
      flags: number | BigInt
    ): void;
    link(oldPath: string, newPath: string): void;
    unlinkat(dirFd: number | BigInt, path: string, flags: number | BigInt): void;
    unlink(path: string): void;
    rmdir(path: string): void;
    renameat2(
      oldDirFd: number | BigInt,
      oldPath: string,
      newDirFd: number | BigInt,
      newPath: string,
      flags: number | BigInt
    ): void;
    renameat(
      oldDirFd: number | BigInt,
      oldPath: string,
      newDirFd: number | BigInt,
      newPath: string
    ): void;
    rename(oldPath: string, newPath: string): void;
    fallocate(
      fd: number | BigInt,
      mode: number | BigInt,
      offset: number | BigInt,
      len: number | BigInt
    ): void;
    lseek(fd: number | BigInt, offset: number | BigInt, whence: number | BigInt): BigInt;
    read(fd: number | BigInt, count: number | BigInt): Buffer;
    readv(fd: number | BigInt, iov: Buffer[]): BigInt;
    pread(fd: number | BigInt, count: number | BigInt, offset: number | BigInt): Buffer;
    preadv(fd: number | BigInt, iov: Buffer[], offset: number | BigInt): BigInt;
    write(fd: number | BigInt, buffer: string | Buffer, count?: number | BigInt): BigInt;
    writev(fd: number | BigInt, iov: Buffer[]): BigInt;
    pwrite(
      fd: number | BigInt,
      buffer: string | Buffer,
      offset: number | BigInt,
      count?: number | BigInt
    ): BigInt;
    pwritev(fd: number | BigInt, iov: Buffer[], offset: number | BigInt): BigInt;
    sendfile(
      outFd: number | BigInt,
      inFd: number | BigInt,
      offset: null | number | BigInt,
      count: number | BigInt
    ): BigInt;
    ftruncate(fd: number | BigInt, length: number | BigInt): void;
    truncate(path: string, length: number | BigInt): void;
    fchmodat(dirFd: number | BigInt, path: string, mode: number | BigInt): void;
    fchmod(fd: number | BigInt, mode: number | BigInt): void;
    chmod(path: string, mode: number | BigInt): void;
    chdir(path: string): void;
    getcwd(): string;
    fstat(fd: number | BigInt): Stats;
    stat(path: string): Stats;
    lstat(path: string): Stats;
    statx(dirFd: number | BigInt, path: string, flags: number | BigInt): Statx;
    utimensat(dirFd: number | BigInt, path: string, times: number[], flags: number | BigInt): void;
    futimesat(dirFd: number | BigInt, path: string, times: number[]): void;
    utimes(path: string, times: number[]): void;
    utime(path: string, times: number[]): void;
    umask(mask: number | BigInt): number;
    dumpTo(path: string): void;
    static loadFrom(path: string): FileSystem;
  }

  export = FileSystem;
}