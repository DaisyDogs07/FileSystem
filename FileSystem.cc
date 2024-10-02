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

#include "FileSystem.h"
#include <new>
#include <pthread.h>
#include <stdlib.h>

#ifdef O_TMPFILE
#undef O_TMPFILE
#endif
#define O_TMPFILE 020000000
#define FOLLOW_MAX 40
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define FALLOC_FL_ALLOCATE_RANGE 0x00
#define FALLOC_FL_MODE_MASK	(FALLOC_FL_ALLOCATE_RANGE |	\
  FALLOC_FL_ZERO_RANGE | \
  FALLOC_FL_PUNCH_HOLE | \
  FALLOC_FL_COLLAPSE_RANGE | \
  FALLOC_FL_INSERT_RANGE)
#define RW_MAX 0x7ffff000
#define IOV_MAX 1024

namespace {
  template<bool I = false, typename T>
  bool TryAlloc(T** ptr, size_t length = 1) {
    T* newPtr = (T*)malloc(sizeof(T) * length);
    if (!newPtr)
      return false;
    if constexpr (I)
      for (size_t i = 0; i != length; ++i)
        new (&newPtr[i]) T;
    *ptr = newPtr;
    return true;
  }
  template<typename T>
  bool TryRealloc(T** ptr, size_t length) {
    T* newPtr = (T*)realloc(*ptr, sizeof(T) * length);
    if (!newPtr)
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

  class ScopedLock {
   public:
    ScopedLock(pthread_mutex_t& mtx) {
      mtx_ = &mtx;
      pthread_mutex_lock(&mtx);
    }
    ~ScopedLock() {
      pthread_mutex_unlock(mtx_);
    }
   private:
    pthread_mutex_t* mtx_;
  };

  struct Attribute {
    const char* name;
    size_t size;
    char* data;
  };

  struct Attributes {
    ~Attributes() {
      if (list) {
        for (size_t i = 0; i != count; ++i) {
          struct Attribute attrib = list[i];
          delete attrib.name;
          if (attrib.size != 0)
            delete attrib.data;
        }
        delete list;
      }
    }
    size_t count = 0;
    struct Attribute* list = NULL;
  };

  struct BaseINode {
    BaseINode() {
      clock_gettime(CLOCK_REALTIME, &btime);
      ctime = mtime = atime = btime;
    }
    ino_t ndx;
    ino_t id;
    off_t size = 0;
    nlink_t nlink = 0;
    mode_t mode;
    struct timespec btime;
    struct timespec ctime;
    struct timespec mtime;
    struct timespec atime;
    struct Attributes attribs = {};

    bool CanUse(int perms) {
      if ((mode & perms) != perms &&
          (mode & (perms << 3)) != (perms << 3) &&
          (mode & (perms << 6)) != (perms << 6))
        return false;
      return true;
    }
    bool IsUnused() {
      if (S_ISDIR(mode))
        return nlink == 1;
      return nlink == 0;
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

  struct RegularINode : BaseINode {
    ~RegularINode();
    struct DataRange** dataRanges = NULL;
    off_t dataRangeCount = 0;

    struct DataRange* InsertRange(off_t offset, off_t length, off_t* index);
    void RemoveRange(off_t index);
    void RemoveRanges(off_t index, off_t count);
    struct DataRange* AllocData(off_t offset, off_t length);
    void TruncateData(off_t length);
  };
  struct DirectoryINode : BaseINode {
    ~DirectoryINode();
    struct Dent* dents = NULL;
    off_t dentCount = 0;

    bool PushDent(const char* name, struct BaseINode* inode);
    void RemoveDent(const char* name);
    bool IsInSelf(struct BaseINode* inode);
  };
  struct SymLinkINode : BaseINode {
    ~SymLinkINode();
    char* data = NULL;
    const char* target = NULL;
  };

  struct DataRange {
    ~DataRange() {
      if (data)
        delete data;
    }
    off_t offset;
    off_t size;
    char* data = NULL;
  };
  struct HoleRange {
    off_t offset;
    off_t size;
  };

  class DataIterator {
   public:
    DataIterator(struct RegularINode* inode, off_t offset) {
      inode_ = inode;
      if (inode->dataRangeCount == 0 || offset < inode->dataRanges[0]->offset) {
        rangeIdx_ = 0;
        atData_ = false;
        isBeforeFirstRange_ = true;
        return;
      }
      isBeforeFirstRange_ = false;
      {
        struct DataRange* lastRange = inode->dataRanges[inode->dataRangeCount - 1];
        if (offset >= lastRange->offset + lastRange->size) {
          rangeIdx_ = inode->dataRangeCount - 1;
          atData_ = false;
          return;
        }
      }
      off_t low = 0;
      off_t high = inode->dataRangeCount - 1;
      while (low <= high) {
        off_t mid = low + ((high - low) / 2);
        struct DataRange* range = inode->dataRanges[mid];
        if (offset >= range->offset) {
          off_t end = range->offset + range->size;
          if (offset < end) {
            rangeIdx_ = mid;
            atData_ = true;
            break;
          }
          low = mid + 1;
          struct DataRange* nextRange = inode->dataRanges[low];
          if (offset >= end && offset < nextRange->offset) {
            rangeIdx_ = mid;
            atData_ = false;
            break;
          }
        } else {
          high = mid - 1;
          struct DataRange* prevRange = inode->dataRanges[high];
          if (offset >= prevRange->offset + prevRange->size && offset < range->offset) {
            rangeIdx_ = high;
            atData_ = false;
            break;
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
        hole.offset = 0;
        if (inode_->dataRangeCount == 0)
          hole.size = inode_->size;
        else hole.size = inode_->dataRanges[0]->offset;
        return hole;
      }
      struct DataRange* currRange = inode_->dataRanges[rangeIdx_];
      hole.offset = currRange->offset + currRange->size;
      if (rangeIdx_ != inode_->dataRangeCount - 1)
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
          struct DataRange* range = GetRange();
          end = range->offset + range->size;
        } else {
          struct HoleRange hole = GetHole();
          end = hole.offset + hole.size;
        }
        if (end >= offset)
          break;
      } while (Next());
    }
   private:
    struct RegularINode* inode_;
    off_t rangeIdx_;
    bool atData_;
    bool isBeforeFirstRange_;
  };

  struct Dent {
    const char* name;
    struct BaseINode* inode;
  };

  RegularINode::~RegularINode() {
    if (dataRanges) {
      for (off_t i = 0; i != dataRangeCount; ++i)
        delete dataRanges[i];
      delete dataRanges;
    }
  }
  struct DataRange* RegularINode::InsertRange(off_t offset, off_t length, off_t* index) {
    off_t rangeIdx = dataRangeCount;
    if (dataRangeCount != 0) {
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
    struct DataRange* range;
    if (!TryAlloc<true>(&range))
      goto err_alloc_failed;
    if (!TryRealloc(&dataRanges, dataRangeCount + 1))
      goto err_after_alloc;
    if (rangeIdx != dataRangeCount)
      memmove(
        &dataRanges[rangeIdx + 1],
        &dataRanges[rangeIdx],
        sizeof(struct DataRange*) * (dataRangeCount - rangeIdx)
      );
    range->offset = offset;
    range->size = length;
    dataRanges[rangeIdx] = range;
    ++dataRangeCount;
    *index = rangeIdx;
    return range;

   err_after_alloc:
    delete range;
   err_alloc_failed:
    return NULL;
  }
  void RegularINode::RemoveRange(off_t index) {
    struct DataRange* range = dataRanges[index];
    delete range;
    if (index != dataRangeCount - 1)
      memmove(
        &dataRanges[index],
        &dataRanges[index + 1],
        sizeof(struct DataRange*) * (dataRangeCount - index)
      );
    dataRanges = (struct DataRange**)realloc(
      dataRanges,
      sizeof(struct DataRange*) * --dataRangeCount
    );
  }
  void RegularINode::RemoveRanges(off_t index, off_t count) {
    off_t endIdx = index + count;
    for (off_t i = index; i != endIdx; ++i)
      delete dataRanges[i];
    if (endIdx != dataRangeCount - 1)
      memmove(
        &dataRanges[index],
        &dataRanges[endIdx],
        sizeof(struct DataRange*) * (dataRangeCount - endIdx)
      );
    dataRangeCount -= count;
    dataRanges = (struct DataRange**)realloc(
      dataRanges,
      sizeof(struct DataRange*) * dataRangeCount
    );
  }
  struct DataRange* RegularINode::AllocData(off_t offset, off_t length) {
    off_t rangeIdx;
    bool createdRange = false;
    struct DataRange* range = NULL;
    off_t end = offset + length;
    if (dataRangeCount != 0) {
      DataIterator it(this, offset);
      for (off_t i = it.GetRangeIdx(); i != dataRangeCount; ++i) {
        struct DataRange* range2 = dataRanges[i];
        if (end == range2->offset) {
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
            if (!TryRealloc(&range3->data, newRangeLength))
              return NULL;
            memmove(&range3->data[newRangeLength - range2->size], range2->data, range2->size);
            range3->size = newRangeLength;
            for (off_t j = rangeIdx + 1; j != i; ++j) {
              struct DataRange* range4 = dataRanges[j];
              memmove(&range3->data[range4->offset - off], range4->data, range4->size);
            }
            RemoveRanges(rangeIdx + 1, i - rangeIdx);
            range3->offset = off;
            return range3;
          } else {
            off_t newRangeLength = range2->size + (range2->offset - offset);
            if (!TryRealloc(&range2->data, newRangeLength))
              return NULL;
            memmove(&range2->data[newRangeLength - range2->size], range2->data, range2->size);
            range2->size = newRangeLength;
            range2->offset = offset;
            return range2;
          }
        } else if (end < range2->offset)
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
      if (!range)
        return NULL;
      createdRange = true;
    } else if (offset >= range->offset && end <= range->offset + range->size)
      return range;
    off_t newRangeLength = end - range->offset;
    for (off_t i = rangeIdx + 1; i != dataRangeCount; ++i) {
      struct DataRange* range2 = dataRanges[i];
      if (range2->offset < end) {
        off_t newLength = (range2->offset - range->offset) + range2->size;
        if (newRangeLength < newLength) {
          newRangeLength = newLength;
          break;
        }
      } else break;
    }
    if (createdRange) {
      if (!TryAlloc(&range->data, newRangeLength)) {
        RemoveRange(rangeIdx);
        return NULL;
      }
    } else if (!TryRealloc(&range->data, newRangeLength))
      return NULL;
    range->size = newRangeLength;
    if (size < end)
      size = end;
    off_t n = 0;
    for (off_t i = rangeIdx + 1; i != dataRangeCount; ++i) {
      struct DataRange* range2 = dataRanges[i];
      if (range2->offset < end) {
        memcpy(&range->data[range2->offset - range->offset], range2->data, range2->size);
        ++n;
      } else break;
    }
    if (n != 0)
      RemoveRanges(rangeIdx + 1, n);
    return range;
  }
  void RegularINode::TruncateData(off_t length) {
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
          range->data = (char*)realloc(range->data, range->size);
        }
        break;
      }
    }
  }

  DirectoryINode::~DirectoryINode() {
    if (dents) {
      for (off_t i = 2; i != dentCount; ++i)
        delete dents[i].name;
      delete dents;
    }
  }
  bool DirectoryINode::PushDent(const char* name, struct BaseINode* inode) {
    if (!TryRealloc(&dents, dentCount + 1))
      return false;
    dents[dentCount++] = { name, inode };
    size += strlen(name);
    return true;
  }
  void DirectoryINode::RemoveDent(const char* name) {
    for (off_t i = 2; i != dentCount; ++i) {
      const char* d_name = dents[i].name;
      if (strcmp(d_name, name) == 0) {
        delete d_name;
        if (i != dentCount - 1)
          memmove(&dents[i], &dents[i + 1], sizeof(struct Dent) * (dentCount - i));
        dents = (struct Dent*)realloc(dents, sizeof(struct Dent) * --dentCount);
        size -= strlen(name);
        break;
      }
    }
  }
  bool DirectoryINode::IsInSelf(struct BaseINode* inode) {
    off_t len = dentCount;
    for (off_t i = 2; i != len; ++i) {
      struct BaseINode* dentInode = dents[i].inode;
      if (dentInode == inode || (
            S_ISDIR(dentInode->mode) && ((struct DirectoryINode*)dentInode)->IsInSelf(inode)
          ))
        return true;
    }
    return false;
  }

  SymLinkINode::~SymLinkINode() {
    if (data)
      delete data;
    if (target)
      delete target;
  }

  void DeleteINode(struct BaseINode* inode) {
    if (S_ISREG(inode->mode))
      delete (struct RegularINode*)inode;
    else if (S_ISDIR(inode->mode))
      delete (struct DirectoryINode*)inode;
    else if (S_ISLNK(inode->mode))
      delete (struct SymLinkINode*)inode;
  }

  struct Fd {
    struct BaseINode* inode;
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
    struct DirectoryINode* inode;
    struct DirectoryINode* parent;
  };

  struct FSInternal {
    FSInternal() {
      pthread_mutex_init(&mtx, NULL);
    }
    ~FSInternal() {
      pthread_mutex_destroy(&mtx);
      for (ino_t i = 0; i != inodeCount; ++i)
        DeleteINode(inodes[i]);
      delete inodes;
      if (fds) {
        for (int i = 0; i != fdCount; ++i)
          delete fds[i];
        delete fds;
      }
    }
    struct BaseINode** inodes = NULL;
    ino_t inodeCount = 0;
    struct Fd** fds = NULL;
    int fdCount = 0;
    struct Cwd cwd = {};
    int umask = 0000;
    pthread_mutex_t mtx;
  };

  bool PushINode(struct FSInternal* fs, struct BaseINode* inode) {
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
    if (!TryRealloc(&fs->inodes, fs->inodeCount + 1))
      return false;
    if (id != fs->inodeCount) {
      memmove(
        &fs->inodes[id + 1],
        &fs->inodes[id],
        sizeof(struct BaseINode*) * (fs->inodeCount - id)
      );
      for (ino_t i = id + 1; i != fs->inodeCount + 1; ++i)
        ++fs->inodes[i]->ndx;
    }
    inode->ndx = id;
    inode->id = id;
    fs->inodes[id] = inode;
    ++fs->inodeCount;
    return true;
  }
  void RemoveINode(struct FSInternal* fs, struct BaseINode* inode) {
    ino_t i = inode->ndx;
    DeleteINode(inode);
    if (i != fs->inodeCount - 1) {
      memmove(&fs->inodes[i], &fs->inodes[i + 1], sizeof(struct BaseINode*) * (fs->inodeCount - i));
      do {
        --fs->inodes[i++]->ndx;
      } while (i != fs->inodeCount - 1);
    }
    fs->inodes = (struct BaseINode**)realloc(
      fs->inodes,
      sizeof(struct BaseINode*) * --fs->inodeCount
    );
  }
  int PushFd(struct FSInternal* fs, struct BaseINode* inode, int flags) {
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
    if (!TryAlloc<true>(&fd))
      return -ENOMEM;
    if (!TryRealloc(&fs->fds, fs->fdCount + 1)) {
      delete fd;
      return -ENOMEM;
    }
    if (fdNum != fs->fdCount)
      memmove(&fs->fds[fdNum + 1], &fs->fds[fdNum], sizeof(struct Fd*) * (fs->fdCount - fdNum));
    fd->inode = inode;
    fd->flags = flags;
    fd->fd = fdNum;
    fs->fds[fdNum] = fd;
    ++fs->fdCount;
    return fdNum;
  }
  void RemoveFd2(struct FSInternal* fs, struct Fd* fd, int i) {
    struct BaseINode* inode = fd->inode;
    if (inode->nlink == 0)
      RemoveINode(fs, inode);
    delete fd;
    if (i != fs->fdCount - 1)
      memmove(&fs->fds[i], &fs->fds[i + 1], sizeof(struct Fd*) * (fs->fdCount - i));
    if (--fs->fdCount == 0) {
      delete fs->fds;
      fs->fds = NULL;
      return;
    }
    fs->fds = (struct Fd**)realloc(fs->fds, sizeof(struct Fd*) * fs->fdCount);
  }
  int RemoveFd(struct FSInternal* fs, unsigned int fd) {
    if (fs->fdCount == 0)
      return -EBADF;
    int low = 0;
    int high = fs->fdCount - 1;
    while (low <= high) {
      int mid = low + ((high - low) / 2);
      struct Fd* f = fs->fds[mid];
      if (f->fd == fd) {
        RemoveFd2(fs, f, mid);
        return 0;
      }
      if (f->fd < fd)
        low = mid + 1;
      else high = mid - 1;
    }
    return -EBADF;
  }
  struct Fd* GetFd(struct FSInternal* fs, unsigned int fdNum) {
    if (fs->fdCount == 0)
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
    struct BaseINode** inode,
    struct DirectoryINode** parent = NULL,
    bool followResolved = false,
    int follow = 0
  ) {
    size_t pathLen = strlen(path);
    if (pathLen == 0)
      return -ENOENT;
    if (pathLen >= PATH_MAX)
      return -ENAMETOOLONG;
    bool isAbsolute = path[0] == '/';
    struct BaseINode* current = isAbsolute
      ? fs->inodes[0]
      : fs->cwd.inode;
    struct DirectoryINode* currParent = isAbsolute
      ? (struct DirectoryINode*)fs->inodes[0]
      : fs->cwd.parent;
    int err = 0;
    char name[NAME_MAX + 1];
    size_t nameLen = 0;
    for (size_t i = 0; i != pathLen; ++i) {
      if (path[i] == '/') {
        if (nameLen == 0)
          continue;
        if (err)
          return err;
        currParent = (struct DirectoryINode*)current;
        if (!current->CanUse(X_OK)) {
          err = -EACCES;
          goto resetName;
        }
        {
          off_t j = 0;
          off_t dentCount = ((struct DirectoryINode*)current)->dentCount;
          for (; j != dentCount; ++j)
            if (strcmp(((struct DirectoryINode*)current)->dents[j].name, name) == 0)
              break;
          if (j == dentCount) {
            err = -ENOENT;
            goto resetName;
          }
          current = ((struct DirectoryINode*)current)->dents[j].inode;
        }
        if (S_ISLNK(current->mode)) {
          if (follow++ == FOLLOW_MAX) {
            err = -ELOOP;
            goto resetName;
          }
          struct DirectoryINode* targetParent;
          int res = GetINode(
            fs,
            ((struct SymLinkINode*)current)->target,
            &current,
            &targetParent,
            true,
            follow
          );
          if (res != 0) {
            err = res;
            goto resetName;
          }
        }
        if (!S_ISDIR(current->mode))
          err = -ENOTDIR;
       resetName:
        name[0] = '\0';
        nameLen = 0;
      } else {
        if (nameLen == NAME_MAX)
          return -ENAMETOOLONG;
        name[nameLen++] = path[i];
        name[nameLen] = '\0';
      }
    }
    if (parent)
      *parent = currParent;
    if (err)
      return err;
    if (nameLen != 0) {
      if (parent)
        *parent = (struct DirectoryINode*)current;
      if (!current->CanUse(X_OK))
        return -EACCES;
      off_t dentCount = ((struct DirectoryINode*)current)->dentCount;
      for (off_t i = 0; i != dentCount; ++i)
        if (strcmp(((struct DirectoryINode*)current)->dents[i].name, name) == 0) {
          current = ((struct DirectoryINode*)current)->dents[i].inode;
          goto out;
        }
      return -ENOENT;
    }
   out:
    if (followResolved) {
      if (S_ISLNK(current->mode)) {
        if (follow++ == FOLLOW_MAX)
          return -ELOOP;
        struct DirectoryINode* targetParent;
        int res = GetINode(
          fs,
          ((struct SymLinkINode*)current)->target,
          &current,
          &targetParent,
          true,
          follow
        );
        if (res != 0)
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
      int cwdPathLen = strlen(fs->cwd.path);
      if (cwdPathLen != 1) {
        memcpy(absPath, fs->cwd.path, cwdPathLen);
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
              i += 2;
            else ++i;
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
    if (!absPath)
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

  bool TryAllocFromMode(struct BaseINode** inode, mode_t mode) {
    if (S_ISREG(mode))
      return TryAlloc((struct RegularINode**)inode);
    if (S_ISDIR(mode))
      return TryAlloc((struct DirectoryINode**)inode);
    if (S_ISLNK(mode))
      return TryAlloc((struct SymLinkINode**)inode);
    __builtin_unreachable();
  }
}

FileSystem* FileSystem::New() {
  struct FSInternal* data;
  if (!TryAlloc<true>(&data))
    goto err1;
  struct DirectoryINode* root;
  if (!TryAlloc(&data->inodes) ||
      !TryAlloc<true>(&root))
    goto err2;
  if (!TryAlloc(&root->dents, 2))
    goto err3;
  root->mode = 0755 | S_IFDIR;
  root->dents[0] = { ".", root };
  root->dents[1] = { "..", root };
  root->dentCount = root->nlink = 2;
  if (!PushINode(data, root))
    goto err3;
  if (!(data->cwd.path = strdup("/")))
    goto err2;
  data->cwd.inode = root;
  data->cwd.parent = root;
  FileSystem* fs;
  if (!TryAlloc(&fs))
    goto err2;
  fs->data = data;
  return fs;

 err3:
  delete root;
 err2:
  delete data;
 err1:
  return NULL;
}
FileSystem::~FileSystem() {
  delete (struct FSInternal*)data;
}
int FileSystem::FAccessAt2(int dirFd, const char* path, int mode, int flags) {
  if (mode   & ~(F_OK | R_OK | W_OK | X_OK) ||
      flags  & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH) ||
      (flags & AT_EMPTY_PATH && path[0] != '\0'))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -EBADF;
    if (!S_ISDIR(fd->inode->mode) && !(flags & AT_EMPTY_PATH))
      return -ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    inode = fs->cwd.inode;
  else res = GetINode(fs, path, &inode, NULL, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (mode != F_OK && !inode->CanUse(mode))
    return -EACCES;
  return 0;
}
int FileSystem::OpenAt(int dirFd, const char* path, int flags, mode_t mode) {
  if (flags & ~(
        O_RDONLY    | O_WRONLY   | O_RDWR    | O_CREAT   |
        O_EXCL      | O_APPEND   | O_TRUNC   | O_TMPFILE |
        O_DIRECTORY | O_NOFOLLOW | O_NOATIME
      ))
    return -EINVAL;
  if (flags & O_TMPFILE) {
    if (!(flags & O_DIRECTORY) ||
        flags & O_CREAT ||
        !(flags & (O_WRONLY | O_RDWR)) ||
        mode & ~0777 || mode == 0)
      return -EINVAL;
    mode |= S_IFREG;
  } else if (flags & O_CREAT) {
    if (flags & O_DIRECTORY ||
        mode & ~0777)
      return -EINVAL;
    mode |= S_IFREG;
  } else if (mode != 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -EBADF;
    if (!S_ISDIR(fd->inode->mode))
      return -ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  if (flags & O_CREAT && flags & O_EXCL)
    flags |= O_NOFOLLOW;
  if (flags & O_WRONLY && flags & O_RDWR)
    flags &= ~O_RDWR;
  struct BaseINode* inode;
  struct DirectoryINode* parent = NULL;
  int res = GetINode(fs, path, &inode, &parent, !(flags & O_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (!parent)
    return res;
  if (res == 0) {
    if (flags & O_CREAT) {
      if (flags & O_EXCL)
        return -EEXIST;
      if (S_ISDIR(inode->mode))
        return -EISDIR;
    }
    if (flags & O_NOFOLLOW && S_ISLNK(inode->mode))
      return -ELOOP;
    if (!inode->CanUse(FlagsToPerms(flags)))
      return -EACCES;
  } else {
    if (flags & O_CREAT && res == -ENOENT) {
      flags &= ~O_TRUNC;
      const char* name = GetAbsoluteLast(fs, path);
      if (!name)
        return -ENOMEM;
      struct RegularINode* x;
      if (!TryAlloc<true>(&x)) {
        delete name;
        return -EIO;
      }
      if (!PushINode(fs, x)) {
        delete x;
        delete name;
        return -EIO;
      }
      if (!parent->PushDent(name, x)) {
        RemoveINode(fs, x);
        delete name;
        return -EIO;
      }
      x->mode = mode & ~fs->umask;
      x->nlink = 1;
      parent->ctime = parent->mtime = x->btime;
      res = PushFd(fs, x, flags);
      if (res < 0) {
        parent->RemoveDent(name);
        RemoveINode(fs, x);
        delete name;
      }
    }
    return res;
  }
  if (S_ISDIR(inode->mode)) {
    if (flags & O_TMPFILE) {
      struct RegularINode* x;
      if (!TryAlloc<true>(&x))
        return -EIO;
      if (!PushINode(fs, x)) {
        delete x;
        return -EIO;
      }
      x->mode = (mode & ~fs->umask) | S_IFREG;
      res = PushFd(fs, x, flags);
      if (res < 0)
        RemoveINode(fs, x);
      return res;
    }
    if (flags & (O_WRONLY | O_RDWR))
      return -EISDIR;
  } else {
    if (flags & O_DIRECTORY)
      return -ENOTDIR;
    if (flags & O_TRUNC && inode->size != 0)
      ((struct RegularINode*)inode)->TruncateData(0);
  }
  return PushFd(fs, inode, flags);
}
int FileSystem::Close(unsigned int fd) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  return RemoveFd(fs, fd);
}
int FileSystem::CloseRange(unsigned int fd, unsigned int maxFd, unsigned int flags) {
  if (flags != 0 || fd > maxFd)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  for (int i = 0; i != fs->fdCount; ++i) {
    struct Fd* f = fs->fds[i];
    if (f->fd >= fd) {
      RemoveFd2(fs, f, i);
      while (i != fs->fdCount) {
        f = fs->fds[i];
        if (f->fd < maxFd)
          RemoveFd2(fs, f, i);
        else break;
      }
      break;
    }
  }
  return 0;
}
int FileSystem::MkNodAt(int dirFd, const char* path, mode_t mode, dev_t dev) {
  if (mode & S_IFMT) {
    if (S_ISDIR(mode))
      return -EPERM;
    if (!S_ISREG(mode))
      return -EINVAL;
  }
  if (dev != 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -EBADF;
    if (!S_ISDIR(fd->inode->mode))
      return -ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  struct DirectoryINode* parent = NULL;
  int res = GetINode(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (!parent)
    return res;
  if (res == 0)
    return -EEXIST;
  if (res != -ENOENT)
    return res;
  const char* name = GetAbsoluteLast(fs, path);
  if (!name)
    return -ENOMEM;
  struct RegularINode* x;
  if (!TryAlloc<true>(&x)) {
    delete name;
    return -EIO;
  }
  if (!PushINode(fs, x)) {
    delete x;
    delete name;
    return -EIO;
  }
  if (!parent->PushDent(name, x)) {
    RemoveINode(fs, x);
    delete name;
    return -EIO;
  }
  x->mode = ((mode & 0777) & ~fs->umask) | S_IFREG;
  x->nlink = 1;
  parent->ctime = parent->mtime = x->btime;
  return 0;
}
int FileSystem::MkDirAt(int dirFd, const char* path, mode_t mode) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -EBADF;
    if (!S_ISDIR(fd->inode->mode))
      return -ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  struct DirectoryINode* parent = NULL;
  int res = GetINode(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (!parent)
    return res;
  if (res == 0)
    return -EEXIST;
  if (res != -ENOENT)
    return res;
  const char* name = GetAbsoluteLast(fs, path);
  if (!name)
    return -ENOMEM;
  struct DirectoryINode* x;
  if (!TryAlloc<true>(&x)) {
    delete name;
    return -EIO;
  }
  if (!TryAlloc(&x->dents, 2)) {
    delete x;
    delete name;
    return -EIO;
  }
  x->dentCount = 2;
  if (!PushINode(fs, x)) {
    delete x;
    delete name;
    return -EIO;
  }
  if (!parent->PushDent(name, x)) {
    RemoveINode(fs, x);
    delete name;
    return -EIO;
  }
  x->mode = ((mode & 0777) & ~fs->umask) | S_IFDIR;
  x->nlink = 2;
  x->dents[0] = { ".", x };
  x->dents[1] = { "..", parent };
  ++parent->nlink;
  parent->ctime = parent->mtime = x->btime;
  return 0;
}
int FileSystem::SymLinkAt(const char* oldPath, int newDirFd, const char* newPath) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (newDirFd != AT_FDCWD) {
    if (!(fd = GetFd(fs, newDirFd)))
      return -EBADF;
    if (!S_ISDIR(fd->inode->mode))
      return -ENOTDIR;
  }
  struct BaseINode* oldInode;
  int res = GetINode(fs, oldPath, &oldInode);
  if (res != 0)
    return res;
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (newDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  struct BaseINode* newInode;
  struct DirectoryINode* newParent = NULL;
  res = GetINode(fs, newPath, &newInode, &newParent);
  fs->cwd.inode = origCwd;
  if (!newParent)
    return res;
  if (res == 0)
    return -EEXIST;
  if (res != -ENOENT)
    return res;
  const char* name = GetAbsoluteLast(fs, newPath);
  if (!name)
    return -ENOMEM;
  struct SymLinkINode* x;
  if (!TryAlloc<true>(&x)) {
    delete name;
    return -EIO;
  }
  size_t oldPathLen = strlen(oldPath);
  if (!TryAlloc(&x->data, oldPathLen) || !PushINode(fs, x)) {
    delete x;
    delete name;
    return -EIO;
  }
  memcpy(x->data, oldPath, oldPathLen);
  if (!(x->target = AbsolutePath(fs, oldPath)) || !newParent->PushDent(name, x)) {
    RemoveINode(fs, x);
    delete name;
    return -EIO;
  }
  x->mode = 0777 | S_IFLNK;
  x->nlink = 1;
  x->size = oldPathLen;
  newParent->ctime = newParent->mtime = x->btime;
  return 0;
}
int FileSystem::ReadLinkAt(int dirFd, const char* path, char* buf, int bufLen) {
  if (bufLen <= 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -EBADF;
    if (!S_ISDIR(fd->inode->mode))
      return -ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (!S_ISLNK(inode->mode))
    return -EINVAL;
  if (inode->size < bufLen)
    bufLen = inode->size;
  memcpy(buf, ((struct SymLinkINode*)inode)->data, bufLen);
  clock_gettime(CLOCK_REALTIME, &inode->atime);
  return bufLen;
}
int FileSystem::GetDents(unsigned int fdNum, struct linux_dirent* dirp, unsigned int count) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  if (!S_ISDIR(inode->mode))
    return -ENOTDIR;
  if (fd->seekOff >= ((struct DirectoryINode*)inode)->dentCount)
    return 0;
  unsigned int nread = 0;
  char* dirpData = (char*)dirp;
  do {
    struct Dent d = ((struct DirectoryINode*)inode)->dents[fd->seekOff];
    size_t nameLen = strlen(d.name);
    unsigned short reclen = ALIGN(
      __builtin_offsetof(struct linux_dirent, d_name) + nameLen + 2,
      sizeof(long)
    );
    if (nread + reclen > count)
      break;
    struct linux_dirent* dent = (struct linux_dirent*)dirpData;
    dent->d_ino = d.inode->id;
    dent->d_off = fd->seekOff + 1;
    dent->d_reclen = reclen;
    memcpy(dent->d_name, d.name, nameLen);
    dent->d_name[nameLen] = '\0';
    dirpData[reclen - 1] = IFTODT(d.inode->mode);
    dirpData += reclen;
    nread += reclen;
  } while (++fd->seekOff != ((struct DirectoryINode*)inode)->dentCount);
  if (nread == 0)
    return -EINVAL;
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  return nread;
}
int FileSystem::LinkAt(
  int oldDirFd,
  const char* oldPath,
  int newDirFd,
  const char* newPath,
  int flags
) {
  if (flags & ~(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH) || (flags & AT_EMPTY_PATH && oldPath[0] != '\0'))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* oldFd;
  struct Fd* newFd;
  if (oldDirFd != AT_FDCWD || flags & AT_EMPTY_PATH) {
    if (!(oldFd = GetFd(fs, oldDirFd)))
      return -EBADF;
    if (!S_ISDIR(oldFd->inode->mode)) {
      if (!(flags & AT_EMPTY_PATH))
        return -ENOTDIR;
    } else if (flags & AT_EMPTY_PATH)
      return -EPERM;
  }
  if (newDirFd != AT_FDCWD) {
    if (!(newFd = GetFd(fs, newDirFd)))
      return -EBADF;
    if (!S_ISDIR(newFd->inode->mode))
      return -ENOTDIR;
  }
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (oldDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)oldFd->inode;
  struct BaseINode* oldInode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    oldInode = fs->cwd.inode;
  else res = GetINode(fs, oldPath, &oldInode, NULL, flags & AT_SYMLINK_FOLLOW);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (newDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)newFd->inode;
  struct BaseINode* newInode;
  struct DirectoryINode* newParent = NULL;
  res = GetINode(fs, newPath, &newInode, &newParent);
  fs->cwd.inode = origCwd;
  if (!newParent)
    return res;
  if (res == 0)
    return -EEXIST;
  if (res != -ENOENT)
    return res;
  if (S_ISDIR(oldInode->mode))
    return -EPERM;
  const char* name = GetAbsoluteLast(fs, newPath);
  if (!name)
    return -ENOMEM;
  if (!newParent->PushDent(name, oldInode)) {
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
  if (flags & ~AT_REMOVEDIR)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -EBADF;
    if (!S_ISDIR(fd->inode->mode))
      return -ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  struct DirectoryINode* parent;
  int res = GetINode(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (flags & AT_REMOVEDIR) {
    if (!S_ISDIR(inode->mode))
      return -ENOTDIR;
    if (inode == fs->inodes[0])
      return -EBUSY;
  } else if (S_ISDIR(inode->mode))
    return -EISDIR;
  for (int i = 0; i != fs->fdCount; ++i)
    if (fs->fds[i]->inode == inode)
      return -EBUSY;
  if (flags & AT_REMOVEDIR) {
    const char* last = GetLast(path);
    if (!last)
      return -ENOMEM;
    bool isDot = strcmp(last, ".") == 0;
    delete last;
    if (isDot)
      return -EINVAL;
    if (((struct DirectoryINode*)inode)->dentCount != 2)
      return -ENOTEMPTY;
  }
  const char* name = GetAbsoluteLast(fs, path);
  if (!name)
    return -ENOMEM;
  parent->RemoveDent(name);
  delete name;
  if (flags & AT_REMOVEDIR)
    --parent->nlink;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  --inode->nlink;
  if (inode->IsUnused())
    RemoveINode(fs, inode);
  else inode->ctime = ts;
  parent->ctime = parent->mtime = ts;
  return 0;
}
int FileSystem::RenameAt2(
  int oldDirFd,
  const char* oldPath,
  int newDirFd,
  const char* newPath,
  unsigned int flags
) {
  if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE) ||
      (flags & RENAME_NOREPLACE && flags & RENAME_EXCHANGE))
    return -EINVAL;
  const char* last = GetLast(oldPath);
  if (!last)
    return -ENOMEM;
  {
    bool isDot = strcmp(last, ".") == 0 || strcmp(last, "..") == 0;
    delete last;
    if (isDot)
      return -EBUSY;
  }
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* oldFd;
  struct Fd* newFd;
  if (oldDirFd != AT_FDCWD) {
    if (!(oldFd = GetFd(fs, oldDirFd)))
      return -EBADF;
    if (!S_ISDIR(oldFd->inode->mode))
      return -ENOTDIR;
  }
  if (newDirFd != AT_FDCWD) {
    if (!(newFd = GetFd(fs, newDirFd)))
      return -EBADF;
    if (!S_ISDIR(newFd->inode->mode))
      return -ENOTDIR;
  }
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (oldDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)oldFd->inode;
  struct BaseINode* oldInode;
  struct DirectoryINode* oldParent;
  int res = GetINode(fs, oldPath, &oldInode, &oldParent);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (newDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)newFd->inode;
  struct BaseINode* newInode = NULL;
  struct DirectoryINode* newParent = NULL;
  res = GetINode(fs, newPath, &newInode, &newParent);
  fs->cwd.inode = origCwd;
  if (!newParent || (!newInode && res != -ENOENT))
    return res;
  if (oldInode == newInode)
    return 0;
  if (flags & RENAME_NOREPLACE && newInode)
    return -EEXIST;
  if (flags & RENAME_EXCHANGE && !newInode)
    return -ENOENT;
  if (S_ISDIR(oldInode->mode)) {
    if (newInode) {
      if (!S_ISDIR(newInode->mode))
        return -ENOTDIR;
      if (((struct DirectoryINode*)newInode)->dentCount > 2)
        return -ENOTEMPTY;
    }
    if (oldInode == fs->inodes[0] || oldInode == fs->cwd.inode)
      return -EBUSY;
  } else if (newInode && S_ISDIR(newInode->mode))
    return -EISDIR;
  if (oldParent->IsInSelf(newParent))
    return -EINVAL;
  const char* oldName = GetAbsoluteLast(fs, oldPath);
  if (!oldName)
    return -ENOMEM;
  const char* newName = GetAbsoluteLast(fs, newPath);
  if (!newName) {
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
    if (!newParent->PushDent(newName, oldInode)) {
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
      --newInode->nlink;
      if (newInode->IsUnused())
        RemoveINode(fs, newInode);
      else newInode->ctime = ts;
    }
  } else newInode->ctime = ts;
  oldInode->ctime = ts;
  oldParent->ctime = oldParent->mtime = ts;
  newParent->ctime = newParent->mtime = ts;
  return 0;
}
int FileSystem::FAllocate(int fdNum, int mode, off_t offset, off_t len) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  if (offset < 0 || len < 0)
    return -EINVAL;
  if (mode & ~(FALLOC_FL_MODE_MASK | FALLOC_FL_KEEP_SIZE))
    return -EOPNOTSUPP;
  switch (mode & FALLOC_FL_MODE_MASK) {
    case FALLOC_FL_ALLOCATE_RANGE:
    case FALLOC_FL_ZERO_RANGE:
      break;
    case FALLOC_FL_PUNCH_HOLE:
      if (!(mode & FALLOC_FL_KEEP_SIZE))
        return -EOPNOTSUPP;
      break;
    case FALLOC_FL_COLLAPSE_RANGE:
    case FALLOC_FL_INSERT_RANGE:
      if (mode & FALLOC_FL_KEEP_SIZE)
        return -EOPNOTSUPP;
      break;
    default:
      return -EOPNOTSUPP;
  }
  if (!(fd->flags & (O_WRONLY | O_RDWR)))
    return -EBADF;
  if ((mode & ~FALLOC_FL_KEEP_SIZE) && fd->flags & O_APPEND)
		return -EPERM;
  struct BaseINode* baseInode = fd->inode;
  if (S_ISDIR(baseInode->mode))
    return -EISDIR;
  if (!S_ISREG(baseInode->mode))
    return -ENODEV;
  struct RegularINode* inode = (struct RegularINode*)baseInode;
  off_t end;
  if (__builtin_add_overflow(offset, len, &end))
    return -EFBIG;
  switch (mode & FALLOC_FL_MODE_MASK) {
    case FALLOC_FL_ALLOCATE_RANGE: {
      if (mode & FALLOC_FL_KEEP_SIZE) {
        if (end > inode->size) {
          if (offset >= inode->size)
            break;
          len = inode->size - offset;
        }
      } else {
        if (inode->size < end)
          inode->size = end;
      }
      if (!inode->AllocData(offset, len))
        return -EIO;
      break;
    }
    case FALLOC_FL_ZERO_RANGE: {
      if (mode & FALLOC_FL_KEEP_SIZE) {
        if (end > inode->size) {
          if (offset >= inode->size)
            break;
          len = inode->size - offset;
        }
      } else {
        if (inode->size < end)
          inode->size = end;
      }
      struct DataRange* range = inode->AllocData(offset, len);
      if (!range)
        return -EIO;
      memset(range->data, '\0', len);
      break;
    }
    case FALLOC_FL_PUNCH_HOLE: {
      for (off_t i = 0; i != inode->dataRangeCount;) {
        struct DataRange* range = inode->dataRanges[i];
        if (offset <= range->offset) {
          if (end <= range->offset)
            break;
          if (end < range->offset + range->size) {
            off_t amountToRemove = len - (range->offset - offset);
            range->size -= amountToRemove;
            range->offset += amountToRemove;
            memmove(range->data, range->data + amountToRemove, range->size);
            range->data = (char*)realloc(range->data, range->size);
          } else {
            inode->RemoveRange(i);
            continue;
          }
        } else {
          if (offset >= range->offset + range->size) {
            ++i;
            continue;
          }
          if (end < range->offset + range->size) {
            off_t rangeSize = range->size;
            range->size = offset - range->offset;
            off_t offsetAfterHole = (offset - range->offset) + len;
            off_t newRangeLength = rangeSize - offsetAfterHole;
            struct DataRange* newRange = inode->AllocData(end, newRangeLength);
            if (!newRange)
              return -ENOMEM;
            memcpy(newRange->data, range->data + offsetAfterHole, newRangeLength);
            range->data = (char*)realloc(range->data, range->size);
            break;
          } else {
            range->size = (range->offset + range->size) - offset;
            range->data = (char*)realloc(range->data, range->size);
          }
        }
        ++i;
      }
      break;
    }
    case FALLOC_FL_COLLAPSE_RANGE: {
      for (off_t i = 0; i != inode->dataRangeCount;) {
        struct DataRange* range = inode->dataRanges[i];
        if (offset <= range->offset) {
          if (end < range->offset) {
            range->offset -= len;
            ++i;
            continue;
          }
          if (end == range->offset) {
            range->offset -= len;
            if (i != 0) {
              struct DataRange* prevRange = inode->dataRanges[i - 1];
              if (!TryRealloc(&prevRange->data, prevRange->size + range->size))
                return -ENOMEM;
              memcpy(prevRange->data + prevRange->size, range->data, range->size);
              prevRange->size += range->size;
              inode->RemoveRange(i);
            } else ++i;
            continue;
          }
          if (end < range->offset + range->size) {
            off_t amountToRemove = len - (range->offset - offset);
            range->size -= amountToRemove;
            memmove(range->data, range->data + amountToRemove, range->size);
            range->data = (char*)realloc(range->data, range->size);
          } else {
            inode->RemoveRange(i);
            continue;
          }
        } else {
          if (offset >= range->offset + range->size) {
            ++i;
            continue;
          }
          if (end < range->offset + range->size) {
            off_t rangeSize = range->size;
            range->size -= len;
            off_t offsetAfterHole = (offset - range->offset) + len;
            memcpy(
              range->data + (offset - range->offset),
              range->data + offsetAfterHole,
              rangeSize - offsetAfterHole
            );
            range->data = (char*)realloc(range->data, range->size);
          } else {
            range->size = (range->offset + range->size) - offset;
            range->data = (char*)realloc(range->data, range->size);
          }
        }
        ++i;
      }
      break;
    }
    case FALLOC_FL_INSERT_RANGE: {
      for (off_t i = 0; i != inode->dataRangeCount;) {
        struct DataRange* range = inode->dataRanges[i];
        if (offset <= range->offset) {
          range->offset += len;
        } else {
          if (offset >= range->offset + range->size) {
            ++i;
            continue;
          }
          off_t rangeSize = range->size;
          off_t offsetAfterHole = offset - range->offset;
          range->size = offsetAfterHole;
          off_t newRangeLength = rangeSize - offsetAfterHole;
          struct DataRange* newRange = inode->AllocData(end, newRangeLength);
          if (!newRange)
            return -ENOMEM;
          memcpy(newRange->data, range->data + offsetAfterHole, newRangeLength);
          range->data = (char*)realloc(range->data, range->size);
          ++i;
        }
        ++i;
      }
      break;
    }
  }
  return 0;
}
off_t FileSystem::LSeek(unsigned int fdNum, off_t offset, unsigned int whence) {
  if (offset < 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  switch (whence) {
    case SEEK_SET:
      return fd->seekOff = offset;
    case SEEK_CUR: {
      off_t res;
      if (__builtin_add_overflow(fd->seekOff, offset, &res))
        return -EOVERFLOW;
      return fd->seekOff = res;
    }
    case SEEK_END: {
      if (S_ISDIR(inode->mode))
        return -EINVAL;
      off_t res;
      if (__builtin_add_overflow(inode->size, offset, &res))
        return -EOVERFLOW;
      return fd->seekOff = res;
    }
    case SEEK_DATA: {
      if (!S_ISREG(inode->mode))
        return -EINVAL;
      DataIterator it((struct RegularINode*)inode, fd->seekOff);
      off_t res;
      if (!it.IsInData()) {
        if (!it.Next()) {
          if (__builtin_add_overflow(inode->size, offset, &res))
            return -EOVERFLOW;
          return fd->seekOff = res;
        }
        struct DataRange* range = it.GetRange();
        if (__builtin_add_overflow(range->offset, offset, &res))
          return -EOVERFLOW;
        return fd->seekOff = res;
      }
      it.Next();
      if (it.Next()) {
        struct DataRange* range = it.GetRange();
        if (__builtin_add_overflow(range->offset, offset, &res))
          return -EOVERFLOW;
        return fd->seekOff = res;
      }
      if (__builtin_add_overflow(inode->size, offset, &res))
        return -EOVERFLOW;
      return fd->seekOff = res;
    }
    case SEEK_HOLE: {
      if (!S_ISREG(inode->mode))
        return -EINVAL;
      DataIterator it((struct RegularINode*)inode, fd->seekOff);
      off_t res;
      if (it.IsInData()) {
        it.Next();
        struct HoleRange hole = it.GetHole();
        if (__builtin_add_overflow(hole.offset, offset, &res))
          return -EOVERFLOW;
        return fd->seekOff = res;
      }
      if (it.Next()) {
        it.Next();
        struct HoleRange hole = it.GetHole();
        if (__builtin_add_overflow(hole.offset, offset, &res))
          return -EOVERFLOW;
        return fd->seekOff = res;
      }
      if (__builtin_add_overflow(inode->size, offset, &res))
        return -EOVERFLOW;
      return fd->seekOff = res;
    }
  }
  return -EINVAL;
}
ssize_t FileSystem::Read(unsigned int fdNum, char* buf, size_t count) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || fd->flags & O_WRONLY)
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  if (S_ISDIR(inode->mode))
    return -EISDIR;
  if (count == 0)
    return 0;
  if (count > RW_MAX)
    count = RW_MAX;
  if (fd->seekOff >= inode->size)
    return 0;
  off_t end = inode->size - fd->seekOff;
  if (end < count)
    count = end;
  DataIterator it((struct RegularINode*)inode, fd->seekOff);
  for (size_t amountRead = 0; amountRead != count; it.Next()) {
    size_t amount;
    size_t currEnd = fd->seekOff + amountRead;
    if (it.IsInData()) {
      struct DataRange* range = it.GetRange();
      amount = min<size_t>((range->offset + range->size) - currEnd, count - amountRead);
      memcpy(buf + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = it.GetHole();
      amount = min<size_t>((hole.offset + hole.size) - currEnd, count - amountRead);
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
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || fd->flags & O_WRONLY)
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  if (S_ISDIR(inode->mode))
    return -EISDIR;
  if (iovcnt == 0)
    return 0;
  if (iovcnt < 0 || iovcnt > IOV_MAX)
    return -EINVAL;
  size_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    size_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (len < 0)
      return -EINVAL;
    size_t limit = RW_MAX - totalLen;
    if (len > limit) {
      len = limit;
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
  DataIterator it((struct RegularINode*)inode, fd->seekOff);
  for (size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; it.Next()) {
    struct iovec curr = iov[iovIdx];
    size_t amount;
    size_t end = totalLen - count;
    size_t iovEnd = curr.iov_len - amountRead;
    size_t currEnd = fd->seekOff + count;
    if (it.IsInData()) {
      struct DataRange* range = it.GetRange();
      amount = min<size_t>(
        min<size_t>(
          (range->offset + range->size) - currEnd,
          iovEnd),
        end
      );
      memcpy((char*)curr.iov_base + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = it.GetHole();
      amount = min<size_t>(
        min<size_t>(
          (hole.offset + hole.size) - currEnd,
          iovEnd),
        end
      );
      memset((char*)curr.iov_base + amountRead, '\0', amount);
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
  if (offset < 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || fd->flags & O_WRONLY)
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  if (S_ISDIR(inode->mode))
    return -EISDIR;
  if (count == 0)
    return 0;
  if (count > RW_MAX)
    count = RW_MAX;
  if (offset >= inode->size)
    return 0;
  off_t end = inode->size - offset;
  if (end < count)
    count = end;
  DataIterator it((struct RegularINode*)inode, offset);
  for (size_t amountRead = 0; amountRead != count; it.Next()) {
    size_t amount;
    size_t currEnd = offset + amountRead;
    if (it.IsInData()) {
      struct DataRange* range = it.GetRange();
      amount = min<size_t>((range->offset + range->size) - currEnd, count - amountRead);
      memcpy(buf + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = it.GetHole();
      amount = min<size_t>((hole.offset + hole.size) - currEnd, count - amountRead);
      memset(buf + amountRead, '\0', amount);
    }
    amountRead += amount;
  }
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  return count;
}
ssize_t FileSystem::PReadv(unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
  if (offset < 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || fd->flags & O_WRONLY)
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  if (S_ISDIR(inode->mode))
    return -EISDIR;
  if (iovcnt == 0)
    return 0;
  if (iovcnt < 0 || iovcnt > IOV_MAX)
    return -EINVAL;
  size_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    size_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (len < 0)
      return -EINVAL;
    size_t limit = RW_MAX - totalLen;
    if (len > limit) {
      len = limit;
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
  DataIterator it((struct RegularINode*)inode, offset);
  for (size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; it.Next()) {
    struct iovec curr = iov[iovIdx];
    size_t amount;
    size_t end = totalLen - count;
    size_t iovEnd = curr.iov_len - amountRead;
    size_t currEnd = offset + count;
    if (it.IsInData()) {
      struct DataRange* range = it.GetRange();
      amount = min<size_t>(
        min<size_t>(
          (range->offset + range->size) - currEnd,
          iovEnd),
        end
      );
      memcpy((char*)curr.iov_base + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = it.GetHole();
      amount = min<size_t>(
        min<size_t>(
          (hole.offset + hole.size) - currEnd,
          iovEnd),
        end
      );
      memset((char*)curr.iov_base + amountRead, '\0', amount);
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
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || !(fd->flags & (O_WRONLY | O_RDWR)))
    return -EBADF;
  if (count == 0)
    return 0;
  if (count > RW_MAX)
    count = RW_MAX;
  struct BaseINode* inode = fd->inode;
  off_t seekOff = fd->flags & O_APPEND
    ? inode->size
    : fd->seekOff;
  off_t seekEnd;
  if (__builtin_add_overflow(seekOff, count, &seekEnd))
    return -EFBIG;
  struct DataRange* range = ((struct RegularINode*)inode)->AllocData(seekOff, count);
  if (!range)
    return -EIO;
  memcpy(range->data + (seekOff - range->offset), buf, count);
  fd->seekOff = seekEnd;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
ssize_t FileSystem::Writev(unsigned int fdNum, struct iovec* iov, int iovcnt) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || !(fd->flags & (O_WRONLY | O_RDWR)))
    return -EBADF;
  if (iovcnt == 0)
    return 0;
  if (iovcnt < 0 || iovcnt > IOV_MAX)
    return -EINVAL;
  ssize_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (len < 0)
      return -EINVAL;
    size_t limit = RW_MAX - totalLen;
    if (len > limit) {
      len = limit;
      iov[i].iov_len = len;
    }
    totalLen += len;
  }
  if (totalLen == 0)
    return 0;
  struct BaseINode* inode = fd->inode;
  off_t seekOff = fd->flags & O_APPEND
    ? inode->size
    : fd->seekOff;
  off_t seekEnd;
  if (__builtin_add_overflow(seekOff, totalLen, &seekEnd))
    return -EFBIG;
  struct DataRange* range = ((struct RegularINode*)inode)->AllocData(seekOff, totalLen);
  if (!range)
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
  fd->seekOff = seekEnd;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
ssize_t FileSystem::PWrite(unsigned int fdNum, const char* buf, size_t count, off_t offset) {
  if (offset < 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || !(fd->flags & (O_WRONLY | O_RDWR)))
    return -EBADF;
  if (count == 0)
    return 0;
  if (count > RW_MAX)
    count = RW_MAX;
  struct BaseINode* inode = fd->inode;
  {
    off_t res;
    if (__builtin_add_overflow(offset, count, &res))
      return -EFBIG;
  }
  struct DataRange* range = ((struct RegularINode*)inode)->AllocData(offset, count);
  if (!range)
    return -EIO;
  memcpy(range->data + (offset - range->offset), buf, count);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
ssize_t FileSystem::PWritev(unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
  if (offset < 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || !(fd->flags & (O_WRONLY | O_RDWR)))
    return -EBADF;
  if (iovcnt == 0)
    return 0;
  if (iovcnt < 0 || iovcnt > IOV_MAX)
    return -EINVAL;
  ssize_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (len < 0)
      return -EINVAL;
    size_t limit = RW_MAX - totalLen;
    if (len > limit) {
      len = limit;
      iov[i].iov_len = len;
    }
    totalLen += len;
  }
  if (totalLen == 0)
    return 0;
  struct BaseINode* inode = fd->inode;
  {
    off_t res;
    if (__builtin_add_overflow(offset, totalLen, &res))
      return -EFBIG;
  }
  struct DataRange* range = ((struct RegularINode*)inode)->AllocData(offset, totalLen);
  if (!range)
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
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fdIn;
  struct Fd* fdOut;
  if (!(fdIn = GetFd(fs, inFd)) || fdIn->flags & O_WRONLY ||
      !(fdOut = GetFd(fs, outFd)) || !(fdOut->flags & (O_WRONLY | O_RDWR)))
    return -EBADF;
  if (S_ISDIR(fdIn->inode->mode) || fdOut->flags & O_APPEND)
    return -EINVAL;
  off_t off;
  if (offset) {
    if ((off = *offset) < 0)
      return -EINVAL;
  } else off = fdIn->seekOff;
  if (count == 0)
    return 0;
  if (count > RW_MAX)
    count = RW_MAX;
  struct BaseINode* inodeIn = fdIn->inode;
  struct BaseINode* inodeOut = fdOut->inode;
  off_t outSeekEnd;
  if (__builtin_add_overflow(fdOut->seekOff, count, &outSeekEnd))
    return -EFBIG;
  if (off >= inodeIn->size)
    return 0;
  off_t end = inodeIn->size - off;
  if (end < count)
    count = end;
  if (!offset)
    fdIn->seekOff += count;
  else *offset += count;
  if (outSeekEnd > inodeOut->size)
    inodeOut->size = outSeekEnd;
  DataIterator itIn((struct RegularINode*)inodeIn, off);
  DataIterator itOut((struct RegularINode*)inodeOut, fdOut->seekOff);
  for (size_t amountRead = 0; amountRead != count;) {
    size_t amount;
    size_t amountToRead = count - amountRead;
    size_t currEndIn = off + amountRead;
    size_t currEndOut = currEndOut;
    if (!itIn.IsInData()) {
      struct HoleRange holeIn = itIn.GetHole();
      off_t holeInEnd = holeIn.offset + holeIn.size;
      if (!itOut.IsInData()) {
        struct HoleRange holeOut = itOut.GetHole();
        amount = (holeOut.offset + holeOut.size) - currEndOut;
        {
          size_t newAmount = holeInEnd - currEndIn;
          if (amount > newAmount) {
            amount = newAmount;
            itIn.Next();
          } else itOut.Next();
        }
        if (amount == 0)
          continue;
        amountRead += min<size_t>(amount, amountToRead);
        continue;
      }
      struct DataRange* rangeOut = itOut.GetRange();
      amount = (rangeOut->offset + rangeOut->size) - currEndOut;
      {
        size_t newAmount = holeInEnd - currEndIn;
        if (amount > newAmount) {
          amount = newAmount;
          itIn.Next();
        } else itOut.Next();
      }
      if (amount == 0)
        continue;
      if (amount > amountToRead)
        amount = amountToRead;
      memset(rangeOut->data + (currEndOut - rangeOut->offset), '\0', amount);
      amountRead += amount;
      continue;
    }
    struct DataRange* rangeIn = itIn.GetRange();
    amount = min<size_t>((rangeIn->offset + rangeIn->size) - currEndIn, amountToRead);
    struct DataRange* rangeOut = ((struct RegularINode*)inodeOut)->AllocData(currEndOut, amount);
    if (!rangeOut)
      return -EIO;
    memcpy(
      rangeOut->data + (currEndOut - rangeOut->offset),
      rangeIn->data + (currEndIn - rangeIn->offset),
      amount
    );
    amountRead += amount;
    itIn.Next();
    itOut.SeekTo(currEndOut);
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (!(fdIn->flags & O_NOATIME))
    inodeIn->atime = ts;
  inodeOut->mtime = inodeOut->ctime = ts;
  return count;
}
int FileSystem::FTruncate(unsigned int fdNum, off_t length) {
  if (length < 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  if (!S_ISREG(inode->mode) || !(fd->flags & (O_WRONLY | O_RDWR)))
    return -EINVAL;
  if (fd->flags & O_APPEND)
    return -EPERM;
  ((struct RegularINode*)inode)->TruncateData(length);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->ctime = inode->mtime = ts;
  return 0;
}
int FileSystem::Truncate(const char* path, off_t length) {
  if (length < 0)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  if (S_ISDIR(inode->mode))
    return -EISDIR;
  if (!S_ISREG(inode->mode))
    return -EINVAL;
  if (!inode->CanUse(W_OK))
    return -EACCES;
  ((struct RegularINode*)inode)->TruncateData(length);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->ctime = inode->mtime = ts;
  return 0;
}
int FileSystem::FChModAt(int dirFd, const char* path, mode_t mode) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -EBADF;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  inode->mode = (mode & 0777) | (inode->mode & S_IFMT);
  clock_gettime(CLOCK_REALTIME, &inode->ctime);
  return 0;
}
int FileSystem::FChMod(unsigned int fdNum, mode_t mode) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  inode->mode = (mode & 0777) | (inode->mode & S_IFMT);
  clock_gettime(CLOCK_REALTIME, &inode->ctime);
  return 0;
}
int FileSystem::ChDir(const char* path) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  struct DirectoryINode* parent;
  int res = GetINode(fs, path, &inode, &parent, true);
  if (res != 0)
    return res;
  if (!S_ISDIR(inode->mode))
    return -ENOTDIR;
  const char* absPath = AbsolutePath(fs, path);
  if (!absPath)
    return -ENOMEM;
  delete fs->cwd.path;
  fs->cwd.path = absPath;
  fs->cwd.inode = (struct DirectoryINode*)inode;
  fs->cwd.parent = parent;
  return 0;
}
int FileSystem::GetCwd(char* buf, size_t size) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  if (fs->cwd.inode != fs->inodes[0]) {
    struct BaseINode* inode;
    struct DirectoryINode* parent;
    int res = GetINode(fs, fs->cwd.path, &inode, &parent, true);
    if (res != 0)
      return res;
  }
  size_t cwdLen = strlen(fs->cwd.path);
  if (size <= cwdLen)
    return -ERANGE;
  if (buf) {
    memcpy(buf, fs->cwd.path, cwdLen);
    buf[cwdLen] = '\0';
  }
  return cwdLen;
}
int FileSystem::FStat(unsigned int fdNum, struct stat* buf) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  fd->inode->FillStat(buf);
  return 0;
}
int FileSystem::Stat(const char* path, struct stat* buf) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  inode->FillStat(buf);
  return 0;
}
int FileSystem::LStat(const char* path, struct stat* buf) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  inode->FillStat(buf);
  return 0;
}
int FileSystem::Statx(int dirFd, const char* path, int flags, int mask, struct statx* buf) {
  if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH) || mask & ~STATX_ALL ||
      (flags & AT_EMPTY_PATH && path[0] != '\0'))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -EBADF;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    inode = fs->cwd.inode;
  else res = GetINode(fs, path, &inode, NULL, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  inode->FillStatx(buf, mask);
  return 0;
}
int FileSystem::GetXAttr(const char* path, const char* name, void* value, size_t size) {
  if (size != 0 && (strlen(name) > XATTR_NAME_MAX || size > XATTR_SIZE_MAX))
    return -ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  for (size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (size != 0) {
        if (size < attrib->size)
          return -ERANGE;
        if (attrib->size != 0)
          memcpy(value, attrib->data, attrib->size);
      }
      return 0;
    }
  }
  return -ENODATA;
}
int FileSystem::LGetXAttr(const char* path, const char* name, void* value, size_t size) {
  if (size != 0 && (strlen(name) > XATTR_NAME_MAX || size > XATTR_SIZE_MAX))
    return -ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  for (size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (size != 0) {
        if (size < attrib->size)
          return -ERANGE;
        if (attrib->size != 0)
          memcpy(value, attrib->data, attrib->size);
      }
      return 0;
    }
  }
  return -ENODATA;
}
int FileSystem::FGetXAttr(int fdNum, const char* name, void* value, size_t size) {
  if (size != 0 && (strlen(name) > XATTR_NAME_MAX || size > XATTR_SIZE_MAX))
    return -ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  for (size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (size != 0) {
        if (size < attrib->size)
          return -ERANGE;
        if (attrib->size != 0)
          memcpy(value, attrib->data, attrib->size);
      }
      return 0;
    }
  }
  return -ENODATA;
}
int FileSystem::SetXAttr(const char* path, const char* name, void* value, size_t size, int flags) {
  if (strlen(name) > XATTR_NAME_MAX || size > XATTR_SIZE_MAX)
    return -ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  struct Attributes* attribs = &inode->attribs;
  for (size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (flags & XATTR_CREATE)
        return -EEXIST;
      const char* kname;
      if (!(kname = strdup(name)))
        return -ENOMEM;
      char* data = NULL;
      if (size != 0) {
        if (!TryAlloc(&data, size)) {
          delete kname;
          return -ENOMEM;
        }
        memcpy(data, value, size);
      }
      attribs->list[i] = {
        kname,
        size,
        data
      };
      return 0;
    }
  }
  if (flags & XATTR_REPLACE)
    return -ENODATA;
  const char* kname;
  if (!(kname = strdup(name)))
    return -ENOMEM;
  char* data = NULL;
  if (size != 0) {
    if (!TryAlloc(&data, size)) {
      delete kname;
      return -ENOMEM;
    }
    memcpy(data, value, size);
  }
  if (!TryRealloc(&attribs->list, attribs->count + 1)) {
    delete kname;
    if (size != 0)
      delete data;
    return -ENOMEM;
  }
  attribs->list[attribs->count] = {
    kname,
    size,
    data
  };
  ++attribs->count;
  return 0;
}
int FileSystem::LSetXAttr(const char* path, const char* name, void* value, size_t size, int flags) {
  if (strlen(name) > XATTR_NAME_MAX || size > XATTR_SIZE_MAX)
    return -ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  struct Attributes* attribs = &inode->attribs;
  for (size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (flags & XATTR_CREATE)
        return -EEXIST;
      const char* kname;
      if (!(kname = strdup(name)))
        return -ENOMEM;
      char* data = NULL;
      if (size != 0) {
        if (!TryAlloc(&data, size)) {
          delete kname;
          return -ENOMEM;
        }
        memcpy(data, value, size);
      }
      attribs->list[i] = {
        kname,
        size,
        data
      };
      return 0;
    }
  }
  if (flags & XATTR_REPLACE)
    return -ENODATA;
  const char* kname;
  if (!(kname = strdup(name)))
    return -ENOMEM;
  char* data = NULL;
  if (size != 0) {
    if (!TryAlloc(&data, size)) {
      delete kname;
      return -ENOMEM;
    }
    memcpy(data, value, size);
  }
  if (!TryRealloc(&attribs->list, attribs->count + 1)) {
    delete kname;
    if (size != 0)
      delete data;
    return -ENOMEM;
  }
  attribs->list[attribs->count] = {
    kname,
    size,
    data
  };
  ++attribs->count;
  return 0;
}
int FileSystem::FSetXAttr(int fdNum, const char* name, void* value, size_t size, int flags) {
  if (strlen(name) > XATTR_NAME_MAX || size > XATTR_SIZE_MAX)
    return -ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  struct Attributes* attribs = &inode->attribs;
  for (size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (flags & XATTR_CREATE)
        return -EEXIST;
      const char* kname;
      if (!(kname = strdup(name)))
        return -ENOMEM;
      char* data = NULL;
      if (size != 0) {
        if (!TryAlloc(&data, size)) {
          delete kname;
          return -ENOMEM;
        }
        memcpy(data, value, size);
      }
      attribs->list[i] = {
        kname,
        size,
        data
      };
      return 0;
    }
  }
  if (flags & XATTR_REPLACE)
    return -ENODATA;
  const char* kname;
  if (!(kname = strdup(name)))
    return -ENOMEM;
  char* data = NULL;
  if (size != 0) {
    if (!TryAlloc(&data, size)) {
      delete kname;
      return -ENOMEM;
    }
    memcpy(data, value, size);
  }
  if (!TryRealloc(&attribs->list, attribs->count + 1)) {
    delete kname;
    if (size != 0)
      delete data;
    return -ENOMEM;
  }
  attribs->list[attribs->count] = {
    kname,
    size,
    data
  };
  ++attribs->count;
  return 0;
}
int FileSystem::RemoveXAttr(const char* path, const char* name) {
  if (strlen(name) > XATTR_NAME_MAX)
    return -ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  struct Attributes* attribs = &inode->attribs;
  for (size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (attribs->count == 1) {
        delete attribs->list;
        attribs->list = NULL;
        attribs->count = 0;
        return 0;
      }
      if (attrib->size != 0)
        delete attrib->data;
      if (i != attribs->count - 1)
        memmove(&attribs->list[i], &attribs->list[i + 1], sizeof(struct Attribute) * (attribs->count - i));
      attribs->list = (struct Attribute*)realloc(
        attribs->list,
        sizeof(struct Attribute) * --attribs->count
      );
      return 0;
    }
  }
  return -ENODATA;
}
int FileSystem::LRemoveXAttr(const char* path, const char* name) {
  if (strlen(name) > XATTR_NAME_MAX)
    return -ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  struct Attributes* attribs = &inode->attribs;
  for (size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (attribs->count == 1) {
        delete attribs->list;
        attribs->list = NULL;
        attribs->count = 0;
        return 0;
      }
      if (attrib->size != 0)
        delete attrib->data;
      if (i != attribs->count - 1)
        memmove(&attribs->list[i], &attribs->list[i + 1], sizeof(struct Attribute) * (attribs->count - i));
      attribs->list = (struct Attribute*)realloc(
        attribs->list,
        sizeof(struct Attribute) * --attribs->count
      );
      return 0;
    }
  }
  return -ENODATA;
}
int FileSystem::FRemoveXAttr(int fdNum, const char* name) {
  if (strlen(name) > XATTR_NAME_MAX)
    return -ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  struct Attributes* attribs = &inode->attribs;
  for (size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (attribs->count == 1) {
        delete attribs->list;
        attribs->list = NULL;
        attribs->count = 0;
        return 0;
      }
      if (attrib->size != 0)
        delete attrib->data;
      if (i != attribs->count - 1)
        memmove(&attribs->list[i], &attribs->list[i + 1], sizeof(struct Attribute) * (attribs->count - i));
      attribs->list = (struct Attribute*)realloc(
        attribs->list,
        sizeof(struct Attribute) * --attribs->count
      );
      return 0;
    }
  }
  return -ENODATA;
}
ssize_t FileSystem::ListXAttr(const char* path, char* list, size_t size) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  size_t listLen = 0;
  for (size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    listLen += strlen(attrib->name) + 1;
    if ((size != 0 && listLen > size) || listLen > XATTR_LIST_MAX)
      return -ERANGE;
  }
  if (size == 0)
    return listLen;
  size_t i = 0;
  for (size_t j = 0; j != inode->attribs.count; ++j) {
    struct Attribute* attrib = &inode->attribs.list[j];
    size_t nameLen = strlen(attrib->name) + 1;
    memcpy(&list[i], attrib->name, nameLen);
    i += nameLen;
  }
  return listLen;
}
ssize_t FileSystem::LListXAttr(const char* path, char* list, size_t size) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  size_t listLen = 0;
  for (size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    listLen += strlen(attrib->name) + 1;
    if (listLen > size || listLen > XATTR_LIST_MAX)
      return -ERANGE;
  }
  if (size == 0)
    return listLen;
  size_t i = 0;
  for (size_t j = 0; j != inode->attribs.count; ++j) {
    struct Attribute* attrib = &inode->attribs.list[j];
    size_t nameLen = strlen(attrib->name) + 1;
    memcpy(&list[i], attrib->name, nameLen);
    i += nameLen;
  }
  return listLen;
}
ssize_t FileSystem::FListXAttr(int fdNum, char* list, size_t size) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -EBADF;
  struct BaseINode* inode = fd->inode;
  size_t listLen = 0;
  for (size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    listLen += strlen(attrib->name) + 1;
    if (listLen > size || listLen > XATTR_LIST_MAX)
      return -ERANGE;
  }
  if (size == 0)
    return listLen;
  size_t i = 0;
  for (size_t j = 0; j != inode->attribs.count; ++j) {
    struct Attribute* attrib = &inode->attribs.list[j];
    size_t nameLen = strlen(attrib->name) + 1;
    memcpy(&list[i], attrib->name, nameLen);
    i += nameLen;
  }
  return listLen;
}
int FileSystem::UTimeNsAt(int dirFd, const char* path, const struct timespec* times, int flags) {
  if (flags & ~AT_SYMLINK_NOFOLLOW)
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -EBADF;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (res != 0)
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
  if (times && (
        (times[0].tv_usec < 0 || times[0].tv_usec >= 1000000) ||
        (times[1].tv_usec < 0 || times[1].tv_usec >= 1000000)
      ))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (fdNum != AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, fdNum)))
      return -EBADF;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  fs->cwd.inode = origCwd;
  if (res != 0)
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
int FileSystem::UMask(int mask) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  int prev = fs->umask;
  fs->umask = mask & 0777;
  return prev;
}

bool FileSystem::DumpToFile(const char* filename) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  int fd = creat(filename, 0644);
  if (fd < 0)
    goto err1;
  if (write(fd, "\x7FVFS", 4) != 4 ||
      write(fd, &fs->inodeCount, sizeof(ino_t)) != sizeof(ino_t))
    goto err2;
  for (ino_t i = 0; i != fs->inodeCount; ++i) {
    struct BaseINode* inode = fs->inodes[i];
    struct DumpedINode dumped;
    dumped.id = inode->id;
    dumped.size = inode->size;
    dumped.nlink = inode->nlink;
    dumped.mode = inode->mode;
    dumped.btime = {
      inode->btime.tv_sec,
      inode->btime.tv_nsec
    };
    dumped.ctime = {
      inode->ctime.tv_sec,
      inode->ctime.tv_nsec
    };
    dumped.mtime = {
      inode->mtime.tv_sec,
      inode->mtime.tv_nsec
    };
    dumped.atime = {
      inode->atime.tv_sec,
      inode->atime.tv_nsec
    };
    if (write(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode))
      goto err2;
    if (S_ISLNK(inode->mode)) {
      size_t targetLen = strlen(((struct SymLinkINode*)inode)->target) + 1;
      off_t dataLen = inode->size;
      if (write(fd, ((struct SymLinkINode*)inode)->target, targetLen) != targetLen ||
          write(fd, ((struct SymLinkINode*)inode)->data, dataLen) != dataLen)
        goto err2;
    }
    if (S_ISDIR(inode->mode)) {
      if (write(fd, &((struct DirectoryINode*)inode)->dentCount, sizeof(off_t)) != sizeof(off_t) ||
          write(
            fd,
            &((struct DirectoryINode*)inode)->dents[1].inode->ndx,
            sizeof(ino_t)
          ) != sizeof(ino_t))
        goto err2;
      for (off_t j = 2; j != ((struct DirectoryINode*)inode)->dentCount; ++j) {
        struct Dent* dent = &((struct DirectoryINode*)inode)->dents[j];
        size_t nameLen = strlen(dent->name) + 1;
        if (write(fd, &dent->inode->ndx, sizeof(ino_t)) != sizeof(ino_t) ||
            write(fd, dent->name, nameLen) != nameLen)
          goto err2;
      }
    } else if (inode->size != 0) {
      if (write(fd, &((struct RegularINode*)inode)->dataRangeCount, sizeof(off_t)) != sizeof(off_t))
        goto err2;
      for (off_t j = 0; j != ((struct RegularINode*)inode)->dataRangeCount; ++j) {
        struct DataRange* range = ((struct RegularINode*)inode)->dataRanges[j];
        if (write(fd, &range->offset, sizeof(off_t)) != sizeof(off_t) ||
            write(fd, &range->size, sizeof(off_t)) != sizeof(off_t))
          goto err2;
        ssize_t written = 0;
        while (written != range->size) {
          size_t amount = range->size - written;
          if (amount > RW_MAX)
            amount = RW_MAX;
          ssize_t count = write(fd, range->data + written, amount);
          if (count != amount)
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
  if (fd < 0)
    goto err_at_open;
  char magic[4];
  ino_t inodeCount;
  struct BaseINode** inodes;
  if (read(fd, magic, 4) != 4 ||
      memcmp(magic, "\x7FVFS", 4) != 0 ||
      read(fd, &inodeCount, sizeof(ino_t)) != sizeof(ino_t))
    goto err_after_open;
  if (!TryAlloc(&inodes, inodeCount))
    goto err_after_open;
  for (ino_t i = 0; i != inodeCount; ++i) {
    struct BaseINode* inode;
    struct DumpedINode dumped;
    if (read(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode))
      goto err_at_inode_loop;
    if (!TryAllocFromMode(&inode, dumped.mode))
      goto err_at_inode_loop;
    inode->ndx = i;
    inode->id = dumped.id;
    inode->size = dumped.size;
    inode->nlink = dumped.nlink;
    inode->mode = dumped.mode;
    inode->btime = {
      dumped.btime.tv_sec,
      dumped.btime.tv_nsec
    };
    inode->ctime = {
      dumped.ctime.tv_sec,
      dumped.ctime.tv_nsec
    };
    inode->mtime = {
      dumped.mtime.tv_sec,
      dumped.mtime.tv_nsec
    };
    inode->atime = {
      dumped.atime.tv_sec,
      dumped.atime.tv_nsec
    };
    if (S_ISLNK(inode->mode)) {
      char target[PATH_MAX];
      size_t targetLen = 0;
      do {
        if (read(fd, &target[targetLen], 1) != 1)
          goto err_after_inode_init;
      } while (target[targetLen++] != '\0');
      ((struct SymLinkINode*)inode)->target = strdup(target);
      if (!((struct SymLinkINode*)inode)->target)
        goto err_after_inode_init;
      char data[PATH_MAX];
      size_t dataLen = 0;
      do {
        if (read(fd, &data[dataLen], 1) != 1)
          goto err_after_target_alloc;
      } while (data[dataLen++] != '\0');
      if (!TryAlloc(&((struct SymLinkINode*)inode)->data, dataLen))
        goto err_after_target_alloc;
      memcpy(((struct SymLinkINode*)inode)->data, data, dataLen);
      goto success_symlink;

     err_after_target_alloc:
      free((void*)((struct SymLinkINode*)inode)->target);
      goto err_after_inode_init;
     success_symlink: {}
    }
    if (S_ISDIR(inode->mode)) {
      off_t dentCount;
      if (read(fd, &dentCount, sizeof(off_t)) != sizeof(off_t))
        goto err_after_inode_init;
      if (!TryAlloc(&((struct DirectoryINode*)inode)->dents, dentCount))
        goto err_after_inode_init;
      ((struct DirectoryINode*)inode)->dents[0] = { ".", inode };
      ((struct DirectoryINode*)inode)->dents[1].name = "..";
      if (read(
            fd,
            &((struct DirectoryINode*)inode)->dents[1].inode,
            sizeof(ino_t)
          ) != sizeof(ino_t))
        goto err_after_dent_alloc;
      for (off_t j = 2; j != dentCount; ++j) {
        struct Dent* dent = &((struct DirectoryINode*)inode)->dents[j];
        char name[PATH_MAX];
        size_t nameLen = 0;
        if (read(fd, &dent->inode, sizeof(ino_t)) != sizeof(ino_t))
          goto err_after_dent_list_init;
        do {
          if (read(fd, &name[nameLen], 1) != 1)
            goto err_after_dent_list_init;
        } while (name[nameLen++] != '\0');
        dent->name = strdup(name);
        if (!dent->name)
          goto err_after_dent_list_init;
        goto success_dent;

       err_after_dent_list_init:
        for (off_t k = 2; k != j; ++k)
          free((void*)((struct DirectoryINode*)inode)->dents[k].name);
        goto err_after_dent_alloc;
       success_dent: {}
      }
      ((struct DirectoryINode*)inode)->dentCount = dentCount;
      goto success_dents;

     err_after_dent_alloc:
      free(((struct DirectoryINode*)inode)->dents);
      goto err_after_inode_init;
     success_dents: {}
    }
    if (S_ISREG(inode->mode)) {
      if (inode->size != 0) {
        off_t dataRangeCount;
        if (read(fd, &dataRangeCount, sizeof(off_t)) != sizeof(off_t))
          goto err_after_inode_init;
        if (!TryAlloc(&((struct RegularINode*)inode)->dataRanges, dataRangeCount))
          goto err_after_inode_init;
        for (off_t j = 0; j != dataRangeCount; ++j) {
          struct DataRange* range;
          ssize_t nread = 0;
          off_t offset;
          off_t size;
          if (read(fd, &offset, sizeof(off_t)) != sizeof(off_t) ||
              read(fd, &size, sizeof(off_t)) != sizeof(off_t))
            goto err_after_dataranges_init;
          if (offset < 0 || offset > inode->size - size ||
              size   < 0 || size   > inode->size - offset)
            goto err_after_dataranges_init;
          if (!TryAlloc(&range))
            goto err_after_dataranges_init;
          range->offset = offset;
          range->size = size;
          if (!TryAlloc(&range->data, size))
            goto err_after_range_alloc;
          while (nread != size) {
            size_t amount = size - nread;
            if (amount > RW_MAX)
              amount = RW_MAX;
            ssize_t count = read(fd, range->data + nread, amount);
            if (count != amount)
              goto err_after_range_data_alloc;
            nread += count;
          }
          ((struct RegularINode*)inode)->dataRanges[j] = range;
          goto success_data_range;

         err_after_range_data_alloc:
          free(range->data);
         err_after_range_alloc:
          free(range);
          goto err_after_dataranges_init;
         success_data_range: {}
        }
        ((struct RegularINode*)inode)->dataRangeCount = dataRangeCount;
        goto success_data;

       err_after_dataranges_init:
        for (off_t k = 0; k != dataRangeCount; ++k)
          delete ((struct RegularINode*)inode)->dataRanges[k];
        delete ((struct RegularINode*)inode)->dataRanges;
        goto err_after_inode_init;
       success_data: {}
      } else {
        ((struct RegularINode*)inode)->dataRanges = NULL;
        ((struct RegularINode*)inode)->dataRangeCount = 0;
      }
    }
    inodes[i] = inode;
    goto success_inode;

   err_after_inode_init:
    free(inode);
   err_at_inode_loop:
    for (ino_t j = 0; j != i; ++i)
      DeleteINode(inodes[j]);
    goto err_after_inode_list_init;
   success_inode: {}
  }
  for (ino_t i = 0; i != inodeCount; ++i) {
    struct BaseINode* inode = inodes[i];
    if (S_ISDIR(inode->mode)) {
      for (off_t j = 1; j != ((struct DirectoryINode*)inode)->dentCount; ++j) {
        struct Dent* dent = &((struct DirectoryINode*)inode)->dents[j];
        ino_t ino = (ino_t)dent->inode;
        if (ino >= inodeCount)
          goto err_after_inodes;
        dent->inode = inodes[ino];
      }
    }
  }
  for (ino_t i = 0; i != inodeCount;) {
    struct BaseINode* inode = inodes[i];
    if (inode->nlink == 0) {
      DeleteINode(inode);
      if (i != inodeCount - 1) {
        memmove(inodes + i, inodes + i + 1, sizeof(struct INode*) * (inodeCount - i));
        for (ino_t j = i; j != inodeCount - 1; ++j)
          --inodes[j]->ndx;
      }
      inodes = (struct BaseINode**)realloc(inodes, sizeof(struct INode*) * --inodeCount);
    } else ++i;
  }
  close(fd);
  FileSystem* fs;
  struct FSInternal* data;
  if (!TryAlloc(&fs))
    goto err_after_inodes;
  if (!TryAlloc<true>(&data))
    goto err_after_fs_init;
  data->inodes = inodes;
  data->inodeCount = inodeCount;
  if (!(data->cwd.path = strdup("/")))
    goto err_after_fsdata_init;
  data->cwd.inode = data->cwd.parent = (struct DirectoryINode*)inodes[0];
  fs->data = data;
  return fs;

 err_after_fsdata_init:
  free(data);
 err_after_fs_init:
  free(fs);
 err_after_inodes:
  for (ino_t i = 0; i != inodeCount; ++i)
    DeleteINode(inodes[i]);
 err_after_inode_list_init:
  free(inodes);
 err_after_open:
  close(fd);
 err_at_open:
  return NULL;
}