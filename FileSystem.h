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

#include "fsdef.h"

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
    return FAccessAt2(dirFd, path, mode, FS_F_OK);
  }
  int Access(const char* path, int mode) {
    return FAccessAt2(FS_AT_FDCWD, path, mode, FS_F_OK);
  }
  int OpenAt(int dirFd, const char* path, int flags, fs_mode_t mode);
  int Open(const char* path, int flags, fs_mode_t mode) {
    return OpenAt(FS_AT_FDCWD, path, flags, mode);
  }
  int Creat(const char* path, fs_mode_t mode) {
    return OpenAt(FS_AT_FDCWD, path, FS_O_CREAT | FS_O_WRONLY | FS_O_TRUNC, mode);
  }
  int Close(unsigned int fd);
  int CloseRange(unsigned int fd, unsigned int maxFd, unsigned int flags);
  int MkNodAt(int dirFd, const char* path, fs_mode_t mode, fs_dev_t dev);
  int MkNod(const char* path, fs_mode_t mode, fs_dev_t dev) {
    return MkNodAt(FS_AT_FDCWD, path, mode, dev);
  }
  int MkDirAt(int dirFd, const char* path, fs_mode_t mode);
  int MkDir(const char* path, fs_mode_t mode) {
    return MkDirAt(FS_AT_FDCWD, path, mode);
  }
  int SymLinkAt(const char* oldPath, int newDirFd, const char* newPath);
  int SymLink(const char* oldPath, const char* newPath) {
    return SymLinkAt(oldPath, FS_AT_FDCWD, newPath);
  }
  int ReadLinkAt(int dirFd, const char* path, char* buf, int bufLen);
  int ReadLink(const char* path, char* buf, int bufLen) {
    return ReadLinkAt(FS_AT_FDCWD, path, buf, bufLen);
  }
  int GetDents(unsigned int fdNum, struct fs_dirent* dirp, unsigned int count);
  int LinkAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, int flags);
  int Link(const char* oldPath, const char* newPath) {
    return LinkAt(FS_AT_FDCWD, oldPath, FS_AT_FDCWD, newPath, 0);
  }
  int UnlinkAt(int dirFd, const char* path, int flags);
  int Unlink(const char* path) {
    return UnlinkAt(FS_AT_FDCWD, path, 0);
  }
  int RmDir(const char* path) {
    return UnlinkAt(FS_AT_FDCWD, path, FS_AT_REMOVEDIR);
  }
  int RenameAt2(
    int oldDirFd,
    const char* oldPath,
    int newDirFd,
    const char* newPath,
    unsigned int flags
  );
  int RenameAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath) {
    return RenameAt2(oldDirFd, oldPath, newDirFd, newPath, 0);
  }
  int Rename(const char* oldPath, const char* newPath) {
    return RenameAt2(FS_AT_FDCWD, oldPath, FS_AT_FDCWD, newPath, 0);
  }
  int FAllocate(int fdNum, int mode, fs_off_t offset, fs_off_t len);
  fs_off_t LSeek(unsigned int fdNum, fs_off_t offset, unsigned int whence);
  fs_ssize_t Read(unsigned int fdNum, char* buf, fs_size_t count);
  fs_ssize_t Readv(unsigned int fdNum, struct fs_iovec* iov, int iovcnt);
  fs_ssize_t PRead(unsigned int fdNum, char* buf, fs_size_t count, fs_off_t offset);
  fs_ssize_t PReadv(unsigned int fdNum, struct fs_iovec* iov, int iovcnt, fs_off_t offset);
  fs_ssize_t Write(unsigned int fdNum, const char* buf, fs_size_t count);
  fs_ssize_t Writev(unsigned int fdNum, struct fs_iovec* iov, int iovcnt);
  fs_ssize_t PWrite(unsigned int fdNum, const char* buf, fs_size_t count, fs_off_t offset);
  fs_ssize_t PWritev(unsigned int fdNum, struct fs_iovec* iov, int iovcnt, fs_off_t offset);
  fs_ssize_t SendFile(unsigned int outFd, unsigned int inFd, fs_off_t* offset, fs_size_t count);
  int FTruncate(unsigned int fdNum, fs_off_t length);
  int Truncate(const char* path, fs_off_t length);
  int FChModAt(int dirFd, const char* path, fs_mode_t mode);
  int FChMod(unsigned int fdNum, fs_mode_t mode);
  int ChMod(const char* path, fs_mode_t mode) {
    return FChModAt(FS_AT_FDCWD, path, mode);
  }
  int ChDir(const char* path);
  int GetCwd(char* buf, fs_size_t size);
  int FStat(unsigned int fdNum, struct fs_stat* buf);
  int Stat(const char* path, struct fs_stat* buf);
  int LStat(const char* path, struct fs_stat* buf);
  int Statx(int dirFd, const char* path, int flags, int mask, struct fs_statx* buf);
  int GetXAttr(const char* path, const char* name, void* value, fs_size_t size);
  int LGetXAttr(const char* path, const char* name, void* value, fs_size_t size);
  int FGetXAttr(int fdNum, const char* name, void* value, fs_size_t size);
  int SetXAttr(const char* path, const char* name, void* value, fs_size_t size, int flags);
  int LSetXAttr(const char* path, const char* name, void* value, fs_size_t size, int flags);
  int FSetXAttr(int fdNum, const char* name, void* value, fs_size_t size, int flags);
  int RemoveXAttr(const char* path, const char* name);
  int LRemoveXAttr(const char* path, const char* name);
  int FRemoveXAttr(int fdNum, const char* name);
  fs_ssize_t ListXAttr(const char* path, char* list, fs_size_t size);
  fs_ssize_t LListXAttr(const char* path, char* list, fs_size_t size);
  fs_ssize_t FListXAttr(int fdNum, char* list, fs_size_t size);
  int UTimeNsAt(int dirFd, const char* path, const struct fs_timespec* times, int flags);
  int FUTimesAt(unsigned int fdNum, const char* path, const struct fs_timeval* times);
  int UTimes(const char* path, const struct fs_timeval* times) {
    return FUTimesAt(FS_AT_FDCWD, path, times);
  }
  int UTime(const char* path, const struct fs_utimbuf* times) {
    struct fs_timeval ts[2];
    if (times) {
      ts[0].tv_sec = times->actime;
      ts[0].tv_usec = 0;
      ts[1].tv_sec = times->modtime;
      ts[1].tv_usec = 0;
    }
    return FUTimesAt(FS_AT_FDCWD, path, times ? ts : FS_NULL);
  }
  int UMask(int mask);

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
   *     attribCount
   *     attribs (if attribCount is not zero):
   *       name
   *       size
   *       data (if size is not zero)
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