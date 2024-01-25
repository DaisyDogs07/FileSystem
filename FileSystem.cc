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

#include "FileSystem.h"
#include <dirent.h>
#include <mutex>

#define LIKELY(expr) __builtin_expect(!!(expr), 1)
#define UNLIKELY(expr) __builtin_expect(!!(expr), 0)

namespace {
  template<bool I = false, typename T>
  bool TryAlloc(T** ptr, size_t length = 1) {
    T* newPtr = reinterpret_cast<T*>(malloc(sizeof(T) * length));
    if (UNLIKELY(!newPtr))
      return false;
    if constexpr (I)
      for (size_t i = 0; i != length; ++i)
        new (&newPtr[i]) T;
    *ptr = newPtr;
    return true;
  }
  template<typename T>
  bool TryRealloc(T** ptr, size_t length) {
    T* newPtr = reinterpret_cast<T*>(realloc(*ptr, sizeof(T) * length));
    if (UNLIKELY(!newPtr))
      return false;
    *ptr = newPtr;
    return true;
  }

  template<typename R, typename T1, typename T2>
  R min(T1 a, T2 b) {
    if (a < b)
      return a;
    return b;
  }

  struct INode {
    INode() {
      clock_gettime(CLOCK_REALTIME, &btime);
      ctime = mtime = atime = btime;
    }
    ~INode() {
      if (dataRanges) {
        for (off_t i = 0; i != dataRangeCount; ++i)
          delete dataRanges[i];
        delete dataRanges;
      }
      if (target)
        delete target;
      if (dents) {
        while (dentCount > 2)
          delete dents[--dentCount].name;
        delete dents;
      }
    }
    ino_t ndx;
    ino_t id;
    const char* target = NULL;
    struct Dent {
      const char* name;
      struct INode* inode;
    };
    struct Dent* dents = NULL;
    off_t dentCount = 0;
    bool PushDent(const char* name, struct INode* inode) {
      if (UNLIKELY(!TryRealloc(&dents, dentCount + 1)))
        return false;
      dents[dentCount++] = { name, inode };
      size += strlen(name);
      return true;
    }
    void RemoveDent(const char* name) {
      for (off_t i = 0; i != dentCount; ++i)
        if (strcmp(dents[i].name, name) == 0) {
          delete dents[i].name;
          if (i != dentCount - 1)
            memmove(dents + i, dents + i + 1, sizeof(struct Dent) * (dentCount - i));
          dents = reinterpret_cast<struct Dent*>(
            realloc(dents, sizeof(struct Dent) * --dentCount)
          );
          size -= strlen(name);
          break;
        }
    }
    bool IsInSelf(struct INode* inode) {
      for (off_t i = 2; i != dentCount; ++i)
        if (dents[i].inode == inode ||
            (S_ISDIR(dents[i].inode->mode) && dents[i].inode->IsInSelf(inode)))
          return true;
      return false;
    }

    struct DataRange {
      ~DataRange() {
        if (data)
          delete data;
      }
      off_t offset = 0;
      off_t size = 0;
      char* data = NULL;
    };
    struct HoleRange {
      off_t offset = 0;
      off_t size = -1;
    };
    struct DataRange** dataRanges = NULL;
    off_t dataRangeCount = 0;

    class DataIterator {
     public:
      DataIterator(struct INode* inode, off_t offset) {
        inode_ = inode;
        if (inode->dataRangeCount == 0 ||
            offset < inode_->dataRanges[0]->offset) {
          rangeIdx_ = 0;
          atData_ = false;
          isBeforeFirstRange_ = true;
        } else if (offset >= inode_->dataRanges[inode_->dataRangeCount - 1]->offset + inode_->dataRanges[inode_->dataRangeCount - 1]->size) {
          rangeIdx_ = inode_->dataRangeCount - 1;
          atData_ = false;
          isBeforeFirstRange_ = false;
        } else {
          isBeforeFirstRange_ = false;
          off_t low = 0;
          off_t high = inode->dataRangeCount - 1;
          while (low <= high) {
            off_t mid = low + ((high - low) / 2);
            struct DataRange* range = inode->dataRanges[mid];
            if (offset >= range->offset) {
              if (offset < range->offset + range->size) {
                rangeIdx_ = mid;
                atData_ = true;
                break;
              }
              low = mid + 1;
              struct DataRange* nextRange = inode->dataRanges[low];
              if (offset >= range->offset + range->size &&
                  offset < nextRange->offset) {
                rangeIdx_ = mid;
                atData_ = false;
                break;
              }
            } else {
              high = mid - 1;
              struct DataRange* prevRange = inode->dataRanges[high];
              if (offset >= prevRange->offset + prevRange->size &&
                  offset < range->offset) {
                rangeIdx_ = high;
                atData_ = false;
                break;
              }
            }
          }
        }
      }
      bool IsInData() {
        return atData_;
      }
      off_t GetRangeIdx() {
        return rangeIdx_;
      }
      bool BeforeFirstRange() {
        return isBeforeFirstRange_;
      }
      struct DataRange* GetRange() {
        return inode_->dataRanges[rangeIdx_];
      }
      struct HoleRange GetHole() {
        struct HoleRange hole;
        if (isBeforeFirstRange_) {
          if (inode_->dataRangeCount == 0)
            hole.size = inode_->size;
          else hole.size = inode_->dataRanges[0]->offset;
          return hole;
        }
        struct DataRange* currRange = inode_->dataRanges[rangeIdx_];
        hole.offset = currRange->offset + currRange->size;
        if (rangeIdx_ < inode_->dataRangeCount - 1)
          hole.size = inode_->dataRanges[rangeIdx_ + 1]->offset - hole.offset;
        else hole.size = inode_->size - hole.offset;
        return hole;
      }
      bool Next() {
        if (!atData_) {
          if (isBeforeFirstRange_) {
            if (inode_->dataRangeCount == 0)
              return false;
            isBeforeFirstRange_ = false;
          } else if (rangeIdx_ == inode_->dataRangeCount - 1)
            return false;
          else ++rangeIdx_;
        }
        atData_ = !atData_;
        return true;
      }
      void SeekTo(off_t offset) {
        do {
          off_t end;
          if (atData_) {
            struct INode::DataRange* range = GetRange();
            end = range->offset + range->size;
          } else {
            struct INode::HoleRange hole = GetHole();
            end = hole.offset + hole.size;
          }
          if (end >= offset)
            break;
        } while (Next());
      }
     private:
      struct INode* inode_;
      off_t rangeIdx_;
      bool atData_;
      bool isBeforeFirstRange_;
    };

    struct DataRange* InsertRange(off_t offset, off_t length, off_t* index) {
      struct DataRange* range;
      if (dataRangeCount == 0) {
        if (UNLIKELY(!TryAlloc<true>(&range)))
          return NULL;
        if (UNLIKELY(!TryAlloc(&range->data, length)) ||
            UNLIKELY(!TryAlloc(&dataRanges, 1))) {
          delete range;
          return NULL;
        }
        range->offset = offset;
        range->size = length;
        *index = 0;
        dataRanges[0] = range;
        dataRangeCount = 1;
        return range;
      }
      if (offset > dataRanges[dataRangeCount - 1]->offset) {
        if (UNLIKELY(!TryAlloc<true>(&range)))
          return NULL;
        if (UNLIKELY(!TryAlloc(&range->data, length)) ||
            UNLIKELY(!TryRealloc(&dataRanges, dataRangeCount + 1))) {
          delete range;
          return NULL;
        }
        range->offset = offset;
        range->size = length;
        *index = dataRangeCount;
        dataRanges[dataRangeCount++] = range;
        return range;
      }
      off_t rangeIdx;
      {
        off_t low = 0;
        off_t high = dataRangeCount - 1;
        while (low <= high) {
          off_t mid = low + ((high - low) / 2);
          struct DataRange* range2 = dataRanges[mid];
          if (offset >= range2->offset)
            low = mid + 1;
          else {
            high = mid - 1;
            rangeIdx = mid;
          }
        }
      }
      if (UNLIKELY(!TryAlloc<true>(&range)))
        return NULL;
      if (UNLIKELY(!TryAlloc(&range->data, length)) ||
          UNLIKELY(!TryRealloc(&dataRanges, dataRangeCount + 1))) {
        delete range;
        return NULL;
      }
      range->offset = offset;
      range->size = length;
      memmove(dataRanges + rangeIdx + 1, dataRanges + rangeIdx, sizeof(struct DataRange*) * (dataRangeCount - rangeIdx));
      *index = rangeIdx;
      dataRanges[rangeIdx] = range;
      ++dataRangeCount;
      return range;
    }
    void RemoveRange(off_t index) {
      struct DataRange* range = dataRanges[index];
      delete range;
      if (index != dataRangeCount - 1)
        memmove(dataRanges + index, dataRanges + index + 1, sizeof(struct DataRange*) * (dataRangeCount - index));
      dataRanges = reinterpret_cast<struct DataRange**>(
        realloc(dataRanges, sizeof(struct DataRange*) * --dataRangeCount)
      );
    }
    void RemoveRanges(off_t index, off_t count) {
      for (off_t i = index; i != index + count; ++i)
        delete dataRanges[i];
      if (index + count < dataRangeCount)
        memmove(dataRanges + index, dataRanges + (index + count), sizeof(struct DataRange*) * (dataRangeCount - (index + count)));
      dataRanges = reinterpret_cast<struct DataRange**>(
        realloc(dataRanges, sizeof(struct DataRange*) * (dataRangeCount - count))
      );
      dataRangeCount -= count;
    }

    struct DataRange* AllocData(off_t offset, off_t length) {
      off_t rangeIdx;
      bool createdRange = false;
      struct DataRange* range = NULL;
      if (dataRangeCount != 0) {
        DataIterator it(this, offset);
        for (off_t i = it.GetRangeIdx(); i != dataRangeCount; ++i) {
          struct DataRange* range2 = dataRanges[i];
          if (offset + length == range2->offset) {
            struct DataRange* range3 = NULL;
            for (off_t j = i - 1; j >= 0; --j) {
              struct DataRange* range4 = dataRanges[j];
              if (offset <= range4->offset + range4->size) {
                rangeIdx = j;
                range3 = range4;
              } else break;
            }
            if (range3) {
              off_t off = min<off_t>(range3->offset, offset);
              off_t newRangeLength = range2->size + (range2->offset - off);
              if (UNLIKELY(!TryRealloc(&range3->data, newRangeLength)))
                return NULL;
              memmove(range3->data + (newRangeLength - range2->size), range2->data, range2->size);
              range3->size = newRangeLength;
              for (off_t j = rangeIdx + 1; j < i; ++j) {
                struct DataRange* range4 = dataRanges[j];
                memmove(range3->data + (range4->offset - off), range4->data, range4->size);
              }
              RemoveRanges(rangeIdx + 1, i - rangeIdx);
              range3->offset = off;
              return range3;
            } else {
              off_t newRangeLength = range2->size + (range2->offset - offset);
              if (UNLIKELY(!TryRealloc(&range2->data, newRangeLength)))
                return NULL;
              memmove(range2->data + (newRangeLength - range2->size), range2->data, range2->size);
              range2->size = newRangeLength;
              range2->offset = offset;
              return range2;
            }
          } else if (offset + length < range2->offset)
            break;
        }
        if (!it.BeforeFirstRange()) {
          struct DataRange* range2 = it.GetRange();
          if (offset <= range2->offset + range2->size) {
            rangeIdx = it.GetRangeIdx();
            range = range2;
          }
        }
      }
      if (!range) {
        range = InsertRange(offset, length, &rangeIdx);
        if (UNLIKELY(!range))
          return NULL;
        createdRange = true;
      } else if (offset >= range->offset &&
          offset + length <= range->offset + range->size)
        return range;
      off_t newRangeLength = length + (offset - range->offset);
      for (off_t i = rangeIdx + 1; i < dataRangeCount; ++i) {
        struct DataRange* range2 = dataRanges[i];
        if (range2->offset < offset + length) {
          if (newRangeLength < (range2->offset - range->offset) + range2->size) {
            newRangeLength = (range2->offset - range->offset) + range2->size;
            break;
          }
        } else break;
      }
      if (UNLIKELY(!TryRealloc(&range->data, newRangeLength))) {
        if (createdRange)
          RemoveRange(rangeIdx);
        return NULL;
      }
      range->size = newRangeLength;
      if (size < offset + length)
        size = offset + length;
      for (off_t i = rangeIdx + 1; i < dataRangeCount; ++i) {
        struct DataRange* range2 = dataRanges[i];
        if (range2->offset < offset + length)
          memcpy(range->data + (range2->offset - range->offset), range2->data, range2->size);
        else {
          off_t n = i - (rangeIdx + 1);
          if (UNLIKELY(n == 0))
            break;
          RemoveRanges(rangeIdx + 1, n);
          break;
        }
      }
      return range;
    }

    void TruncateData(off_t length) {
      if (length >= size) {
        size = length;
        return;
      }
      size = length;
      if (length == 0) {
        for (off_t i = 0; i != dataRangeCount; ++i)
          delete dataRanges[i];
        delete dataRanges;
        dataRanges = NULL;
        dataRangeCount = 0;
        return;
      }
      for (off_t i = dataRangeCount - 1; i >= 0; --i) {
        struct DataRange* range = dataRanges[i];
        if (length > range->offset) {
          RemoveRanges(i + 1, dataRangeCount - (i + 1));
          if (length - range->offset < range->size) {
            range->size = length - range->offset;
            range->data = reinterpret_cast<char*>(
              realloc(range->data, range->size)
            );
          }
          break;
        }
      }
    }
    off_t size = 0;
    nlink_t nlink = 0;
    mode_t mode;
    struct timespec btime;
    struct timespec ctime;
    struct timespec mtime;
    struct timespec atime;
    bool CanUse(int perms) {
      if (!(mode & perms) &&
          !(mode & (perms << 3)) &&
          !(mode & (perms << 6)))
        return false;
      return true;
    }
    void FillStat(struct stat* buf) {
      memset(buf, '\0', sizeof(struct stat));
      buf->st_ino = id;
      buf->st_mode = mode;
      buf->st_nlink = nlink;
      buf->st_size = size;
      buf->st_atim = atime;
      buf->st_mtim = mtime;
      buf->st_ctim = ctime;
    }
    void FillStatx(struct statx* buf, int mask) {
      memset(buf, '\0', sizeof(struct statx));
      buf->stx_mask = mask & (
        STATX_INO   | STATX_TYPE  | STATX_MODE  |
        STATX_NLINK | STATX_SIZE  | STATX_ATIME |
        STATX_MTIME | STATX_CTIME | STATX_BTIME
      );
      if (mask & STATX_INO)
        buf->stx_ino = id;
      if (mask & STATX_TYPE)
        buf->stx_mode |= mode & S_IFMT;
      if (mask & STATX_MODE)
        buf->stx_mode |= mode & ~S_IFMT;
      if (mask & STATX_NLINK)
        buf->stx_nlink = nlink;
      if (mask & STATX_SIZE)
        buf->stx_size = size;
      if (mask & STATX_ATIME) {
        buf->stx_atime.tv_sec = atime.tv_sec;
        buf->stx_atime.tv_nsec = atime.tv_nsec;
      }
      if (mask & STATX_MTIME) {
        buf->stx_mtime.tv_sec = mtime.tv_sec;
        buf->stx_mtime.tv_nsec = mtime.tv_nsec;
      }
      if (mask & STATX_CTIME) {
        buf->stx_ctime.tv_sec = ctime.tv_sec;
        buf->stx_ctime.tv_nsec = ctime.tv_nsec;
      }
      if (mask & STATX_BTIME) {
        buf->stx_btime.tv_sec = btime.tv_sec;
        buf->stx_btime.tv_nsec = btime.tv_nsec;
      }
    }
  };
  struct Fd {
    struct INode* inode;
    int fd;
    int flags;
    off_t seekOff = 0;
  };
  struct Cwd {
    ~Cwd() {
      if (path)
        delete path;
    }
    const char* path = NULL;
    struct INode* inode;
    struct INode* parent;
  };

  struct FSInternal {
    ~FSInternal() {
      while (inodeCount--)
        delete inodes[inodeCount];
      delete inodes;
      if (fds) {
        while (fdCount--)
          delete fds[fdCount];
        delete fds;
      }
      if (cwd)
        delete cwd;
    }
    struct INode** inodes = NULL;
    ino_t inodeCount = 0;
    struct Fd** fds = NULL;
    int fdCount = 0;
    struct Cwd* cwd = NULL;
    std::mutex mtx;
  };

  bool PushINode(struct FSInternal* fs, struct INode* inode) {
    ino_t id = fs->inodeCount;
    if (fs->inodeCount != 0) {
      ino_t low = 0;
      ino_t high = fs->inodeCount - 1;
      while (low <= high) {
        ino_t mid = low + ((high - low) / 2);
        if (fs->inodes[mid]->id == mid)
          low = mid + 1;
        else {
          high = mid - 1;
          id = mid;
        }
      }
    }
    if (UNLIKELY(!TryRealloc(&fs->inodes, sizeof(struct INode*) * (fs->inodeCount + 1))))
      return false;
    if (id != fs->inodeCount) {
      memmove(fs->inodes + id + 1, fs->inodes + id, sizeof(struct INode*) * (fs->inodeCount - id));
      for (ino_t i = id + 1; i != fs->inodeCount + 1; ++i)
        ++fs->inodes[i]->ndx;
    }
    inode->ndx = id;
    inode->id = id;
    fs->inodes[id] = inode;
    ++fs->inodeCount;
    return true;
  }
  void RemoveINode(struct FSInternal* fs, struct INode* inode) {
    ino_t i = inode->ndx;
    delete inode;
    if (i != fs->inodeCount - 1) {
      memmove(fs->inodes + i, fs->inodes + i + 1, sizeof(struct INode*) * (fs->inodeCount - i));
      do {
        --fs->inodes[i++]->ndx;
      } while (i != fs->inodeCount - 1);
    }
    fs->inodes = reinterpret_cast<struct INode**>(
      realloc(fs->inodes, sizeof(struct INode*) * --fs->inodeCount)
    );
  }
  int PushFd(struct FSInternal* fs, struct INode* inode, int flags) {
    int fdNum = fs->fdCount;
    if (fs->fdCount != 0) {
      int low = 0;
      int high = fs->fdCount - 1;
      while (low <= high) {
        int mid = low + ((high - low) / 2);
        if (fs->fds[mid]->fd == mid)
          low = mid + 1;
        else {
          high = mid - 1;
          fdNum = mid;
        }
      }
    }
    struct Fd* fd;
    if (UNLIKELY(!TryAlloc<true>(&fd)))
      return -ENOMEM;
    if (UNLIKELY(!TryRealloc(&fs->fds, fs->fdCount + 1))) {
      delete fd;
      return -ENOMEM;
    }
    if (fdNum != fs->fdCount)
      memmove(fs->fds + fdNum + 1, fs->fds + fdNum, sizeof(struct Fd*) * (fs->fdCount - fdNum));
    fd->inode = inode;
    fd->flags = flags;
    fd->fd = fdNum;
    fs->fds[fdNum] = fd;
    ++fs->fdCount;
    return fdNum;
  }
  void RemoveFd(struct FSInternal* fs, struct Fd* fd, int i) {
    struct INode* inode = fd->inode;
    if (inode->nlink == 0)
      RemoveINode(fs, inode);
    delete fd;
    if (i != fs->fdCount - 1)
      memmove(fs->fds + i, fs->fds + i + 1, sizeof(struct Fd*) * (fs->fdCount - i));
    if (--fs->fdCount == 0) {
      delete fs->fds;
      fs->fds = NULL;
      return;
    }
    fs->fds = reinterpret_cast<struct Fd**>(
      realloc(fs->fds, sizeof(struct Fd*) * --fs->fdCount)
    );
  }
  int RemoveFd(struct FSInternal* fs, unsigned int fd) {
    if (UNLIKELY(fs->fdCount == 0))
      return -EBADF;
    int low = 0;
    int high = fs->fdCount - 1;
    while (low <= high) {
      int mid = low + ((high - low) / 2);
      struct Fd* f = fs->fds[mid];
      if (f->fd == fd) {
        RemoveFd(fs, f, mid);
        return 0;
      }
      if (f->fd < fd)
        low = mid + 1;
      else high = mid - 1;
    }
    return -EBADF;
  }
  struct Fd* GetFd(struct FSInternal* fs, unsigned int fdNum) {
    if (UNLIKELY(fs->fdCount == 0))
      return NULL;
    int low = 0;
    int high = fs->fdCount - 1;
    while (low <= high) {
      int mid = low + ((high - low) / 2);
      struct Fd* f = fs->fds[mid];
      if (f->fd == fdNum)
        return f;
      if (f->fd < fdNum)
        low = mid + 1;
      else high = mid - 1;
    }
    return NULL;
  }
  int GetINode(
    struct FSInternal* fs,
    const char* path,
    struct INode** inode,
    struct INode** parent = NULL,
    bool followResolved = false,
    int* follow = NULL
  ) {
    int followCount;
    if (follow)
      followCount = *follow;
    else followCount = 0;
    size_t pathLen = strlen(path);
    if (UNLIKELY(pathLen == 0))
      return -ENOENT;
    if (UNLIKELY(pathLen >= PATH_MAX))
      return -ENAMETOOLONG;
    bool isAbsolute = path[0] == '/';
    struct INode* current = isAbsolute
      ? fs->inodes[0]
      : fs->cwd->inode;
    struct INode* currParent = isAbsolute
      ? fs->inodes[0]
      : fs->cwd->parent;
    int err = 0;
    char name[NAME_MAX + 1];
    size_t nameLen = 0;
    for (size_t i = 0; i != pathLen; ++i) {
      if (path[i] == '/') {
        if (nameLen == 0)
          continue;
        if (UNLIKELY(err))
          return err;
        currParent = current;
        if (UNLIKELY(!current->CanUse(X_OK))) {
          err = -EACCES;
          goto resetName;
        }
        {
          off_t j = 0;
          for (; j != current->dentCount; ++j)
            if (strcmp(current->dents[j].name, name) == 0)
              break;
          if (UNLIKELY(j == current->dentCount)) {
            err = -ENOENT;
            goto resetName;
          }
          current = current->dents[j].inode;
        }
        if (S_ISLNK(current->mode)) {
          if (UNLIKELY(followCount++ == 40)) {
            err = -ELOOP;
            goto resetName;
          }
          struct INode* targetParent;
          int res = GetINode(fs, current->target, &current, &targetParent, true, &followCount);
          if (UNLIKELY(res != 0)) {
            err = res;
            goto resetName;
          }
        }
        if (UNLIKELY(!S_ISDIR(current->mode)))
          err = -ENOTDIR;
       resetName:
        name[0] = '\0';
        nameLen = 0;
      } else {
        if (UNLIKELY(nameLen == NAME_MAX))
          return -ENAMETOOLONG;
        name[nameLen++] = path[i];
        name[nameLen] = '\0';
      }
    }
    if (parent)
      *parent = currParent;
    if (UNLIKELY(err))
      return err;
    if (nameLen != 0) {
      if (parent)
        *parent = current;
      if (UNLIKELY(!current->CanUse(X_OK)))
        return -EACCES;
      for (off_t i = 0; i != current->dentCount; ++i)
        if (strcmp(current->dents[i].name, name) == 0) {
          current = current->dents[i].inode;
          goto out;
        }
      return -ENOENT;
    }
   out:
    if (followResolved) {
      if (S_ISLNK(current->mode)) {
        if (UNLIKELY(followCount++ == 40))
          return -ELOOP;
        struct INode* targetParent;
        int res = GetINode(fs, current->target, &current, &targetParent, true, &followCount);
        if (UNLIKELY(res != 0))
          return res;
      }
    }
    *inode = current;
    return 0;
  }
  const char* GetLast(const char* path) {
    int pathLen = strlen(path);
    char name[NAME_MAX + 1];
    int nameNdx = NAME_MAX;
    name[nameNdx--] = '\0';
    int i = pathLen;
    while (path[i] == '/')
      --i;
    while (i >= 0) {
      if (path[i] == '/')
        break;
      name[nameNdx--] = path[i--];
    }
    return strdup(name + nameNdx + 1);
  }
  const char* AbsolutePath(struct FSInternal* fs, const char* path) {
    char absPath[PATH_MAX];
    int absPathLen = 0;
    if (path[0] != '/') {
      int cwdPathLen = strlen(fs->cwd->path);
      if (cwdPathLen != 1) {
        memcpy(absPath, fs->cwd->path, cwdPathLen);
        absPath[cwdPathLen] = '/';
        absPathLen = cwdPathLen + 1;
      } else absPath[absPathLen++] = '/';
    }
    int pathLen = strlen(path);
    for (int i = 0; i != pathLen; ++i) {
      if (path[i] == '/') {
        if (absPathLen != 0 &&
            absPath[absPathLen - 1] != '/')
          absPath[absPathLen++] = '/';
      } else if (path[i] == '.' && absPath[absPathLen - 1] == '/') {
        if (path[i + 1] == '.') {
          if (path[i + 2] == '/' || i + 2 == pathLen) {
            --absPathLen;
            while (absPathLen > 0 && absPath[--absPathLen - 1] != '/');
            if (i + 2 != pathLen)
              ++i;
            ++i;
          }
        } else if (path[i + 1] == '/')
          ++i;
        else if (i + 1 != pathLen)
          absPath[absPathLen++] = '.';
      } else absPath[absPathLen++] = path[i];
    }
    if (absPathLen != 1 && absPath[absPathLen - 1] == '/')
      --absPathLen;
    absPath[absPathLen] = '\0';
    return strdup(absPath);
  }
  const char* GetAbsoluteLast(struct FSInternal* fs, const char* path) {
    const char* absPath = AbsolutePath(fs, path);
    if (UNLIKELY(!absPath))
      return NULL;
    const char* last = GetLast(absPath);
    delete absPath;
    return last;
  }
  int FlagsToPerms(int flags) {
    int perms = F_OK;
    if ((flags & O_ACCMODE) == O_RDONLY)
      perms |= R_OK;
    else if ((flags & O_ACCMODE) == O_WRONLY)
      perms |= W_OK;
    else if ((flags & O_ACCMODE) == O_RDWR)
      perms |= (R_OK | W_OK);
    if (flags & O_TRUNC)
      perms |= W_OK;
    return perms;
  }

  struct DumpedINode {
    ino_t id;
    off_t size;
    nlink_t nlink;
    mode_t mode;
    struct timespec btime;
    struct timespec ctime;
    struct timespec mtime;
    struct timespec atime;
  };

  template<typename T>
  T toLittle(T value) {
#if __BYTE_ORDER == __BIG_ENDIAN
    if constexpr (sizeof(T) == 1)
      return value;
    if constexpr (sizeof(T) == 2)
      return htole16(value);
    if constexpr (sizeof(T) == 4)
      return htole32(value);
    if constexpr (sizeof(T) == 8)
      return htole64(value);
#else
    return value;
#endif
  }
  template<typename T>
  T toHost(T value) {
#if __BYTE_ORDER == __BIG_ENDIAN
    if constexpr (sizeof(T) == 1)
      return value;
    if constexpr (sizeof(T) == 2)
      return le16toh(value);
    if constexpr (sizeof(T) == 4)
      return le32toh(value);
    if constexpr (sizeof(T) == 8)
      return le64toh(value);
#else
    return value;
#endif
  }
}

FileSystem* FileSystem::New() {
  struct FSInternal* data;
  if (UNLIKELY(!TryAlloc<true>(&data)))
    return NULL;
  struct INode* root;
  if (UNLIKELY(!TryAlloc(&data->inodes)) ||
      UNLIKELY(!TryAlloc<true>(&root))) {
    delete data;
    return NULL;
  }
  if (UNLIKELY(!TryAlloc(&root->dents, 2))) {
    delete root;
    delete data;
    return NULL;
  }
  root->mode = 0755 | S_IFDIR;
  root->dents[0] = { ".", root };
  root->dents[1] = { "..", root };
  root->dentCount = root->nlink = 2;
  if (UNLIKELY(!PushINode(data, root))) {
    delete root;
    delete data;
    return NULL;
  }
  if (UNLIKELY(!TryAlloc<true>(&data->cwd)) ||
      UNLIKELY(!(data->cwd->path = strdup("/")))) {
    RemoveINode(data, root);
    delete data;
    return NULL;
  }
  data->cwd->inode = root;
  data->cwd->parent = root;
  FileSystem* fs;
  if (UNLIKELY(!TryAlloc(&fs))) {
    RemoveINode(data, root);
    delete data;
    return NULL;
  }
  fs->data = data;
  return fs;
}
FileSystem::~FileSystem() {
  delete reinterpret_cast<struct FSInternal*>(data);
}
int FileSystem::FAccessAt2(int dirFd, const char* path, int mode, int flags) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(mode & ~(F_OK | R_OK | W_OK | X_OK)) ||
      UNLIKELY(flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) ||
      UNLIKELY(flags & AT_EMPTY_PATH && path[0] != '\0'))
    return -EINVAL;
  struct INode* origCwd = fs->cwd->inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(fd->inode->mode) && !(flags & AT_EMPTY_PATH)))
      return -ENOTDIR;
    fs->cwd->inode = fd->inode;
  }
  struct INode* inode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    inode = fs->cwd->inode;
  else res = GetINode(fs, path, &inode, NULL, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd->inode = origCwd;
  if (UNLIKELY(res != 0))
    return res;
  if (mode != F_OK && !inode->CanUse(mode))
    return -EACCES;
  return 0;
}
int FileSystem::OpenAt(int dirFd, const char* path, int flags, mode_t mode) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(flags & ~(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_APPEND | O_TRUNC | 020000000 | O_DIRECTORY | O_NOFOLLOW | O_NOATIME)))
    return -EINVAL;
  if (flags & 020000000) {
    if (UNLIKELY(!(flags & O_DIRECTORY)) ||
        UNLIKELY(flags & O_CREAT) ||
        UNLIKELY(!(flags & (O_WRONLY | O_RDWR))) ||
        UNLIKELY(mode & ~07777 || mode == 0))
      return -EINVAL;
    mode |= S_IFREG;
  } else if (flags & O_CREAT) {
    if (UNLIKELY(flags & O_DIRECTORY) ||
        UNLIKELY(mode & ~07777))
      return -EINVAL;
    mode |= S_IFREG;
  } else if (UNLIKELY(mode != 0))
    return -EINVAL;
  struct INode* origCwd = fs->cwd->inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(fd->inode->mode)))
      return -ENOTDIR;
    fs->cwd->inode = fd->inode;
  }
  if (flags & O_CREAT && flags & O_EXCL)
    flags |= O_NOFOLLOW;
  if (flags & O_WRONLY && flags & O_RDWR)
    flags &= ~O_RDWR;
  struct INode* inode;
  struct INode* parent = NULL;
  int res = GetINode(fs, path, &inode, &parent, !(flags & O_NOFOLLOW));
  fs->cwd->inode = origCwd;
  if (UNLIKELY(!parent))
    return res;
  if (res == 0) {
    if (flags & O_CREAT) {
      if (UNLIKELY(flags & O_EXCL))
        return -EEXIST;
      if (UNLIKELY(S_ISDIR(inode->mode)))
        return -EISDIR;
    }
    if (flags & O_NOFOLLOW && UNLIKELY(S_ISLNK(inode->mode)))
      return -ELOOP;
    if (!inode->CanUse(FlagsToPerms(flags)))
      return -EACCES;
  } else {
    if (flags & O_CREAT && res == -ENOENT) {
      flags &= ~O_TRUNC;
      const char* name = GetAbsoluteLast(fs, path);
      if (UNLIKELY(!name))
        return -ENOMEM;
      struct INode* x;
      if (UNLIKELY(!TryAlloc<true>(&x))) {
        delete name;
        return -EIO;
      }
      if (UNLIKELY(!PushINode(fs, x))) {
        delete name;
        delete x;
        return -EIO;
      }
      if (UNLIKELY(!parent->PushDent(name, x))) {
        RemoveINode(fs, x);
        delete name;
        return -EIO;
      }
      x->mode = mode;
      x->nlink = 1;
      parent->ctime = parent->mtime = x->btime;
      res = PushFd(fs, x, flags);
      if (UNLIKELY(res < 0)) {
        parent->RemoveDent(name);
        RemoveINode(fs, x);
        delete name;
      }
    }
    return res;
  }
  if (S_ISDIR(inode->mode)) {
    if (flags & 020000000) {
      struct INode* x;
      if (UNLIKELY(!TryAlloc<true>(&x)))
        return -EIO;
      if (UNLIKELY(!PushINode(fs, x))) {
        delete x;
        return -EIO;
      }
      x->mode = (mode & ~S_IFMT) | S_IFREG;
      res = PushFd(fs, x, flags);
      if (UNLIKELY(res < 0))
        RemoveINode(fs, x);
      return res;
    }
    if (UNLIKELY(flags & (O_WRONLY | O_RDWR)))
      return -EISDIR;
  } else {
    if (UNLIKELY(flags & O_DIRECTORY))
      return -ENOTDIR;
    if (flags & O_TRUNC && inode->size != 0)
      inode->TruncateData(0);
  }
  return PushFd(fs, inode, flags);
}
int FileSystem::Close(unsigned int fd) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  return RemoveFd(fs, fd);
}
int FileSystem::CloseRange(unsigned int fd, unsigned int maxFd, unsigned int flags) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(flags != 0) ||
      UNLIKELY(fd > maxFd))
    return -EINVAL;
  for (int i = 0; i != fs->fdCount; ++i)
    if (fs->fds[i]->fd > fd) {
      RemoveFd(fs, fs->fds[i], i);
      for (++i; i != fs->fdCount; ++i) {
        if (fs->fds[i]->fd < maxFd)
          RemoveFd(fs, fs->fds[i], i);
        else break;
      }
      break;
    }
  return 0;
}
int FileSystem::MkNodAt(int dirFd, const char* path, mode_t mode, dev_t dev) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (mode & S_IFMT) {
    if (UNLIKELY(S_ISDIR(mode)))
      return -EPERM;
    if (UNLIKELY(!S_ISREG(mode)))
      return -EINVAL;
  }
  if (UNLIKELY(dev != 0))
    return -EINVAL;
  struct INode* origCwd = fs->cwd->inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(fd->inode->mode)))
      return -ENOTDIR;
    fs->cwd->inode = fd->inode;
  }
  struct INode* inode;
  struct INode* parent = NULL;
  int res = GetINode(fs, path, &inode, &parent);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(!parent) ||
      UNLIKELY(res != -ENOENT))
    return res;
  if (UNLIKELY(res == 0))
    return -EEXIST;
  const char* name = GetAbsoluteLast(fs, path);
  if (UNLIKELY(!name))
    return -ENOMEM;
  struct INode* x;
  if (UNLIKELY(!TryAlloc<true>(&x))) {
    delete name;
    return -EIO;
  }
  if (UNLIKELY(!PushINode(fs, x))) {
    delete name;
    delete x;
    return -EIO;
  }
  if (UNLIKELY(!parent->PushDent(name, x))) {
    RemoveINode(fs, x);
    delete name;
    return -EIO;
  }
  x->mode = (mode & 07777) | S_IFREG;
  x->nlink = 1;
  parent->ctime = parent->mtime = x->btime;
  return 0;
}
int FileSystem::MkDirAt(int dirFd, const char* path, mode_t mode) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct INode* origCwd = fs->cwd->inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(fd->inode->mode)))
      return -ENOTDIR;
    fs->cwd->inode = fd->inode;
  }
  struct INode* inode;
  struct INode* parent = NULL;
  int res = GetINode(fs, path, &inode, &parent);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(!parent) ||
      UNLIKELY(res != -ENOENT))
    return res;
  if (UNLIKELY(res == 0))
    return -EEXIST;
  const char* name = GetAbsoluteLast(fs, path);
  if (UNLIKELY(!name))
    return -ENOMEM;
  struct INode* x;
  if (UNLIKELY(!TryAlloc<true>(&x))) {
    delete name;
    return -EIO;
  }
  if (UNLIKELY(!TryAlloc(&x->dents, 2)) ||
      UNLIKELY(!PushINode(fs, x))) {
    delete name;
    delete x;
    return -EIO;
  }
  if (UNLIKELY(!parent->PushDent(name, x))) {
    RemoveINode(fs, x);
    delete name;
    return -EIO;
  }
  x->mode = (mode & 07777) | S_IFDIR;
  x->nlink = 2;
  x->dents[0] = { ".", x };
  x->dents[1] = { "..", parent };
  x->dentCount = 2;
  ++parent->nlink;
  parent->ctime = parent->mtime = x->btime;
  return 0;
}
int FileSystem::SymLinkAt(const char* oldPath, int newDirFd, const char* newPath) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct Fd* fd;
  if (newDirFd != AT_FDCWD) {
    if (UNLIKELY(!(fd = GetFd(fs, newDirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(fd->inode->mode)))
      return -ENOTDIR;
  }
  struct INode* oldInode;
  int res = GetINode(fs, oldPath, &oldInode);
  if (UNLIKELY(res != 0))
    return res;
  struct INode* origCwd = fs->cwd->inode;
  if (newDirFd != AT_FDCWD)
    fs->cwd->inode = fd->inode;
  struct INode* newInode;
  struct INode* newParent = NULL;
  res = GetINode(fs, newPath, &newInode, &newParent);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(!newParent))
    return res;
  if (UNLIKELY(res == 0))
    return -EEXIST;
  if (UNLIKELY(res != -ENOENT))
    return res;
  const char* name = GetAbsoluteLast(fs, newPath);
  if (UNLIKELY(!name))
    return -ENOMEM;
  struct INode* x;
  if (UNLIKELY(!TryAlloc<true>(&x))) {
    delete name;
    return -EIO;
  }
  size_t oldPathLen = strlen(oldPath);
  struct INode::DataRange* range = x->AllocData(0, oldPathLen);
  if (UNLIKELY(!range) ||
      UNLIKELY(!PushINode(fs, x))) {
    delete name;
    delete x;
    return -EIO;
  }
  memcpy(range->data, oldPath, oldPathLen);
  if (UNLIKELY(!newParent->PushDent(name, x))) {
    RemoveINode(fs, x);
    delete name;
    return -EIO;
  }
  x->mode = 0777 | S_IFLNK;
  x->target = AbsolutePath(fs, oldPath);
  x->nlink = 1;
  x->size = oldPathLen;
  newParent->ctime = newParent->mtime = x->btime;
  return 0;
}
int FileSystem::ReadLinkAt(int dirFd, const char* path, char* buf, int bufLen) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(bufLen <= 0))
    return -EINVAL;
  struct INode* origCwd = fs->cwd->inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(fd->inode->mode)))
      return -ENOTDIR;
    fs->cwd->inode = fd->inode;
  }
  struct INode* inode;
  int res = GetINode(fs, path, &inode);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(res != 0))
    return res;
  if (UNLIKELY(!S_ISLNK(inode->mode)))
    return -EINVAL;
  if (UNLIKELY(inode->size < bufLen))
    bufLen = inode->size;
  memcpy(buf, inode->dataRanges[0]->data, bufLen);
  clock_gettime(CLOCK_REALTIME, &inode->atime);
  return bufLen;
}
int FileSystem::GetDents(unsigned int fdNum, struct linux_dirent* dirp, unsigned int count) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))))
    return -EBADF;
  struct INode* inode = fd->inode;
  if (UNLIKELY(!S_ISDIR(inode->mode)))
    return -ENOTDIR;
  if (fd->seekOff >= inode->dentCount)
    return 0;
  unsigned int nread = 0;
  char* dirpData = reinterpret_cast<char*>(dirp);
  do {
    struct INode::Dent d = inode->dents[fd->seekOff];
    size_t nameLen = strlen(d.name);
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
    unsigned short reclen = ALIGN(__builtin_offsetof(struct linux_dirent, d_name) + nameLen + 2, sizeof(long));
#undef ALIGN
    if (nread + reclen > count)
      break;
    struct linux_dirent* dent = reinterpret_cast<struct linux_dirent*>(dirpData);
    dent->d_ino = d.inode->id;
    dent->d_off = fd->seekOff + 1;
    dent->d_reclen = reclen;
    memcpy(dent->d_name, d.name, nameLen);
    dent->d_name[nameLen] = '\0';
    dirpData[reclen - 1] = (d.inode->mode & S_IFMT) >> 12;
    dirpData += reclen;
    nread += reclen;
  } while (++fd->seekOff != inode->dentCount);
  if (UNLIKELY(nread == 0))
    return -EINVAL;
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  return nread;
}
int FileSystem::LinkAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, int flags) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(flags & ~(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH)) ||
      UNLIKELY(flags & AT_EMPTY_PATH && oldPath[0] != '\0'))
    return -EINVAL;
  struct Fd* oldFd;
  struct Fd* newFd;
  if (oldDirFd != AT_FDCWD || flags & AT_EMPTY_PATH) {
    if (UNLIKELY(!(oldFd = GetFd(fs, oldDirFd))))
      return -EBADF;
    if (!S_ISDIR(oldFd->inode->mode)) {
      if (UNLIKELY(!(flags & AT_EMPTY_PATH)))
        return -ENOTDIR;
    } else if (UNLIKELY(flags & AT_EMPTY_PATH))
      return -EPERM;
  }
  if (newDirFd != AT_FDCWD) {
    if (UNLIKELY(!(newFd = GetFd(fs, newDirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(newFd->inode->mode)))
      return -ENOTDIR;
  }
  struct INode* origCwd = fs->cwd->inode;
  if (oldDirFd != AT_FDCWD)
    fs->cwd->inode = oldFd->inode;
  struct INode* oldInode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    oldInode = fs->cwd->inode;
  else res = GetINode(fs, oldPath, &oldInode, NULL, flags & AT_SYMLINK_FOLLOW);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(res != 0))
    return res;
  if (newDirFd != AT_FDCWD)
    fs->cwd->inode = newFd->inode;
  struct INode* newInode;
  struct INode* newParent = NULL;
  res = GetINode(fs, newPath, &newInode, &newParent);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(!newParent))
    return res;
  if (UNLIKELY(res == 0))
    return -EEXIST;
  if (UNLIKELY(res != -ENOENT))
    return res;
  if (UNLIKELY(S_ISDIR(oldInode->mode)))
    return -EPERM;
  const char* name = GetAbsoluteLast(fs, newPath);
  if (UNLIKELY(!name))
    return -ENOMEM;
  if (UNLIKELY(!newParent->PushDent(name, oldInode))) {
    delete name;
    return -EIO;
  }
  ++oldInode->nlink;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  oldInode->ctime = newParent->ctime = newParent->mtime = ts;
  return 0;
}
int FileSystem::UnlinkAt(int dirFd, const char* path, int flags) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(flags & ~AT_REMOVEDIR))
    return -EINVAL;
  struct INode* origCwd = fs->cwd->inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(fd->inode->mode)))
      return -ENOTDIR;
    fs->cwd->inode = fd->inode;
  }
  struct INode* inode;
  struct INode* parent;
  int res = GetINode(fs, path, &inode, &parent);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(res != 0))
    return res;
  if (flags & AT_REMOVEDIR) {
    if (UNLIKELY(!S_ISDIR(inode->mode)))
      return -ENOTDIR;
    if (UNLIKELY(inode == fs->inodes[0]))
      return -EBUSY;
  } else if (UNLIKELY(S_ISDIR(inode->mode)))
    return -EISDIR;
  for (int i = 0; i != fs->fdCount; ++i)
    if (UNLIKELY(fs->fds[i]->inode == inode))
      return -EBUSY;
  if (flags & AT_REMOVEDIR) {
    const char* last = GetLast(path);
    if (UNLIKELY(!last))
      return -ENOMEM;
    bool isDot = strcmp(last, ".") == 0;
    delete last;
    if (UNLIKELY(isDot))
      return -EINVAL;
    if (UNLIKELY(inode->dentCount != 2))
      return -ENOTEMPTY;
  }
  const char* name = GetAbsoluteLast(fs, path);
  if (UNLIKELY(!name))
    return -ENOMEM;
  parent->RemoveDent(name);
  delete name;
  if (flags & AT_REMOVEDIR)
    --parent->nlink;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (UNLIKELY(--inode->nlink == 0))
    RemoveINode(fs, inode);
  else inode->ctime = ts;
  parent->ctime = parent->mtime = ts;
  return 0;
}
int FileSystem::RenameAt2(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, unsigned int flags) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE)) ||
      UNLIKELY((flags & (RENAME_NOREPLACE | RENAME_EXCHANGE)) == (RENAME_NOREPLACE | RENAME_EXCHANGE)))
    return -EINVAL;
  const char* last = GetLast(oldPath);
  if (UNLIKELY(!last))
    return -ENOMEM;
  bool isDot = strcmp(last, ".") == 0 || strcmp(last, "..") == 0;
  delete last;
  if (UNLIKELY(isDot))
    return -EBUSY;
  struct Fd* oldFd;
  struct Fd* newFd;
  if (oldDirFd != AT_FDCWD) {
    if (UNLIKELY(!(oldFd = GetFd(fs, oldDirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(oldFd->inode->mode)))
      return -ENOTDIR;
  }
  if (newDirFd != AT_FDCWD) {
    if (UNLIKELY(!(newFd = GetFd(fs, newDirFd))))
      return -EBADF;
    if (UNLIKELY(!S_ISDIR(newFd->inode->mode)))
      return -ENOTDIR;
  }
  struct INode* origCwd = fs->cwd->inode;
  if (oldDirFd != AT_FDCWD)
    fs->cwd->inode = oldFd->inode;
  struct INode* oldInode;
  struct INode* oldParent;
  int res = GetINode(fs, oldPath, &oldInode, &oldParent);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(res != 0))
    return res;
  if (newDirFd != AT_FDCWD)
    fs->cwd->inode = newFd->inode;
  struct INode* newInode = NULL;
  struct INode* newParent = NULL;
  res = GetINode(fs, newPath, &newInode, &newParent);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(!newParent) ||
      (!newInode && UNLIKELY(res != -ENOENT)))
    return res;
  if (UNLIKELY(oldInode == newInode))
    return 0;
  if (flags & RENAME_NOREPLACE && UNLIKELY(newInode))
    return -EEXIST;
  if (flags & RENAME_EXCHANGE && UNLIKELY(!newInode))
    return -ENOENT;
  if (S_ISDIR(oldInode->mode)) {
    if (newInode) {
      if (UNLIKELY(!S_ISDIR(newInode->mode)))
        return -ENOTDIR;
      if (UNLIKELY(newInode->dentCount > 2))
        return -ENOTEMPTY;
    }
    if (UNLIKELY(oldInode == fs->inodes[0]) ||
        UNLIKELY(oldInode == fs->cwd->inode))
      return -EBUSY;
  } else if (newInode && UNLIKELY(S_ISDIR(newInode->mode)))
    return -EISDIR;
  if (UNLIKELY(oldParent->IsInSelf(newParent)))
    return -EINVAL;
  const char* oldName = GetAbsoluteLast(fs, oldPath);
  if (UNLIKELY(!oldName))
    return -ENOMEM;
  const char* newName = GetAbsoluteLast(fs, newPath);
  if (UNLIKELY(!newName)) {
    delete oldName;
    return -ENOMEM;
  }
  if (flags & RENAME_EXCHANGE) {
    for (off_t i = 0; i != oldParent->dentCount; ++i)
      if (strcmp(oldParent->dents[i].name, oldName) == 0) {
        for (off_t j = 0; j != newParent->dentCount; ++j)
          if (strcmp(newParent->dents[j].name, newName) == 0) {
            oldParent->dents[i].inode = newInode;
            newParent->dents[j].inode = oldInode;
            break;
          }
        break;
      }
    delete oldName;
    delete newName;
  } else {
    if (UNLIKELY(!newParent->PushDent(newName, oldInode))) {
      delete oldName;
      delete newName;
      return -EIO;
    }
    oldParent->RemoveDent(oldName);
    delete oldName;
    if (newInode)
      newParent->RemoveDent(newName);
    if (S_ISDIR(oldInode->mode)) {
      --oldParent->nlink;
      ++newParent->nlink;
    }
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (!(flags & RENAME_EXCHANGE)) {
    if (newInode) {
      if (UNLIKELY(--newInode->nlink == 0))
        RemoveINode(fs, newInode);
      else newInode->ctime = ts;
    }
  } else newInode->ctime = ts;
  oldInode->ctime = ts;
  oldParent->ctime = oldParent->mtime = ts;
  newParent->ctime = newParent->mtime = ts;
  return 0;
}
off_t FileSystem::LSeek(unsigned int fdNum, off_t offset, unsigned int whence) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))))
    return -EBADF;
  struct INode* inode = fd->inode;
  switch (whence) {
    case SEEK_SET:
      return fd->seekOff = offset;
    case SEEK_CUR:
      if (UNLIKELY(fd->seekOff > std::numeric_limits<off_t>::max() - offset))
        return -EOVERFLOW;
      return fd->seekOff += offset;
    case SEEK_END:
      if (UNLIKELY(S_ISDIR(inode->mode)))
        return -EINVAL;
      if (UNLIKELY(inode->size > std::numeric_limits<off_t>::max() - offset))
        return -EOVERFLOW;
      return fd->seekOff = inode->size + offset;
    case SEEK_DATA: {
      INode::DataIterator it(inode, fd->seekOff);
      if (!it.IsInData()) {
        if (!it.Next()) {
          if (UNLIKELY(inode->size > std::numeric_limits<off_t>::max() - offset))
            return -EOVERFLOW;
          return fd->seekOff = inode->size + offset;
        }
        struct INode::DataRange* range = it.GetRange();
        if (UNLIKELY(range->offset > std::numeric_limits<off_t>::max() - offset))
          return -EOVERFLOW;
        return fd->seekOff = range->offset + offset;
      }
      it.Next();
      if (it.Next()) {
        struct INode::DataRange* range = it.GetRange();
        if (UNLIKELY(range->offset > std::numeric_limits<off_t>::max() - offset))
          return -EOVERFLOW;
        return fd->seekOff = range->offset + offset;
      }
      if (UNLIKELY(inode->size > std::numeric_limits<off_t>::max() - offset))
        return -EOVERFLOW;
      return fd->seekOff = inode->size + offset;
    }
    case SEEK_HOLE: {
      INode::DataIterator it(inode, fd->seekOff);
      if (it.IsInData()) {
        it.Next();
        struct INode::HoleRange hole = it.GetHole();
        if (UNLIKELY(hole.offset > std::numeric_limits<off_t>::max() - offset))
          return -EOVERFLOW;
        return fd->seekOff = hole.offset + offset;
      }
      if (it.Next()) {
        it.Next();
        struct INode::HoleRange hole = it.GetHole();
        if (UNLIKELY(hole.offset > std::numeric_limits<off_t>::max() - offset))
          return -EOVERFLOW;
        return fd->seekOff = hole.offset + offset;
      }
      if (UNLIKELY(inode->size > std::numeric_limits<off_t>::max() - offset))
        return -EOVERFLOW;
      return fd->seekOff = inode->size + offset;
    }
  }
  return -EINVAL;
}
ssize_t FileSystem::Read(unsigned int fdNum, char* buf, size_t count) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY))
    return -EBADF;
  struct INode* inode = fd->inode;
  if (UNLIKELY(S_ISDIR(inode->mode)))
    return -EISDIR;
  if (count == 0)
    return 0;
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  if (fd->seekOff >= inode->size)
    return 0;
  off_t end = inode->size - fd->seekOff;
  if (end < count)
    count = end;
  INode::DataIterator it(inode, fd->seekOff);
  for (size_t amountRead = 0; amountRead != count; it.Next()) {
    size_t amount;
    if (it.IsInData()) {
      struct INode::DataRange* range = it.GetRange();
      amount = min<size_t>((range->offset + range->size) - (fd->seekOff + amountRead), count - amountRead);
      memcpy(buf + amountRead, range->data + (fd->seekOff + amountRead) - range->offset, amount);
    } else {
      struct INode::HoleRange hole = it.GetHole();
      amount = min<size_t>((hole.offset + hole.size) - (fd->seekOff + amountRead), count - amountRead);
      memset(buf + amountRead, '\0', amount);
    }
    amountRead += amount;
  }
  buf[count] = '\0';
  fd->seekOff += count;
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  return count;
}
ssize_t FileSystem::Readv(unsigned int fdNum, struct iovec* iov, int iovcnt) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY))
    return -EBADF;
  struct INode* inode = fd->inode;
  if (UNLIKELY(S_ISDIR(inode->mode)))
    return -EISDIR;
  if (iovcnt == 0)
    return 0;
  if (UNLIKELY(iovcnt < 0) ||
      UNLIKELY(iovcnt > 1024))
    return -EINVAL;
  size_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    size_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (UNLIKELY(len < 0))
      return -EINVAL;
    if (len > 0x7ffff000 - totalLen) {
      len = 0x7ffff000 - totalLen;
      iov[i].iov_len = len;
      totalLen += len;
      break;
    }
    totalLen += len;
  }
  if (totalLen == 0 || fd->seekOff >= inode->size)
    return 0;
  off_t end = inode->size - fd->seekOff;
  if (end < totalLen)
    totalLen = end;
  INode::DataIterator it(inode, fd->seekOff);
  for (size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; it.Next()) {
    struct iovec curr = iov[iovIdx];
    size_t amount;
    if (it.IsInData()) {
      struct INode::DataRange* range = it.GetRange();
      amount = min<size_t>(
        min<size_t>(
          (range->offset + range->size) - (fd->seekOff + count),
          curr.iov_len - amountRead),
        totalLen - count
      );
      memcpy(reinterpret_cast<char*>(curr.iov_base) + amountRead, range->data + (fd->seekOff + count) - range->offset, amount);
    } else {
      struct INode::HoleRange hole = it.GetHole();
      amount = min<size_t>(
        min<size_t>(
          (hole.offset + hole.size) - (fd->seekOff + count),
          curr.iov_len - amountRead),
        totalLen - count
      );
      memset(reinterpret_cast<char*>(curr.iov_base) + amountRead, '\0', amount);
    }
    amountRead += amount;
    count += amount;
    if (amountRead == curr.iov_len) {
      ++iovIdx;
      amountRead = 0;
    }
  }
  fd->seekOff += totalLen;
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  return totalLen;
}
ssize_t FileSystem::PRead(unsigned int fdNum, char* buf, size_t count, off_t offset) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY))
    return -EBADF;
  struct INode* inode = fd->inode;
  if (UNLIKELY(S_ISDIR(inode->mode)))
    return -EISDIR;
  if (count == 0)
    return 0;
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  if (offset >= inode->size)
    return 0;
  off_t end = inode->size - offset;
  if (end < count)
    count = end;
  INode::DataIterator it(inode, offset);
  for (size_t amountRead = 0; amountRead != count; it.Next()) {
    size_t amount;
    if (it.IsInData()) {
      struct INode::DataRange* range = it.GetRange();
      amount = min<size_t>((range->offset + range->size) - (offset + amountRead), count - amountRead);
      memcpy(buf + amountRead, range->data + (offset + amountRead) - range->offset, amount);
    } else {
      struct INode::HoleRange hole = it.GetHole();
      amount = min<size_t>((hole.offset + hole.size) - (offset + amountRead), count - amountRead);
      memset(buf + amountRead, '\0', amount);
    }
    amountRead += amount;
  }
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  return count;
}
ssize_t FileSystem::PReadv(unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY))
    return -EBADF;
  struct INode* inode = fd->inode;
  if (UNLIKELY(S_ISDIR(inode->mode)))
    return -EISDIR;
  if (iovcnt == 0)
    return 0;
  if (UNLIKELY(iovcnt < 0) ||
      UNLIKELY(iovcnt > 1024))
    return -EINVAL;
  size_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    size_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (UNLIKELY(len < 0))
      return -EINVAL;
    if (len > 0x7ffff000 - totalLen) {
      len = 0x7ffff000 - totalLen;
      iov[i].iov_len = len;
      totalLen += len;
      break;
    }
    totalLen += len;
  }
  if (totalLen == 0 || offset >= inode->size)
    return 0;
  off_t end = inode->size - offset;
  if (end < totalLen)
    totalLen = end;
  INode::DataIterator it(inode, fd->seekOff);
  for (size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; it.Next()) {
    struct iovec curr = iov[iovIdx];
    size_t amount;
    if (it.IsInData()) {
      struct INode::DataRange* range = it.GetRange();
      amount = min<size_t>(
        min<size_t>(
          (range->offset + range->size) - (fd->seekOff + count),
          curr.iov_len - amountRead),
        totalLen - count
      );
      memcpy(reinterpret_cast<char*>(curr.iov_base) + amountRead, range->data + (fd->seekOff + count) - range->offset, amount);
    } else {
      struct INode::HoleRange hole = it.GetHole();
      amount = min<size_t>(
        min<size_t>(
          (hole.offset + hole.size) - (fd->seekOff + count),
          curr.iov_len - amountRead),
        totalLen - count
      );
      memset(reinterpret_cast<char*>(curr.iov_base) + amountRead, '\0', amount);
    }
    amountRead += amount;
    count += amount;
    if (amountRead == curr.iov_len) {
      ++iovIdx;
      amountRead = 0;
    }
  }
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  return totalLen;
}
ssize_t FileSystem::Write(unsigned int fdNum, const char* buf, size_t count) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR))))
    return -EBADF;
  if (count == 0)
    return 0;
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  struct INode* inode = fd->inode;
  off_t seekOff = fd->flags & O_APPEND
    ? inode->size
    : fd->seekOff;
  if (UNLIKELY(seekOff > std::numeric_limits<off_t>::max() - count))
    return -EFBIG;
  struct INode::DataRange* range = inode->AllocData(seekOff, count);
  if (UNLIKELY(!range))
    return -EIO;
  memcpy(range->data + (seekOff - range->offset), buf, count);
  fd->seekOff += count;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
ssize_t FileSystem::Writev(unsigned int fdNum, struct iovec* iov, int iovcnt) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR))))
    return -EBADF;
  if (iovcnt == 0)
    return 0;
  if (UNLIKELY(iovcnt < 0) ||
      UNLIKELY(iovcnt > 1024))
    return -EINVAL;
  ssize_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (UNLIKELY(len < 0))
      return -EINVAL;
    if (len > 0x7ffff000 - totalLen) {
      len = 0x7ffff000 - totalLen;
      iov[i].iov_len = len;
    }
    totalLen += len;
  }
  if (totalLen == 0)
    return 0;
  struct INode* inode = fd->inode;
  off_t seekOff = fd->flags & O_APPEND
    ? inode->size
    : fd->seekOff;
  if (UNLIKELY(seekOff > std::numeric_limits<off_t>::max() - totalLen))
    return -EFBIG;
  struct INode::DataRange* range = inode->AllocData(seekOff, totalLen);
  if (UNLIKELY(!range))
    return -EIO;
  ssize_t count = 0;
  for (int i = 0; i != iovcnt; ++i) {
    ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    memcpy(range->data + (seekOff - range->offset) + count, iov[i].iov_base, len);
    count += len;
    if (count == totalLen)
      break;
  }
  fd->seekOff += count;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
ssize_t FileSystem::PWrite(unsigned int fdNum, const char* buf, size_t count, off_t offset) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR))))
    return -EBADF;
  if (count == 0)
    return 0;
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  struct INode* inode = fd->inode;
  if (UNLIKELY(offset > std::numeric_limits<off_t>::max() - count))
    return -EFBIG;
  struct INode::DataRange* range = inode->AllocData(offset, count);
  if (UNLIKELY(!range))
    return -EIO;
  memcpy(range->data + (offset - range->offset), buf, count);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
ssize_t FileSystem::PWritev(unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR))))
    return -EBADF;
  if (iovcnt == 0)
    return 0;
  if (UNLIKELY(iovcnt < 0) ||
      UNLIKELY(iovcnt > 1024))
    return -EINVAL;
  ssize_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (UNLIKELY(len < 0))
      return -EINVAL;
    if (len > 0x7ffff000 - totalLen) {
      len = 0x7ffff000 - totalLen;
      iov[i].iov_len = len;
    }
    totalLen += len;
  }
  if (UNLIKELY(totalLen == 0))
    return 0;
  struct INode* inode = fd->inode;
  if (UNLIKELY(offset > std::numeric_limits<off_t>::max() - totalLen))
    return -EFBIG;
  struct INode::DataRange* range = inode->AllocData(offset, totalLen);
  if (UNLIKELY(!range))
    return -EIO;
  ssize_t count = 0;
  for (int i = 0; i != iovcnt; ++i) {
    ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    memcpy(range->data + (offset - range->offset) + count, iov[i].iov_base, len);
    count += len;
    if (count == totalLen)
      break;
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
ssize_t FileSystem::SendFile(unsigned int outFd, unsigned int inFd, off_t* offset, size_t count) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct Fd* fdIn;
  struct Fd* fdOut;
  if (UNLIKELY(!(fdIn = GetFd(fs, inFd))) || UNLIKELY(fdIn->flags & O_WRONLY) ||
      UNLIKELY(!(fdOut = GetFd(fs, outFd))) || UNLIKELY(!(fdOut->flags & (O_WRONLY | O_RDWR))))
    return -EBADF;
  if (UNLIKELY(S_ISDIR(fdIn->inode->mode)) ||
      UNLIKELY(fdOut->flags & O_APPEND))
    return -EINVAL;
  off_t off;
  if (offset) {
    if (UNLIKELY((off = *offset) < 0))
      return -EINVAL;
  } else off = fdIn->seekOff;
  if (count == 0)
    return 0;
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  struct INode* inodeIn = fdIn->inode;
  struct INode* inodeOut = fdOut->inode;
  if (UNLIKELY(fdOut->seekOff > std::numeric_limits<off_t>::max() - count))
    return -EFBIG;
  if (off >= inodeIn->size)
    return 0;
  off_t end = inodeIn->size - off;
  if (end < count)
    count = end;
  if (!offset)
    fdIn->seekOff += count;
  else *offset += count;
  if (fdOut->seekOff + count > inodeOut->size)
    inodeOut->TruncateData(fdOut->seekOff + count);
  INode::DataIterator itIn(inodeIn, off);
  INode::DataIterator itOut(inodeOut, fdOut->seekOff);
  for (size_t amountRead = 0; amountRead != count;) {
    size_t amount;
    if (!itIn.IsInData()) {
      struct INode::HoleRange holeIn = itIn.GetHole();
      if (!itOut.IsInData()) {
        struct INode::HoleRange holeOut = itOut.GetHole();
        amount = (holeOut.offset + holeOut.size) - (fdOut->seekOff + amountRead);
        if (amount > (holeIn.offset + holeIn.size) - (off + amountRead)) {
          amount = (holeIn.offset + holeIn.size) - (off + amountRead);
          itIn.Next();
        } else itOut.Next();
        if (amount == 0)
          continue;
        amountRead += min<size_t>(amount, count - amountRead);
        continue;
      }
      struct INode::DataRange* rangeOut = itOut.GetRange();
      amount = (rangeOut->offset + rangeOut->size) - (fdOut->seekOff + amountRead);
      if (amount > (holeIn.offset + holeIn.size) - (off + amountRead)) {
        amount = (holeIn.offset + holeIn.size) - (off + amountRead);
        itIn.Next();
      } else itOut.Next();
      if (amount == 0)
        continue;
      if (amount > count - amountRead)
        amount = count - amountRead;
      memset(rangeOut->data + ((fdOut->seekOff + amountRead) - rangeOut->offset), '\0', amount);
      amountRead += amount;
      continue;
    }
    struct INode::DataRange* rangeIn = itIn.GetRange();
    amount = min<size_t>((rangeIn->offset + rangeIn->size) - (off + amountRead), count - amountRead);
    struct INode::DataRange* rangeOut = inodeOut->AllocData(fdOut->seekOff + amountRead, amount);
    if (UNLIKELY(!rangeOut))
      return -EIO;
    memcpy(
      rangeOut->data + ((fdOut->seekOff + amountRead) - rangeOut->offset),
      rangeIn->data + ((off + amountRead) - rangeIn->offset),
      amount
    );
    amountRead += amount;
    itIn.Next();
    itOut.SeekTo(fdOut->seekOff + amountRead);
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (!(fdIn->flags & O_NOATIME))
    inodeIn->atime = ts;
  inodeOut->mtime = inodeOut->ctime = ts;
  return count;
}
int FileSystem::FTruncate(unsigned int fdNum, off_t length) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(length < 0))
    return -EINVAL;
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))))
    return -EBADF;
  struct INode* inode = fd->inode;
  if (UNLIKELY(!S_ISREG(inode->mode)) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR))))
    return -EINVAL;
  if (UNLIKELY(fd->flags & O_APPEND))
    return -EPERM;
  inode->TruncateData(length);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->ctime = inode->mtime = ts;
  return 0;
}
int FileSystem::Truncate(const char* path, off_t length) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(length < 0))
    return -EINVAL;
  struct INode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (UNLIKELY(res != 0))
    return res;
  if (UNLIKELY(S_ISDIR(inode->mode)))
    return -EISDIR;
  if (UNLIKELY(!S_ISREG(inode->mode)))
    return -EINVAL;
  if (UNLIKELY(!inode->CanUse(W_OK)))
    return -EACCES;
  inode->TruncateData(length);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->ctime = inode->mtime = ts;
  return 0;
}
int FileSystem::FChModAt(int dirFd, const char* path, mode_t mode) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct INode* origCwd = fs->cwd->inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd))))
      return -EBADF;
    fs->cwd->inode = fd->inode;
  }
  struct INode* inode;
  int res = GetINode(fs, path, &inode);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(res != 0))
    return res;
  inode->mode = (mode & 07777) | (inode->mode & S_IFMT);
  clock_gettime(CLOCK_REALTIME, &inode->ctime);
  return 0;
}
int FileSystem::FChMod(unsigned int fdNum, mode_t mode) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))))
    return -EBADF;
  struct INode* inode = fd->inode;
  inode->mode = (mode & 07777) | (inode->mode & S_IFMT);
  clock_gettime(CLOCK_REALTIME, &inode->ctime);
  return 0;
}
int FileSystem::ChDir(const char* path) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct INode* inode;
  struct INode* parent;
  int res = GetINode(fs, path, &inode, &parent, true);
  if (UNLIKELY(res != 0))
    return res;
  if (UNLIKELY(!S_ISDIR(inode->mode)))
    return -ENOTDIR;
  const char* absPath = AbsolutePath(fs, path);
  if (UNLIKELY(!absPath))
    return -ENOMEM;
  delete fs->cwd->path;
  fs->cwd->path = absPath;
  fs->cwd->inode = inode;
  fs->cwd->parent = parent;
  return 0;
}
int FileSystem::GetCwd(char* buf, size_t size) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (fs->cwd->inode != fs->inodes[0]) {
    struct INode* inode;
    struct INode* parent;
    int res = GetINode(fs, fs->cwd->path, &inode, &parent, true);
    if (UNLIKELY(res != 0))
      return res;
  }
  size_t cwdLen = strlen(fs->cwd->path);
  if (UNLIKELY(size <= cwdLen))
    return -ERANGE;
  if (buf) {
    memcpy(buf, fs->cwd->path, cwdLen);
    buf[cwdLen] = '\0';
  }
  return cwdLen;
}
int FileSystem::FStat(unsigned int fdNum, struct stat* buf) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))))
    return -EBADF;
  fd->inode->FillStat(buf);
  return 0;
}
int FileSystem::Stat(const char* path, struct stat* buf) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct INode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (UNLIKELY(res != 0))
    return res;
  inode->FillStat(buf);
  return 0;
}
int FileSystem::LStat(const char* path, struct stat* buf) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  struct INode* inode;
  int res = GetINode(fs, path, &inode);
  if (UNLIKELY(res != 0))
    return res;
  inode->FillStat(buf);
  return 0;
}
int FileSystem::Statx(int dirFd, const char* path, int flags, int mask, struct statx* buf) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) ||
      UNLIKELY(mask & ~STATX_ALL) ||
      UNLIKELY(flags & AT_EMPTY_PATH && path[0] != '\0'))
    return -EINVAL;
  struct INode* origCwd = fs->cwd->inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd))))
      return -EBADF;
    fs->cwd->inode = fd->inode;
  }
  struct INode* inode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    inode = fs->cwd->inode;
  else res = GetINode(fs, path, &inode, NULL, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd->inode = origCwd;
  if (UNLIKELY(res != 0))
    return res;
  inode->FillStatx(buf, mask);
  return 0;
}
int FileSystem::UTimeNsAt(int dirFd, const char* path, const struct timespec* times, int flags) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (UNLIKELY(flags & ~AT_SYMLINK_NOFOLLOW))
    return -EINVAL;
  struct INode* origCwd = fs->cwd->inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd))))
      return -EBADF;
    fs->cwd->inode = fd->inode;
  }
  struct INode* inode;
  int res = GetINode(fs, path, &inode, NULL, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd->inode = origCwd;
  if (UNLIKELY(res != 0))
    return res;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (times) {
    if (times[0].tv_nsec != UTIME_OMIT) {
      if (times[0].tv_nsec == UTIME_NOW)
        inode->atime = ts;
      else inode->atime = times[0];
    }
    if (times[1].tv_nsec != UTIME_OMIT) {
      if (times[1].tv_nsec == UTIME_NOW)
        inode->mtime = ts;
      else inode->mtime = times[1];
    }
  } else inode->atime = inode->mtime = ts;
  inode->ctime = ts;
  return 0;
}
int FileSystem::FUTimesAt(unsigned int fdNum, const char* path, const struct timeval* times) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  if (times &&
      UNLIKELY(
        UNLIKELY(times[0].tv_usec < 0) || UNLIKELY(times[0].tv_usec >= 1000000) ||
        UNLIKELY(times[1].tv_usec < 0) || UNLIKELY(times[1].tv_usec >= 1000000)
      ))
    return -EINVAL;
  struct INode* origCwd = fs->cwd->inode;
  if (fdNum != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, fdNum))))
      return -EBADF;
    fs->cwd->inode = fd->inode;
  }
  struct INode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  fs->cwd->inode = origCwd;
  if (UNLIKELY(res != 0))
    return res;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (times) {
    inode->atime.tv_sec = times[0].tv_sec;
    inode->atime.tv_nsec = times[0].tv_usec * 1000;
    inode->mtime.tv_sec = times[1].tv_sec;
    inode->mtime.tv_nsec = times[1].tv_usec * 1000;
  } else inode->atime = inode->mtime = ts;
  inode->ctime = ts;
  return 0;
}

bool FileSystem::DumpToFile(const char* filename) {
  struct FSInternal* fs = reinterpret_cast<struct FSInternal*>(data);
  std::lock_guard<std::mutex> lock(fs->mtx);
  int fd = creat(filename, 0644);
  if (UNLIKELY(fd < 0))
    goto err1;
  {
    ino_t littleInodeCount = toLittle(fs->inodeCount);
    if (UNLIKELY(write(fd, "\x7FVFS", 4) != 4) ||
        UNLIKELY(write(fd, &littleInodeCount, sizeof(ino_t)) != sizeof(ino_t)))
      goto err2;
  }
  for (ino_t i = 0; i != fs->inodeCount; ++i) {
    struct INode* inode = fs->inodes[i];
    struct DumpedINode dumped;
    dumped.id = toLittle(inode->id);
    dumped.size = toLittle(inode->size);
    dumped.nlink = toLittle(inode->nlink);
    dumped.mode = toLittle(inode->mode);
    dumped.btime = {
      toLittle(inode->btime.tv_sec),
      toLittle(inode->btime.tv_nsec)
    };
    dumped.ctime = {
      toLittle(inode->ctime.tv_sec),
      toLittle(inode->ctime.tv_nsec)
    };
    dumped.mtime = {
      toLittle(inode->mtime.tv_sec),
      toLittle(inode->mtime.tv_nsec)
    };
    dumped.atime = {
      toLittle(inode->atime.tv_sec),
      toLittle(inode->atime.tv_nsec)
    };
    if (UNLIKELY(write(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode)))
      goto err2;
    if (S_ISLNK(inode->mode)) {
      size_t targetLen = strlen(inode->target) + 1;
      off_t dataLen = inode->size;
      if (UNLIKELY(write(fd, inode->target, targetLen) != targetLen) ||
          UNLIKELY(write(fd, inode->dataRanges[0]->data, dataLen) != dataLen))
        goto err2;
    }
    if (S_ISDIR(inode->mode)) {
      {
        off_t littleDentCount = toLittle(inode->dentCount);
        ino_t littleNdx = toLittle(inode->dents[1].inode->ndx);
        if (UNLIKELY(write(fd, &littleDentCount, sizeof(off_t)) != sizeof(off_t)) ||
            UNLIKELY(write(fd, &littleNdx, sizeof(ino_t)) != sizeof(ino_t)))
          goto err2;
      }
      for (off_t j = 2; j != inode->dentCount; ++j) {
        struct INode::Dent* dent = &inode->dents[j];
        size_t nameLen = strlen(dent->name) + 1;
        ino_t littleNdx = toLittle(dent->inode->ndx);
        if (UNLIKELY(write(fd, &littleNdx, sizeof(ino_t)) != sizeof(ino_t)) ||
            UNLIKELY(write(fd, dent->name, nameLen) != nameLen))
          goto err2;
      }
    } else if (inode->size != 0) {
      {
        off_t littleDataRangeCount = toLittle(inode->dataRangeCount);
        if (UNLIKELY(write(fd, &littleDataRangeCount, sizeof(off_t)) != sizeof(off_t)))
          goto err2;
      }
      for (off_t j = 0; j != inode->dataRangeCount; ++j) {
        struct INode::DataRange* range = inode->dataRanges[j];
        {
          off_t littleOffset = toLittle(range->offset);
          off_t littleSize = toLittle(range->size);
          if (UNLIKELY(write(fd, &littleOffset, sizeof(off_t)) != sizeof(off_t)) ||
              UNLIKELY(write(fd, &littleSize, sizeof(off_t)) != sizeof(off_t)))
            goto err2;
        }
        ssize_t written = 0;
        while (written != range->size) {
          size_t amount = range->size - written;
          if (amount > 0x7ffff000)
            amount = 0x7ffff000;
          ssize_t count = write(fd, range->data + written, amount);
          if (UNLIKELY(count < 0))
            goto err2;
          written += count;
        }
      }
    }
  }
  close(fd);
  return true;
 err2:
  close(fd);
 err1:
  return false;
}
FileSystem* FileSystem::LoadFromFile(const char* filename) {
  int fd = open(filename, O_RDONLY);
  if (UNLIKELY(fd < 0))
    goto err_at_open;
  char magic[4];
  ino_t inodeCount;
  struct INode** inodes;
  if (UNLIKELY(read(fd, magic, 4) != 4) ||
      UNLIKELY(memcmp(magic, "\x7FVFS", 4) != 0) ||
      UNLIKELY(read(fd, &inodeCount, sizeof(ino_t)) != sizeof(ino_t)))
    goto err_after_open;
  inodeCount = toHost(inodeCount);
  if (UNLIKELY(!TryAlloc(&inodes, inodeCount)))
    goto err_after_open;
  for (ino_t i = 0; i != inodeCount; ++i) {
    struct INode* inode;
    struct DumpedINode dumped;
    if (UNLIKELY(!TryAlloc(&inode)))
      goto err_at_inode_loop;
    inode->ndx = i;
    if (UNLIKELY(read(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode)))
      goto err_after_inode_init;
    inode->id = toHost(dumped.id);
    inode->size = toHost(dumped.size);
    inode->nlink = toHost(dumped.nlink);
    inode->mode = toHost(dumped.mode);
    inode->btime = {
      toHost(dumped.btime.tv_sec),
      toHost(dumped.btime.tv_nsec)
    };
    inode->ctime = {
      toHost(dumped.ctime.tv_sec),
      toHost(dumped.ctime.tv_nsec)
    };
    inode->mtime = {
      toHost(dumped.mtime.tv_sec),
      toHost(dumped.mtime.tv_nsec)
    };
    inode->atime = {
      toHost(dumped.atime.tv_sec),
      toHost(dumped.atime.tv_nsec)
    };
    if (S_ISLNK(inode->mode)) {
      char target[PATH_MAX];
      size_t targetLen = 0;
      do {
        if (UNLIKELY(read(fd, &target[targetLen], 1) != 1))
          goto err_after_inode_init;
      } while (target[targetLen++] != '\0');
      inode->target = strdup(target);
      if (UNLIKELY(!inode->target))
        goto err_after_inode_init;
      char data[PATH_MAX];
      size_t dataLen = 0;
      do {
        if (UNLIKELY(read(fd, &data[dataLen], 1) != 1))
          goto err_after_target_alloc;
      } while (data[dataLen++] != '\0');
      if (UNLIKELY(!inode->AllocData(0, dataLen)))
        goto err_after_target_alloc;
      memcpy(inode->dataRanges[0]->data, data, dataLen);
      goto success_symlink;

     err_after_target_alloc:
      free(const_cast<char*>(inode->target));
      goto err_after_inode_init;
     success_symlink: {}
    } else inode->target = NULL;
    if (S_ISDIR(inode->mode)) {
      off_t dentCount;
      if (UNLIKELY(read(fd, &dentCount, sizeof(off_t)) != sizeof(off_t)))
        goto err_after_inode_init;
      dentCount = toHost(dentCount);
      if (UNLIKELY(!TryAlloc(&inode->dents, dentCount)))
        goto err_after_inode_init;
      inode->dents[0] = { ".", inode };
      inode->dents[1].name = "..";
      if (UNLIKELY(read(fd, &inode->dents[1].inode, sizeof(ino_t)) != sizeof(ino_t)))
        goto err_after_dent_alloc;
      for (off_t j = 2; j != dentCount; ++j) {
        struct INode::Dent* dent = &inode->dents[j];
        char name[PATH_MAX];
        size_t nameLen = 0;
        if (UNLIKELY(read(fd, &dent->inode, sizeof(ino_t)) != sizeof(ino_t)))
          goto err_after_dent_list_init;
        do {
          if (UNLIKELY(read(fd, &name[nameLen], 1) != 1))
            goto err_after_dent_list_init;
        } while (name[nameLen++] != '\0');
        dent->name = strdup(name);
        if (UNLIKELY(!dent->name))
          goto err_after_dent_list_init;
        goto success_dent;

       err_after_dent_list_init:
        for (off_t k = 2; k != j; ++k)
          free(const_cast<char*>(inode->dents[k].name));
        goto err_after_dent_alloc;
       success_dent: {}
      }
      inode->dentCount = dentCount;
      goto success_dents;

     err_after_dent_alloc:
      free(inode->dents);
      goto err_after_inode_init;
     success_dents: {}
    } else  {
      inode->dents = NULL;
      inode->dentCount = 0;
    }
    if (S_ISREG(inode->mode) &&
        LIKELY(inode->size != 0)) {
      off_t dataRangeCount;
      if (UNLIKELY(read(fd, &dataRangeCount, sizeof(off_t)) != sizeof(off_t)))
        goto err_after_inode_init;
      dataRangeCount = toHost(dataRangeCount);
      if (UNLIKELY(!TryAlloc(&inode->dataRanges, dataRangeCount)))
        goto err_after_inode_init;
      for (off_t j = 0; j != dataRangeCount; ++j) {
        struct INode::DataRange* range;
        ssize_t nread = 0;
        off_t offset;
        off_t size;
        if (UNLIKELY(read(fd, &offset, sizeof(off_t)) != sizeof(off_t)) ||
            UNLIKELY(read(fd, &size, sizeof(off_t)) != sizeof(off_t)))
          goto err_after_dataranges_init;
        offset = toHost(offset);
        size = toHost(size);
        if (UNLIKELY(offset < 0) || UNLIKELY(offset > inode->size - size) ||
            UNLIKELY(size < 0) || UNLIKELY(size > inode->size - offset))
          goto err_after_dataranges_init;
        if (UNLIKELY(!TryAlloc(&range)))
          goto err_after_dataranges_init;
        range->offset = offset;
        range->size = size;
        if (UNLIKELY(!TryAlloc(&range->data, size)))
          goto err_after_range_alloc;
        while (nread != size) {
          size_t amount = size - nread;
          if (amount > 0x7ffff000)
            amount = 0x7ffff000;
          ssize_t count = read(fd, range->data + nread, amount);
          if (UNLIKELY(count != amount))
            goto err_after_range_data_alloc;
          nread += count;
        }
        inode->dataRanges[j] = range;
        goto success_data_range;

        err_after_range_data_alloc:
        free(range->data);
        err_after_range_alloc:
        free(range);
        goto err_after_dataranges_init;
        success_data_range: {}
      }
      inode->dataRangeCount = dataRangeCount;
      goto success_data;

      err_after_dataranges_init:
      for (off_t k = 0; k != dataRangeCount; ++k)
        delete inode->dataRanges[k];
      delete inode->dataRanges;
      goto err_after_inode_init;
      success_data: {}
    } else {
      inode->dataRangeCount = 0;
      inode->dataRanges = NULL;
    }
    inodes[i] = inode;
    goto success_inode;

   err_after_inode_init:
    free(inode);
   err_at_inode_loop:
    for (ino_t j = 0; j != i; ++i)
      delete inodes[j];
    goto err_after_inode_list_init;
   success_inode: {}
  }
  for (ino_t i = 0; i != inodeCount; ++i) {
    struct INode* inode = inodes[i];
    if (S_ISDIR(inode->mode)) {
      for (off_t j = 1; j != inode->dentCount; ++j) {
        struct INode::Dent* dent = &inode->dents[j];
        ino_t hostIno = toHost(reinterpret_cast<ino_t>(dent->inode));
        if (UNLIKELY(hostIno >= inodeCount))
          goto err_after_inodes;
        dent->inode = inodes[hostIno];
      }
    }
  }
  for (ino_t i = 0; i != inodeCount;) {
    struct INode* inode = inodes[i];
    if (inode->nlink == 0) {
      delete inode;
      if (i != inodeCount - 1) {
        memmove(inodes + i, inodes + i + 1, sizeof(struct INode*) * (inodeCount - i));
        for (ino_t j = i; j != inodeCount - 1; ++j)
          --inodes[j]->ndx;
      }
      inodes = reinterpret_cast<struct INode**>(
        realloc(inodes, sizeof(struct INode*) * --inodeCount)
      );
    } else ++i;
  }
  close(fd);
  FileSystem* fs;
  struct FSInternal* data;
  if (UNLIKELY(!TryAlloc(&fs)))
    goto err_after_inodes;
  if (UNLIKELY(!TryAlloc<true>(&data)))
    goto err_after_fs_init;
  data->inodes = inodes;
  data->inodeCount = inodeCount;
  struct Cwd* cwd;
  if (UNLIKELY(!TryAlloc(&cwd)))
    goto err_after_fsdata_init;
  if (UNLIKELY(!(cwd->path = strdup("/"))))
    goto err_after_cwd_init;
  cwd->inode = inodes[0];
  cwd->parent = inodes[0];
  data->cwd = cwd;
  fs->data = data;
  return fs;

 err_after_cwd_init:
  free(cwd);
 err_after_fsdata_init:
  delete data;
 err_after_fs_init:
  free(fs);
 err_after_inodes:
  for (ino_t i = 0; i != inodeCount; ++i)
    delete inodes[i];
 err_after_inode_list_init:
  free(inodes);
 err_after_open:
  close(fd);
 err_at_open:
  return NULL;
}