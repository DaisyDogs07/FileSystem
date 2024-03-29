// Copyright (c) 2024 DaisyDogs07
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to
// do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once
#ifndef __linux__
#error FileSystem is only available in Linux
#endif
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

struct linux_dirent {
  unsigned long	d_ino;
  unsigned long	d_off;
  unsigned short d_reclen;
  char d_name[1];
};

class FileSystem {
 public:
  static FileSystem* New();
  FileSystem() = delete;
  FileSystem(const FileSystem&) = delete;
  FileSystem& operator=(const FileSystem&) = delete;
  FileSystem(FileSystem&&) = delete;
  FileSystem& operator=(FileSystem&&) = delete;
  ~FileSystem();

  int FAccessAt2(int dirFd, const char* path, int mode, int flags);
  int FAccessAt(int dirFd, const char* path, int mode) {
    return FAccessAt2(dirFd, path, mode, F_OK);
  }
  int Access(const char* path, int mode) {
    return FAccessAt2(AT_FDCWD, path, mode, F_OK);
  }
  int OpenAt(int dirFd, const char* path, int flags, mode_t mode);
  int Open(const char* path, int flags, mode_t mode) {
    return OpenAt(AT_FDCWD, path, flags, mode);
  }
  int Creat(const char* path, mode_t mode) {
    return OpenAt(AT_FDCWD, path, O_CREAT | O_WRONLY | O_TRUNC, mode);
  }
  int Close(unsigned int fd);
  int CloseRange(unsigned int fd, unsigned int maxFd, unsigned int flags);
  int MkNodAt(int dirFd, const char* path, mode_t mode, dev_t dev);
  int MkNod(const char* path, mode_t mode, dev_t dev) {
    return MkNodAt(AT_FDCWD, path, mode, dev);
  }
  int MkDirAt(int dirFd, const char* path, mode_t mode);
  int MkDir(const char* path, mode_t mode) {
    return MkDirAt(AT_FDCWD, path, mode);
  }
  int SymLinkAt(const char* oldPath, int newDirFd, const char* newPath);
  int SymLink(const char* oldPath, const char* newPath) {
    return SymLinkAt(oldPath, AT_FDCWD, newPath);
  }
  int ReadLinkAt(int dirFd, const char* path, char* buf, int bufLen);
  int ReadLink(const char* path, char* buf, int bufLen) {
    return ReadLinkAt(AT_FDCWD, path, buf, bufLen);
  }
  int GetDents(unsigned int fdNum, struct linux_dirent* dirp, unsigned int count);
  int LinkAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, int flags);
  int Link(const char* oldPath, const char* newPath) {
    return LinkAt(AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
  }
  int UnlinkAt(int dirFd, const char* path, int flags);
  int Unlink(const char* path) {
    return UnlinkAt(AT_FDCWD, path, 0);
  }
  int RmDir(const char* path) {
    return UnlinkAt(AT_FDCWD, path, AT_REMOVEDIR);
  }
  int RenameAt2(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, unsigned int flags);
  int RenameAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath) {
    return RenameAt2(oldDirFd, oldPath, newDirFd, newPath, 0);
  }
  int Rename(const char* oldPath, const char* newPath) {
    return RenameAt2(AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
  }
  off_t LSeek(unsigned int fdNum, off_t offset, unsigned int whence);
  ssize_t Read(unsigned int fdNum, char* buf, size_t count);
  ssize_t Readv(unsigned int fdNum, struct iovec* iov, int iovcnt);
  ssize_t PRead(unsigned int fdNum, char* buf, size_t count, off_t offset);
  ssize_t PReadv(unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset);
  ssize_t Write(unsigned int fdNum, const char* buf, size_t count);
  ssize_t Writev(unsigned int fdNum, struct iovec* iov, int iovcnt);
  ssize_t PWrite(unsigned int fdNum, const char* buf, size_t count, off_t offset);
  ssize_t PWritev(unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset);
  ssize_t SendFile(unsigned int outFd, unsigned int inFd, off_t* offset, size_t count);
  int FTruncate(unsigned int fdNum, off_t length);
  int Truncate(const char* path, off_t length);
  int FChModAt(int dirFd, const char* path, mode_t mode);
  int FChMod(unsigned int fdNum, mode_t mode);
  int ChMod(const char* path, mode_t mode) {
    return FChModAt(AT_FDCWD, path, mode);
  }
  int ChDir(const char* path);
  int GetCwd(char* buf, size_t size);
  int FStat(unsigned int fdNum, struct stat* buf);
  int Stat(const char* path, struct stat* buf);
  int LStat(const char* path, struct stat* buf);
  int Statx(int dirFd, const char* path, int flags, int mask, struct statx* buf);
  int UTimeNsAt(int dirFd, const char* path, const struct timespec* times, int flags);
  int FUTimesAt(unsigned int fdNum, const char* path, const struct timeval* times);
  int UTimes(const char* path, const struct timeval* times) {
    return FUTimesAt(AT_FDCWD, path, times);
  }
  int UTime(const char* path, const struct utimbuf* times) {
    struct timeval ts[2];
    if (times) {
      ts[0].tv_sec = times->actime;
      ts[0].tv_usec = 0;
      ts[1].tv_sec = times->modtime;
      ts[1].tv_usec = 0;
    }
    return FUTimesAt(AT_FDCWD, path, times ? ts : NULL);
  }

  /**
   * format:
   *   magic number ("\x7FVFS")
   *   inodeCount
   *   inodes:
   *     id
   *     size
   *     nlink
   *     mode
   *     btime
   *     ctime
   *     mtime
   *     atime
   *     target (if symlink)
   *     data (if symlink)
   *     dentCount (if directory)
   *     parent (if directory)
   *     dents (if directory):
   *       inode index
   *       name
   *     dataRangeCount (if regular)
   *     dataRanges (if regular):
   *       offset
   *       size
   *       data
   */
  bool DumpToFile(const char* filename);
  static FileSystem* LoadFromFile(const char* filename);

 private:
  void* data;
};