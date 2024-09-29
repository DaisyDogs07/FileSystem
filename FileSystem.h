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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _ATFILE_SOURCE
#define _ATFILE_SOURCE
#endif

#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
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

struct FileSystem {
  void* data;
};

struct FileSystem* FileSystem_New();
void FileSystem_Delete(struct FileSystem* thisArg);

int FileSystem_FAccessAt2(struct FileSystem* thisArg, int dirFd, const char* path, int mode, int flags);
int FileSystem_FAccessAt(struct FileSystem* thisArg, int dirFd, const char* path, int mode);
int FileSystem_Access(struct FileSystem* thisArg, const char* path, int mode);
int FileSystem_OpenAt(struct FileSystem* thisArg, int dirFd, const char* path, int flags, mode_t mode);
int FileSystem_Open(struct FileSystem* thisArg, const char* path, int flags, mode_t mode);
int FileSystem_Creat(struct FileSystem* thisArg, const char* path, mode_t mode);
int FileSystem_Close(struct FileSystem* thisArg, unsigned int fd);
int FileSystem_CloseRange(struct FileSystem* thisArg, unsigned int fd, unsigned int maxFd, unsigned int flags);
int FileSystem_MkNodAt(struct FileSystem* thisArg, int dirFd, const char* path, mode_t mode, dev_t dev);
int FileSystem_MkNod(struct FileSystem* thisArg, const char* path, mode_t mode, dev_t dev);
int FileSystem_MkDirAt(struct FileSystem* thisArg, int dirFd, const char* path, mode_t mode);
int FileSystem_MkDir(struct FileSystem* thisArg, const char* path, mode_t mode);
int FileSystem_SymLinkAt(struct FileSystem* thisArg, const char* oldPath, int newDirFd, const char* newPath);
int FileSystem_SymLink(struct FileSystem* thisArg, const char* oldPath, const char* newPath);
int FileSystem_ReadLinkAt(struct FileSystem* thisArg, int dirFd, const char* path, char* buf, int bufLen);
int FileSystem_ReadLink(struct FileSystem* thisArg, const char* path, char* buf, int bufLen);
int FileSystem_GetDents(struct FileSystem* thisArg, unsigned int fdNum, struct linux_dirent* dirp, unsigned int count);
int FileSystem_LinkAt(struct FileSystem* thisArg, int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, int flags);
int FileSystem_Link(struct FileSystem* thisArg, const char* oldPath, const char* newPath);
int FileSystem_UnlinkAt(struct FileSystem* thisArg, int dirFd, const char* path, int flags);
int FileSystem_Unlink(struct FileSystem* thisArg, const char* path);
int FileSystem_RmDir(struct FileSystem* thisArg, const char* path);
int FileSystem_RenameAt2(struct FileSystem* thisArg, int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, unsigned int flags);
int FileSystem_RenameAt(struct FileSystem* thisArg, int oldDirFd, const char* oldPath, int newDirFd, const char* newPath);
int FileSystem_Rename(struct FileSystem* thisArg, const char* oldPath, const char* newPath);
int FileSystem_FAllocate(struct FileSystem* thisArg, int fdNum, int mode, off_t offset, off_t len);
off_t FileSystem_LSeek(struct FileSystem* thisArg, unsigned int fdNum, off_t offset, unsigned int whence);
ssize_t FileSystem_Read(struct FileSystem* thisArg, unsigned int fdNum, char* buf, size_t count);
ssize_t FileSystem_Readv(struct FileSystem* thisArg, unsigned int fdNum, struct iovec* iov, int iovcnt);
ssize_t FileSystem_PRead(struct FileSystem* thisArg, unsigned int fdNum, char* buf, size_t count, off_t offset);
ssize_t FileSystem_PReadv(struct FileSystem* thisArg, unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset);
ssize_t FileSystem_Write(struct FileSystem* thisArg, unsigned int fdNum, const char* buf, size_t count);
ssize_t FileSystem_Writev(struct FileSystem* thisArg, unsigned int fdNum, struct iovec* iov, int iovcnt);
ssize_t FileSystem_PWrite(struct FileSystem* thisArg, unsigned int fdNum, const char* buf, size_t count, off_t offset);
ssize_t FileSystem_PWritev(struct FileSystem* thisArg, unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset);
ssize_t FileSystem_SendFile(struct FileSystem* thisArg, unsigned int outFd, unsigned int inFd, off_t* offset, size_t count);
int FileSystem_FTruncate(struct FileSystem* thisArg, unsigned int fdNum, off_t length);
int FileSystem_Truncate(struct FileSystem* thisArg, const char* path, off_t length);
int FileSystem_FChModAt(struct FileSystem* thisArg, int dirFd, const char* path, mode_t mode);
int FileSystem_FChMod(struct FileSystem* thisArg, unsigned int fdNum, mode_t mode);
int FileSystem_ChMod(struct FileSystem* thisArg, const char* path, mode_t mode);
int FileSystem_ChDir(struct FileSystem* thisArg, const char* path);
int FileSystem_GetCwd(struct FileSystem* thisArg, char* buf, size_t size);
int FileSystem_FStat(struct FileSystem* thisArg, unsigned int fdNum, struct stat* buf);
int FileSystem_Stat(struct FileSystem* thisArg, const char* path, struct stat* buf);
int FileSystem_LStat(struct FileSystem* thisArg, const char* path, struct stat* buf);
int FileSystem_Statx(struct FileSystem* thisArg, int dirFd, const char* path, int flags, int mask, struct statx* buf);
int FileSystem_UTimeNsAt(struct FileSystem* thisArg, int dirFd, const char* path, const struct timespec* times, int flags);
int FileSystem_FUTimesAt(struct FileSystem* thisArg, unsigned int fdNum, const char* path, const struct timeval* times);
int FileSystem_UTimes(struct FileSystem* thisArg, const char* path, const struct timeval* times);
int FileSystem_UTime(struct FileSystem* thisArg, const char* path, const struct utimbuf* times);

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
bool FileSystem_DumpToFile(struct FileSystem* thisArg, const char* filename);
struct FileSystem* FileSystem_LoadFromFile(const char* filename);