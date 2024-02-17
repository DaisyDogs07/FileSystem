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
#include <pthread.h>
#include <stdlib.h>

#define LIKELY(expr) __builtin_expect(!!(expr), 1)
#define UNLIKELY(expr) __builtin_expect(!!(expr), 0)

bool TryAlloc(void** ptr, size_t length) {
  void* newPtr = malloc(length);
  if (UNLIKELY(!newPtr))
    return false;
  *ptr = newPtr;
  return true;
}
bool TryRealloc(void** ptr, size_t length) {
  void* newPtr = realloc(*ptr, length);
  if (UNLIKELY(!newPtr))
    return false;
  *ptr = newPtr;
  return true;
}

size_t MinUnsigned(size_t a, size_t b) {
  if (a < b)
    return a;
  return b;
}
ssize_t MinSigned(ssize_t a, ssize_t b) {
  if (a < b)
    return a;
  return b;
}

struct INode;

struct Dent {
  const char* name;
  struct INode* inode;
};

#define Dent_Create(d_name, d_inode) ({ \
  struct Dent _d; \
  _d.name = d_name; \
  _d.inode = d_inode; \
  _d; \
})

struct DataRange {
  off_t offset;
  off_t size;
  char* data;
};

void DataRange_Delete(struct DataRange* this) {
  free(this->data);
  free(this);
}

struct HoleRange {
  off_t offset;
  off_t size;
};

struct INode {
  ino_t ndx;
  ino_t id;
  const char* target;
  struct Dent* dents;
  off_t dentCount;
  struct DataRange** dataRanges;
  off_t dataRangeCount;
  off_t size;
  nlink_t nlink;
  mode_t mode;
  struct timespec btime;
  struct timespec ctime;
  struct timespec mtime;
  struct timespec atime;
};

struct DataIterator {
  struct INode* inode;
  off_t rangeIdx;
  bool atData;
  bool isBeforeFirstRange;
};

void DataIterator_New(struct DataIterator* this, struct INode* inode, off_t offset) {
  this->inode = inode;
  if (inode->dataRangeCount == 0 ||
      offset < inode->dataRanges[0]->offset) {
    this->rangeIdx = 0;
    this->atData = false;
    this->isBeforeFirstRange = true;
  } else if (offset >= inode->dataRanges[inode->dataRangeCount - 1]->offset + inode->dataRanges[inode->dataRangeCount - 1]->size) {
    this->rangeIdx = inode->dataRangeCount - 1;
    this->atData = false;
    this->isBeforeFirstRange = false;
  } else {
    this->isBeforeFirstRange = false;
    off_t low = 0;
    off_t high = inode->dataRangeCount - 1;
    while (low <= high) {
      off_t mid = low + ((high - low) / 2);
      struct DataRange* range = inode->dataRanges[mid];
      if (offset >= range->offset) {
        if (offset < range->offset + range->size) {
          this->rangeIdx = mid;
          this->atData = true;
          break;
        }
        low = mid + 1;
        struct DataRange* nextRange = inode->dataRanges[low];
        if (offset >= range->offset + range->size &&
            offset < nextRange->offset) {
          this->rangeIdx = mid;
          this->atData = false;
          break;
        }
      } else {
        high = mid - 1;
        struct DataRange* prevRange = inode->dataRanges[high];
        if (offset >= prevRange->offset + prevRange->size &&
            offset < range->offset) {
          this->rangeIdx = high;
          this->atData = false;
          break;
        }
      }
    }
  }
}

bool DataIterator_IsInData(struct DataIterator* this) {
  return this->atData;
}
off_t DataIterator_GetRangeIdx(struct DataIterator* this) {
  return this->rangeIdx;
}
bool DataIterator_BeforeFirstRange(struct DataIterator* this) {
  return this->isBeforeFirstRange;
}
struct DataRange* DataIterator_GetRange(struct DataIterator* this) {
  return this->inode->dataRanges[this->rangeIdx];
}
struct HoleRange DataIterator_GetHole(struct DataIterator* this) {
  struct HoleRange hole;
  if (this->isBeforeFirstRange) {
    hole.offset = 0;
    if (this->inode->dataRangeCount == 0)
      hole.size = this->inode->size;
    else hole.size = this->inode->dataRanges[0]->offset;
    return hole;
  }
  struct DataRange* currRange = this->inode->dataRanges[this->rangeIdx];
  hole.offset = currRange->offset + currRange->size;
  if (this->rangeIdx < this->inode->dataRangeCount - 1)
    hole.size = this->inode->dataRanges[this->rangeIdx + 1]->offset - hole.offset;
  else hole.size = this->inode->size - hole.offset;
  return hole;
}
bool DataIterator_Next(struct DataIterator* this) {
  if (!this->atData) {
    if (this->isBeforeFirstRange) {
      if (this->inode->dataRangeCount == 0)
        return false;
      this->isBeforeFirstRange = false;
    } else if (this->rangeIdx == this->inode->dataRangeCount - 1)
      return false;
    else ++this->rangeIdx;
  }
  this->atData = !this->atData;
  return true;
}
void DataIterator_SeekTo(struct DataIterator* this, off_t offset) {
  do {
    off_t end;
    if (this->atData) {
      struct DataRange* range = DataIterator_GetRange(this);
      end = range->offset + range->size;
    } else {
      struct HoleRange hole = DataIterator_GetHole(this);
      end = hole.offset + hole.size;
    }
    if (end >= offset)
      break;
  } while (DataIterator_Next(this));
}

void INode_New(struct INode* this) {
  this->target = NULL;
  this->dents = NULL;
  this->dentCount = 0;
  this->dataRanges = NULL;
  this->dataRangeCount = 0;
  this->size = 0;
  this->nlink = 0;
  clock_gettime(CLOCK_REALTIME, &this->btime);
  this->ctime = this->mtime = this->atime = this->btime;
}
void INode_Delete(struct INode* this) {
  if (this->dataRanges) {
    for (off_t i = 0; i != this->dataRangeCount; ++i)
      DataRange_Delete(this->dataRanges[i]);
    free(this->dataRanges);
  }
  if (this->target)
    free(this->target);
  if (this->dents) {
    for (off_t i = 2; i != this->dentCount; ++i)
      free(this->dents[i].name);
    free(this->dents);
  }
  free(this);
}

bool INode_PushDent(struct INode* this, const char* name, struct INode* inode) {
  if (UNLIKELY(!TryRealloc(&this->dents, sizeof(struct Dent) * (this->dentCount + 1))))
    return false;
  this->dents[this->dentCount++] = Dent_Create(name, inode);
  this->size += strlen(name);
  return true;
}
void INode_RemoveDent(struct INode* this, const char* name) {
  for (off_t i = 2; i != this->dentCount; ++i) {
    const char* d_name = this->dents[i].name;
    if (strcmp(d_name, name) == 0) {
      free(d_name);
      if (i != this->dentCount - 1)
        memmove(&this->dents[i], &this->dents[i + 1], sizeof(struct Dent) * (this->dentCount - i));
      this->dents = realloc(this->dents, sizeof(struct Dent) * --this->dentCount);
      this->size -= strlen(name);
      break;
    }
  }
}
bool INode_IsInSelf(struct INode* this, struct INode* inode) {
  for (off_t i = 2; i != this->dentCount; ++i) {
    struct INode* dentInode = this->dents[i].inode;
    if (dentInode == inode ||
        (S_ISDIR(dentInode->mode) && INode_IsInSelf(dentInode, inode)))
      return true;
  }
  return false;
}

struct DataRange* INode_InsertRange(struct INode* this, off_t offset, off_t length, off_t* index) {
  struct DataRange* range;
  if (UNLIKELY(!TryAlloc(&range, sizeof(struct DataRange))))
    goto err_alloc_failed;
  if (this->dataRangeCount == 0) {
    if (UNLIKELY(!TryAlloc(&this->dataRanges, sizeof(struct DataRange*))))
      goto err_after_alloc;
    range->offset = offset;
    range->size = length;
    *index = 0;
    this->dataRanges[0] = range;
    this->dataRangeCount = 1;
    return range;
  }
  if (UNLIKELY(!TryRealloc(&this->dataRanges, sizeof(struct DataRange*) * (this->dataRangeCount + 1))))
    goto err_after_alloc;
  if (offset > this->dataRanges[this->dataRangeCount - 1]->offset) {
    range->offset = offset;
    range->size = length;
    *index = this->dataRangeCount;
    this->dataRanges[this->dataRangeCount++] = range;
    return range;
  }
  off_t rangeIdx;
  {
    off_t low = 0;
    off_t high = this->dataRangeCount - 1;
    while (low <= high) {
      off_t mid = low + ((high - low) / 2);
      struct DataRange* range2 = this->dataRanges[mid];
      if (offset >= range2->offset)
        low = mid + 1;
      else {
        high = mid - 1;
        rangeIdx = mid;
      }
    }
  }
  range->offset = offset;
  range->size = length;
  memmove(&this->dataRanges[rangeIdx + 1], &this->dataRanges[rangeIdx], sizeof(struct DataRange*) * (this->dataRangeCount - rangeIdx));
  *index = rangeIdx;
  this->dataRanges[rangeIdx] = range;
  ++this->dataRangeCount;
  return range;

 err_after_alloc:
  free(range);
 err_alloc_failed:
  return NULL;
}
void INode_RemoveRange(struct INode* this, off_t index) {
  struct DataRange* range = this->dataRanges[index];
  free(range);
  if (index != this->dataRangeCount - 1)
    memmove(&this->dataRanges[index], &this->dataRanges[index + 1], sizeof(struct DataRange*) * (this->dataRangeCount - index));
  this->dataRanges = realloc(this->dataRanges, sizeof(struct DataRange*) * --this->dataRangeCount);
}
void INode_RemoveRanges(struct INode* this, off_t index, off_t count) {
  for (off_t i = index; i != index + count; ++i)
    free(this->dataRanges[i]);
  if (index + count < this->dataRangeCount)
    memmove(&this->dataRanges[index], &this->dataRanges[index + count], sizeof(struct DataRange*) * (this->dataRangeCount - (index + count)));
  this->dataRangeCount -= count;
  this->dataRanges = realloc(this->dataRanges, sizeof(struct DataRange*) * this->dataRangeCount);
}
struct DataRange* INode_AllocData(struct INode* this, off_t offset, off_t length) {
  off_t rangeIdx;
  bool createdRange = false;
  struct DataRange* range = NULL;
  if (this->dataRangeCount != 0) {
    struct DataIterator it;
    DataIterator_New(&it, this, offset);
    for (off_t i = DataIterator_GetRangeIdx(&it); i != this->dataRangeCount; ++i) {
      struct DataRange* range2 = this->dataRanges[i];
      if (offset + length == range2->offset) {
        struct DataRange* range3 = NULL;
        for (off_t j = i - 1; j >= 0; --j) {
          struct DataRange* range4 = this->dataRanges[j];
          if (offset <= range4->offset + range4->size) {
            rangeIdx = j;
            range3 = range4;
          } else break;
        }
        if (range3) {
          off_t off = MinSigned(range3->offset, offset);
          off_t newRangeLength = range2->size + (range2->offset - off);
          if (UNLIKELY(!TryRealloc(&range3->data, newRangeLength)))
            return NULL;
          memmove(&range3->data[newRangeLength - range2->size], range2->data, range2->size);
          range3->size = newRangeLength;
          for (off_t j = rangeIdx + 1; j < i; ++j) {
            struct DataRange* range4 = this->dataRanges[j];
            memmove(&range3->data[range4->offset - off], range4->data, range4->size);
          }
          INode_RemoveRanges(this, rangeIdx + 1, i - rangeIdx);
          range3->offset = off;
          return range3;
        } else {
          off_t newRangeLength = range2->size + (range2->offset - offset);
          if (UNLIKELY(!TryRealloc(&range2->data, newRangeLength)))
            return NULL;
          memmove(&range2->data[newRangeLength - range2->size], range2->data, range2->size);
          range2->size = newRangeLength;
          range2->offset = offset;
          return range2;
        }
      } else if (offset + length < range2->offset)
        break;
    }
    if (!DataIterator_BeforeFirstRange(&it)) {
      struct DataRange* range2 = DataIterator_GetRange(&it);
      if (offset <= range2->offset + range2->size) {
        rangeIdx = DataIterator_GetRangeIdx(&it);
        range = range2;
      }
    }
  }
  if (!range) {
    range = INode_InsertRange(this, offset, length, &rangeIdx);
    if (UNLIKELY(!range))
      return NULL;
    createdRange = true;
  } else if (offset >= range->offset &&
      offset + length <= range->offset + range->size)
    return range;
  off_t newRangeLength = length + (offset - range->offset);
  for (off_t i = rangeIdx + 1; i < this->dataRangeCount; ++i) {
    struct DataRange* range2 = this->dataRanges[i];
    if (range2->offset < offset + length) {
      if (newRangeLength < (range2->offset - range->offset) + range2->size) {
        newRangeLength = (range2->offset - range->offset) + range2->size;
        break;
      }
    } else break;
  }
  if (createdRange) {
    if (UNLIKELY(!TryAlloc(&range->data, newRangeLength))) {
      INode_RemoveRange(this, rangeIdx);
      return NULL;
    }
  } else if (UNLIKELY(!TryRealloc(&range->data, newRangeLength)))
    return NULL;
  range->size = newRangeLength;
  if (this->size < offset + length)
    this->size = offset + length;
  for (off_t i = rangeIdx + 1; i < this->dataRangeCount; ++i) {
    struct DataRange* range2 = this->dataRanges[i];
    if (range2->offset < offset + length)
      memcpy(&range->data[range2->offset - range->offset], range2->data, range2->size);
    else {
      off_t n = i - (rangeIdx + 1);
      if (UNLIKELY(n == 0))
        break;
      INode_RemoveRanges(this, rangeIdx + 1, n);
      break;
    }
  }
  return range;
}
void INode_TruncateData(struct INode* this, off_t length) {
  if (length >= this->size) {
    this->size = length;
    return;
  }
  this->size = length;
  if (length == 0) {
    for (off_t i = 0; i != this->dataRangeCount; ++i)
      DataRange_Delete(this->dataRanges[i]);
    free(this->dataRanges);
    this->dataRanges = NULL;
    this->dataRangeCount = 0;
    return;
  }
  for (off_t i = this->dataRangeCount - 1; i >= 0; --i) {
    struct DataRange* range = this->dataRanges[i];
    if (length > range->offset) {
      INode_RemoveRanges(this, i + 1, this->dataRangeCount - (i + 1));
      if (length - range->offset < range->size) {
        range->size = length - range->offset;
        range->data = realloc(range->data, range->size);
      }
      break;
    }
  }
}
bool INode_CanUse(struct INode* this, int perms) {
  if ((this->mode & perms) != perms &&
      (this->mode & (perms << 3)) != (perms << 3) &&
      (this->mode & (perms << 6)) != (perms << 6))
    return false;
  return true;
}
bool INode_IsUnused(struct INode* this) {
  if (S_ISDIR(this->mode))
    return this->nlink == 1;
  return this->nlink == 0;
}
void INode_FillStat(struct INode* this, struct stat* buf) {
  memset(buf, '\0', sizeof(struct stat));
  buf->st_ino = this->id;
  buf->st_mode = this->mode;
  buf->st_nlink = this->nlink;
  buf->st_size = this->size;
  buf->st_atim = this->atime;
  buf->st_mtim = this->mtime;
  buf->st_ctim = this->ctime;
}
void INode_FillStatx(struct INode* this, struct statx* buf, int mask) {
  memset(buf, '\0', sizeof(struct statx));
  buf->stx_mask = mask & (
    STATX_INO   | STATX_TYPE  | STATX_MODE  |
    STATX_NLINK | STATX_SIZE  | STATX_ATIME |
    STATX_MTIME | STATX_CTIME | STATX_BTIME
  );
  if (mask & STATX_INO)
    buf->stx_ino = this->id;
  if (mask & STATX_TYPE)
    buf->stx_mode |= this->mode & S_IFMT;
  if (mask & STATX_MODE)
    buf->stx_mode |= this->mode & ~S_IFMT;
  if (mask & STATX_NLINK)
    buf->stx_nlink = this->nlink;
  if (mask & STATX_SIZE)
    buf->stx_size = this->size;
  if (mask & STATX_ATIME) {
    buf->stx_atime.tv_sec = this->atime.tv_sec;
    buf->stx_atime.tv_nsec = this->atime.tv_nsec;
  }
  if (mask & STATX_MTIME) {
    buf->stx_mtime.tv_sec = this->mtime.tv_sec;
    buf->stx_mtime.tv_nsec = this->mtime.tv_nsec;
  }
  if (mask & STATX_CTIME) {
    buf->stx_ctime.tv_sec = this->ctime.tv_sec;
    buf->stx_ctime.tv_nsec = this->ctime.tv_nsec;
  }
  if (mask & STATX_BTIME) {
    buf->stx_btime.tv_sec = this->btime.tv_sec;
    buf->stx_btime.tv_nsec = this->btime.tv_nsec;
  }
}

struct Fd {
  struct INode* inode;
  int fd;
  int flags;
  off_t seekOff;
};

void Fd_New(struct Fd* this) {
  this->seekOff = 0;
}

struct Cwd {
  const char* path;
  struct INode* inode;
  struct INode* parent;
};

struct FSInternal {
  struct INode** inodes;
  ino_t inodeCount;
  struct Fd** fds;
  int fdCount;
  struct Cwd cwd;
  pthread_mutex_t mtx;
};

void FSInternal_New(struct FSInternal* this) {
  this->inodes = NULL;
  this->inodeCount = 0;
  this->fds = NULL;
  this->fdCount = 0;
  this->cwd.path = NULL;
  pthread_mutex_init(&this->mtx, NULL);
}
void FSInternal_Delete(struct FSInternal* this) {
  pthread_mutex_destroy(&this->mtx);
  for (ino_t i = 0; i != this->inodeCount; ++i)
    INode_Delete(this->inodes[i]);
  free(this->inodes);
  if (this->fds) {
    for (int i = 0; i != this->fdCount; ++i)
      free(this->fds[i]);
    free(this->fds);
  }
  if (this->cwd.path)
    free(this->cwd.path);
  free(this);
}

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
    memmove(&fs->inodes[id + 1], &fs->inodes[id], sizeof(struct INode*) * (fs->inodeCount - id));
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
  INode_Delete(inode);
  if (i != fs->inodeCount - 1) {
    memmove(&fs->inodes[i], &fs->inodes[i + 1], sizeof(struct INode*) * (fs->inodeCount - i));
    do {
      --fs->inodes[i++]->ndx;
    } while (i != fs->inodeCount - 1);
  }
  fs->inodes = realloc(fs->inodes, sizeof(struct INode*) * --fs->inodeCount);
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
  if (UNLIKELY(!TryAlloc(&fd, sizeof(struct Fd))))
    return -ENOMEM;
  Fd_New(fd);
  if (UNLIKELY(!TryRealloc(&fs->fds, sizeof(struct Fd*) * (fs->fdCount + 1)))) {
    free(fd);
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
  struct INode* inode = fd->inode;
  if (inode->nlink == 0)
    RemoveINode(fs, inode);
  free(fd);
  if (i != fs->fdCount - 1)
    memmove(&fs->fds[i], &fs->fds[i + 1], sizeof(struct Fd*) * (fs->fdCount - i));
  if (--fs->fdCount == 0) {
    free(fs->fds);
    fs->fds = NULL;
    return;
  }
  fs->fds = realloc(fs->fds, sizeof(struct Fd*) * fs->fdCount);
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
  struct INode** parent,
  bool followResolved,
  int* follow
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
    : fs->cwd.inode;
  struct INode* currParent = isAbsolute
    ? fs->inodes[0]
    : fs->cwd.parent;
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
      if (UNLIKELY(!INode_CanUse(current, X_OK))) {
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
    if (UNLIKELY(!INode_CanUse(current, X_OK)))
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

#define GetINodeParent(fs, path, inode, parent) \
  GetINode(fs, path, inode, parent, false, NULL)
#define GetINodeParentFollow(fs, path, inode, parent, followResolved) \
  GetINode(fs, path, inode, parent, followResolved, NULL)
#define GetINodeNoParent(fs, path, inode) \
  GetINode(fs, path, inode, NULL, false, NULL)
#define GetINodeNoParentFollow(fs, path, inode, followResolved) \
  GetINode(fs, path, inode, NULL, followResolved, NULL)

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
  free(absPath);
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

struct FileSystem* FileSystem_New() {
  struct FSInternal* data;
  if (UNLIKELY(!TryAlloc(&data, sizeof(struct FSInternal))))
    return NULL;
  FSInternal_New(data);
  struct INode* root;
  if (UNLIKELY(!TryAlloc(&data->inodes, sizeof(struct INode*))) ||
      UNLIKELY(!TryAlloc(&root, sizeof(struct INode)))) {
    FSInternal_Delete(data);
    return NULL;
  }
  INode_New(root);
  if (UNLIKELY(!TryAlloc(&root->dents, sizeof(struct Dent) * 2))) {
    INode_Delete(root);
    FSInternal_Delete(data);
    return NULL;
  }
  root->mode = 0755 | S_IFDIR;
  root->dents[0] = Dent_Create(".", root);
  root->dents[1] = Dent_Create("..", root);
  root->dentCount = root->nlink = 2;
  if (UNLIKELY(!PushINode(data, root))) {
    INode_Delete(root);
    FSInternal_Delete(data);
    return NULL;
  }
  if (UNLIKELY(!(data->cwd.path = strdup("/")))) {
    FSInternal_Delete(data);
    return NULL;
  }
  data->cwd.inode = root;
  data->cwd.parent = root;
  struct FileSystem* fs;
  if (UNLIKELY(!TryAlloc(&fs, sizeof(struct FileSystem)))) {
    FSInternal_Delete(data);
    return NULL;
  }
  fs->data = data;
  return fs;
}
void FileSystem_Delete(struct FileSystem* this) {
  FSInternal_Delete(this->data);
  free(this);
}

int FileSystem_FAccessAt2(struct FileSystem* this, int dirFd, const char* path, int mode, int flags) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(mode & ~(F_OK | R_OK | W_OK | X_OK)) ||
      UNLIKELY(flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) ||
      UNLIKELY(flags & AT_EMPTY_PATH && path[0] != '\0'))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(fd->inode->mode) && !(flags & AT_EMPTY_PATH))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
    fs->cwd.inode = fd->inode;
  }
  struct INode* inode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    inode = fs->cwd.inode;
  else res = GetINodeNoParentFollow(fs, path, &inode, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (mode != F_OK && !INode_CanUse(inode, mode)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EACCES;
  }
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_FAccessAt(struct FileSystem* this, int dirFd, const char* path, int mode) {
  return FileSystem_FAccessAt2(this, dirFd, path, mode, F_OK);
}
int FileSystem_Access(struct FileSystem* this, const char* path, int mode) {
  return FileSystem_FAccessAt2(this, AT_FDCWD, path, mode, F_OK);
}
int FileSystem_OpenAt(struct FileSystem* this, int dirFd, const char* path, int flags, mode_t mode) {
  struct FSInternal* fs = this->data;
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
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(fd->inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
    fs->cwd.inode = fd->inode;
  }
  if (flags & O_CREAT && flags & O_EXCL)
    flags |= O_NOFOLLOW;
  if (flags & O_WRONLY && flags & O_RDWR)
    flags &= ~O_RDWR;
  struct INode* inode;
  struct INode* parent = NULL;
  int res = GetINodeParentFollow(fs, path, &inode, &parent, !(flags & O_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (UNLIKELY(!parent)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (res == 0) {
    if (flags & O_CREAT) {
      if (UNLIKELY(flags & O_EXCL)) {
        pthread_mutex_unlock(&fs->mtx);
        return -EEXIST;
      }
      if (UNLIKELY(S_ISDIR(inode->mode))) {
        pthread_mutex_unlock(&fs->mtx);
        return -EISDIR;
      }
    }
    if (flags & O_NOFOLLOW && UNLIKELY(S_ISLNK(inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ELOOP;
    }
    if (!INode_CanUse(inode, FlagsToPerms(flags))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EACCES;
    }
  } else {
    if (flags & O_CREAT && res == -ENOENT) {
      flags &= ~O_TRUNC;
      const char* name = GetAbsoluteLast(fs, path);
      if (UNLIKELY(!name)) {
        pthread_mutex_unlock(&fs->mtx);
        return -ENOMEM;
      }
      struct INode* x;
      if (UNLIKELY(!TryAlloc(&x, sizeof(struct INode)))) {
        pthread_mutex_unlock(&fs->mtx);
        free(name);
        return -EIO;
      }
      INode_New(x);
      if (UNLIKELY(!PushINode(fs, x))) {
        pthread_mutex_unlock(&fs->mtx);
        INode_Delete(x);
        free(name);
        return -EIO;
      }
      if (UNLIKELY(!INode_PushDent(parent, name, x))) {
        RemoveINode(fs, x);
        pthread_mutex_unlock(&fs->mtx);
        free(name);
        return -EIO;
      }
      x->mode = mode;
      x->nlink = 1;
      parent->ctime = parent->mtime = x->btime;
      res = PushFd(fs, x, flags);
      if (UNLIKELY(res < 0)) {
        INode_RemoveDent(parent, name);
        RemoveINode(fs, x);
        free(name);
      }
    }
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (S_ISDIR(inode->mode)) {
    if (flags & 020000000) {
      struct INode* x;
      if (UNLIKELY(!TryAlloc(&x, sizeof(struct INode)))) {
        pthread_mutex_unlock(&fs->mtx);
        return -EIO;
      }
      INode_New(x);
      if (UNLIKELY(!PushINode(fs, x))) {
        pthread_mutex_unlock(&fs->mtx);
        INode_Delete(x);
        return -EIO;
      }
      x->mode = (mode & ~S_IFMT) | S_IFREG;
      res = PushFd(fs, x, flags);
      if (UNLIKELY(res < 0))
        RemoveINode(fs, x);
      pthread_mutex_unlock(&fs->mtx);
      return res;
    }
    if (UNLIKELY(flags & (O_WRONLY | O_RDWR))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EISDIR;
    }
  } else {
    if (UNLIKELY(flags & O_DIRECTORY)) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
    if (flags & O_TRUNC && inode->size != 0)
      INode_TruncateData(inode, 0);
  }
  res = PushFd(fs, inode, flags);
  pthread_mutex_unlock(&fs->mtx);
  return res;
}
int FileSystem_Open(struct FileSystem* this, const char* path, int flags, mode_t mode) {
  return FileSystem_OpenAt(this, AT_FDCWD, path, flags, mode);
}
int FileSystem_Creat(struct FileSystem* this, const char* path, mode_t mode) {
  return FileSystem_OpenAt(this, AT_FDCWD, path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}
int FileSystem_Close(struct FileSystem* this, unsigned int fd) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  int res = RemoveFd(fs, fd);
  pthread_mutex_unlock(&fs->mtx);
  return res;
}
int FileSystem_CloseRange(struct FileSystem* this, unsigned int fd, unsigned int maxFd, unsigned int flags) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(flags != 0) ||
      UNLIKELY(fd > maxFd))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  for (int i = 0; i != fs->fdCount; ++i) {
    struct Fd* f = fs->fds[i];
    if (f->fd > fd) {
      RemoveFd2(fs, f, i);
      for (++i; i != fs->fdCount; ++i) {
        f = fs->fds[i];
        if (f->fd < maxFd)
          RemoveFd2(fs, f, i);
        else break;
      }
      break;
    }
  }
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_MkNodAt(struct FileSystem* this, int dirFd, const char* path, mode_t mode, dev_t dev) {
  struct FSInternal* fs = this->data;
  if (mode & S_IFMT) {
    if (UNLIKELY(S_ISDIR(mode)))
      return -EPERM;
    if (UNLIKELY(!S_ISREG(mode)))
      return -EINVAL;
  }
  if (UNLIKELY(dev != 0))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(fd->inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
    fs->cwd.inode = fd->inode;
  }
  struct INode* inode;
  struct INode* parent = NULL;
  int res = GetINodeParent(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(!parent) ||
      UNLIKELY(res != -ENOENT)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (UNLIKELY(res == 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EEXIST;
  }
  const char* name = GetAbsoluteLast(fs, path);
  if (UNLIKELY(!name)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  struct INode* x;
  if (UNLIKELY(!TryAlloc(&x, sizeof(struct INode)))) {
    pthread_mutex_unlock(&fs->mtx);
    free(name);
    return -EIO;
  }
  INode_New(x);
  if (UNLIKELY(!PushINode(fs, x))) {
    pthread_mutex_unlock(&fs->mtx);
    INode_Delete(x);
    free(name);
    return -EIO;
  }
  if (UNLIKELY(!INode_PushDent(parent, name, x))) {
    RemoveINode(fs, x);
    pthread_mutex_unlock(&fs->mtx);
    free(name);
    return -EIO;
  }
  x->mode = (mode & 07777) | S_IFREG;
  x->nlink = 1;
  parent->ctime = parent->mtime = x->btime;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_MkNod(struct FileSystem* this, const char* path, mode_t mode, dev_t dev) {
  return FileSystem_MkNodAt(this, AT_FDCWD, path, mode, dev);
}
int FileSystem_MkDirAt(struct FileSystem* this, int dirFd, const char* path, mode_t mode) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(fd->inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
    fs->cwd.inode = fd->inode;
  }
  struct INode* inode;
  struct INode* parent = NULL;
  int res = GetINodeParent(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(!parent) ||
      UNLIKELY(res != -ENOENT)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (UNLIKELY(res == 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EEXIST;
  }
  const char* name = GetAbsoluteLast(fs, path);
  if (UNLIKELY(!name)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  struct INode* x;
  if (UNLIKELY(!TryAlloc(&x, sizeof(struct INode)))) {
    pthread_mutex_unlock(&fs->mtx);
    free(name);
    return -EIO;
  }
  INode_New(x);
  if (UNLIKELY(!TryAlloc(&x->dents, sizeof(struct Dent) * 2)) ||
      UNLIKELY(!PushINode(fs, x))) {
    pthread_mutex_unlock(&fs->mtx);
    INode_Delete(x);
    free(name);
    return -EIO;
  }
  if (UNLIKELY(!INode_PushDent(parent, name, x))) {
    RemoveINode(fs, x);
    pthread_mutex_unlock(&fs->mtx);
    free(name);
    return -EIO;
  }
  x->mode = (mode & 07777) | S_IFDIR;
  x->nlink = 2;
  x->dents[0] = Dent_Create(".", x);
  x->dents[1] = Dent_Create("..", parent);
  x->dentCount = 2;
  ++parent->nlink;
  parent->ctime = parent->mtime = x->btime;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_MkDir(struct FileSystem* this, const char* path, mode_t mode) {
  return FileSystem_MkDirAt(this, AT_FDCWD, path, mode);
}
int FileSystem_SymLinkAt(struct FileSystem* this, const char* oldPath, int newDirFd, const char* newPath) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (newDirFd != AT_FDCWD) {
    if (UNLIKELY(!(fd = GetFd(fs, newDirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(fd->inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
  }
  struct INode* oldInode;
  int res = GetINodeNoParent(fs, oldPath, &oldInode);
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  struct INode* origCwd = fs->cwd.inode;
  if (newDirFd != AT_FDCWD)
    fs->cwd.inode = fd->inode;
  struct INode* newInode;
  struct INode* newParent = NULL;
  res = GetINodeParent(fs, newPath, &newInode, &newParent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(!newParent)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (UNLIKELY(res == 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EEXIST;
  }
  if (UNLIKELY(res != -ENOENT)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  const char* name = GetAbsoluteLast(fs, newPath);
  if (UNLIKELY(!name)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  struct INode* x;
  if (UNLIKELY(!TryAlloc(&x, sizeof(struct INode)))) {
    pthread_mutex_unlock(&fs->mtx);
    free(name);
    return -EIO;
  }
  INode_New(x);
  size_t oldPathLen = strlen(oldPath);
  struct DataRange* range = INode_AllocData(x, 0, oldPathLen);
  if (UNLIKELY(!range) ||
      UNLIKELY(!PushINode(fs, x))) {
    pthread_mutex_unlock(&fs->mtx);
    INode_Delete(x);
    free(name);
    return -EIO;
  }
  memcpy(range->data, oldPath, oldPathLen);
  if (UNLIKELY(!(x->target = AbsolutePath(fs, oldPath))) ||
      UNLIKELY(!INode_PushDent(newParent, name, x))) {
    RemoveINode(fs, x);
    pthread_mutex_unlock(&fs->mtx);
    free(name);
    return -EIO;
  }
  x->mode = 0777 | S_IFLNK;
  x->nlink = 1;
  x->size = oldPathLen;
  newParent->ctime = newParent->mtime = x->btime;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_SymLink(struct FileSystem* this, const char* oldPath, const char* newPath) {
  return FileSystem_SymLinkAt(this, oldPath, AT_FDCWD, newPath);
}
int FileSystem_ReadLinkAt(struct FileSystem* this, int dirFd, const char* path, char* buf, int bufLen) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(bufLen <= 0))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(fd->inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
    fs->cwd.inode = fd->inode;
  }
  struct INode* inode;
  int res = GetINodeNoParent(fs, path, &inode);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (UNLIKELY(!S_ISLNK(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  if (UNLIKELY(inode->size < bufLen))
    bufLen = inode->size;
  memcpy(buf, inode->dataRanges[0]->data, bufLen);
  clock_gettime(CLOCK_REALTIME, &inode->atime);
  pthread_mutex_unlock(&fs->mtx);
  return bufLen;
}
int FileSystem_ReadLink(struct FileSystem* this, const char* path, char* buf, int bufLen) {
  return FileSystem_ReadLinkAt(this, AT_FDCWD, path, buf, bufLen);
}
int FileSystem_GetDents(struct FileSystem* this, unsigned int fdNum, struct linux_dirent* dirp, unsigned int count) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct INode* inode = fd->inode;
  if (UNLIKELY(!S_ISDIR(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOTDIR;
  }
  if (fd->seekOff >= inode->dentCount) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  unsigned int nread = 0;
  char* dirpData = dirp;
  do {
    struct Dent d = inode->dents[fd->seekOff];
    size_t nameLen = strlen(d.name);
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
    unsigned short reclen = ALIGN(__builtin_offsetof(struct linux_dirent, d_name) + nameLen + 2, sizeof(long));
#undef ALIGN
    if (nread + reclen > count)
      break;
    struct linux_dirent* dent = dirpData;
    dent->d_ino = d.inode->id;
    dent->d_off = fd->seekOff + 1;
    dent->d_reclen = reclen;
    memcpy(dent->d_name, d.name, nameLen);
    dent->d_name[nameLen] = '\0';
    dirpData[reclen - 1] = IFTODT(d.inode->mode);
    dirpData += reclen;
    nread += reclen;
  } while (++fd->seekOff != inode->dentCount);
  if (UNLIKELY(nread == 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  pthread_mutex_unlock(&fs->mtx);
  return nread;
}
int FileSystem_LinkAt(struct FileSystem* this, int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, int flags) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(flags & ~(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH)) ||
      UNLIKELY(flags & AT_EMPTY_PATH && oldPath[0] != '\0'))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* oldFd;
  struct Fd* newFd;
  if (oldDirFd != AT_FDCWD || flags & AT_EMPTY_PATH) {
    if (UNLIKELY(!(oldFd = GetFd(fs, oldDirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (!S_ISDIR(oldFd->inode->mode)) {
      if (UNLIKELY(!(flags & AT_EMPTY_PATH))) {
        pthread_mutex_unlock(&fs->mtx);
        return -ENOTDIR;
      }
    } else if (UNLIKELY(flags & AT_EMPTY_PATH)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EPERM;
    }
  }
  if (newDirFd != AT_FDCWD) {
    if (UNLIKELY(!(newFd = GetFd(fs, newDirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(newFd->inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
  }
  struct INode* origCwd = fs->cwd.inode;
  if (oldDirFd != AT_FDCWD)
    fs->cwd.inode = oldFd->inode;
  struct INode* oldInode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    oldInode = fs->cwd.inode;
  else res = GetINodeNoParentFollow(fs, oldPath, &oldInode, flags & AT_SYMLINK_FOLLOW);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (newDirFd != AT_FDCWD)
    fs->cwd.inode = newFd->inode;
  struct INode* newInode;
  struct INode* newParent = NULL;
  res = GetINodeParent(fs, newPath, &newInode, &newParent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(!newParent)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (UNLIKELY(res == 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EEXIST;
  }
  if (UNLIKELY(res != -ENOENT)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (UNLIKELY(S_ISDIR(oldInode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EPERM;
  }
  const char* name = GetAbsoluteLast(fs, newPath);
  if (UNLIKELY(!name)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  if (UNLIKELY(!INode_PushDent(newParent, name, oldInode))) {
    pthread_mutex_unlock(&fs->mtx);
    free(name);
    return -EIO;
  }
  ++oldInode->nlink;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  oldInode->ctime = newParent->ctime = newParent->mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Link(struct FileSystem* this, const char* oldPath, const char* newPath) {
  return FileSystem_LinkAt(this, AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
}
int FileSystem_UnlinkAt(struct FileSystem* this, int dirFd, const char* path, int flags) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(flags & ~AT_REMOVEDIR))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(fd->inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
    fs->cwd.inode = fd->inode;
  }
  struct INode* inode;
  struct INode* parent;
  int res = GetINodeParent(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (flags & AT_REMOVEDIR) {
    if (UNLIKELY(!S_ISDIR(inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
    if (UNLIKELY(inode == fs->inodes[0])) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBUSY;
    }
  } else if (UNLIKELY(S_ISDIR(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EISDIR;
  }
  for (int i = 0; i != fs->fdCount; ++i)
    if (UNLIKELY(fs->fds[i]->inode == inode)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBUSY;
    }
  if (flags & AT_REMOVEDIR) {
    const char* last = GetLast(path);
    if (UNLIKELY(!last)) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOMEM;
    }
    bool isDot = strcmp(last, ".") == 0;
    free(last);
    if (UNLIKELY(isDot)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EINVAL;
    }
    if (UNLIKELY(inode->dentCount != 2)) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTEMPTY;
    }
  }
  const char* name = GetAbsoluteLast(fs, path);
  if (UNLIKELY(!name)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  INode_RemoveDent(parent, name);
  free(name);
  if (flags & AT_REMOVEDIR)
    --parent->nlink;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  --inode->nlink;
  if (UNLIKELY(INode_IsUnused(inode)))
    RemoveINode(fs, inode);
  else inode->ctime = ts;
  parent->ctime = parent->mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Unlink(struct FileSystem* this, const char* path) {
  return FileSystem_UnlinkAt(this, AT_FDCWD, path, 0);
}
int FileSystem_RmDir(struct FileSystem* this, const char* path) {
  return FileSystem_UnlinkAt(this, AT_FDCWD, path, AT_REMOVEDIR);
}
int FileSystem_RenameAt2(struct FileSystem* this, int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, unsigned int flags) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE)) ||
      UNLIKELY(flags & RENAME_NOREPLACE && flags & RENAME_EXCHANGE))
    return -EINVAL;
  const char* last = GetLast(oldPath);
  if (UNLIKELY(!last))
    return -ENOMEM;
  bool isDot = strcmp(last, ".") == 0 || strcmp(last, "..") == 0;
  free(last);
  if (UNLIKELY(isDot))
    return -EBUSY;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* oldFd;
  struct Fd* newFd;
  if (oldDirFd != AT_FDCWD) {
    if (UNLIKELY(!(oldFd = GetFd(fs, oldDirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(oldFd->inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
  }
  if (newDirFd != AT_FDCWD) {
    if (UNLIKELY(!(newFd = GetFd(fs, newDirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    if (UNLIKELY(!S_ISDIR(newFd->inode->mode))) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTDIR;
    }
  }
  struct INode* origCwd = fs->cwd.inode;
  if (oldDirFd != AT_FDCWD)
    fs->cwd.inode = oldFd->inode;
  struct INode* oldInode;
  struct INode* oldParent;
  int res = GetINodeParent(fs, oldPath, &oldInode, &oldParent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (newDirFd != AT_FDCWD)
    fs->cwd.inode = newFd->inode;
  struct INode* newInode = NULL;
  struct INode* newParent = NULL;
  res = GetINodeParent(fs, newPath, &newInode, &newParent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(!newParent) ||
      (!newInode && UNLIKELY(res != -ENOENT))) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (UNLIKELY(oldInode == newInode)) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (flags & RENAME_NOREPLACE && UNLIKELY(newInode)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EEXIST;
  }
  if (flags & RENAME_EXCHANGE && UNLIKELY(!newInode)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOENT;
  }
  if (S_ISDIR(oldInode->mode)) {
    if (newInode) {
      if (UNLIKELY(!S_ISDIR(newInode->mode))) {
        pthread_mutex_unlock(&fs->mtx);
        return -ENOTDIR;
      }
      if (UNLIKELY(newInode->dentCount > 2)) {
        pthread_mutex_unlock(&fs->mtx);
        return -ENOTEMPTY;
      }
    }
    if (UNLIKELY(oldInode == fs->inodes[0]) ||
        UNLIKELY(oldInode == fs->cwd.inode)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBUSY;
    }
  } else if (newInode && UNLIKELY(S_ISDIR(newInode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EISDIR;
  }
  if (UNLIKELY(INode_IsInSelf(oldParent, newParent))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  const char* oldName = GetAbsoluteLast(fs, oldPath);
  if (UNLIKELY(!oldName)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  const char* newName = GetAbsoluteLast(fs, newPath);
  if (UNLIKELY(!newName)) {
    pthread_mutex_unlock(&fs->mtx);
    free(oldName);
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
    free(oldName);
    free(newName);
  } else {
    if (UNLIKELY(!INode_PushDent(newParent, newName, oldInode))) {
      pthread_mutex_unlock(&fs->mtx);
      free(oldName);
      free(newName);
      return -EIO;
    }
    INode_RemoveDent(oldParent, oldName);
    free(oldName);
    if (newInode)
      INode_RemoveDent(newParent, newName);
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
      if (UNLIKELY(INode_IsUnused(newInode)))
        RemoveINode(fs, newInode);
      else newInode->ctime = ts;
    }
  } else newInode->ctime = ts;
  oldInode->ctime = ts;
  oldParent->ctime = oldParent->mtime = ts;
  newParent->ctime = newParent->mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_RenameAt(struct FileSystem* this, int oldDirFd, const char* oldPath, int newDirFd, const char* newPath) {
  return FileSystem_RenameAt2(this, oldDirFd, oldPath, newDirFd, newPath, 0);
}
int FileSystem_Rename(struct FileSystem* this, const char* oldPath, const char* newPath) {
  return FileSystem_RenameAt2(this, AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
}
off_t FileSystem_LSeek(struct FileSystem* this, unsigned int fdNum, off_t offset, unsigned int whence) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct INode* inode = fd->inode;
  switch (whence) {
    case SEEK_SET:
      pthread_mutex_unlock(&fs->mtx);
      return fd->seekOff = offset;
    case SEEK_CUR: {
      off_t res;
      if (UNLIKELY(__builtin_add_overflow(fd->seekOff, offset, &res))) {
        pthread_mutex_unlock(&fs->mtx);
        return -EOVERFLOW;
      }
      pthread_mutex_unlock(&fs->mtx);
      return fd->seekOff = res;
    }
    case SEEK_END: {
      if (UNLIKELY(S_ISDIR(inode->mode))) {
        pthread_mutex_unlock(&fs->mtx);
        return -EINVAL;
      }
      off_t res;
      if (UNLIKELY(__builtin_add_overflow(inode->size, offset, &res))) {
        pthread_mutex_unlock(&fs->mtx);
        return -EOVERFLOW;
      }
      pthread_mutex_unlock(&fs->mtx);
      return fd->seekOff = res;
    }
    case SEEK_DATA: {
      struct DataIterator it;
      DataIterator_New(&it, inode, fd->seekOff);
      off_t res;
      if (!DataIterator_IsInData(&it)) {
        if (!DataIterator_Next(&it)) {
          if (UNLIKELY(__builtin_add_overflow(inode->size, offset, &res))) {
            pthread_mutex_unlock(&fs->mtx);
            return -EOVERFLOW;
          }
          pthread_mutex_unlock(&fs->mtx);
          return fd->seekOff = res;
        }
        struct DataRange* range = DataIterator_GetRange(&it);
        if (UNLIKELY(__builtin_add_overflow(range->offset, offset, &res))) {
          pthread_mutex_unlock(&fs->mtx);
          return -EOVERFLOW;
        }
        pthread_mutex_unlock(&fs->mtx);
        return fd->seekOff = res;
      }
      DataIterator_Next(&it);
      if (DataIterator_Next(&it)) {
        struct DataRange* range = DataIterator_GetRange(&it);
        if (UNLIKELY(__builtin_add_overflow(range->offset, offset, &res))) {
          pthread_mutex_unlock(&fs->mtx);
          return -EOVERFLOW;
        }
        pthread_mutex_unlock(&fs->mtx);
        return fd->seekOff = res;
      }
      if (UNLIKELY(__builtin_add_overflow(inode->size, offset, &res))) {
        pthread_mutex_unlock(&fs->mtx);
        return -EOVERFLOW;
      }
      pthread_mutex_unlock(&fs->mtx);
      return fd->seekOff = res;
    }
    case SEEK_HOLE: {
      struct DataIterator it;
      DataIterator_New(&it, inode, fd->seekOff);
      off_t res;
      if (DataIterator_IsInData(&it)) {
        DataIterator_Next(&it);
        struct HoleRange hole = DataIterator_GetHole(&it);
        if (UNLIKELY(__builtin_add_overflow(hole.offset, offset, &res))) {
          pthread_mutex_unlock(&fs->mtx);
          return -EOVERFLOW;
        }
        pthread_mutex_unlock(&fs->mtx);
        return fd->seekOff = res;
      }
      if (DataIterator_Next(&it)) {
        DataIterator_Next(&it);
        struct HoleRange hole = DataIterator_GetHole(&it);
        if (UNLIKELY(__builtin_add_overflow(hole.offset, offset, &res))) {
          pthread_mutex_unlock(&fs->mtx);
          return -EOVERFLOW;
        }
        pthread_mutex_unlock(&fs->mtx);
        return fd->seekOff = res;
      }
      if (UNLIKELY(__builtin_add_overflow(inode->size, offset, &res))) {
        pthread_mutex_unlock(&fs->mtx);
        return -EOVERFLOW;
      }
      pthread_mutex_unlock(&fs->mtx);
      return fd->seekOff = res;
    }
  }
  pthread_mutex_unlock(&fs->mtx);
  return -EINVAL;
}
ssize_t FileSystem_Read(struct FileSystem* this, unsigned int fdNum, char* buf, size_t count) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct INode* inode = fd->inode;
  if (UNLIKELY(S_ISDIR(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EISDIR;
  }
  if (count == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  if (fd->seekOff >= inode->size) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  off_t end = inode->size - fd->seekOff;
  if (end < count)
    count = end;
  struct DataIterator it;
  DataIterator_New(&it, inode, fd->seekOff);
  for (size_t amountRead = 0; amountRead != count; DataIterator_Next(&it)) {
    size_t amount;
    if (DataIterator_IsInData(&it)) {
      struct DataRange* range = DataIterator_GetRange(&it);
      amount = MinUnsigned((range->offset + range->size) - (fd->seekOff + amountRead), count - amountRead);
      memcpy(buf + amountRead, range->data + (fd->seekOff + amountRead) - range->offset, amount);
    } else {
      struct HoleRange hole = DataIterator_GetHole(&it);
      amount = MinUnsigned((hole.offset + hole.size) - (fd->seekOff + amountRead), count - amountRead);
      memset(buf + amountRead, '\0', amount);
    }
    amountRead += amount;
  }
  buf[count] = '\0';
  fd->seekOff += count;
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  pthread_mutex_unlock(&fs->mtx);
  return count;
}
ssize_t FileSystem_Readv(struct FileSystem* this, unsigned int fdNum, struct iovec* iov, int iovcnt) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct INode* inode = fd->inode;
  if (UNLIKELY(S_ISDIR(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EISDIR;
  }
  if (iovcnt == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (UNLIKELY(iovcnt < 0) ||
      UNLIKELY(iovcnt > 1024)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  size_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    size_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (UNLIKELY(len < 0)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EINVAL;
    }
    if (len > 0x7ffff000 - totalLen) {
      len = 0x7ffff000 - totalLen;
      iov[i].iov_len = len;
      totalLen += len;
      break;
    }
    totalLen += len;
  }
  if (totalLen == 0 || fd->seekOff >= inode->size) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  off_t end = inode->size - fd->seekOff;
  if (end < totalLen)
    totalLen = end;
  struct DataIterator it;
  DataIterator_New(&it, inode, fd->seekOff);
  for (size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; DataIterator_Next(&it)) {
    struct iovec curr = iov[iovIdx];
    size_t amount;
    if (DataIterator_IsInData(&it)) {
      struct DataRange* range = DataIterator_GetRange(&it);
      amount = MinUnsigned(
        MinUnsigned(
          (range->offset + range->size) - (fd->seekOff + count),
          curr.iov_len - amountRead),
        totalLen - count
      );
      memcpy(((char*)curr.iov_base) + amountRead, range->data + (fd->seekOff + count) - range->offset, amount);
    } else {
      struct HoleRange hole = DataIterator_GetHole(&it);
      amount = MinUnsigned(
        MinUnsigned(
          (hole.offset + hole.size) - (fd->seekOff + count),
          curr.iov_len - amountRead),
        totalLen - count
      );
      memset(((char*)curr.iov_base) + amountRead, '\0', amount);
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
  pthread_mutex_unlock(&fs->mtx);
  return totalLen;
}
ssize_t FileSystem_PRead(struct FileSystem* this, unsigned int fdNum, char* buf, size_t count, off_t offset) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct INode* inode = fd->inode;
  if (UNLIKELY(S_ISDIR(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EISDIR;
  }
  if (count == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  if (offset >= inode->size) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  off_t end = inode->size - offset;
  if (end < count)
    count = end;
  struct DataIterator it;
  DataIterator_New(&it, inode, offset);
  for (size_t amountRead = 0; amountRead != count; DataIterator_Next(&it)) {
    size_t amount;
    if (DataIterator_IsInData(&it)) {
      struct DataRange* range = DataIterator_GetRange(&it);
      amount = MinUnsigned((range->offset + range->size) - (offset + amountRead), count - amountRead);
      memcpy(buf + amountRead, range->data + (offset + amountRead) - range->offset, amount);
    } else {
      struct HoleRange hole = DataIterator_GetHole(&it);
      amount = MinUnsigned((hole.offset + hole.size) - (offset + amountRead), count - amountRead);
      memset(buf + amountRead, '\0', amount);
    }
    amountRead += amount;
  }
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  pthread_mutex_unlock(&fs->mtx);
  return count;
}
ssize_t FileSystem_PReadv(struct FileSystem* this, unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct INode* inode = fd->inode;
  if (UNLIKELY(S_ISDIR(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EISDIR;
  }
  if (iovcnt == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (UNLIKELY(iovcnt < 0) ||
      UNLIKELY(iovcnt > 1024)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  size_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    size_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (UNLIKELY(len < 0)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EINVAL;
    }
    if (len > 0x7ffff000 - totalLen) {
      len = 0x7ffff000 - totalLen;
      iov[i].iov_len = len;
      totalLen += len;
      break;
    }
    totalLen += len;
  }
  if (totalLen == 0 || offset >= inode->size) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  off_t end = inode->size - offset;
  if (end < totalLen)
    totalLen = end;
  struct DataIterator it;
  DataIterator_New(&it, inode, fd->seekOff);
  for (size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; DataIterator_Next(&it)) {
    struct iovec curr = iov[iovIdx];
    size_t amount;
    if (DataIterator_IsInData(&it)) {
      struct DataRange* range = DataIterator_GetRange(&it);
      amount = MinUnsigned(
        MinUnsigned(
          (range->offset + range->size) - (fd->seekOff + count),
          curr.iov_len - amountRead),
        totalLen - count
      );
      memcpy(((char*)curr.iov_base) + amountRead, range->data + (fd->seekOff + count) - range->offset, amount);
    } else {
      struct HoleRange hole = DataIterator_GetHole(&it);
      amount = MinUnsigned(
        MinUnsigned(
          (hole.offset + hole.size) - (fd->seekOff + count),
          curr.iov_len - amountRead),
        totalLen - count
      );
      memset(((char*)curr.iov_base) + amountRead, '\0', amount);
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
  pthread_mutex_unlock(&fs->mtx);
  return totalLen;
}
ssize_t FileSystem_Write(struct FileSystem* this, unsigned int fdNum, const char* buf, size_t count) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  if (count == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  struct INode* inode = fd->inode;
  off_t seekOff = fd->flags & O_APPEND
    ? inode->size
    : fd->seekOff;
  off_t seekEnd;
  if (UNLIKELY(__builtin_add_overflow(seekOff, count, &seekEnd))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EFBIG;
  }
  struct DataRange* range = INode_AllocData(inode, seekOff, count);
  if (UNLIKELY(!range)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EIO;
  }
  memcpy(range->data + (seekOff - range->offset), buf, count);
  fd->seekOff = seekEnd;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->mtime = inode->ctime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return count;
}
ssize_t FileSystem_Writev(struct FileSystem* this, unsigned int fdNum, struct iovec* iov, int iovcnt) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  if (iovcnt == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (UNLIKELY(iovcnt < 0) ||
      UNLIKELY(iovcnt > 1024)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  ssize_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (UNLIKELY(len < 0)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EINVAL;
    }
    if (len > 0x7ffff000 - totalLen) {
      len = 0x7ffff000 - totalLen;
      iov[i].iov_len = len;
    }
    totalLen += len;
  }
  if (totalLen == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  struct INode* inode = fd->inode;
  off_t seekOff = fd->flags & O_APPEND
    ? inode->size
    : fd->seekOff;
  off_t seekEnd;
  if (UNLIKELY(__builtin_add_overflow(seekOff, totalLen, &seekEnd))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EFBIG;
  }
  struct DataRange* range = INode_AllocData(inode, seekOff, totalLen);
  if (UNLIKELY(!range)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EIO;
  }
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
  pthread_mutex_unlock(&fs->mtx);
  return count;
}
ssize_t FileSystem_PWrite(struct FileSystem* this, unsigned int fdNum, const char* buf, size_t count, off_t offset) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  if (count == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  struct INode* inode = fd->inode;
  {
    off_t res;
    if (UNLIKELY(__builtin_add_overflow(offset, count, &res))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EFBIG;
    }
  }
  struct DataRange* range = INode_AllocData(inode, offset, count);
  if (UNLIKELY(!range)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EIO;
  }
  memcpy(range->data + (offset - range->offset), buf, count);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->mtime = inode->ctime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return count;
}
ssize_t FileSystem_PWritev(struct FileSystem* this, unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  if (iovcnt == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (UNLIKELY(iovcnt < 0) ||
      UNLIKELY(iovcnt > 1024)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  ssize_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (UNLIKELY(len < 0)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EINVAL;
    }
    if (len > 0x7ffff000 - totalLen) {
      len = 0x7ffff000 - totalLen;
      iov[i].iov_len = len;
    }
    totalLen += len;
  }
  if (UNLIKELY(totalLen == 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  struct INode* inode = fd->inode;
  {
    off_t res;
    if (UNLIKELY(__builtin_add_overflow(offset, totalLen, &res))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EFBIG;
    }
  }
  struct DataRange* range = INode_AllocData(inode, offset, totalLen);
  if (UNLIKELY(!range)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EIO;
  }
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
  pthread_mutex_unlock(&fs->mtx);
  return count;
}
ssize_t FileSystem_SendFile(struct FileSystem* this, unsigned int outFd, unsigned int inFd, off_t* offset, size_t count) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fdIn;
  struct Fd* fdOut;
  if (UNLIKELY(!(fdIn = GetFd(fs, inFd))) || UNLIKELY(fdIn->flags & O_WRONLY) ||
      UNLIKELY(!(fdOut = GetFd(fs, outFd))) || UNLIKELY(!(fdOut->flags & (O_WRONLY | O_RDWR)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  if (UNLIKELY(S_ISDIR(fdIn->inode->mode)) ||
      UNLIKELY(fdOut->flags & O_APPEND)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  off_t off;
  if (offset) {
    if (UNLIKELY((off = *offset) < 0)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EINVAL;
    }
  } else off = fdIn->seekOff;
  if (count == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  if (count > 0x7ffff000)
    count = 0x7ffff000;
  struct INode* inodeIn = fdIn->inode;
  struct INode* inodeOut = fdOut->inode;
  off_t outSeekEnd;
  if (UNLIKELY(__builtin_add_overflow(fdOut->seekOff, count, &outSeekEnd))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EFBIG;
  }
  if (off >= inodeIn->size) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  off_t end = inodeIn->size - off;
  if (end < count)
    count = end;
  if (!offset)
    fdIn->seekOff += count;
  else *offset += count;
  if (outSeekEnd > inodeOut->size)
    INode_TruncateData(inodeOut, outSeekEnd);
  struct DataIterator itIn;
  struct DataIterator itOut;
  DataIterator_New(&itIn, inodeIn, off);
  DataIterator_New(&itOut, inodeOut, fdOut->seekOff);
  for (size_t amountRead = 0; amountRead != count;) {
    size_t amount;
    if (!DataIterator_IsInData(&itIn)) {
      struct HoleRange holeIn = DataIterator_GetHole(&itIn);
      if (!DataIterator_IsInData(&itOut)) {
        struct HoleRange holeOut = DataIterator_GetHole(&itOut);
        amount = (holeOut.offset + holeOut.size) - (fdOut->seekOff + amountRead);
        if (amount > (holeIn.offset + holeIn.size) - (off + amountRead)) {
          amount = (holeIn.offset + holeIn.size) - (off + amountRead);
          DataIterator_Next(&itIn);
        } else DataIterator_Next(&itOut);
        if (amount == 0)
          continue;
        amountRead += MinUnsigned(amount, count - amountRead);
        continue;
      }
      struct DataRange* rangeOut = DataIterator_GetRange(&itOut);
      amount = (rangeOut->offset + rangeOut->size) - (fdOut->seekOff + amountRead);
      if (amount > (holeIn.offset + holeIn.size) - (off + amountRead)) {
        amount = (holeIn.offset + holeIn.size) - (off + amountRead);
        DataIterator_Next(&itIn);
      } else DataIterator_Next(&itOut);
      if (amount == 0)
        continue;
      if (amount > count - amountRead)
        amount = count - amountRead;
      memset(rangeOut->data + ((fdOut->seekOff + amountRead) - rangeOut->offset), '\0', amount);
      amountRead += amount;
      continue;
    }
    struct DataRange* rangeIn = DataIterator_GetRange(&itIn);
    amount = MinUnsigned((rangeIn->offset + rangeIn->size) - (off + amountRead), count - amountRead);
    struct DataRange* rangeOut = INode_AllocData(inodeOut, fdOut->seekOff + amountRead, amount);
    if (UNLIKELY(!rangeOut)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EIO;
    }
    memcpy(
      rangeOut->data + ((fdOut->seekOff + amountRead) - rangeOut->offset),
      rangeIn->data + ((off + amountRead) - rangeIn->offset),
      amount
    );
    amountRead += amount;
    DataIterator_Next(&itIn);
    DataIterator_SeekTo(&itOut, fdOut->seekOff + amountRead);
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (!(fdIn->flags & O_NOATIME))
    inodeIn->atime = ts;
  inodeOut->mtime = inodeOut->ctime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return count;
}
int FileSystem_FTruncate(struct FileSystem* this, unsigned int fdNum, off_t length) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(length < 0))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct INode* inode = fd->inode;
  if (UNLIKELY(!S_ISREG(inode->mode)) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  if (UNLIKELY(fd->flags & O_APPEND)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EPERM;
  }
  INode_TruncateData(inode, length);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->ctime = inode->mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Truncate(struct FileSystem* this, const char* path, off_t length) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(length < 0))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct INode* inode;
  int res = GetINodeNoParentFollow(fs, path, &inode, true);
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (UNLIKELY(S_ISDIR(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EISDIR;
  }
  if (UNLIKELY(!S_ISREG(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  if (UNLIKELY(!INode_CanUse(inode, W_OK))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EACCES;
  }
  INode_TruncateData(inode, length);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->ctime = inode->mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_FChModAt(struct FileSystem* this, int dirFd, const char* path, mode_t mode) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    fs->cwd.inode = fd->inode;
  }
  struct INode* inode;
  int res = GetINodeNoParent(fs, path, &inode);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  inode->mode = (mode & 07777) | (inode->mode & S_IFMT);
  clock_gettime(CLOCK_REALTIME, &inode->ctime);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_FChMod(struct FileSystem* this, unsigned int fdNum, mode_t mode) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct INode* inode = fd->inode;
  inode->mode = (mode & 07777) | (inode->mode & S_IFMT);
  clock_gettime(CLOCK_REALTIME, &inode->ctime);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_ChMod(struct FileSystem* this, const char* path, mode_t mode) {
  return FileSystem_FChModAt(this, AT_FDCWD, path, mode);
}
int FileSystem_ChDir(struct FileSystem* this, const char* path) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct INode* inode;
  struct INode* parent;
  int res = GetINodeParentFollow(fs, path, &inode, &parent, true);
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (UNLIKELY(!S_ISDIR(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOTDIR;
  }
  const char* absPath = AbsolutePath(fs, path);
  if (UNLIKELY(!absPath)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  free(fs->cwd.path);
  fs->cwd.path = absPath;
  fs->cwd.inode = inode;
  fs->cwd.parent = parent;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_GetCwd(struct FileSystem* this, char* buf, size_t size) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  if (fs->cwd.inode != fs->inodes[0]) {
    struct INode* inode;
    struct INode* parent;
    int res = GetINodeParentFollow(fs, fs->cwd.path, &inode, &parent, true);
    if (UNLIKELY(res != 0)) {
      pthread_mutex_unlock(&fs->mtx);
      return res;
    }
  }
  size_t cwdLen = strlen(fs->cwd.path);
  if (UNLIKELY(size <= cwdLen)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ERANGE;
  }
  if (buf) {
    memcpy(buf, fs->cwd.path, cwdLen);
    buf[cwdLen] = '\0';
  }
  pthread_mutex_unlock(&fs->mtx);
  return cwdLen;
}
int FileSystem_FStat(struct FileSystem* this, unsigned int fdNum, struct stat* buf) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  INode_FillStat(fd->inode, buf);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Stat(struct FileSystem* this, const char* path, struct stat* buf) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct INode* inode;
  int res = GetINodeNoParentFollow(fs, path, &inode, true);
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  INode_FillStat(inode, buf);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_LStat(struct FileSystem* this, const char* path, struct stat* buf) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  struct INode* inode;
  int res = GetINodeNoParent(fs, path, &inode);
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  INode_FillStat(inode, buf);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Statx(struct FileSystem* this, int dirFd, const char* path, int flags, int mask, struct statx* buf) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) ||
      UNLIKELY(mask & ~STATX_ALL) ||
      UNLIKELY(flags & AT_EMPTY_PATH && path[0] != '\0'))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    fs->cwd.inode = fd->inode;
  }
  struct INode* inode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    inode = fs->cwd.inode;
  else res = GetINodeNoParentFollow(fs, path, &inode, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  INode_FillStatx(inode, buf, mask);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_UTimeNsAt(struct FileSystem* this, int dirFd, const char* path, const struct timespec* times, int flags) {
  struct FSInternal* fs = this->data;
  if (UNLIKELY(flags & ~AT_SYMLINK_NOFOLLOW))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    fs->cwd.inode = fd->inode;
  }
  struct INode* inode;
  int res = GetINodeNoParentFollow(fs, path, &inode, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
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
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_FUTimesAt(struct FileSystem* this, unsigned int fdNum, const char* path, const struct timeval* times) {
  struct FSInternal* fs = this->data;
  if (times &&
      UNLIKELY(
        UNLIKELY(times[0].tv_usec < 0) || UNLIKELY(times[0].tv_usec >= 1000000) ||
        UNLIKELY(times[1].tv_usec < 0) || UNLIKELY(times[1].tv_usec >= 1000000)
      ))
    return -EINVAL;
  pthread_mutex_lock(&fs->mtx);
  struct INode* origCwd = fs->cwd.inode;
  if (fdNum != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    fs->cwd.inode = fd->inode;
  }
  struct INode* inode;
  int res = GetINodeNoParentFollow(fs, path, &inode, true);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (times) {
    inode->atime.tv_sec = times[0].tv_sec;
    inode->atime.tv_nsec = times[0].tv_usec * 1000;
    inode->mtime.tv_sec = times[1].tv_sec;
    inode->mtime.tv_nsec = times[1].tv_usec * 1000;
  } else inode->atime = inode->mtime = ts;
  inode->ctime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_UTimes(struct FileSystem* this, const char* path, const struct timeval* times) {
  return FileSystem_FUTimesAt(this, AT_FDCWD, path, times);
}
int FileSystem_UTime(struct FileSystem* this, const char* path, const struct utimbuf* times) {
  struct timeval ts[2];
  if (times) {
    ts[0].tv_sec = times->actime;
    ts[0].tv_usec = 0;
    ts[1].tv_sec = times->modtime;
    ts[1].tv_usec = 0;
  }
  return FileSystem_FUTimesAt(this, AT_FDCWD, path, times ? ts : NULL);
}

#define TimeSpec_Create(ts_sec, ts_nsec) ({ \
  struct timespec ts; \
  ts.tv_sec = ts_sec; \
  ts.tv_nsec = ts_nsec; \
  ts; \
})

bool FileSystem_DumpToFile(struct FileSystem* this, const char* filename) {
  struct FSInternal* fs = this->data;
  pthread_mutex_lock(&fs->mtx);
  int fd = creat(filename, 0644);
  if (UNLIKELY(fd < 0))
    goto err1;
  if (UNLIKELY(write(fd, "\x7FVFS", 4) != 4) ||
      UNLIKELY(write(fd, &fs->inodeCount, sizeof(ino_t)) != sizeof(ino_t)))
    goto err2;
  for (ino_t i = 0; i != fs->inodeCount; ++i) {
    struct INode* inode = fs->inodes[i];
    struct DumpedINode dumped;
    dumped.id = inode->id;
    dumped.size = inode->size;
    dumped.nlink = inode->nlink;
    dumped.mode = inode->mode;
    dumped.btime = TimeSpec_Create(
      inode->btime.tv_sec,
      inode->btime.tv_nsec
    );
    dumped.ctime = TimeSpec_Create(
      inode->ctime.tv_sec,
      inode->ctime.tv_nsec
    );
    dumped.mtime = TimeSpec_Create(
      inode->mtime.tv_sec,
      inode->mtime.tv_nsec
    );
    dumped.atime = TimeSpec_Create(
      inode->atime.tv_sec,
      inode->atime.tv_nsec
    );
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
      if (UNLIKELY(write(fd, &inode->dentCount, sizeof(off_t)) != sizeof(off_t)) ||
          UNLIKELY(write(fd, &inode->dents[1].inode->ndx, sizeof(ino_t)) != sizeof(ino_t)))
        goto err2;
      for (off_t j = 2; j != inode->dentCount; ++j) {
        struct Dent* dent = &inode->dents[j];
        size_t nameLen = strlen(dent->name) + 1;
        if (UNLIKELY(write(fd, &dent->inode->ndx, sizeof(ino_t)) != sizeof(ino_t)) ||
            UNLIKELY(write(fd, dent->name, nameLen) != nameLen))
          goto err2;
      }
    } else if (inode->size != 0) {
      if (UNLIKELY(write(fd, &inode->dataRangeCount, sizeof(off_t)) != sizeof(off_t)))
        goto err2;
      for (off_t j = 0; j != inode->dataRangeCount; ++j) {
        struct DataRange* range = inode->dataRanges[j];
        if (UNLIKELY(write(fd, &range->offset, sizeof(off_t)) != sizeof(off_t)) ||
            UNLIKELY(write(fd, &range->size, sizeof(off_t)) != sizeof(off_t)))
          goto err2;
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
  pthread_mutex_unlock(&fs->mtx);
  return true;
 err2:
  close(fd);
 err1:
  pthread_mutex_unlock(&fs->mtx);
  return false;
}
struct FileSystem* FileSystem_LoadFromFile(const char* filename) {
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
  if (UNLIKELY(!TryAlloc(&inodes, sizeof(struct INode*) * inodeCount)))
    goto err_after_open;
  for (ino_t i = 0; i != inodeCount; ++i) {
    struct INode* inode;
    struct DumpedINode dumped;
    if (UNLIKELY(!TryAlloc(&inode, sizeof(struct INode))))
      goto err_at_inode_loop;
    inode->ndx = i;
    if (UNLIKELY(read(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode)))
      goto err_after_inode_init;
    inode->id = dumped.id;
    inode->size = dumped.size;
    inode->nlink = dumped.nlink;
    inode->mode = dumped.mode;
    inode->btime = TimeSpec_Create(
      dumped.btime.tv_sec,
      dumped.btime.tv_nsec
    );
    inode->ctime = TimeSpec_Create(
      dumped.ctime.tv_sec,
      dumped.ctime.tv_nsec
    );
    inode->mtime = TimeSpec_Create(
      dumped.mtime.tv_sec,
      dumped.mtime.tv_nsec
    );
    inode->atime = TimeSpec_Create(
      dumped.atime.tv_sec,
      dumped.atime.tv_nsec
    );
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
      if (UNLIKELY(!INode_AllocData(inode, 0, dataLen)))
        goto err_after_target_alloc;
      memcpy(inode->dataRanges[0]->data, data, dataLen);
      goto success_symlink;

     err_after_target_alloc:
      free((void*)inode->target);
      goto err_after_inode_init;
     success_symlink: {}
    } else inode->target = NULL;
    if (S_ISDIR(inode->mode)) {
      off_t dentCount;
      if (UNLIKELY(read(fd, &dentCount, sizeof(off_t)) != sizeof(off_t)))
        goto err_after_inode_init;
      if (UNLIKELY(!TryAlloc(&inode->dents, sizeof(struct Dent) * dentCount)))
        goto err_after_inode_init;
      inode->dents[0] = Dent_Create(".", inode);
      inode->dents[1].name = "..";
      if (UNLIKELY(read(fd, &inode->dents[1].inode, sizeof(ino_t)) != sizeof(ino_t)))
        goto err_after_dent_alloc;
      for (off_t j = 2; j != dentCount; ++j) {
        struct Dent* dent = &inode->dents[j];
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
          free((void*)inode->dents[k].name);
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
      if (UNLIKELY(!TryAlloc(&inode->dataRanges, sizeof(struct DataRange*) * dataRangeCount)))
        goto err_after_inode_init;
      for (off_t j = 0; j != dataRangeCount; ++j) {
        struct DataRange* range;
        ssize_t nread = 0;
        off_t offset;
        off_t size;
        if (UNLIKELY(read(fd, &offset, sizeof(off_t)) != sizeof(off_t)) ||
            UNLIKELY(read(fd, &size, sizeof(off_t)) != sizeof(off_t)))
          goto err_after_dataranges_init;
        if (UNLIKELY(offset < 0) || UNLIKELY(offset > inode->size - size) ||
            UNLIKELY(size < 0) || UNLIKELY(size > inode->size - offset))
          goto err_after_dataranges_init;
        if (UNLIKELY(!TryAlloc(&range, sizeof(struct DataRange))))
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
        DataRange_Delete(inode->dataRanges[k]);
      free(inode->dataRanges);
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
      INode_Delete(inodes[j]);
    goto err_after_inode_list_init;
   success_inode: {}
  }
  for (ino_t i = 0; i != inodeCount; ++i) {
    struct INode* inode = inodes[i];
    if (S_ISDIR(inode->mode)) {
      for (off_t j = 1; j != inode->dentCount; ++j) {
        struct Dent* dent = &inode->dents[j];
        ino_t ino = (ino_t)dent->inode;
        if (UNLIKELY(ino >= inodeCount))
          goto err_after_inodes;
        dent->inode = inodes[ino];
      }
    }
  }
  for (ino_t i = 0; i != inodeCount;) {
    struct INode* inode = inodes[i];
    if (inode->nlink == 0) {
      INode_Delete(inode);
      if (i != inodeCount - 1) {
        memmove(inodes + i, inodes + i + 1, sizeof(struct INode*) * (inodeCount - i));
        for (ino_t j = i; j != inodeCount - 1; ++j)
          --inodes[j]->ndx;
      }
      inodes = realloc(inodes, sizeof(struct INode*) * --inodeCount);
    } else ++i;
  }
  close(fd);
  struct FileSystem* fs;
  struct FSInternal* data;
  if (UNLIKELY(!TryAlloc(&fs, sizeof(struct FileSystem))))
    goto err_after_inodes;
  if (UNLIKELY(!TryAlloc(&data, sizeof(struct FSInternal))))
    goto err_after_fs_init;
  FSInternal_New(data);
  data->inodes = inodes;
  data->inodeCount = inodeCount;
  if (UNLIKELY(!(data->cwd.path = strdup("/"))))
    goto err_after_fsdata_init;
  data->cwd.inode = data->cwd.parent = inodes[0];
  fs->data = data;
  return fs;

 err_after_fsdata_init:
  free(data);
 err_after_fs_init:
  free(fs);
 err_after_inodes:
  for (ino_t i = 0; i != inodeCount; ++i)
    INode_Delete(inodes[i]);
 err_after_inode_list_init:
  free(inodes);
 err_after_open:
  close(fd);
 err_at_open:
  return NULL;
}