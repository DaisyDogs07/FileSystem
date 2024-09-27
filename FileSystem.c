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

struct BaseINode {
  ino_t ndx;
  ino_t id;
  off_t size;
  nlink_t nlink;
  mode_t mode;
  struct timespec btime;
  struct timespec ctime;
  struct timespec mtime;
  struct timespec atime;
};

void BaseINode_New(struct BaseINode* thisArg) {
  thisArg->size = 0;
  thisArg->nlink = 0;
  clock_gettime(CLOCK_REALTIME, &thisArg->btime);
  thisArg->ctime = thisArg->mtime = thisArg->atime = thisArg->btime;
}

bool BaseINode_CanUse(struct BaseINode* thisArg, int perms) {
  if ((thisArg->mode & perms) != perms &&
      (thisArg->mode & (perms << 3)) != (perms << 3) &&
      (thisArg->mode & (perms << 6)) != (perms << 6))
    return false;
  return true;
}
bool BaseINode_IsUnused(struct BaseINode* thisArg) {
  if (S_ISDIR(thisArg->mode))
    return thisArg->nlink == 1;
  return thisArg->nlink == 0;
}
void BaseINode_FillStat(struct BaseINode* thisArg, struct stat* buf) {
  memset(buf, '\0', sizeof(struct stat));
  buf->st_ino = thisArg->id;
  buf->st_mode = thisArg->mode;
  buf->st_nlink = thisArg->nlink;
  buf->st_size = thisArg->size;
  buf->st_atim = thisArg->atime;
  buf->st_mtim = thisArg->mtime;
  buf->st_ctim = thisArg->ctime;
}
void BaseINode_FillStatx(struct BaseINode* thisArg, struct statx* buf, int mask) {
  memset(buf, '\0', sizeof(struct statx));
  buf->stx_mask = mask & (
    STATX_INO   | STATX_TYPE  | STATX_MODE  |
    STATX_NLINK | STATX_SIZE  | STATX_ATIME |
    STATX_MTIME | STATX_CTIME | STATX_BTIME
  );
  if (mask & STATX_INO)
    buf->stx_ino = thisArg->id;
  if (mask & STATX_TYPE)
    buf->stx_mode |= thisArg->mode & S_IFMT;
  if (mask & STATX_MODE)
    buf->stx_mode |= thisArg->mode & ~S_IFMT;
  if (mask & STATX_NLINK)
    buf->stx_nlink = thisArg->nlink;
  if (mask & STATX_SIZE)
    buf->stx_size = thisArg->size;
  if (mask & STATX_ATIME) {
    buf->stx_atime.tv_sec = thisArg->atime.tv_sec;
    buf->stx_atime.tv_nsec = thisArg->atime.tv_nsec;
  }
  if (mask & STATX_MTIME) {
    buf->stx_mtime.tv_sec = thisArg->mtime.tv_sec;
    buf->stx_mtime.tv_nsec = thisArg->mtime.tv_nsec;
  }
  if (mask & STATX_CTIME) {
    buf->stx_ctime.tv_sec = thisArg->ctime.tv_sec;
    buf->stx_ctime.tv_nsec = thisArg->ctime.tv_nsec;
  }
  if (mask & STATX_BTIME) {
    buf->stx_btime.tv_sec = thisArg->btime.tv_sec;
    buf->stx_btime.tv_nsec = thisArg->btime.tv_nsec;
  }
}

struct RegularINode {
  struct BaseINode base;
  struct DataRange** dataRanges;
  off_t dataRangeCount;
};

void RegularINode_New(struct RegularINode* thisArg) {
  BaseINode_New(&thisArg->base);
  thisArg->dataRanges = NULL;
  thisArg->dataRangeCount = 0;
}
void RegularINode_Delete(struct RegularINode* thisArg);

struct DataRange* RegularINode_InsertRange(struct RegularINode* thisArg, off_t offset, off_t length, off_t* index);
void RegularINode_RemoveRange(struct RegularINode* thisArg, off_t index);
void RegularINode_RemoveRanges(struct RegularINode* thisArg, off_t index, off_t count);
struct DataRange* RegularINode_AllocData(struct RegularINode* thisArg, off_t offset, off_t length);
void RegularINode_TruncateData(struct RegularINode* thisArg, off_t length);

struct DirectoryINode {
  struct BaseINode base;
  struct Dent* dents;
  off_t dentCount;
};

void DirectoryINode_New(struct DirectoryINode* thisArg) {
  BaseINode_New(&thisArg->base);
  thisArg->dents = NULL;
  thisArg->dentCount = 0;
}
void DirectoryINode_Delete(struct DirectoryINode* thisArg);

bool DirectoryINode_PushDent(struct DirectoryINode* thisArg, const char* name, struct BaseINode* inode);
void DirectoryINode_RemoveDent(struct DirectoryINode* thisArg, const char* name);
bool DirectoryINode_IsInSelf(struct DirectoryINode* thisArg, struct BaseINode* inode);

struct SymLinkINode {
  struct BaseINode base;
  char* data;
  const char* target;
};

void SymLinkINode_New(struct SymLinkINode* thisArg) {
  BaseINode_New(&thisArg->base);
  thisArg->data = NULL;
  thisArg->target = NULL;
}
void SymLinkINode_Delete(struct SymLinkINode* thisArg);

struct DataRange {
  off_t offset;
  off_t size;
  char* data;
};

void DataRange_New(struct DataRange* thisArg) {
  thisArg->data = NULL;
}
void DataRange_Delete(struct DataRange* thisArg) {
  if (thisArg->data)
    free(thisArg->data);
  free(thisArg);
}

struct HoleRange {
  off_t offset;
  off_t size;
};

struct DataIterator {
  struct RegularINode* inode;
  off_t rangeIdx;
  bool atData;
  bool isBeforeFirstRange;
};

void DataIterator_New(struct DataIterator* thisArg, struct RegularINode* inode, off_t offset) {
  thisArg->inode = inode;
  if (inode->dataRangeCount == 0 ||
      offset < inode->dataRanges[0]->offset) {
    thisArg->rangeIdx = 0;
    thisArg->atData = false;
    thisArg->isBeforeFirstRange = true;
    return;
  }
  thisArg->isBeforeFirstRange = false;
  {
    struct DataRange* lastRange = inode->dataRanges[inode->dataRangeCount - 1];
    if (offset >= lastRange->offset + lastRange->size) {
      thisArg->rangeIdx = inode->dataRangeCount - 1;
      thisArg->atData = false;
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
        thisArg->rangeIdx = mid;
        thisArg->atData = true;
        break;
      }
      low = mid + 1;
      struct DataRange* nextRange = inode->dataRanges[low];
      if (offset >= end &&
          offset < nextRange->offset) {
        thisArg->rangeIdx = mid;
        thisArg->atData = false;
        break;
      }
    } else {
      high = mid - 1;
      struct DataRange* prevRange = inode->dataRanges[high];
      if (offset >= prevRange->offset + prevRange->size &&
          offset < range->offset) {
        thisArg->rangeIdx = high;
        thisArg->atData = false;
        break;
      }
    }
  }
}
bool DataIterator_IsInData(struct DataIterator* thisArg) {
  return thisArg->atData;
}
off_t DataIterator_GetRangeIdx(struct DataIterator* thisArg) {
  return thisArg->rangeIdx;
}
bool DataIterator_BeforeFirstRange(struct DataIterator* thisArg) {
  return thisArg->isBeforeFirstRange;
}
struct DataRange* DataIterator_GetRange(struct DataIterator* thisArg) {
  return thisArg->inode->dataRanges[thisArg->rangeIdx];
}
struct HoleRange DataIterator_GetHole(struct DataIterator* thisArg) {
  struct HoleRange hole;
  if (thisArg->isBeforeFirstRange) {
    hole.offset = 0;
    if (thisArg->inode->dataRangeCount == 0)
      hole.size = thisArg->inode->base.size;
    else hole.size = thisArg->inode->dataRanges[0]->offset;
    return hole;
  }
  struct DataRange* currRange = thisArg->inode->dataRanges[thisArg->rangeIdx];
  hole.offset = currRange->offset + currRange->size;
  if (thisArg->rangeIdx < thisArg->inode->dataRangeCount - 1)
    hole.size = thisArg->inode->dataRanges[thisArg->rangeIdx + 1]->offset - hole.offset;
  else hole.size = thisArg->inode->base.size - hole.offset;
  return hole;
}
bool DataIterator_Next(struct DataIterator* thisArg) {
  if (!thisArg->atData) {
    if (thisArg->isBeforeFirstRange) {
      if (thisArg->inode->dataRangeCount == 0)
        return false;
      thisArg->isBeforeFirstRange = false;
    } else if (thisArg->rangeIdx == thisArg->inode->dataRangeCount - 1)
      return false;
    else ++thisArg->rangeIdx;
  }
  thisArg->atData = !thisArg->atData;
  return true;
}
void DataIterator_SeekTo(struct DataIterator* thisArg, off_t offset) {
  do {
    off_t end;
    if (thisArg->atData) {
      struct DataRange* range = DataIterator_GetRange(thisArg);
      end = range->offset + range->size;
    } else {
      struct HoleRange hole = DataIterator_GetHole(thisArg);
      end = hole.offset + hole.size;
    }
    if (end >= offset)
      break;
  } while (DataIterator_Next(thisArg));
}

struct Dent {
  const char* name;
  struct BaseINode* inode;
};

#define Dent_Create(d_name, d_inode) ({ \
  struct Dent _d; \
  _d.name = d_name; \
  _d.inode = d_inode; \
  _d; \
})

void RegularINode_Delete(struct RegularINode* thisArg) {
  if (thisArg->dataRanges) {
    off_t len = thisArg->dataRangeCount;
    for (off_t i = 0; i != len; ++i)
      DataRange_Delete(thisArg->dataRanges[i]);
    free(thisArg->dataRanges);
  }
  free(thisArg);
}
struct DataRange* RegularINode_InsertRange(struct RegularINode* thisArg, off_t offset, off_t length, off_t* index) {
  off_t rangeIdx = thisArg->dataRangeCount;
  if (thisArg->dataRangeCount != 0) {
    off_t low = 0;
    off_t high = thisArg->dataRangeCount - 1;
    while (low <= high) {
      off_t mid = low + ((high - low) / 2);
      struct DataRange* range2 = thisArg->dataRanges[mid];
      if (offset >= range2->offset)
        low = mid + 1;
      else {
        high = mid - 1;
        rangeIdx = mid;
      }
    }
  }
  struct DataRange* range;
  if (UNLIKELY(!TryAlloc((void**)&range, sizeof(struct DataRange))))
    goto err_alloc_failed;
  DataRange_New(range);
  if (UNLIKELY(!TryRealloc((void**)&thisArg->dataRanges, sizeof(struct DataRange*) * (thisArg->dataRangeCount + 1))))
    goto err_after_alloc;
  if (rangeIdx != thisArg->dataRangeCount)
    memmove(&thisArg->dataRanges[rangeIdx + 1], &thisArg->dataRanges[rangeIdx], sizeof(struct DataRange*) * (thisArg->dataRangeCount - rangeIdx));
  range->offset = offset;
  range->size = length;
  thisArg->dataRanges[rangeIdx] = range;
  ++thisArg->dataRangeCount;
  *index = rangeIdx;
  return range;

 err_after_alloc:
  DataRange_Delete(range);
 err_alloc_failed:
  return NULL;
}
void RegularINode_RemoveRange(struct RegularINode* thisArg, off_t index) {
  struct DataRange* range = thisArg->dataRanges[index];
  DataRange_Delete(range);
  if (index != thisArg->dataRangeCount - 1)
    memmove(&thisArg->dataRanges[index], &thisArg->dataRanges[index + 1], sizeof(struct DataRange*) * (thisArg->dataRangeCount - index));
  thisArg->dataRanges = (struct DataRange**)realloc(thisArg->dataRanges, sizeof(struct DataRange*) * --thisArg->dataRangeCount);
}
void RegularINode_RemoveRanges(struct RegularINode* thisArg, off_t index, off_t count) {
  off_t endIdx = index + count;
  for (off_t i = index; i != endIdx; ++i)
    DataRange_Delete(thisArg->dataRanges[i]);
  if (endIdx < thisArg->dataRangeCount)
    memmove(&thisArg->dataRanges[index], &thisArg->dataRanges[endIdx], sizeof(struct DataRange*) * (thisArg->dataRangeCount - endIdx));
  thisArg->dataRangeCount -= count;
  thisArg->dataRanges = (struct DataRange**)realloc(thisArg->dataRanges, sizeof(struct DataRange*) * thisArg->dataRangeCount);
}
struct DataRange* RegularINode_AllocData(struct RegularINode* thisArg, off_t offset, off_t length) {
  off_t rangeIdx;
  bool createdRange = false;
  struct DataRange* range = NULL;
  off_t end = offset + length;
  if (thisArg->dataRangeCount != 0) {
    struct DataIterator it;
    DataIterator_New(&it, thisArg, offset);
    for (off_t i = DataIterator_GetRangeIdx(&it); i != thisArg->dataRangeCount; ++i) {
      struct DataRange* range2 = thisArg->dataRanges[i];
      if (end == range2->offset) {
        struct DataRange* range3 = NULL;
        for (off_t j = i - 1; j >= 0; --j) {
          struct DataRange* range4 = thisArg->dataRanges[j];
          if (offset <= range4->offset + range4->size) {
            rangeIdx = j;
            range3 = range4;
          } else break;
        }
        if (range3) {
          off_t off = MinSigned(range3->offset, offset);
          off_t newRangeLength = range2->size + (range2->offset - off);
          if (UNLIKELY(!TryRealloc((void**)&range3->data, newRangeLength)))
            return NULL;
          memmove(&range3->data[newRangeLength - range2->size], range2->data, range2->size);
          range3->size = newRangeLength;
          for (off_t j = rangeIdx + 1; j < i; ++j) {
            struct DataRange* range4 = thisArg->dataRanges[j];
            memmove(&range3->data[range4->offset - off], range4->data, range4->size);
          }
          RegularINode_RemoveRanges(thisArg, rangeIdx + 1, i - rangeIdx);
          range3->offset = off;
          return range3;
        } else {
          off_t newRangeLength = range2->size + (range2->offset - offset);
          if (UNLIKELY(!TryRealloc((void**)&range2->data, newRangeLength)))
            return NULL;
          memmove(&range2->data[newRangeLength - range2->size], range2->data, range2->size);
          range2->size = newRangeLength;
          range2->offset = offset;
          return range2;
        }
      } else if (end < range2->offset)
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
    range = RegularINode_InsertRange(thisArg, offset, length, &rangeIdx);
    if (UNLIKELY(!range))
      return NULL;
    createdRange = true;
  } else if (offset >= range->offset &&
      end <= range->offset + range->size)
    return range;
  off_t newRangeLength = end - range->offset;
  for (off_t i = rangeIdx + 1; i < thisArg->dataRangeCount; ++i) {
    struct DataRange* range2 = thisArg->dataRanges[i];
    if (range2->offset < end) {
      off_t newLength = (range2->offset - range->offset) + range2->size;
      if (newRangeLength < newLength) {
        newRangeLength = newLength;
        break;
      }
    } else break;
  }
  if (createdRange) {
    if (UNLIKELY(!TryAlloc((void**)&range->data, newRangeLength))) {
      RegularINode_RemoveRange(thisArg, rangeIdx);
      return NULL;
    }
  } else if (UNLIKELY(!TryRealloc((void**)&range->data, newRangeLength)))
    return NULL;
  range->size = newRangeLength;
  if (thisArg->base.size < end)
    thisArg->base.size = end;
  off_t n = 0;
  for (off_t i = rangeIdx + 1; i < thisArg->dataRangeCount; ++i) {
    struct DataRange* range2 = thisArg->dataRanges[i];
    if (range2->offset < end) {
      memcpy(&range->data[range2->offset - range->offset], range2->data, range2->size);
      ++n;
    } else break;
  }
  if (LIKELY(n != 0))
    RegularINode_RemoveRanges(thisArg, rangeIdx + 1, n);
  return range;
}
void RegularINode_TruncateData(struct RegularINode* thisArg, off_t length) {
  if (length >= thisArg->base.size) {
    thisArg->base.size = length;
    return;
  }
  thisArg->base.size = length;
  if (length == 0) {
    off_t len = thisArg->dataRangeCount;
    for (off_t i = 0; i != len; ++i)
      DataRange_Delete(thisArg->dataRanges[i]);
    free(thisArg->dataRanges);
    thisArg->dataRanges = NULL;
    thisArg->dataRangeCount = 0;
    return;
  }
  for (off_t i = thisArg->dataRangeCount - 1; i >= 0; --i) {
    struct DataRange* range = thisArg->dataRanges[i];
    if (length > range->offset) {
      RegularINode_RemoveRanges(thisArg, i + 1, thisArg->dataRangeCount - (i + 1));
      if (length - range->offset < range->size) {
        range->size = length - range->offset;
        range->data = (char*)realloc(range->data, range->size);
      }
      break;
    }
  }
}

void DirectoryINode_Delete(struct DirectoryINode* thisArg) {
  if (thisArg->dents) {
    off_t len = thisArg->dentCount;
    for (off_t i = 2; i != len; ++i)
      free((void*)thisArg->dents[i].name);
    free(thisArg->dents);
  }
}
bool DirectoryINode_PushDent(struct DirectoryINode* thisArg, const char* name, struct BaseINode* inode) {
  if (UNLIKELY(!TryRealloc((void**)&thisArg->dents, sizeof(struct Dent) * (thisArg->dentCount + 1))))
    return false;
  thisArg->dents[thisArg->dentCount++] = Dent_Create(name, inode);
  thisArg->base.size += strlen(name);
  return true;
}
void DirectoryINode_RemoveDent(struct DirectoryINode* thisArg, const char* name) {
  off_t len = thisArg->dentCount;
  for (off_t i = 2; i != len; ++i) {
    const char* d_name = thisArg->dents[i].name;
    if (strcmp(d_name, name) == 0) {
      free((void*)d_name);
      if (i != len - 1)
        memmove(&thisArg->dents[i], &thisArg->dents[i + 1], sizeof(struct Dent) * (len - i));
      thisArg->dents = (struct Dent*)realloc(thisArg->dents, sizeof(struct Dent) * --thisArg->dentCount);
      thisArg->base.size -= strlen(name);
      break;
    }
  }
}
bool DirectoryINode_IsInSelf(struct DirectoryINode* thisArg, struct BaseINode* inode) {
  off_t len = thisArg->dentCount;
  for (off_t i = 2; i != len; ++i) {
    struct BaseINode* dentInode = thisArg->dents[i].inode;
    if (dentInode == inode ||
        (S_ISDIR(dentInode->mode) && DirectoryINode_IsInSelf((struct DirectoryINode*)dentInode, inode)))
      return true;
  }
  return false;
}

void SymLinkINode_Delete(struct SymLinkINode* thisArg) {
  if (thisArg->data)
    free(thisArg->data);
  if (thisArg->target)
    free((void*)thisArg->target);
}

struct Fd {
  struct BaseINode* inode;
  int fd;
  int flags;
  off_t seekOff;
};

void Fd_New(struct Fd* thisArg) {
  thisArg->seekOff = 0;
}

struct Cwd {
  const char* path;
  struct DirectoryINode* inode;
  struct DirectoryINode* parent;
};

void Cwd_New(struct Cwd* thisArg) {
  thisArg->path = NULL;
}

void DeleteINode(struct BaseINode* inode) {
  if (S_ISREG(inode->mode))
    RegularINode_Delete((struct RegularINode*)inode);
  else if (S_ISDIR(inode->mode))
    DirectoryINode_Delete((struct DirectoryINode*)inode);
  else if (S_ISLNK(inode->mode))
    SymLinkINode_Delete((struct SymLinkINode*)inode);
}

struct FSInternal {
  struct BaseINode** inodes;
  ino_t inodeCount;
  struct Fd** fds;
  int fdCount;
  struct Cwd cwd;
  pthread_mutex_t mtx;
};

void FSInternal_New(struct FSInternal* thisArg) {
  thisArg->inodes = NULL;
  thisArg->inodeCount = 0;
  thisArg->fds = NULL;
  thisArg->fdCount = 0;
  Cwd_New(&thisArg->cwd);
  pthread_mutex_init(&thisArg->mtx, NULL);
}
void FSInternal_Delete(struct FSInternal* thisArg) {
  pthread_mutex_destroy(&thisArg->mtx);
  {
    ino_t len = thisArg->inodeCount;
    for (ino_t i = 0; i != len; ++i)
      DeleteINode(thisArg->inodes[i]);
  }
  free(thisArg->inodes);
  if (thisArg->fds) {
    int len = thisArg->fdCount;
    for (int i = 0; i != len; ++i)
      free(thisArg->fds[i]);
    free(thisArg->fds);
  }
  if (thisArg->cwd.path)
    free((void*)thisArg->cwd.path);
  free(thisArg);
}

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
  if (UNLIKELY(!TryRealloc((void**)&fs->inodes, sizeof(struct BaseINode*) * (fs->inodeCount + 1))))
    return false;
  if (id != fs->inodeCount) {
    memmove(&fs->inodes[id + 1], &fs->inodes[id], sizeof(struct BaseINode*) * (fs->inodeCount - id));
    ino_t endIdx = fs->inodeCount + 1;
    for (ino_t i = id + 1; i != endIdx; ++i)
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
    ino_t endIdx = fs->inodeCount - 1;
    do {
      --fs->inodes[i++]->ndx;
    } while (i != endIdx);
  }
  fs->inodes = (struct BaseINode**)realloc(fs->inodes, sizeof(struct BaseINode*) * --fs->inodeCount);
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
  if (UNLIKELY(!TryAlloc((void**)&fd, sizeof(struct Fd))))
    return -ENOMEM;
  Fd_New(fd);
  if (UNLIKELY(!TryRealloc((void**)&fs->fds, sizeof(struct Fd*) * (fs->fdCount + 1)))) {
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
  struct BaseINode* inode = fd->inode;
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
  fs->fds = (struct Fd**)realloc(fs->fds, sizeof(struct Fd*) * fs->fdCount);
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
  struct BaseINode** inode,
  struct DirectoryINode** parent,
  bool followResolved,
  int follow
) {
  size_t pathLen = strlen(path);
  if (UNLIKELY(pathLen == 0))
    return -ENOENT;
  if (UNLIKELY(pathLen >= PATH_MAX))
    return -ENAMETOOLONG;
  bool isAbsolute = path[0] == '/';
  struct BaseINode* current = isAbsolute
    ? fs->inodes[0]
    : (struct BaseINode*)fs->cwd.inode;
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
      if (UNLIKELY(err))
        return err;
      currParent = (struct DirectoryINode*)current;
      if (UNLIKELY(!BaseINode_CanUse(current, X_OK))) {
        err = -EACCES;
        goto resetName;
      }
      {
        off_t j = 0;
        off_t dentCount = ((struct DirectoryINode*)current)->dentCount;
        for (; j != dentCount; ++j)
          if (strcmp(((struct DirectoryINode*)current)->dents[j].name, name) == 0)
            break;
        if (UNLIKELY(j == dentCount)) {
          err = -ENOENT;
          goto resetName;
        }
        current = ((struct DirectoryINode*)current)->dents[j].inode;
      }
      if (S_ISLNK(current->mode)) {
        if (UNLIKELY(follow++ == 40)) {
          err = -ELOOP;
          goto resetName;
        }
        struct DirectoryINode* targetParent;
        int res = GetINode(fs, ((struct SymLinkINode*)current)->target, &current, &targetParent, true, follow);
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
      *parent = (struct DirectoryINode*)current;
    if (UNLIKELY(!BaseINode_CanUse(current, X_OK)))
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
      if (UNLIKELY(follow++ == 40))
        return -ELOOP;
      struct DirectoryINode* targetParent;
      int res = GetINode(fs, ((struct SymLinkINode*)current)->target, &current, &targetParent, true, follow);
      if (UNLIKELY(res != 0))
        return res;
    }
  }
  *inode = current;
  return 0;
}

#define GetINodeParent(fs, path, inode, parent) \
  GetINode(fs, path, inode, parent, false, 0)
#define GetINodeParentFollow(fs, path, inode, parent, followResolved) \
  GetINode(fs, path, inode, parent, followResolved, 0)
#define GetINodeNoParent(fs, path, inode) \
  GetINode(fs, path, inode, NULL, false, 0)
#define GetINodeNoParentFollow(fs, path, inode, followResolved) \
  GetINode(fs, path, inode, NULL, followResolved, 0)

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
  free((void*)absPath);
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
  if (UNLIKELY(!TryAlloc((void**)&data, sizeof(struct FSInternal))))
    return NULL;
  FSInternal_New(data);
  struct DirectoryINode* root;
  if (UNLIKELY(!TryAlloc((void**)&data->inodes, sizeof(struct BaseINode*))) ||
      UNLIKELY(!TryAlloc((void**)&root, sizeof(struct DirectoryINode)))) {
    FSInternal_Delete(data);
    return NULL;
  }
  DirectoryINode_New(root);
  if (UNLIKELY(!TryAlloc((void**)&root->dents, sizeof(struct Dent) * 2))) {
    DirectoryINode_Delete(root);
    FSInternal_Delete(data);
    return NULL;
  }
  root->base.mode = 0755 | S_IFDIR;
  root->dents[0] = Dent_Create(".", (struct BaseINode*)root);
  root->dents[1] = Dent_Create("..", (struct BaseINode*)root);
  root->dentCount = root->base.nlink = 2;
  if (UNLIKELY(!PushINode(data, (struct BaseINode*)root))) {
    DirectoryINode_Delete(root);
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
  if (UNLIKELY(!TryAlloc((void**)&fs, sizeof(struct FileSystem)))) {
    FSInternal_Delete(data);
    return NULL;
  }
  fs->data = data;
  return fs;
}
void FileSystem_Delete(struct FileSystem* thisArg) {
  FSInternal_Delete(thisArg->data);
  free(thisArg);
}

int FileSystem_FAccessAt2(struct FileSystem* thisArg, int dirFd, const char* path, int mode, int flags) {
  if (UNLIKELY(mode & ~(F_OK | R_OK | W_OK | X_OK)) ||
      UNLIKELY(flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) ||
      UNLIKELY(flags & AT_EMPTY_PATH && path[0] != '\0'))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
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
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    inode = (struct BaseINode*)fs->cwd.inode;
  else res = GetINodeNoParentFollow(fs, path, &inode, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (mode != F_OK && !BaseINode_CanUse(inode, mode)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EACCES;
  }
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_FAccessAt(struct FileSystem* thisArg, int dirFd, const char* path, int mode) {
  return FileSystem_FAccessAt2(thisArg, dirFd, path, mode, F_OK);
}
int FileSystem_Access(struct FileSystem* thisArg, const char* path, int mode) {
  return FileSystem_FAccessAt2(thisArg, AT_FDCWD, path, mode, F_OK);
}
int FileSystem_OpenAt(struct FileSystem* thisArg, int dirFd, const char* path, int flags, mode_t mode) {
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
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
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
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  if (flags & O_CREAT && flags & O_EXCL)
    flags |= O_NOFOLLOW;
  if (flags & O_WRONLY && flags & O_RDWR)
    flags &= ~O_RDWR;
  struct BaseINode* inode;
  struct DirectoryINode* parent = NULL;
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
    if (!BaseINode_CanUse(inode, FlagsToPerms(flags))) {
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
      struct RegularINode* x;
      if (UNLIKELY(!TryAlloc((void**)&x, sizeof(struct RegularINode)))) {
        pthread_mutex_unlock(&fs->mtx);
        free((void*)name);
        return -EIO;
      }
      RegularINode_New(x);
      if (UNLIKELY(!PushINode(fs, (struct BaseINode*)x))) {
        pthread_mutex_unlock(&fs->mtx);
        RegularINode_Delete(x);
        free((void*)name);
        return -EIO;
      }
      if (UNLIKELY(!DirectoryINode_PushDent(parent, name, (struct BaseINode*)x))) {
        RemoveINode(fs, (struct BaseINode*)x);
        pthread_mutex_unlock(&fs->mtx);
        free((void*)name);
        return -EIO;
      }
      x->base.mode = mode;
      x->base.nlink = 1;
      parent->base.ctime = parent->base.mtime = x->base.btime;
      res = PushFd(fs, (struct BaseINode*)x, flags);
      if (UNLIKELY(res < 0)) {
        DirectoryINode_RemoveDent(parent, name);
        RemoveINode(fs, (struct BaseINode*)x);
        free((void*)name);
      }
    }
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (S_ISDIR(inode->mode)) {
    if (flags & 020000000) {
      struct RegularINode* x;
      if (UNLIKELY(!TryAlloc((void**)&x, sizeof(struct RegularINode)))) {
        pthread_mutex_unlock(&fs->mtx);
        return -EIO;
      }
      RegularINode_New(x);
      if (UNLIKELY(!PushINode(fs, (struct BaseINode*)x))) {
        pthread_mutex_unlock(&fs->mtx);
        RegularINode_Delete(x);
        return -EIO;
      }
      x->base.mode = (mode & ~S_IFMT) | S_IFREG;
      res = PushFd(fs, (struct BaseINode*)x, flags);
      if (UNLIKELY(res < 0))
        RemoveINode(fs, (struct BaseINode*)x);
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
      RegularINode_TruncateData((struct RegularINode*)inode, 0);
  }
  res = PushFd(fs, inode, flags);
  pthread_mutex_unlock(&fs->mtx);
  return res;
}
int FileSystem_Open(struct FileSystem* thisArg, const char* path, int flags, mode_t mode) {
  return FileSystem_OpenAt(thisArg, AT_FDCWD, path, flags, mode);
}
int FileSystem_Creat(struct FileSystem* thisArg, const char* path, mode_t mode) {
  return FileSystem_OpenAt(thisArg, AT_FDCWD, path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}
int FileSystem_Close(struct FileSystem* thisArg, unsigned int fd) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  int res = RemoveFd(fs, fd);
  pthread_mutex_unlock(&fs->mtx);
  return res;
}
int FileSystem_CloseRange(struct FileSystem* thisArg, unsigned int fd, unsigned int maxFd, unsigned int flags) {
  if (UNLIKELY(flags != 0) ||
      UNLIKELY(fd > maxFd))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  int endIdx = fs->fdCount;
  for (int i = 0; i != endIdx; ++i) {
    struct Fd* f = fs->fds[i];
    if (f->fd >= fd) {
      RemoveFd2(fs, f, i);
      --endIdx;
      while (i != endIdx) {
        f = fs->fds[i];
        if (f->fd < maxFd) {
          RemoveFd2(fs, f, i);
          --endIdx;
        } else break;
      }
      break;
    }
  }
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_MkNodAt(struct FileSystem* thisArg, int dirFd, const char* path, mode_t mode, dev_t dev) {
  if (mode & S_IFMT) {
    if (UNLIKELY(S_ISDIR(mode)))
      return -EPERM;
    if (UNLIKELY(!S_ISREG(mode)))
      return -EINVAL;
  }
  if (UNLIKELY(dev != 0))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
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
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  struct DirectoryINode* parent = NULL;
  int res = GetINodeParent(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(!parent)) {
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
  const char* name = GetAbsoluteLast(fs, path);
  if (UNLIKELY(!name)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  struct RegularINode* x;
  if (UNLIKELY(!TryAlloc((void**)&x, sizeof(struct RegularINode)))) {
    pthread_mutex_unlock(&fs->mtx);
    free((void*)name);
    return -EIO;
  }
  RegularINode_New(x);
  if (UNLIKELY(!PushINode(fs, (struct BaseINode*)x))) {
    pthread_mutex_unlock(&fs->mtx);
    RegularINode_Delete(x);
    free((void*)name);
    return -EIO;
  }
  if (UNLIKELY(!DirectoryINode_PushDent(parent, name, (struct BaseINode*)x))) {
    RemoveINode(fs, (struct BaseINode*)x);
    pthread_mutex_unlock(&fs->mtx);
    free((void*)name);
    return -EIO;
  }
  x->base.mode = (mode & 07777) | S_IFREG;
  x->base.nlink = 1;
  parent->base.ctime = parent->base.mtime = x->base.btime;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_MkNod(struct FileSystem* thisArg, const char* path, mode_t mode, dev_t dev) {
  return FileSystem_MkNodAt(thisArg, AT_FDCWD, path, mode, dev);
}
int FileSystem_MkDirAt(struct FileSystem* thisArg, int dirFd, const char* path, mode_t mode) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
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
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  struct DirectoryINode* parent = NULL;
  int res = GetINodeParent(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(!parent)) {
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
  const char* name = GetAbsoluteLast(fs, path);
  if (UNLIKELY(!name)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  struct DirectoryINode* x;
  if (UNLIKELY(!TryAlloc((void**)&x, sizeof(struct DirectoryINode)))) {
    pthread_mutex_unlock(&fs->mtx);
    free((void*)name);
    return -EIO;
  }
  DirectoryINode_New(x);
  if (UNLIKELY(!TryAlloc((void**)&x->dents, sizeof(struct Dent) * 2))) {
    pthread_mutex_unlock(&fs->mtx);
    DirectoryINode_Delete(x);
    free((void*)name);
    return -EIO;
  }
  x->dentCount = 2;
  if (UNLIKELY(!PushINode(fs, (struct BaseINode*)x))) {
    pthread_mutex_unlock(&fs->mtx);
    DirectoryINode_Delete(x);
    free((void*)name);
    return -EIO;
  }
  if (UNLIKELY(!DirectoryINode_PushDent(parent, name, (struct BaseINode*)x))) {
    RemoveINode(fs, (struct BaseINode*)x);
    pthread_mutex_unlock(&fs->mtx);
    free((void*)name);
    return -EIO;
  }
  x->base.mode = (mode & 07777) | S_IFDIR;
  x->base.nlink = 2;
  x->dents[0] = Dent_Create(".", (struct BaseINode*)x);
  x->dents[1] = Dent_Create("..", (struct BaseINode*)parent);
  ++parent->base.nlink;
  parent->base.ctime = parent->base.mtime = x->base.btime;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_MkDir(struct FileSystem* thisArg, const char* path, mode_t mode) {
  return FileSystem_MkDirAt(thisArg, AT_FDCWD, path, mode);
}
int FileSystem_SymLinkAt(struct FileSystem* thisArg, const char* oldPath, int newDirFd, const char* newPath) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
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
  struct BaseINode* oldInode;
  int res = GetINodeNoParent(fs, oldPath, &oldInode);
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (newDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  struct BaseINode* newInode;
  struct DirectoryINode* newParent = NULL;
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
  struct SymLinkINode* x;
  if (UNLIKELY(!TryAlloc((void**)&x, sizeof(struct SymLinkINode)))) {
    pthread_mutex_unlock(&fs->mtx);
    free((void*)name);
    return -EIO;
  }
  SymLinkINode_New(x);
  size_t oldPathLen = strlen(oldPath);
  if (UNLIKELY(!TryAlloc((void**)&x->data, oldPathLen)) ||
      UNLIKELY(!PushINode(fs, (struct BaseINode*)x))) {
    pthread_mutex_unlock(&fs->mtx);
    SymLinkINode_Delete(x);
    free((void*)name);
    return -EIO;
  }
  memcpy(x->data, oldPath, oldPathLen);
  if (UNLIKELY(!(x->target = AbsolutePath(fs, oldPath))) ||
      UNLIKELY(!DirectoryINode_PushDent(newParent, name, (struct BaseINode*)x))) {
    RemoveINode(fs, (struct BaseINode*)x);
    pthread_mutex_unlock(&fs->mtx);
    free((void*)name);
    return -EIO;
  }
  x->base.mode = 0777 | S_IFLNK;
  x->base.nlink = 1;
  x->base.size = oldPathLen;
  newParent->base.ctime = newParent->base.mtime = x->base.btime;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_SymLink(struct FileSystem* thisArg, const char* oldPath, const char* newPath) {
  return FileSystem_SymLinkAt(thisArg, oldPath, AT_FDCWD, newPath);
}
int FileSystem_ReadLinkAt(struct FileSystem* thisArg, int dirFd, const char* path, char* buf, int bufLen) {
  if (UNLIKELY(bufLen <= 0))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
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
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
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
  memcpy(buf, ((struct SymLinkINode*)inode)->data, bufLen);
  clock_gettime(CLOCK_REALTIME, &inode->atime);
  pthread_mutex_unlock(&fs->mtx);
  return bufLen;
}
int FileSystem_ReadLink(struct FileSystem* thisArg, const char* path, char* buf, int bufLen) {
  return FileSystem_ReadLinkAt(thisArg, AT_FDCWD, path, buf, bufLen);
}
int FileSystem_GetDents(struct FileSystem* thisArg, unsigned int fdNum, struct linux_dirent* dirp, unsigned int count) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct BaseINode* inode = fd->inode;
  if (UNLIKELY(!S_ISDIR(inode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOTDIR;
  }
  if (fd->seekOff >= ((struct DirectoryINode*)inode)->dentCount) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  unsigned int nread = 0;
  char* dirpData = (char*)dirp;
  off_t endIdx = ((struct DirectoryINode*)inode)->dentCount;
  do {
    struct Dent d = ((struct DirectoryINode*)inode)->dents[fd->seekOff];
    size_t nameLen = strlen(d.name);
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
    unsigned short reclen = ALIGN(__builtin_offsetof(struct linux_dirent, d_name) + nameLen + 2, sizeof(long));
#undef ALIGN
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
  } while (++fd->seekOff != endIdx);
  if (UNLIKELY(nread == 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  pthread_mutex_unlock(&fs->mtx);
  return nread;
}
int FileSystem_LinkAt(struct FileSystem* thisArg, int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, int flags) {
  if (UNLIKELY(flags & ~(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH)) ||
      UNLIKELY(flags & AT_EMPTY_PATH && oldPath[0] != '\0'))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
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
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (oldDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)oldFd->inode;
  struct BaseINode* oldInode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    oldInode = (struct BaseINode*)fs->cwd.inode;
  else res = GetINodeNoParentFollow(fs, oldPath, &oldInode, flags & AT_SYMLINK_FOLLOW);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (newDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)newFd->inode;
  struct BaseINode* newInode;
  struct DirectoryINode* newParent = NULL;
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
  if (UNLIKELY(!DirectoryINode_PushDent(newParent, name, oldInode))) {
    pthread_mutex_unlock(&fs->mtx);
    free((void*)name);
    return -EIO;
  }
  ++oldInode->nlink;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  oldInode->ctime = newParent->base.ctime = newParent->base.mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Link(struct FileSystem* thisArg, const char* oldPath, const char* newPath) {
  return FileSystem_LinkAt(thisArg, AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
}
int FileSystem_UnlinkAt(struct FileSystem* thisArg, int dirFd, const char* path, int flags) {
  if (UNLIKELY(flags & ~AT_REMOVEDIR))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
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
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  struct DirectoryINode* parent;
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
  {
    int fdCount = fs->fdCount;
    for (int i = 0; i != fdCount; ++i)
      if (UNLIKELY(fs->fds[i]->inode == inode)) {
        pthread_mutex_unlock(&fs->mtx);
        return -EBUSY;
      }
  }
  if (flags & AT_REMOVEDIR) {
    const char* last = GetLast(path);
    if (UNLIKELY(!last)) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOMEM;
    }
    bool isDot = strcmp(last, ".") == 0;
    free((void*)last);
    if (UNLIKELY(isDot)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EINVAL;
    }
    if (UNLIKELY(((struct DirectoryINode*)inode)->dentCount != 2)) {
      pthread_mutex_unlock(&fs->mtx);
      return -ENOTEMPTY;
    }
  }
  const char* name = GetAbsoluteLast(fs, path);
  if (UNLIKELY(!name)) {
    pthread_mutex_unlock(&fs->mtx);
    return -ENOMEM;
  }
  DirectoryINode_RemoveDent(parent, name);
  free((void*)name);
  if (flags & AT_REMOVEDIR)
    --parent->base.nlink;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  --inode->nlink;
  if (UNLIKELY(BaseINode_IsUnused(inode)))
    RemoveINode(fs, inode);
  else inode->ctime = ts;
  parent->base.ctime = parent->base.mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Unlink(struct FileSystem* thisArg, const char* path) {
  return FileSystem_UnlinkAt(thisArg, AT_FDCWD, path, 0);
}
int FileSystem_RmDir(struct FileSystem* thisArg, const char* path) {
  return FileSystem_UnlinkAt(thisArg, AT_FDCWD, path, AT_REMOVEDIR);
}
int FileSystem_RenameAt2(struct FileSystem* thisArg, int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, unsigned int flags) {
  if (UNLIKELY(flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE)) ||
      UNLIKELY(flags & RENAME_NOREPLACE && flags & RENAME_EXCHANGE))
    return -EINVAL;
  const char* last = GetLast(oldPath);
  if (UNLIKELY(!last))
    return -ENOMEM;
  bool isDot = strcmp(last, ".") == 0 || strcmp(last, "..") == 0;
  free((void*)last);
  if (UNLIKELY(isDot))
    return -EBUSY;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
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
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (oldDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)oldFd->inode;
  struct BaseINode* oldInode;
  struct DirectoryINode* oldParent;
  int res = GetINodeParent(fs, oldPath, &oldInode, &oldParent);
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  if (newDirFd != AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)newFd->inode;
  struct BaseINode* newInode = NULL;
  struct DirectoryINode* newParent = NULL;
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
      if (UNLIKELY(((struct DirectoryINode*)newInode)->dentCount > 2)) {
        pthread_mutex_unlock(&fs->mtx);
        return -ENOTEMPTY;
      }
    }
    if (UNLIKELY(oldInode == fs->inodes[0]) ||
        UNLIKELY(oldInode == (struct BaseINode*)fs->cwd.inode)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBUSY;
    }
  } else if (newInode && UNLIKELY(S_ISDIR(newInode->mode))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EISDIR;
  }
  if (UNLIKELY(DirectoryINode_IsInSelf(oldParent, (struct BaseINode*)newParent))) {
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
    free((void*)oldName);
    return -ENOMEM;
  }
  if (flags & RENAME_EXCHANGE) {
    off_t oldDentCount = oldParent->dentCount;
    for (off_t i = 0; i != oldDentCount; ++i)
      if (strcmp(oldParent->dents[i].name, oldName) == 0) {
        off_t newDentCount = newParent->dentCount;
        for (off_t j = 0; j != newDentCount; ++j)
          if (strcmp(newParent->dents[j].name, newName) == 0) {
            oldParent->dents[i].inode = newInode;
            newParent->dents[j].inode = oldInode;
            break;
          }
        break;
      }
    free((void*)oldName);
    free((void*)newName);
  } else {
    if (UNLIKELY(!DirectoryINode_PushDent(newParent, newName, oldInode))) {
      pthread_mutex_unlock(&fs->mtx);
      free((void*)oldName);
      free((void*)newName);
      return -EIO;
    }
    DirectoryINode_RemoveDent(oldParent, oldName);
    free((void*)oldName);
    if (newInode)
      DirectoryINode_RemoveDent(newParent, newName);
    if (S_ISDIR(oldInode->mode)) {
      --oldParent->base.nlink;
      ++newParent->base.nlink;
    }
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (!(flags & RENAME_EXCHANGE)) {
    if (newInode) {
      --newInode->nlink;
      if (UNLIKELY(BaseINode_IsUnused(newInode)))
        RemoveINode(fs, newInode);
      else newInode->ctime = ts;
    }
  } else newInode->ctime = ts;
  oldInode->ctime = ts;
  oldParent->base.ctime = oldParent->base.mtime = ts;
  newParent->base.ctime = newParent->base.mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_RenameAt(struct FileSystem* thisArg, int oldDirFd, const char* oldPath, int newDirFd, const char* newPath) {
  return FileSystem_RenameAt2(thisArg, oldDirFd, oldPath, newDirFd, newPath, 0);
}
int FileSystem_Rename(struct FileSystem* thisArg, const char* oldPath, const char* newPath) {
  return FileSystem_RenameAt2(thisArg, AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
}
off_t FileSystem_LSeek(struct FileSystem* thisArg, unsigned int fdNum, off_t offset, unsigned int whence) {
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct BaseINode* inode = fd->inode;
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
      if (!S_ISREG(inode->mode)) {
        pthread_mutex_unlock(&fs->mtx);
        return -EINVAL;
      }
      struct DataIterator it;
      DataIterator_New(&it, (struct RegularINode*)inode, fd->seekOff);
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
      if (!S_ISREG(inode->mode)) {
        pthread_mutex_unlock(&fs->mtx);
        return -EINVAL;
      }
      struct DataIterator it;
      DataIterator_New(&it, (struct RegularINode*)inode, fd->seekOff);
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
ssize_t FileSystem_Read(struct FileSystem* thisArg, unsigned int fdNum, char* buf, size_t count) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct BaseINode* inode = fd->inode;
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
  DataIterator_New(&it, (struct RegularINode*)inode, fd->seekOff);
  for (size_t amountRead = 0; amountRead != count; DataIterator_Next(&it)) {
    size_t amount;
    size_t currEnd = fd->seekOff + amountRead;
    if (DataIterator_IsInData(&it)) {
      struct DataRange* range = DataIterator_GetRange(&it);
      amount = MinUnsigned((range->offset + range->size) - currEnd, count - amountRead);
      memcpy(buf + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = DataIterator_GetHole(&it);
      amount = MinUnsigned((hole.offset + hole.size) - currEnd, count - amountRead);
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
ssize_t FileSystem_Readv(struct FileSystem* thisArg, unsigned int fdNum, struct iovec* iov, int iovcnt) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct BaseINode* inode = fd->inode;
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
    size_t limit = 0x7ffff000 - totalLen;
    if (len > limit) {
      len = limit;
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
  DataIterator_New(&it, (struct RegularINode*)inode, fd->seekOff);
  for (size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; DataIterator_Next(&it)) {
    struct iovec curr = iov[iovIdx];
    size_t amount;
    size_t end = totalLen - count;
    size_t iovEnd = curr.iov_len - amountRead;
    size_t currEnd = fd->seekOff + count;
    if (DataIterator_IsInData(&it)) {
      struct DataRange* range = DataIterator_GetRange(&it);
      amount = MinUnsigned(
        MinUnsigned(
          (range->offset + range->size) - currEnd,
          iovEnd),
        end
      );
      memcpy((char*)curr.iov_base + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = DataIterator_GetHole(&it);
      amount = MinUnsigned(
        MinUnsigned(
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
  pthread_mutex_unlock(&fs->mtx);
  return totalLen;
}
ssize_t FileSystem_PRead(struct FileSystem* thisArg, unsigned int fdNum, char* buf, size_t count, off_t offset) {
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct BaseINode* inode = fd->inode;
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
  DataIterator_New(&it, (struct RegularINode*)inode, offset);
  for (size_t amountRead = 0; amountRead != count; DataIterator_Next(&it)) {
    size_t amount;
    size_t currEnd = offset + amountRead;
    if (DataIterator_IsInData(&it)) {
      struct DataRange* range = DataIterator_GetRange(&it);
      amount = MinUnsigned((range->offset + range->size) - currEnd, count - amountRead);
      memcpy(buf + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = DataIterator_GetHole(&it);
      amount = MinUnsigned((hole.offset + hole.size) - currEnd, count - amountRead);
      memset(buf + amountRead, '\0', amount);
    }
    amountRead += amount;
  }
  if (!(fd->flags & O_NOATIME))
    clock_gettime(CLOCK_REALTIME, &inode->atime);
  pthread_mutex_unlock(&fs->mtx);
  return count;
}
ssize_t FileSystem_PReadv(struct FileSystem* thisArg, unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum))) ||
      UNLIKELY(fd->flags & O_WRONLY)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct BaseINode* inode = fd->inode;
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
    size_t limit = 0x7ffff000 - totalLen;
    if (len > limit) {
      len = limit;
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
  DataIterator_New(&it, (struct RegularINode*)inode, offset);
  for (size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; DataIterator_Next(&it)) {
    struct iovec curr = iov[iovIdx];
    size_t amount;
    size_t end = totalLen - count;
    size_t iovEnd = curr.iov_len - amountRead;
    size_t currEnd = offset + count;
    if (DataIterator_IsInData(&it)) {
      struct DataRange* range = DataIterator_GetRange(&it);
      amount = MinUnsigned(
        MinUnsigned(
          (range->offset + range->size) - currEnd,
          iovEnd),
        end
      );
      memcpy((char*)curr.iov_base + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = DataIterator_GetHole(&it);
      amount = MinUnsigned(
        MinUnsigned(
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
  pthread_mutex_unlock(&fs->mtx);
  return totalLen;
}
ssize_t FileSystem_Write(struct FileSystem* thisArg, unsigned int fdNum, const char* buf, size_t count) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_unlock(&fs->mtx);
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
  struct BaseINode* inode = fd->inode;
  off_t seekOff = fd->flags & O_APPEND
    ? inode->size
    : fd->seekOff;
  off_t seekEnd;
  if (UNLIKELY(__builtin_add_overflow(seekOff, count, &seekEnd))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EFBIG;
  }
  struct DataRange* range = RegularINode_AllocData((struct RegularINode*)inode, seekOff, count);
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
ssize_t FileSystem_Writev(struct FileSystem* thisArg, unsigned int fdNum, struct iovec* iov, int iovcnt) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
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
    size_t limit = 0x7ffff000 - totalLen;
    if (len > limit) {
      len = limit;
      iov[i].iov_len = len;
    }
    totalLen += len;
  }
  if (totalLen == 0) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  struct BaseINode* inode = fd->inode;
  off_t seekOff = fd->flags & O_APPEND
    ? inode->size
    : fd->seekOff;
  off_t seekEnd;
  if (UNLIKELY(__builtin_add_overflow(seekOff, totalLen, &seekEnd))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EFBIG;
  }
  struct DataRange* range = RegularINode_AllocData((struct RegularINode*)inode, seekOff, totalLen);
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
ssize_t FileSystem_PWrite(struct FileSystem* thisArg, unsigned int fdNum, const char* buf, size_t count, off_t offset) {
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
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
  struct BaseINode* inode = fd->inode;
  {
    off_t res;
    if (UNLIKELY(__builtin_add_overflow(offset, count, &res))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EFBIG;
    }
  }
  struct DataRange* range = RegularINode_AllocData((struct RegularINode*)inode, offset, count);
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
ssize_t FileSystem_PWritev(struct FileSystem* thisArg, unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
  if (UNLIKELY(offset < 0))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
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
    size_t limit = 0x7ffff000 - totalLen;
    if (len > limit) {
      len = limit;
      iov[i].iov_len = len;
    }
    totalLen += len;
  }
  if (UNLIKELY(totalLen == 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return 0;
  }
  struct BaseINode* inode = fd->inode;
  {
    off_t res;
    if (UNLIKELY(__builtin_add_overflow(offset, totalLen, &res))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EFBIG;
    }
  }
  struct DataRange* range = RegularINode_AllocData((struct RegularINode*)inode, offset, totalLen);
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
ssize_t FileSystem_SendFile(struct FileSystem* thisArg, unsigned int outFd, unsigned int inFd, off_t* offset, size_t count) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
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
  struct BaseINode* inodeIn = fdIn->inode;
  struct BaseINode* inodeOut = fdOut->inode;
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
    inodeOut->size = outSeekEnd;
  struct DataIterator itIn;
  struct DataIterator itOut;
  DataIterator_New(&itIn, (struct RegularINode*)inodeIn, off);
  DataIterator_New(&itOut, (struct RegularINode*)inodeOut, fdOut->seekOff);
  for (size_t amountRead = 0; amountRead != count;) {
    size_t amount;
    size_t amountToRead = count - amountRead;
    size_t currEndIn = off + amountRead;
    size_t currEndOut = currEndOut;
    if (!DataIterator_IsInData(&itIn)) {
      struct HoleRange holeIn = DataIterator_GetHole(&itIn);
      off_t holeInEnd = holeIn.offset + holeIn.size;
      if (!DataIterator_IsInData(&itOut)) {
        struct HoleRange holeOut = DataIterator_GetHole(&itOut);
        amount = (holeOut.offset + holeOut.size) - currEndOut;
        {
          size_t newAmount = holeInEnd - currEndIn;
          if (amount > newAmount) {
            amount = newAmount;
            DataIterator_Next(&itIn);
          } else DataIterator_Next(&itOut);
        }
        if (amount == 0)
          continue;
        amountRead += MinUnsigned(amount, amountToRead);
        continue;
      }
      struct DataRange* rangeOut = DataIterator_GetRange(&itOut);
      amount = (rangeOut->offset + rangeOut->size) - currEndOut;
      {
        size_t newAmount = holeInEnd - currEndIn;
        if (amount > newAmount) {
          amount = newAmount;
          DataIterator_Next(&itIn);
        } else DataIterator_Next(&itOut);
      }
      if (amount == 0)
        continue;
      if (amount > amountToRead)
        amount = amountToRead;
      memset(rangeOut->data + (currEndOut - rangeOut->offset), '\0', amount);
      amountRead += amount;
      continue;
    }
    struct DataRange* rangeIn = DataIterator_GetRange(&itIn);
    amount = MinUnsigned((rangeIn->offset + rangeIn->size) - currEndIn, amountToRead);
    struct DataRange* rangeOut = RegularINode_AllocData((struct RegularINode*)inodeOut, currEndOut, amount);
    if (UNLIKELY(!rangeOut)) {
      pthread_mutex_unlock(&fs->mtx);
      return -EIO;
    }
    memcpy(
      rangeOut->data + (currEndOut - rangeOut->offset),
      rangeIn->data + (currEndIn - rangeIn->offset),
      amount
    );
    amountRead += amount;
    DataIterator_Next(&itIn);
    DataIterator_SeekTo(&itOut, currEndOut);
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  if (!(fdIn->flags & O_NOATIME))
    inodeIn->atime = ts;
  inodeOut->mtime = inodeOut->ctime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return count;
}
int FileSystem_FTruncate(struct FileSystem* thisArg, unsigned int fdNum, off_t length) {
  if (UNLIKELY(length < 0))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct BaseINode* inode = fd->inode;
  if (UNLIKELY(!S_ISREG(inode->mode)) ||
      UNLIKELY(!(fd->flags & (O_WRONLY | O_RDWR)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EINVAL;
  }
  if (UNLIKELY(fd->flags & O_APPEND)) {
    pthread_mutex_unlock(&fs->mtx);
    return -EPERM;
  }
  RegularINode_TruncateData((struct RegularINode*)inode, length);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->ctime = inode->mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Truncate(struct FileSystem* thisArg, const char* path, off_t length) {
  if (UNLIKELY(length < 0))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct BaseINode* inode;
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
  if (UNLIKELY(!BaseINode_CanUse(inode, W_OK))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EACCES;
  }
  RegularINode_TruncateData((struct RegularINode*)inode, length);
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  inode->ctime = inode->mtime = ts;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_FChModAt(struct FileSystem* thisArg, int dirFd, const char* path, mode_t mode) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
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
int FileSystem_FChMod(struct FileSystem* thisArg, unsigned int fdNum, mode_t mode) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  struct BaseINode* inode = fd->inode;
  inode->mode = (mode & 07777) | (inode->mode & S_IFMT);
  clock_gettime(CLOCK_REALTIME, &inode->ctime);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_ChMod(struct FileSystem* thisArg, const char* path, mode_t mode) {
  return FileSystem_FChModAt(thisArg, AT_FDCWD, path, mode);
}
int FileSystem_ChDir(struct FileSystem* thisArg, const char* path) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct BaseINode* inode;
  struct DirectoryINode* parent;
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
  free((void*)fs->cwd.path);
  fs->cwd.path = absPath;
  fs->cwd.inode = (struct DirectoryINode*)inode;
  fs->cwd.parent = parent;
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_GetCwd(struct FileSystem* thisArg, char* buf, size_t size) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  if ((struct BaseINode*)fs->cwd.inode != fs->inodes[0]) {
    struct BaseINode* inode;
    struct DirectoryINode* parent;
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
int FileSystem_FStat(struct FileSystem* thisArg, unsigned int fdNum, struct stat* buf) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct Fd* fd;
  if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
    pthread_mutex_unlock(&fs->mtx);
    return -EBADF;
  }
  BaseINode_FillStat(fd->inode, buf);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Stat(struct FileSystem* thisArg, const char* path, struct stat* buf) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct BaseINode* inode;
  int res = GetINodeNoParentFollow(fs, path, &inode, true);
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  BaseINode_FillStat(inode, buf);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_LStat(struct FileSystem* thisArg, const char* path, struct stat* buf) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct BaseINode* inode;
  int res = GetINodeNoParent(fs, path, &inode);
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  BaseINode_FillStat(inode, buf);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_Statx(struct FileSystem* thisArg, int dirFd, const char* path, int flags, int mask, struct statx* buf) {
  if (UNLIKELY(flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) ||
      UNLIKELY(mask & ~STATX_ALL) ||
      UNLIKELY(flags & AT_EMPTY_PATH && path[0] != '\0'))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = 0;
  if (flags & AT_EMPTY_PATH)
    inode = (struct BaseINode*)fs->cwd.inode;
  else res = GetINodeNoParentFollow(fs, path, &inode, !(flags & AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (UNLIKELY(res != 0)) {
    pthread_mutex_unlock(&fs->mtx);
    return res;
  }
  BaseINode_FillStatx(inode, buf, mask);
  pthread_mutex_unlock(&fs->mtx);
  return 0;
}
int FileSystem_UTimeNsAt(struct FileSystem* thisArg, int dirFd, const char* path, const struct timespec* times, int flags) {
  if (UNLIKELY(flags & ~AT_SYMLINK_NOFOLLOW))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, dirFd)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
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
int FileSystem_FUTimesAt(struct FileSystem* thisArg, unsigned int fdNum, const char* path, const struct timeval* times) {
  if (times &&
      UNLIKELY(
        UNLIKELY(times[0].tv_usec < 0) || UNLIKELY(times[0].tv_usec >= 1000000) ||
        UNLIKELY(times[1].tv_usec < 0) || UNLIKELY(times[1].tv_usec >= 1000000)
      ))
    return -EINVAL;
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (fdNum != AT_FDCWD) {
    struct Fd* fd;
    if (UNLIKELY(!(fd = GetFd(fs, fdNum)))) {
      pthread_mutex_unlock(&fs->mtx);
      return -EBADF;
    }
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
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
int FileSystem_UTimes(struct FileSystem* thisArg, const char* path, const struct timeval* times) {
  return FileSystem_FUTimesAt(thisArg, AT_FDCWD, path, times);
}
int FileSystem_UTime(struct FileSystem* thisArg, const char* path, const struct utimbuf* times) {
  struct timeval ts[2];
  if (times) {
    ts[0].tv_sec = times->actime;
    ts[0].tv_usec = 0;
    ts[1].tv_sec = times->modtime;
    ts[1].tv_usec = 0;
  }
  return FileSystem_FUTimesAt(thisArg, AT_FDCWD, path, times ? ts : NULL);
}

#define TimeSpec_Create(ts_sec, ts_nsec) ({ \
  struct timespec ts; \
  ts.tv_sec = ts_sec; \
  ts.tv_nsec = ts_nsec; \
  ts; \
})

bool FileSystem_DumpToFile(struct FileSystem* thisArg, const char* filename) {
  struct FSInternal* fs = (struct FSInternal*)thisArg->data;
  pthread_mutex_lock(&fs->mtx);
  int fd = creat(filename, 0644);
  if (UNLIKELY(fd < 0))
    goto err1;
  if (UNLIKELY(write(fd, "\x7FVFS", 4) != 4) ||
      UNLIKELY(write(fd, &fs->inodeCount, sizeof(ino_t)) != sizeof(ino_t)))
    goto err2;
  for (ino_t i = 0, inodeCount = fs->inodeCount; i != inodeCount; ++i) {
    struct BaseINode* inode = fs->inodes[i];
    struct DumpedINode dumped;
    dumped.id = inode->id;
    dumped.size = inode->size;
    dumped.nlink = inode->nlink;
    dumped.mode = inode->mode;
    dumped.btime = TimeSpec_Create(inode->btime.tv_sec, inode->btime.tv_nsec);
    dumped.ctime = TimeSpec_Create(inode->ctime.tv_sec, inode->ctime.tv_nsec);
    dumped.mtime = TimeSpec_Create(inode->mtime.tv_sec, inode->mtime.tv_nsec);
    dumped.atime = TimeSpec_Create(inode->atime.tv_sec, inode->atime.tv_nsec);
    if (UNLIKELY(write(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode)))
      goto err2;
    if (S_ISLNK(inode->mode)) {
      size_t targetLen = strlen(((struct SymLinkINode*)inode)->target) + 1;
      off_t dataLen = inode->size;
      if (UNLIKELY(write(fd, ((struct SymLinkINode*)inode)->target, targetLen) != targetLen) ||
          UNLIKELY(write(fd, ((struct SymLinkINode*)inode)->data, dataLen) != dataLen))
        goto err2;
    }
    if (S_ISDIR(inode->mode)) {
      if (UNLIKELY(write(fd, &((struct DirectoryINode*)inode)->dentCount, sizeof(off_t)) != sizeof(off_t)) ||
          UNLIKELY(write(fd, &((struct DirectoryINode*)inode)->dents[1].inode->ndx, sizeof(ino_t)) != sizeof(ino_t)))
        goto err2;
      for (off_t j = 2; j != ((struct DirectoryINode*)inode)->dentCount; ++j) {
        struct Dent* dent = &((struct DirectoryINode*)inode)->dents[j];
        size_t nameLen = strlen(dent->name) + 1;
        if (UNLIKELY(write(fd, &dent->inode->ndx, sizeof(ino_t)) != sizeof(ino_t)) ||
            UNLIKELY(write(fd, dent->name, nameLen) != nameLen))
          goto err2;
      }
    } else if (inode->size != 0) {
      if (UNLIKELY(write(fd, &((struct RegularINode*)inode)->dataRangeCount, sizeof(off_t)) != sizeof(off_t)))
        goto err2;
      off_t dataRangeCount = ((struct RegularINode*)inode)->dataRangeCount;
      for (off_t j = 0; j != dataRangeCount; ++j) {
        struct DataRange* range = ((struct RegularINode*)inode)->dataRanges[j];
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

bool TryAllocFromMode(struct BaseINode** inode, mode_t mode) {
  if (S_ISREG(mode))
    return TryAlloc((void**)inode, sizeof(struct RegularINode));
  if (S_ISDIR(mode))
    return TryAlloc((void**)inode, sizeof(struct DirectoryINode));
  if (S_ISLNK(mode))
    return TryAlloc((void**)inode, sizeof(struct SymLinkINode));
  __builtin_unreachable();
}

struct FileSystem* FileSystem_LoadFromFile(const char* filename) {
  int fd = open(filename, O_RDONLY);
  if (UNLIKELY(fd < 0))
    goto err_at_open;
  char magic[4];
  ino_t inodeCount;
  struct BaseINode** inodes;
  if (UNLIKELY(read(fd, magic, 4) != 4) ||
      UNLIKELY(memcmp(magic, "\x7FVFS", 4) != 0) ||
      UNLIKELY(read(fd, &inodeCount, sizeof(ino_t)) != sizeof(ino_t)))
    goto err_after_open;
  if (UNLIKELY(!TryAlloc((void**)&inodes, sizeof(struct BaseINode*) * inodeCount)))
    goto err_after_open;
  for (ino_t i = 0; i != inodeCount; ++i) {
    struct BaseINode* inode;
    struct DumpedINode dumped;
    if (UNLIKELY(read(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode)))
      goto err_at_inode_loop;
    if (UNLIKELY(!TryAllocFromMode(&inode, dumped.mode)))
      goto err_at_inode_loop;
    inode->ndx = i;
    inode->id = dumped.id;
    inode->size = dumped.size;
    inode->nlink = dumped.nlink;
    inode->mode = dumped.mode;
    inode->btime = TimeSpec_Create(dumped.btime.tv_sec, dumped.btime.tv_nsec);
    inode->ctime = TimeSpec_Create(dumped.ctime.tv_sec, dumped.ctime.tv_nsec);
    inode->mtime = TimeSpec_Create(dumped.mtime.tv_sec, dumped.mtime.tv_nsec);
    inode->atime = TimeSpec_Create(dumped.atime.tv_sec, dumped.atime.tv_nsec);
    if (S_ISLNK(inode->mode)) {
      char target[PATH_MAX];
      size_t targetLen = 0;
      do {
        if (UNLIKELY(read(fd, &target[targetLen], 1) != 1))
          goto err_after_inode_init;
      } while (target[targetLen++] != '\0');
      ((struct SymLinkINode*)inode)->target = strdup(target);
      if (UNLIKELY(!((struct SymLinkINode*)inode)->target))
        goto err_after_inode_init;
      char data[PATH_MAX];
      size_t dataLen = 0;
      do {
        if (UNLIKELY(read(fd, &data[dataLen], 1) != 1))
          goto err_after_target_alloc;
      } while (data[dataLen++] != '\0');
      if (UNLIKELY(!TryAlloc((void**)&((struct SymLinkINode*)inode)->data, dataLen)))
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
      if (UNLIKELY(read(fd, &dentCount, sizeof(off_t)) != sizeof(off_t)))
        goto err_after_inode_init;
      if (UNLIKELY(!TryAlloc((void**)&((struct DirectoryINode*)inode)->dents, sizeof(struct Dent) * dentCount)))
        goto err_after_inode_init;
      ((struct DirectoryINode*)inode)->dents[0] = Dent_Create(".", inode);
      ((struct DirectoryINode*)inode)->dents[1].name = "..";
      if (UNLIKELY(read(fd, &((struct DirectoryINode*)inode)->dents[1].inode, sizeof(ino_t)) != sizeof(ino_t)))
        goto err_after_dent_alloc;
      for (off_t j = 2; j != dentCount; ++j) {
        struct Dent* dent = &((struct DirectoryINode*)inode)->dents[j];
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
      if (LIKELY(inode->size != 0)) {
        off_t dataRangeCount;
        if (UNLIKELY(read(fd, &dataRangeCount, sizeof(off_t)) != sizeof(off_t)))
          goto err_after_inode_init;
        if (UNLIKELY(!TryAlloc((void**)&((struct RegularINode*)inode)->dataRanges, sizeof(struct DataRange*) * dataRangeCount)))
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
          if (UNLIKELY(!TryAlloc((void**)&range, sizeof(struct DataRange))))
            goto err_after_dataranges_init;
          range->offset = offset;
          range->size = size;
          if (UNLIKELY(!TryAlloc((void**)&range->data, size)))
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
          DataRange_Delete(((struct RegularINode*)inode)->dataRanges[k]);
        free(((struct RegularINode*)inode)->dataRanges);
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
      off_t dentCount = ((struct DirectoryINode*)inode)->dentCount;
      for (off_t j = 1; j != dentCount; ++j) {
        struct Dent* dent = &((struct DirectoryINode*)inode)->dents[j];
        ino_t ino = (ino_t)dent->inode;
        if (UNLIKELY(ino >= inodeCount))
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
  struct FileSystem* fs;
  struct FSInternal* data;
  if (UNLIKELY(!TryAlloc((void**)&fs, sizeof(struct FileSystem))))
    goto err_after_inodes;
  if (UNLIKELY(!TryAlloc((void**)&data, sizeof(struct FSInternal))))
    goto err_after_fs_init;
  FSInternal_New(data);
  data->inodes = inodes;
  data->inodeCount = inodeCount;
  if (UNLIKELY(!(data->cwd.path = strdup("/"))))
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