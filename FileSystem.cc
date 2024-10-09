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

#include <stdint.h>

#if !(defined(__linux__) || defined(_WIN32))
#error FileSystem is only available on Linux and Windows
#elif INTPTR_MAX == INT32_MAX
#error FileSystem is not available on 32-bit platforms
#else

#include "FileSystem.h"
#include <new>

#ifdef __linux__
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#else
#include <Windows.h>
#endif

#undef FS_O_TMPFILE
#define FS_O_TMPFILE 020000000
#define FS_FOLLOW_MAX 40
#define FS_ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define FS_FALLOC_FL_ALLOCATE_RANGE 0x00
#define FS_FALLOC_FL_MODE_MASK (FS_FALLOC_FL_ALLOCATE_RANGE |	\
  FS_FALLOC_FL_ZERO_RANGE | \
  FS_FALLOC_FL_PUNCH_HOLE | \
  FS_FALLOC_FL_COLLAPSE_RANGE | \
  FS_FALLOC_FL_INSERT_RANGE)
#define FS_RW_MAX 0x7ffff000
#define FS_IOV_MAX 1024

namespace {
  void* MemCpy(void* dest, const void* src, uint64_t len) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    uint32_t i = 0;
    while (len - i >= sizeof(uint64_t)) {
      *(uint64_t*)(d + i) = *(uint64_t*)(s + i);
      i += sizeof(uint64_t);
    }
    if (len - i >= sizeof(uint32_t)) {
      *(uint32_t*)(d + i) = *(uint32_t*)(s + i);
      i += sizeof(uint32_t);
    }
    if (len - i >= sizeof(uint16_t)) {
      *(uint16_t*)(d + i) = *(uint16_t*)(s + i);
      i += sizeof(uint16_t);
    }
    if (len - i >= sizeof(uint8_t)) {
      *(uint8_t*)(d + i) = *(uint8_t*)(s + i);
      i += sizeof(uint8_t);
    }
    return dest;
  }
  void* MemMove(void* dest, const void* src, uint64_t len) {
    if (dest < src)
      return MemCpy(dest, src, len);
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (len >= sizeof(uint64_t)) {
      *(uint64_t*)(d + (len - sizeof(uint64_t))) = *(uint64_t*)(s + (len - sizeof(uint64_t)));
      len -= sizeof(uint64_t);
    }
    if (len >= sizeof(uint32_t)) {
      *(uint32_t*)(d + (len - sizeof(uint32_t))) = *(uint32_t*)(s + (len - sizeof(uint32_t)));
      len -= sizeof(uint32_t);
    }
    if (len >= sizeof(uint16_t)) {
      *(uint16_t*)(d + (len - sizeof(uint16_t))) = *(uint16_t*)(s + (len - sizeof(uint16_t)));
      len -= sizeof(uint16_t);
    }
    if (len >= sizeof(uint8_t)) {
      *(uint8_t*)(d + (len - sizeof(uint8_t))) = *(uint8_t*)(s + (len - sizeof(uint8_t)));
      len -= sizeof(uint8_t);
    }
    return dest;
  }

  template<typename R, typename T1, typename T2>
  R Min(T1 a, T2 b) {
    if (a < b)
      return a;
    return b;
  }

  template<bool I = false, typename T>
  bool Alloc(T** ptr, fs_size_t length = 1) {
    T* newPtr;
#ifdef __linux__
    newPtr = (T*)malloc(sizeof(T) * length);
#else
    newPtr = (T*)VirtualAlloc(NULL, sizeof(T) * length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#endif
    if (!newPtr)
      return false;
    if constexpr (I)
      for (fs_size_t i = 0; i != length; ++i)
        new (&newPtr[i]) T;
    *ptr = newPtr;
    return true;
  }
  template<bool D = true, typename T>
  void Delete(T* ptr) {
    if constexpr (D)
      ptr->~T();
#ifdef __linux__
    free((void*)ptr);
#else
    VirtualFree((void*)ptr, 0, MEM_RELEASE);
#endif
  }
  template<typename T>
  bool Realloc(T** ptr, fs_size_t ptrLen, fs_size_t length) {
    T* newPtr;
    if (!Alloc(&newPtr, sizeof(T) * length))
      return length < ptrLen;
    if (*ptr) {
      MemCpy(newPtr, *ptr, sizeof(T) * Min<uint64_t>(ptrLen, length));
      Delete(*ptr);
    }
    *ptr = newPtr;
    return true;
  }

  template<typename R, typename T1, typename T2>
  bool AddOverflow(T1 a, T2 b, R* res) {
#ifdef __linux__
    return __builtin_add_overflow(a, b, res);
#else
    return (*res = (a + b)) < a;
#endif
  }

  void GetTime(struct fs_timespec* fts) {
#ifdef __linux__
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    fts->tv_sec = ts.tv_sec;
    fts->tv_nsec = ts.tv_nsec;
#else
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    LARGE_INTEGER li;
    li.HighPart = ft.dwHighDateTime;
    li.LowPart = ft.dwLowDateTime;
    li.QuadPart -= 116444736000000000;
    fts->tv_sec = li.QuadPart / 10000000;
    fts->tv_nsec = (li.QuadPart % 10000000) * 100;
#endif
  }

#ifdef _WIN32
  fs_ssize_t WriteToFile(HANDLE handle, void* data, fs_size_t size) {
    DWORD written = 0;
    fs_size_t actualWritten = 0;
    BOOL res = WriteFile(handle, data, size, &written, NULL);
    if (!res)
      return -1;
    actualWritten = written;
    size -= actualWritten;
    if (size != 0) {
      data = (char*)data + actualWritten;
      res = WriteFile(handle, data, size, &written, NULL);
      if (!res)
        return -1;
      actualWritten += written;
    }
    return actualWritten;
  }
  fs_ssize_t ReadFromFile(HANDLE handle, void* data, fs_size_t size) {
   DWORD read = 0;
    fs_size_t actualRead = 0;
    BOOL res = ReadFile(handle, data, size, &read, NULL);
    if (!res)
      return -1;
    actualRead = read;
    size -= actualRead;
    if (size != 0) {
      data = (char*)data + actualRead;
      res = ReadFile(handle, data, size, &read, NULL);
      if (!res)
        return -1;
      actualRead += read;
    }
    return actualRead;
  }
#endif

  class ScopedLock {
#ifdef __linux__
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
#else
   public:
    ScopedLock(HANDLE mtx) {
      mtx_ = mtx;
      WaitForSingleObject(mtx, INFINITE);
    }
    ~ScopedLock() {
      ReleaseMutex(mtx_);
    }
   private:
    HANDLE mtx_;
#endif
  };

  struct Attribute {
    const char* name;
    fs_size_t size;
    char* data;
  };

  struct Attributes {
    ~Attributes() {
      if (list) {
        for (fs_size_t i = 0; i != count; ++i) {
          struct Attribute attrib = list[i];
          Delete(attrib.name);
          if (attrib.size != 0)
            Delete(attrib.data);
        }
        Delete(list);
      }
    }
    fs_size_t count = 0;
    struct Attribute* list = NULL;
  };

  struct BaseINode {
    BaseINode() {
      GetTime(&btime);
      ctime = mtime = atime = btime;
    }
    fs_ino_t ndx;
    fs_ino_t id;
    fs_off_t size = 0;
    fs_nlink_t nlink = 0;
    fs_mode_t mode;
    struct fs_timespec btime;
    struct fs_timespec ctime;
    struct fs_timespec mtime;
    struct fs_timespec atime;
    struct Attributes attribs = {};

    bool CanUse(int perms) {
      if ((mode & perms) != perms &&
          (mode & (perms << 3)) != (perms << 3) &&
          (mode & (perms << 6)) != (perms << 6))
        return false;
      return true;
    }
    bool IsUnused() {
      if (FS_S_ISDIR(mode))
        return nlink == 1;
      return nlink == 0;
    }
    void FillStat(struct fs_stat* buf) {
      memset(buf, '\0', sizeof(struct fs_stat));
      buf->st_ino = id;
      buf->st_mode = mode;
      buf->st_nlink = nlink;
      buf->st_size = size;
      buf->st_atim = atime;
      buf->st_mtim = mtime;
      buf->st_ctim = ctime;
    }
    void FillStatx(struct fs_statx* buf, int mask) {
      memset(buf, '\0', sizeof(struct fs_statx));
      buf->stx_mask = mask;
      if (mask & FS_STATX_INO)
        buf->stx_ino = id;
      if (mask & FS_STATX_TYPE)
        buf->stx_mode |= mode & FS_S_IFMT;
      if (mask & FS_STATX_MODE)
        buf->stx_mode |= mode & ~FS_S_IFMT;
      if (mask & FS_STATX_NLINK)
        buf->stx_nlink = nlink;
      if (mask & FS_STATX_SIZE)
        buf->stx_size = size;
      if (mask & FS_STATX_ATIME) {
        buf->stx_atime.tv_sec = atime.tv_sec;
        buf->stx_atime.tv_nsec = atime.tv_nsec;
      }
      if (mask & FS_STATX_MTIME) {
        buf->stx_mtime.tv_sec = mtime.tv_sec;
        buf->stx_mtime.tv_nsec = mtime.tv_nsec;
      }
      if (mask & FS_STATX_CTIME) {
        buf->stx_ctime.tv_sec = ctime.tv_sec;
        buf->stx_ctime.tv_nsec = ctime.tv_nsec;
      }
      if (mask & FS_STATX_BTIME) {
        buf->stx_btime.tv_sec = btime.tv_sec;
        buf->stx_btime.tv_nsec = btime.tv_nsec;
      }
    }
  };

  struct RegularINode : BaseINode {
    ~RegularINode();
    struct DataRange** dataRanges = NULL;
    fs_off_t dataRangeCount = 0;

    struct DataRange* InsertRange(fs_off_t offset, fs_off_t length, fs_off_t* index);
    void RemoveRange(fs_off_t index);
    void RemoveRanges(fs_off_t index, fs_off_t count);
    struct DataRange* AllocData(fs_off_t offset, fs_off_t length);
    void TruncateData(fs_off_t length);
  };
  struct DirectoryINode : BaseINode {
    ~DirectoryINode();
    struct Dent* dents = NULL;
    fs_off_t dentCount = 0;

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
        Delete(data);
    }
    fs_off_t offset;
    fs_off_t size;
    char* data = NULL;
  };
  struct HoleRange {
    fs_off_t offset;
    fs_off_t size;
  };

  class DataIterator {
   public:
    DataIterator(struct RegularINode* inode, fs_off_t offset) {
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
      fs_off_t low = 0;
      fs_off_t high = inode->dataRangeCount - 1;
      while (low <= high) {
        fs_off_t mid = low + ((high - low) / 2);
        struct DataRange* range = inode->dataRanges[mid];
        if (offset >= range->offset) {
          fs_off_t end = range->offset + range->size;
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
    fs_off_t GetRangeIdx() {
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
    void SeekTo(fs_off_t offset) {
      do {
        fs_off_t end;
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
    fs_off_t rangeIdx_;
    bool atData_;
    bool isBeforeFirstRange_;
  };

  struct Dent {
    const char* name;
    struct BaseINode* inode;
  };

  RegularINode::~RegularINode() {
    if (dataRanges) {
      for (fs_off_t i = 0; i != dataRangeCount; ++i)
        Delete(dataRanges[i]);
      Delete(dataRanges);
    }
  }
  struct DataRange* RegularINode::InsertRange(fs_off_t offset, fs_off_t length, fs_off_t* index) {
    fs_off_t rangeIdx = dataRangeCount;
    if (dataRangeCount != 0) {
      fs_off_t low = 0;
      fs_off_t high = dataRangeCount - 1;
      while (low <= high) {
        fs_off_t mid = low + ((high - low) / 2);
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
    if (!Alloc<true>(&range))
      goto err_alloc_failed;
    if (!Realloc(&dataRanges, dataRangeCount, dataRangeCount + 1))
      goto err_after_alloc;
    if (rangeIdx != dataRangeCount)
      MemMove(
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
    Delete(range);
   err_alloc_failed:
    return NULL;
  }
  void RegularINode::RemoveRange(fs_off_t index) {
    Delete(dataRanges[index]);
    if (index != dataRangeCount - 1)
      MemMove(
        &dataRanges[index],
        &dataRanges[index + 1],
        sizeof(struct DataRange*) * (dataRangeCount - index)
      );
    Realloc(&dataRanges, dataRangeCount, --dataRangeCount);
  }
  void RegularINode::RemoveRanges(fs_off_t index, fs_off_t count) {
    fs_off_t endIdx = index + count;
    for (fs_off_t i = index; i != endIdx; ++i)
      Delete(dataRanges[i]);
    if (endIdx != dataRangeCount)
      MemMove(
        &dataRanges[index],
        &dataRanges[endIdx],
        sizeof(struct DataRange*) * (dataRangeCount - endIdx)
      );
    Realloc(dataRanges, dataRangeCount, dataRangeCount - count);
  }
  struct DataRange* RegularINode::AllocData(fs_off_t offset, fs_off_t length) {
    fs_off_t rangeIdx;
    bool createdRange = false;
    struct DataRange* range = NULL;
    fs_off_t end = offset + length;
    if (dataRangeCount != 0) {
      DataIterator it(this, offset);
      for (fs_off_t i = it.GetRangeIdx(); i != dataRangeCount; ++i) {
        struct DataRange* range2 = dataRanges[i];
        if (end == range2->offset) {
          struct DataRange* range3 = NULL;
          for (fs_off_t j = i - 1; j >= 0; --j) {
            struct DataRange* range4 = dataRanges[j];
            if (offset <= range4->offset + range4->size) {
              rangeIdx = j;
              range3 = range4;
            } else break;
          }
          if (range3) {
            fs_off_t off = Min<fs_off_t>(range3->offset, offset);
            fs_off_t newRangeLength = range2->size + (range2->offset - off);
            if (!Realloc(&range3->data, range3->size, newRangeLength))
              return NULL;
            MemMove(&range3->data[newRangeLength - range2->size], range2->data, range2->size);
            range3->size = newRangeLength;
            for (fs_off_t j = rangeIdx + 1; j != i; ++j) {
              struct DataRange* range4 = dataRanges[j];
              MemMove(&range3->data[range4->offset - off], range4->data, range4->size);
            }
            RemoveRanges(rangeIdx + 1, i - rangeIdx);
            range3->offset = off;
            return range3;
          } else {
            fs_off_t newRangeLength = range2->size + (range2->offset - offset);
            if (!Realloc(&range2->data, range2->size, newRangeLength))
              return NULL;
            MemMove(&range2->data[newRangeLength - range2->size], range2->data, range2->size);
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
    fs_off_t newRangeLength = end - range->offset;
    for (fs_off_t i = rangeIdx + 1; i != dataRangeCount; ++i) {
      struct DataRange* range2 = dataRanges[i];
      if (range2->offset < end) {
        fs_off_t newLength = (range2->offset - range->offset) + range2->size;
        if (newRangeLength < newLength) {
          newRangeLength = newLength;
          break;
        }
      } else break;
    }
    if (createdRange) {
      if (!Alloc(&range->data, newRangeLength)) {
        RemoveRange(rangeIdx);
        return NULL;
      }
    } else if (!Realloc(&range->data, range->size, newRangeLength))
      return NULL;
    range->size = newRangeLength;
    if (size < end)
      size = end;
    fs_off_t n = 0;
    for (fs_off_t i = rangeIdx + 1; i != dataRangeCount; ++i) {
      struct DataRange* range2 = dataRanges[i];
      if (range2->offset < end) {
        MemCpy(&range->data[range2->offset - range->offset], range2->data, range2->size);
        ++n;
      } else break;
    }
    if (n != 0)
      RemoveRanges(rangeIdx + 1, n);
    return range;
  }
  void RegularINode::TruncateData(fs_off_t length) {
    if (length >= size) {
      size = length;
      return;
    }
    size = length;
    if (length == 0) {
      for (fs_off_t i = 0; i != dataRangeCount; ++i)
        Delete(dataRanges[i]);
      Delete(dataRanges);
      dataRanges = NULL;
      dataRangeCount = 0;
      return;
    }
    for (fs_off_t i = dataRangeCount - 1; i >= 0; --i) {
      struct DataRange* range = dataRanges[i];
      if (length > range->offset) {
        RemoveRanges(i + 1, dataRangeCount - (i + 1));
        if (length - range->offset < range->size) {
          Realloc(&range->data, range->size, length - range->offset);
          range->size = length - range->offset;
        }
        break;
      }
    }
  }

  DirectoryINode::~DirectoryINode() {
    if (dents) {
      for (fs_off_t i = 2; i != dentCount; ++i)
        Delete(dents[i].name);
      Delete(dents);
    }
  }
  bool DirectoryINode::PushDent(const char* name, struct BaseINode* inode) {
    if (!Realloc(&dents, dentCount, dentCount + 1))
      return false;
    dents[dentCount++] = { name, inode };
    size += strlen(name);
    return true;
  }
  void DirectoryINode::RemoveDent(const char* name) {
    for (fs_off_t i = 2; i != dentCount; ++i) {
      const char* dentName = dents[i].name;
      if (strcmp(dentName, name) == 0) {
        Delete(dentName);
        if (i != dentCount - 1)
          MemMove(&dents[i], &dents[i + 1], sizeof(struct Dent) * (dentCount - i));
        Realloc(&dents, dentCount, --dentCount);
        size -= strlen(name);
        break;
      }
    }
  }
  bool DirectoryINode::IsInSelf(struct BaseINode* inode) {
    fs_off_t len = dentCount;
    for (fs_off_t i = 2; i != len; ++i) {
      struct BaseINode* dentInode = dents[i].inode;
      if (dentInode == inode ||
          (FS_S_ISDIR(dentInode->mode) && ((struct DirectoryINode*)dentInode)->IsInSelf(inode)))
        return true;
    }
    return false;
  }

  SymLinkINode::~SymLinkINode() {
    if (data)
      Delete(data);
    if (target)
      Delete(target);
  }

  void DeleteINode(struct BaseINode* inode) {
    if (FS_S_ISREG(inode->mode))
      Delete((struct RegularINode*)inode);
    else if (FS_S_ISDIR(inode->mode))
      Delete((struct DirectoryINode*)inode);
    else if (FS_S_ISLNK(inode->mode))
      Delete((struct SymLinkINode*)inode);
  }

  struct Fd {
    struct BaseINode* inode;
    int fd;
    int flags;
    fs_off_t seekOff = 0;
  };
  struct Cwd {
    ~Cwd() {
      if (path)
        Delete(path);
    }
    const char* path = NULL;
    struct DirectoryINode* inode;
    struct DirectoryINode* parent;
  };

  struct FSInternal {
    FSInternal() {
#ifdef __linux__
      pthread_mutex_init(&mtx, NULL);
#else
    mtx = CreateMutex(NULL, FALSE, NULL);
#endif
    }
    ~FSInternal() {
#ifdef __linux__
      pthread_mutex_destroy(&mtx);
#else
      CloseHandle(mtx);
#endif
      for (fs_ino_t i = 0; i != inodeCount; ++i)
        DeleteINode(inodes[i]);
      Delete(inodes);
      if (fds) {
        for (int i = 0; i != fdCount; ++i)
          Delete(fds[i]);
        Delete(fds);
      }
    }
    struct BaseINode** inodes = NULL;
    fs_ino_t inodeCount = 0;
    struct Fd** fds = NULL;
    int fdCount = 0;
    struct Cwd cwd = {};
    int umask = 0000;
#ifdef __linux__
    pthread_mutex_t mtx;
#else
    HANDLE mtx;
#endif
  };

  bool PushINode(struct FSInternal* fs, struct BaseINode* inode) {
    fs_ino_t id = fs->inodeCount;
    if (fs->inodeCount != 0) {
      fs_ino_t low = 0;
      fs_ino_t high = fs->inodeCount - 1;
      while (low <= high) {
        fs_ino_t mid = low + ((high - low) / 2);
        if (fs->inodes[mid]->id == mid)
          low = mid + 1;
        else {
          high = mid - 1;
          id = mid;
        }
      }
    }
    if (!Realloc(&fs->inodes, fs->inodeCount, fs->inodeCount + 1))
      return false;
    if (id != fs->inodeCount) {
      MemMove(
        &fs->inodes[id + 1],
        &fs->inodes[id],
        sizeof(struct BaseINode*) * (fs->inodeCount - id)
      );
      for (fs_ino_t i = id + 1; i != fs->inodeCount + 1; ++i)
        ++fs->inodes[i]->ndx;
    }
    inode->ndx = id;
    inode->id = id;
    fs->inodes[id] = inode;
    ++fs->inodeCount;
    return true;
  }
  void RemoveINode(struct FSInternal* fs, struct BaseINode* inode) {
    fs_ino_t i = inode->ndx;
    DeleteINode(inode);
    if (i != fs->inodeCount - 1) {
      MemMove(&fs->inodes[i], &fs->inodes[i + 1], sizeof(struct BaseINode*) * (fs->inodeCount - i));
      do {
        --fs->inodes[i++]->ndx;
      } while (i != fs->inodeCount - 1);
    }
    Realloc(&fs->inodes, fs->inodeCount, --fs->inodeCount);
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
    if (!Alloc<true>(&fd))
      return -FS_ENOMEM;
    if (!Realloc(&fs->fds, fs->fdCount, fs->fdCount + 1)) {
      Delete(fd);
      return -FS_ENOMEM;
    }
    if (fdNum != fs->fdCount)
      MemMove(&fs->fds[fdNum + 1], &fs->fds[fdNum], sizeof(struct Fd*) * (fs->fdCount - fdNum));
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
    Delete(fd);
    if (i != fs->fdCount - 1)
      MemMove(&fs->fds[i], &fs->fds[i + 1], sizeof(struct Fd*) * (fs->fdCount - i));
    if (fs->fdCount - 1 == 0) {
      Delete(fs->fds);
      fs->fds = NULL;
      return;
    }
    Realloc(fs->fds, fs->fdCount, --fs->fdCount);
  }
  int RemoveFd(struct FSInternal* fs, unsigned int fd) {
    if (fs->fdCount == 0)
      return -FS_EBADF;
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
    return -FS_EBADF;
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
    fs_size_t pathLen = strlen(path);
    if (pathLen == 0)
      return -FS_ENOENT;
    if (pathLen >= FS_PATH_MAX)
      return -FS_ENAMETOOLONG;
    bool isAbsolute = path[0] == '/';
    struct BaseINode* current = isAbsolute
      ? fs->inodes[0]
      : fs->cwd.inode;
    struct DirectoryINode* currParent = isAbsolute
      ? (struct DirectoryINode*)fs->inodes[0]
      : fs->cwd.parent;
    int err = 0;
    char name[FS_NAME_MAX + 1];
    fs_size_t nameLen = 0;
    for (fs_size_t i = 0; i != pathLen; ++i) {
      if (path[i] == '/') {
        if (nameLen == 0)
          continue;
        if (err)
          return err;
        currParent = (struct DirectoryINode*)current;
        if (!current->CanUse(FS_X_OK)) {
          err = -FS_EACCES;
          goto resetName;
        }
        {
          fs_off_t j = 0;
          fs_off_t dentCount = ((struct DirectoryINode*)current)->dentCount;
          for (; j != dentCount; ++j)
            if (strcmp(((struct DirectoryINode*)current)->dents[j].name, name) == 0)
              break;
          if (j == dentCount) {
            err = -FS_ENOENT;
            goto resetName;
          }
          current = ((struct DirectoryINode*)current)->dents[j].inode;
        }
        if (FS_S_ISLNK(current->mode)) {
          if (follow++ == FS_FOLLOW_MAX) {
            err = -FS_ELOOP;
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
        if (!FS_S_ISDIR(current->mode))
          err = -FS_ENOTDIR;
       resetName:
        name[0] = '\0';
        nameLen = 0;
      } else {
        if (nameLen == FS_NAME_MAX)
          return -FS_ENAMETOOLONG;
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
      if (!current->CanUse(FS_X_OK))
        return -FS_EACCES;
      fs_off_t dentCount = ((struct DirectoryINode*)current)->dentCount;
      for (fs_off_t i = 0; i != dentCount; ++i)
        if (strcmp(((struct DirectoryINode*)current)->dents[i].name, name) == 0) {
          current = ((struct DirectoryINode*)current)->dents[i].inode;
          goto out;
        }
      return -FS_ENOENT;
    }
   out:
    if (followResolved) {
      if (FS_S_ISLNK(current->mode)) {
        if (follow++ == FS_FOLLOW_MAX)
          return -FS_ELOOP;
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
    char name[FS_NAME_MAX + 1];
    int nameNdx = FS_NAME_MAX;
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
    char absPath[FS_PATH_MAX];
    int absPathLen = 0;
    if (path[0] != '/') {
      int cwdPathLen = strlen(fs->cwd.path);
      if (cwdPathLen != 1) {
        MemCpy(absPath, fs->cwd.path, cwdPathLen);
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
    Delete(absPath);
    return last;
  }
  int FlagsToPerms(int flags) {
    int perms = FS_F_OK;
    if ((flags & FS_O_ACCMODE) == FS_O_RDONLY)
      perms |= FS_R_OK;
    else if ((flags & FS_O_ACCMODE) == FS_O_WRONLY)
      perms |= FS_W_OK;
    else if ((flags & FS_O_ACCMODE) == FS_O_RDWR)
      perms |= (FS_R_OK | FS_W_OK);
    if (flags & FS_O_TRUNC)
      perms |= FS_W_OK;
    return perms;
  }

  struct DumpedINode {
    fs_ino_t id;
    fs_off_t size;
    fs_nlink_t nlink;
    fs_mode_t mode;
    struct fs_timespec btime;
    struct fs_timespec ctime;
    struct fs_timespec mtime;
    struct fs_timespec atime;
  };

  bool TryAllocFromMode(struct BaseINode** inode, fs_mode_t mode) {
    if (FS_S_ISREG(mode))
      return Alloc((struct RegularINode**)inode);
    if (FS_S_ISDIR(mode))
      return Alloc((struct DirectoryINode**)inode);
    if (FS_S_ISLNK(mode))
      return Alloc((struct SymLinkINode**)inode);
#ifdef __linux__
    __builtin_unreachable();
#else
    __assume(0);
#endif
  }
}

FileSystem* FileSystem::New() {
  struct FSInternal* data;
  if (!Alloc<true>(&data))
    goto err1;
  struct DirectoryINode* root;
  if (!Alloc(&data->inodes) ||
      !Alloc<true>(&root))
    goto err2;
  if (!Alloc(&root->dents, 2))
    goto err3;
  root->mode = 0755 | FS_S_IFDIR;
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
  if (!Alloc(&fs))
    goto err2;
  fs->data = data;
  return fs;

 err3:
  Delete(root);
 err2:
  Delete(data);
 err1:
  return NULL;
}
FileSystem::~FileSystem() {
  Delete((struct FSInternal*)data);
}
int FileSystem::FAccessAt2(int dirFd, const char* path, int mode, int flags) {
  if (mode   & ~(FS_F_OK | FS_R_OK | FS_W_OK | FS_X_OK) ||
      flags  & ~(FS_AT_SYMLINK_NOFOLLOW | FS_AT_EMPTY_PATH) ||
      (flags & FS_AT_EMPTY_PATH && path[0] != '\0'))
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode) && !(flags & FS_AT_EMPTY_PATH))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = 0;
  if (flags & FS_AT_EMPTY_PATH)
    inode = fs->cwd.inode;
  else res = GetINode(fs, path, &inode, NULL, !(flags & FS_AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (mode != FS_F_OK && !inode->CanUse(mode))
    return -FS_EACCES;
  return 0;
}
int FileSystem::FAccessAt(int dirFd, const char* path, int mode) {
  return FAccessAt2(dirFd, path, mode, FS_F_OK);
}
int FileSystem::Access(const char* path, int mode) {
  return FAccessAt2(FS_AT_FDCWD, path, mode, FS_F_OK);
}
int FileSystem::OpenAt(int dirFd, const char* path, int flags, fs_mode_t mode) {
  if (flags & ~(
        FS_O_RDONLY    | FS_O_WRONLY   | FS_O_RDWR    | FS_O_CREAT   |
        FS_O_EXCL      | FS_O_APPEND   | FS_O_TRUNC   | FS_O_TMPFILE |
        FS_O_DIRECTORY | FS_O_NOFOLLOW | FS_O_NOATIME
      ))
    return -FS_EINVAL;
  if (flags & FS_O_TMPFILE) {
    if (!(flags & FS_O_DIRECTORY) ||
        flags & FS_O_CREAT ||
        !(flags & (FS_O_WRONLY | FS_O_RDWR)) ||
        mode & ~0777 || mode == 0)
      return -FS_EINVAL;
    mode |= FS_S_IFREG;
  } else if (flags & FS_O_CREAT) {
    if (flags & FS_O_DIRECTORY ||
        mode & ~0777)
      return -FS_EINVAL;
    mode |= FS_S_IFREG;
  } else if (mode != 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  if (flags & FS_O_CREAT && flags & FS_O_EXCL)
    flags |= FS_O_NOFOLLOW;
  if (flags & FS_O_WRONLY && flags & FS_O_RDWR)
    flags &= ~FS_O_RDWR;
  struct BaseINode* inode;
  struct DirectoryINode* parent = NULL;
  int res = GetINode(fs, path, &inode, &parent, !(flags & FS_O_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (!parent)
    return res;
  if (res == 0) {
    if (flags & FS_O_CREAT) {
      if (flags & FS_O_EXCL)
        return -FS_EEXIST;
      if (FS_S_ISDIR(inode->mode))
        return -FS_EISDIR;
    }
    if (flags & FS_O_NOFOLLOW && FS_S_ISLNK(inode->mode))
      return -FS_ELOOP;
    if (!inode->CanUse(FlagsToPerms(flags)))
      return -FS_EACCES;
  } else {
    if (flags & FS_O_CREAT && res == -FS_ENOENT) {
      flags &= ~FS_O_TRUNC;
      const char* name = GetAbsoluteLast(fs, path);
      if (!name)
        return -FS_ENOMEM;
      struct RegularINode* x;
      if (!Alloc<true>(&x)) {
        Delete(name);
        return -FS_ENOMEM;
      }
      if (!PushINode(fs, x)) {
        Delete(x);
        Delete(name);
        return -FS_ENOMEM;
      }
      if (!parent->PushDent(name, x)) {
        RemoveINode(fs, x);
        Delete(name);
        return -FS_ENOMEM;
      }
      x->mode = mode & ~fs->umask;
      x->nlink = 1;
      parent->ctime = parent->mtime = x->btime;
      res = PushFd(fs, x, flags);
      if (res < 0) {
        parent->RemoveDent(name);
        RemoveINode(fs, x);
        Delete(name);
      }
    }
    return res;
  }
  if (FS_S_ISDIR(inode->mode)) {
    if (flags & FS_O_TMPFILE) {
      struct RegularINode* x;
      if (!Alloc<true>(&x))
        return -FS_ENOMEM;
      if (!PushINode(fs, x)) {
        Delete(x);
        return -FS_ENOMEM;
      }
      x->mode = (mode & ~fs->umask) | FS_S_IFREG;
      res = PushFd(fs, x, flags);
      if (res < 0)
        RemoveINode(fs, x);
      return res;
    }
    if (flags & (FS_O_WRONLY | FS_O_RDWR))
      return -FS_EISDIR;
  } else {
    if (flags & FS_O_DIRECTORY)
      return -FS_ENOTDIR;
    if (flags & FS_O_TRUNC && inode->size != 0)
      ((struct RegularINode*)inode)->TruncateData(0);
  }
  return PushFd(fs, inode, flags);
}
int FileSystem::Open(const char* path, int flags, fs_mode_t mode) {
  return OpenAt(FS_AT_FDCWD, path, flags, mode);
}
int FileSystem::Creat(const char* path, fs_mode_t mode) {
  return OpenAt(FS_AT_FDCWD, path, FS_O_CREAT | FS_O_WRONLY | FS_O_TRUNC, mode);
}
int FileSystem::Close(unsigned int fd) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  return RemoveFd(fs, fd);
}
int FileSystem::CloseRange(unsigned int fd, unsigned int maxFd, unsigned int flags) {
  if (flags != 0 || fd > maxFd)
    return -FS_EINVAL;
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
int FileSystem::MkNodAt(int dirFd, const char* path, fs_mode_t mode, fs_dev_t dev) {
  if (mode & FS_S_IFMT) {
    if (FS_S_ISDIR(mode))
      return -FS_EPERM;
    if (!FS_S_ISREG(mode))
      return -FS_EINVAL;
  }
  if (dev != 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  struct DirectoryINode* parent = NULL;
  int res = GetINode(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (!parent)
    return res;
  if (res == 0)
    return -FS_EEXIST;
  if (res != -FS_ENOENT)
    return res;
  const char* name = GetAbsoluteLast(fs, path);
  if (!name)
    return -FS_ENOMEM;
  struct RegularINode* x;
  if (!Alloc<true>(&x)) {
    Delete(name);
    return -FS_ENOMEM;
  }
  if (!PushINode(fs, x)) {
    Delete(x);
    Delete(name);
    return -FS_ENOMEM;
  }
  if (!parent->PushDent(name, x)) {
    RemoveINode(fs, x);
    Delete(name);
    return -FS_ENOMEM;
  }
  x->mode = ((mode & 0777) & ~fs->umask) | FS_S_IFREG;
  x->nlink = 1;
  parent->ctime = parent->mtime = x->btime;
  return 0;
}
int FileSystem::MkNod(const char* path, fs_mode_t mode, fs_dev_t dev) {
  return MkNodAt(FS_AT_FDCWD, path, mode, dev);
}
int FileSystem::MkDirAt(int dirFd, const char* path, fs_mode_t mode) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  struct DirectoryINode* parent = NULL;
  int res = GetINode(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (!parent)
    return res;
  if (res == 0)
    return -FS_EEXIST;
  if (res != -FS_ENOENT)
    return res;
  const char* name = GetAbsoluteLast(fs, path);
  if (!name)
    return -FS_ENOMEM;
  struct DirectoryINode* x;
  if (!Alloc<true>(&x)) {
    Delete(name);
    return -FS_ENOMEM;
  }
  if (!Alloc(&x->dents, 2)) {
    Delete(x);
    Delete(name);
    return -FS_ENOMEM;
  }
  x->dentCount = 2;
  if (!PushINode(fs, x)) {
    Delete(x);
    Delete(name);
    return -FS_ENOMEM;
  }
  if (!parent->PushDent(name, x)) {
    RemoveINode(fs, x);
    Delete(name);
    return -FS_ENOMEM;
  }
  x->mode = ((mode & 0777) & ~fs->umask) | FS_S_IFDIR;
  x->nlink = 2;
  x->dents[0] = { ".", x };
  x->dents[1] = { "..", parent };
  ++parent->nlink;
  parent->ctime = parent->mtime = x->btime;
  return 0;
}
int FileSystem::MkDir(const char* path, fs_mode_t mode) {
  return MkDirAt(FS_AT_FDCWD, path, mode);
}
int FileSystem::SymLinkAt(const char* oldPath, int newDirFd, const char* newPath) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (newDirFd != FS_AT_FDCWD) {
    if (!(fd = GetFd(fs, newDirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
  }
  struct BaseINode* oldInode;
  int res = GetINode(fs, oldPath, &oldInode);
  if (res != 0)
    return res;
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (newDirFd != FS_AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  struct BaseINode* newInode;
  struct DirectoryINode* newParent = NULL;
  res = GetINode(fs, newPath, &newInode, &newParent);
  fs->cwd.inode = origCwd;
  if (!newParent)
    return res;
  if (res == 0)
    return -FS_EEXIST;
  if (res != -FS_ENOENT)
    return res;
  const char* name = GetAbsoluteLast(fs, newPath);
  if (!name)
    return -FS_ENOMEM;
  struct SymLinkINode* x;
  if (!Alloc<true>(&x)) {
    Delete(name);
    return -FS_ENOMEM;
  }
  fs_size_t oldPathLen = strlen(oldPath);
  if (!Alloc(&x->data, oldPathLen) || !PushINode(fs, x)) {
    Delete(x);
    Delete(name);
    return -FS_ENOMEM;
  }
  MemCpy(x->data, oldPath, oldPathLen);
  if (!(x->target = AbsolutePath(fs, oldPath)) || !newParent->PushDent(name, x)) {
    RemoveINode(fs, x);
    Delete(name);
    return -FS_ENOMEM;
  }
  x->mode = 0777 | FS_S_IFLNK;
  x->nlink = 1;
  x->size = oldPathLen;
  newParent->ctime = newParent->mtime = x->btime;
  return 0;
}
int FileSystem::SymLink(const char* oldPath, const char* newPath) {
  return SymLinkAt(oldPath, FS_AT_FDCWD, newPath);
}
int FileSystem::ReadLinkAt(int dirFd, const char* path, char* buf, int bufLen) {
  if (bufLen <= 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (!FS_S_ISLNK(inode->mode))
    return -FS_EINVAL;
  if (inode->size < bufLen)
    bufLen = inode->size;
  MemCpy(buf, ((struct SymLinkINode*)inode)->data, bufLen);
  GetTime(&inode->atime);
  return bufLen;
}
int FileSystem::ReadLink(const char* path, char* buf, int bufLen) {
  return ReadLinkAt(FS_AT_FDCWD, path, buf, bufLen);
}
int FileSystem::GetDents(unsigned int fdNum, struct fs_dirent* dirp, unsigned int count) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  if (!FS_S_ISDIR(inode->mode))
    return -FS_ENOTDIR;
  if (fd->seekOff >= ((struct DirectoryINode*)inode)->dentCount)
    return 0;
  unsigned int nread = 0;
  char* dirpData = (char*)dirp;
  do {
    struct Dent d = ((struct DirectoryINode*)inode)->dents[fd->seekOff];
    fs_size_t nameLen = strlen(d.name);
    unsigned short reclen = FS_ALIGN(
      __builtin_offsetof(struct fs_dirent, d_name) + nameLen + 2,
      sizeof(long)
    );
    if (nread + reclen > count)
      break;
    struct fs_dirent* dent = (struct fs_dirent*)dirpData;
    dent->d_ino = d.inode->id;
    dent->d_off = fd->seekOff + 1;
    dent->d_reclen = reclen;
    MemCpy(dent->d_name, d.name, nameLen);
    dent->d_name[nameLen] = '\0';
    dirpData[reclen - 1] = FS_IFTODT(d.inode->mode);
    dirpData += reclen;
    nread += reclen;
  } while (++fd->seekOff != ((struct DirectoryINode*)inode)->dentCount);
  if (nread == 0)
    return -FS_EINVAL;
  if (!(fd->flags & FS_O_NOATIME))
    GetTime(&inode->atime);
  return nread;
}
int FileSystem::LinkAt(
  int oldDirFd,
  const char* oldPath,
  int newDirFd,
  const char* newPath,
  int flags
) {
  if (flags & ~(FS_AT_SYMLINK_FOLLOW | FS_AT_EMPTY_PATH) ||
      (flags & FS_AT_EMPTY_PATH && oldPath[0] != '\0'))
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* oldFd;
  struct Fd* newFd;
  if (oldDirFd != FS_AT_FDCWD || flags & FS_AT_EMPTY_PATH) {
    if (!(oldFd = GetFd(fs, oldDirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(oldFd->inode->mode)) {
      if (!(flags & FS_AT_EMPTY_PATH))
        return -FS_ENOTDIR;
    } else if (flags & FS_AT_EMPTY_PATH)
      return -FS_EPERM;
  }
  if (newDirFd != FS_AT_FDCWD) {
    if (!(newFd = GetFd(fs, newDirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(newFd->inode->mode))
      return -FS_ENOTDIR;
  }
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (oldDirFd != FS_AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)oldFd->inode;
  struct BaseINode* oldInode;
  int res = 0;
  if (flags & FS_AT_EMPTY_PATH)
    oldInode = fs->cwd.inode;
  else res = GetINode(fs, oldPath, &oldInode, NULL, flags & FS_AT_SYMLINK_FOLLOW);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (newDirFd != FS_AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)newFd->inode;
  struct BaseINode* newInode;
  struct DirectoryINode* newParent = NULL;
  res = GetINode(fs, newPath, &newInode, &newParent);
  fs->cwd.inode = origCwd;
  if (!newParent)
    return res;
  if (res == 0)
    return -FS_EEXIST;
  if (res != -FS_ENOENT)
    return res;
  if (FS_S_ISDIR(oldInode->mode))
    return -FS_EPERM;
  const char* name = GetAbsoluteLast(fs, newPath);
  if (!name)
    return -FS_ENOMEM;
  if (!newParent->PushDent(name, oldInode)) {
    Delete(name);
    return -FS_ENOMEM;
  }
  ++oldInode->nlink;
  struct fs_timespec ts;
  GetTime(&ts);
  oldInode->ctime = newParent->ctime = newParent->mtime = ts;
  return 0;
}
int FileSystem::Link(const char* oldPath, const char* newPath) {
  return LinkAt(FS_AT_FDCWD, oldPath, FS_AT_FDCWD, newPath, 0);
}
int FileSystem::UnlinkAt(int dirFd, const char* path, int flags) {
  if (flags & ~FS_AT_REMOVEDIR)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  struct DirectoryINode* parent;
  int res = GetINode(fs, path, &inode, &parent);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (flags & FS_AT_REMOVEDIR) {
    if (!FS_S_ISDIR(inode->mode))
      return -FS_ENOTDIR;
    if (inode == fs->inodes[0])
      return -FS_EBUSY;
  } else if (FS_S_ISDIR(inode->mode))
    return -FS_EISDIR;
  for (int i = 0; i != fs->fdCount; ++i)
    if (fs->fds[i]->inode == inode)
      return -FS_EBUSY;
  if (flags & FS_AT_REMOVEDIR) {
    const char* last = GetLast(path);
    if (!last)
      return -FS_ENOMEM;
    bool isDot = strcmp(last, ".") == 0;
    Delete(last);
    if (isDot)
      return -FS_EINVAL;
    if (((struct DirectoryINode*)inode)->dentCount != 2)
      return -FS_ENOTEMPTY;
  }
  const char* name = GetAbsoluteLast(fs, path);
  if (!name)
    return -FS_ENOMEM;
  parent->RemoveDent(name);
  Delete(name);
  if (flags & FS_AT_REMOVEDIR)
    --parent->nlink;
  struct fs_timespec ts;
  GetTime(&ts);
  --inode->nlink;
  if (inode->IsUnused())
    RemoveINode(fs, inode);
  else inode->ctime = ts;
  parent->ctime = parent->mtime = ts;
  return 0;
}
int FileSystem::Unlink(const char* path) {
  return UnlinkAt(FS_AT_FDCWD, path, 0);
}
int FileSystem::RmDir(const char* path) {
  return UnlinkAt(FS_AT_FDCWD, path, FS_AT_REMOVEDIR);
}
int FileSystem::RenameAt2(
  int oldDirFd,
  const char* oldPath,
  int newDirFd,
  const char* newPath,
  unsigned int flags
) {
  if (flags & ~(FS_RENAME_NOREPLACE | FS_RENAME_EXCHANGE) ||
      (flags & FS_RENAME_NOREPLACE && flags & FS_RENAME_EXCHANGE))
    return -FS_EINVAL;
  const char* last = GetLast(oldPath);
  if (!last)
    return -FS_ENOMEM;
  {
    bool isDot = strcmp(last, ".") == 0 || strcmp(last, "..") == 0;
    Delete(last);
    if (isDot)
      return -FS_EBUSY;
  }
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* oldFd;
  struct Fd* newFd;
  if (oldDirFd != FS_AT_FDCWD) {
    if (!(oldFd = GetFd(fs, oldDirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(oldFd->inode->mode))
      return -FS_ENOTDIR;
  }
  if (newDirFd != FS_AT_FDCWD) {
    if (!(newFd = GetFd(fs, newDirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(newFd->inode->mode))
      return -FS_ENOTDIR;
  }
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (oldDirFd != FS_AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)oldFd->inode;
  struct BaseINode* oldInode;
  struct DirectoryINode* oldParent;
  int res = GetINode(fs, oldPath, &oldInode, &oldParent);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  if (newDirFd != FS_AT_FDCWD)
    fs->cwd.inode = (struct DirectoryINode*)newFd->inode;
  struct BaseINode* newInode = NULL;
  struct DirectoryINode* newParent = NULL;
  res = GetINode(fs, newPath, &newInode, &newParent);
  fs->cwd.inode = origCwd;
  if (!newParent || (!newInode && res != -FS_ENOENT))
    return res;
  if (oldInode == newInode)
    return 0;
  if (flags & FS_RENAME_NOREPLACE && newInode)
    return -FS_EEXIST;
  if (flags & FS_RENAME_EXCHANGE && !newInode)
    return -FS_ENOENT;
  if (FS_S_ISDIR(oldInode->mode)) {
    if (newInode) {
      if (!FS_S_ISDIR(newInode->mode))
        return -FS_ENOTDIR;
      if (((struct DirectoryINode*)newInode)->dentCount > 2)
        return -FS_ENOTEMPTY;
    }
    if (oldInode == fs->inodes[0] || oldInode == fs->cwd.inode)
      return -FS_EBUSY;
  } else if (newInode && FS_S_ISDIR(newInode->mode))
    return -FS_EISDIR;
  if (oldParent->IsInSelf(newParent))
    return -FS_EINVAL;
  const char* oldName = GetAbsoluteLast(fs, oldPath);
  if (!oldName)
    return -FS_ENOMEM;
  const char* newName = GetAbsoluteLast(fs, newPath);
  if (!newName) {
    Delete(oldName);
    return -FS_ENOMEM;
  }
  if (flags & FS_RENAME_EXCHANGE) {
    for (fs_off_t i = 0; i != oldParent->dentCount; ++i)
      if (strcmp(oldParent->dents[i].name, oldName) == 0) {
        for (fs_off_t j = 0; j != newParent->dentCount; ++j)
          if (strcmp(newParent->dents[j].name, newName) == 0) {
            oldParent->dents[i].inode = newInode;
            newParent->dents[j].inode = oldInode;
            break;
          }
        break;
      }
    Delete(oldName);
    Delete(newName);
  } else {
    if (!newParent->PushDent(newName, oldInode)) {
      Delete(oldName);
      Delete(newName);
      return -FS_ENOMEM;
    }
    oldParent->RemoveDent(oldName);
    Delete(oldName);
    if (newInode)
      newParent->RemoveDent(newName);
    if (FS_S_ISDIR(oldInode->mode)) {
      --oldParent->nlink;
      ++newParent->nlink;
    }
  }
  struct fs_timespec ts;
  GetTime(&ts);
  if (!(flags & FS_RENAME_EXCHANGE)) {
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
int FileSystem::RenameAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath) {
  return RenameAt2(oldDirFd, oldPath, newDirFd, newPath, 0);
}
int FileSystem::Rename(const char* oldPath, const char* newPath) {
  return RenameAt2(FS_AT_FDCWD, oldPath, FS_AT_FDCWD, newPath, 0);
}
int FileSystem::FAllocate(int fdNum, int mode, fs_off_t offset, fs_off_t len) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  if (offset < 0 || len < 0)
    return -FS_EINVAL;
  if (mode & ~(FS_FALLOC_FL_MODE_MASK | FS_FALLOC_FL_KEEP_SIZE))
    return -FS_EOPNOTSUPP;
  switch (mode & FS_FALLOC_FL_MODE_MASK) {
    case FS_FALLOC_FL_ALLOCATE_RANGE:
    case FS_FALLOC_FL_ZERO_RANGE:
      break;
    case FS_FALLOC_FL_PUNCH_HOLE:
      if (!(mode & FS_FALLOC_FL_KEEP_SIZE))
        return -FS_EOPNOTSUPP;
      break;
    case FS_FALLOC_FL_COLLAPSE_RANGE:
    case FS_FALLOC_FL_INSERT_RANGE:
      if (mode & FS_FALLOC_FL_KEEP_SIZE)
        return -FS_EOPNOTSUPP;
      break;
    default:
      return -FS_EOPNOTSUPP;
  }
  if (!(fd->flags & (FS_O_WRONLY | FS_O_RDWR)))
    return -FS_EBADF;
  if ((mode & ~FS_FALLOC_FL_KEEP_SIZE) && fd->flags & FS_O_APPEND)
		return -FS_EPERM;
  struct BaseINode* baseInode = fd->inode;
  if (FS_S_ISDIR(baseInode->mode))
    return -FS_EISDIR;
  if (!FS_S_ISREG(baseInode->mode))
    return -FS_ENODEV;
  struct RegularINode* inode = (struct RegularINode*)baseInode;
  fs_off_t end;
  if (AddOverflow(offset, len, &end))
    return -FS_EFBIG;
  switch (mode & FS_FALLOC_FL_MODE_MASK) {
    case FS_FALLOC_FL_ALLOCATE_RANGE: {
      if (mode & FS_FALLOC_FL_KEEP_SIZE) {
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
        return -FS_ENOMEM;
      break;
    }
    case FS_FALLOC_FL_ZERO_RANGE: {
      if (mode & FS_FALLOC_FL_KEEP_SIZE) {
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
        return -FS_ENOMEM;
      memset(range->data, '\0', len);
      break;
    }
    case FS_FALLOC_FL_PUNCH_HOLE: {
      for (fs_off_t i = 0; i != inode->dataRangeCount;) {
        struct DataRange* range = inode->dataRanges[i];
        if (offset <= range->offset) {
          if (end <= range->offset)
            break;
          if (end < range->offset + range->size) {
            fs_off_t amountToRemove = len - (range->offset - offset);
            fs_off_t newSize = range->size - amountToRemove;
            MemMove(range->data, range->data + amountToRemove, newSize);
            Realloc(&range->data, range->size, newSize);
            range->offset += amountToRemove;
            range->size = newSize;
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
            fs_off_t rangeSize = range->size;
            range->size = offset - range->offset;
            fs_off_t offsetAfterHole = (offset - range->offset) + len;
            fs_off_t newRangeLength = rangeSize - offsetAfterHole;
            struct DataRange* newRange = inode->AllocData(end, newRangeLength);
            if (!newRange)
              return -FS_ENOMEM;
            MemCpy(newRange->data, range->data + offsetAfterHole, newRangeLength);
            Realloc(&range->data, rangeSize, range->size);
            break;
          } else {
            fs_off_t newSize = (range->offset + range->size) - offset;
            Realloc(&range->data, range->size, newSize);
            range->size = newSize;
          }
        }
        ++i;
      }
      break;
    }
    case FS_FALLOC_FL_COLLAPSE_RANGE: {
      for (fs_off_t i = 0; i != inode->dataRangeCount;) {
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
              if (!Realloc(&prevRange->data, prevRange->size, prevRange->size + range->size))
                return -FS_ENOMEM;
              MemCpy(prevRange->data + prevRange->size, range->data, range->size);
              prevRange->size += range->size;
              inode->RemoveRange(i);
            } else ++i;
            continue;
          }
          if (end < range->offset + range->size) {
            fs_off_t amountToRemove = len - (range->offset - offset);
            fs_off_t newSize = range->size - amountToRemove;
            MemMove(range->data, range->data + amountToRemove, newSize);
            Realloc(&range->data, range->size, newSize);
            range->size = newSize;
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
            fs_off_t rangeSize = range->size;
            range->size -= len;
            fs_off_t offsetAfterHole = (offset - range->offset) + len;
            MemCpy(
              range->data + (offset - range->offset),
              range->data + offsetAfterHole,
              rangeSize - offsetAfterHole
            );
            Realloc(&range->data, rangeSize, range->size);
          } else {
            fs_off_t newSize = (range->offset + range->size) - offset;
            Realloc(&range->data, range->size, newSize);
            range->size = newSize;
          }
        }
        ++i;
      }
      break;
    }
    case FS_FALLOC_FL_INSERT_RANGE: {
      for (fs_off_t i = 0; i != inode->dataRangeCount;) {
        struct DataRange* range = inode->dataRanges[i];
        if (offset <= range->offset)
          range->offset += len;
        else {
          if (offset >= range->offset + range->size) {
            ++i;
            continue;
          }
          fs_off_t rangeSize = range->size;
          fs_off_t offsetAfterHole = offset - range->offset;
          range->size = offsetAfterHole;
          fs_off_t newRangeLength = rangeSize - offsetAfterHole;
          struct DataRange* newRange = inode->AllocData(end, newRangeLength);
          if (!newRange)
            return -FS_ENOMEM;
          MemCpy(newRange->data, range->data + offsetAfterHole, newRangeLength);
          Realloc(&range->data, range->size, rangeSize);
          ++i;
        }
        ++i;
      }
      break;
    }
  }
  return 0;
}
fs_off_t FileSystem::LSeek(unsigned int fdNum, fs_off_t offset, unsigned int whence) {
  if (offset < 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  switch (whence) {
    case FS_SEEK_SET:
      return fd->seekOff = offset;
    case FS_SEEK_CUR: {
      fs_off_t res;
      if (AddOverflow(fd->seekOff, offset, &res))
        return -FS_EOVERFLOW;
      return fd->seekOff = res;
    }
    case FS_SEEK_END: {
      if (FS_S_ISDIR(inode->mode))
        return -FS_EINVAL;
      fs_off_t res;
      if (AddOverflow(inode->size, offset, &res))
        return -FS_EOVERFLOW;
      return fd->seekOff = res;
    }
    case FS_SEEK_DATA: {
      if (!FS_S_ISREG(inode->mode))
        return -FS_EINVAL;
      DataIterator it((struct RegularINode*)inode, fd->seekOff);
      fs_off_t res;
      if (!it.IsInData()) {
        if (!it.Next()) {
          if (AddOverflow(inode->size, offset, &res))
            return -FS_EOVERFLOW;
          return fd->seekOff = res;
        }
        struct DataRange* range = it.GetRange();
        if (AddOverflow(range->offset, offset, &res))
          return -FS_EOVERFLOW;
        return fd->seekOff = res;
      }
      it.Next();
      if (it.Next()) {
        struct DataRange* range = it.GetRange();
        if (AddOverflow(range->offset, offset, &res))
          return -FS_EOVERFLOW;
        return fd->seekOff = res;
      }
      if (AddOverflow(inode->size, offset, &res))
        return -FS_EOVERFLOW;
      return fd->seekOff = res;
    }
    case FS_SEEK_HOLE: {
      if (!FS_S_ISREG(inode->mode))
        return -FS_EINVAL;
      DataIterator it((struct RegularINode*)inode, fd->seekOff);
      fs_off_t res;
      if (it.IsInData()) {
        it.Next();
        struct HoleRange hole = it.GetHole();
        if (AddOverflow(hole.offset, offset, &res))
          return -FS_EOVERFLOW;
        return fd->seekOff = res;
      }
      if (it.Next()) {
        it.Next();
        struct HoleRange hole = it.GetHole();
        if (AddOverflow(hole.offset, offset, &res))
          return -FS_EOVERFLOW;
        return fd->seekOff = res;
      }
      if (AddOverflow(inode->size, offset, &res))
        return -FS_EOVERFLOW;
      return fd->seekOff = res;
    }
  }
  return -FS_EINVAL;
}
fs_ssize_t FileSystem::Read(unsigned int fdNum, char* buf, fs_size_t count) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || fd->flags & FS_O_WRONLY)
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  if (FS_S_ISDIR(inode->mode))
    return -FS_EISDIR;
  if (count == 0)
    return 0;
  if (count > FS_RW_MAX)
    count = FS_RW_MAX;
  if (fd->seekOff >= inode->size)
    return 0;
  fs_off_t end = inode->size - fd->seekOff;
  if (end < count)
    count = end;
  DataIterator it((struct RegularINode*)inode, fd->seekOff);
  for (fs_size_t amountRead = 0; amountRead != count; it.Next()) {
    fs_size_t amount;
    fs_size_t currEnd = fd->seekOff + amountRead;
    if (it.IsInData()) {
      struct DataRange* range = it.GetRange();
      amount = Min<fs_size_t>((range->offset + range->size) - currEnd, count - amountRead);
      MemCpy(buf + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = it.GetHole();
      amount = Min<fs_size_t>((hole.offset + hole.size) - currEnd, count - amountRead);
      memset(buf + amountRead, '\0', amount);
    }
    amountRead += amount;
  }
  buf[count] = '\0';
  fd->seekOff += count;
  if (!(fd->flags & FS_O_NOATIME))
    GetTime(&inode->atime);
  return count;
}
fs_ssize_t FileSystem::Readv(unsigned int fdNum, struct fs_iovec* iov, int iovcnt) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || fd->flags & FS_O_WRONLY)
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  if (FS_S_ISDIR(inode->mode))
    return -FS_EISDIR;
  if (iovcnt == 0)
    return 0;
  if (iovcnt < 0 || iovcnt > FS_IOV_MAX)
    return -FS_EINVAL;
  fs_size_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    fs_size_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (len < 0)
      return -FS_EINVAL;
    fs_size_t limit = FS_RW_MAX - totalLen;
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
  fs_off_t end = inode->size - fd->seekOff;
  if (end < totalLen)
    totalLen = end;
  DataIterator it((struct RegularINode*)inode, fd->seekOff);
  for (fs_size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; it.Next()) {
    struct fs_iovec curr = iov[iovIdx];
    fs_size_t amount;
    fs_size_t end = totalLen - count;
    fs_size_t iovEnd = curr.iov_len - amountRead;
    fs_size_t currEnd = fd->seekOff + count;
    if (it.IsInData()) {
      struct DataRange* range = it.GetRange();
      amount = Min<fs_size_t>(
        Min<fs_size_t>(
          (range->offset + range->size) - currEnd,
          iovEnd),
        end
      );
      MemCpy((char*)curr.iov_base + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = it.GetHole();
      amount = Min<fs_size_t>(
        Min<fs_size_t>(
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
  if (!(fd->flags & FS_O_NOATIME))
    GetTime(&inode->atime);
  return totalLen;
}
fs_ssize_t FileSystem::PRead(unsigned int fdNum, char* buf, fs_size_t count, fs_off_t offset) {
  if (offset < 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || fd->flags & FS_O_WRONLY)
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  if (FS_S_ISDIR(inode->mode))
    return -FS_EISDIR;
  if (count == 0)
    return 0;
  if (count > FS_RW_MAX)
    count = FS_RW_MAX;
  if (offset >= inode->size)
    return 0;
  fs_off_t end = inode->size - offset;
  if (end < count)
    count = end;
  DataIterator it((struct RegularINode*)inode, offset);
  for (fs_size_t amountRead = 0; amountRead != count; it.Next()) {
    fs_size_t amount;
    fs_size_t currEnd = offset + amountRead;
    if (it.IsInData()) {
      struct DataRange* range = it.GetRange();
      amount = Min<fs_size_t>((range->offset + range->size) - currEnd, count - amountRead);
      MemCpy(buf + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = it.GetHole();
      amount = Min<fs_size_t>((hole.offset + hole.size) - currEnd, count - amountRead);
      memset(buf + amountRead, '\0', amount);
    }
    amountRead += amount;
  }
  if (!(fd->flags & FS_O_NOATIME))
    GetTime(&inode->atime);
  return count;
}
fs_ssize_t FileSystem::PReadv(
  unsigned int fdNum,
  struct fs_iovec* iov,
  int iovcnt,
  fs_off_t offset
) {
  if (offset < 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || fd->flags & FS_O_WRONLY)
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  if (FS_S_ISDIR(inode->mode))
    return -FS_EISDIR;
  if (iovcnt == 0)
    return 0;
  if (iovcnt < 0 || iovcnt > FS_IOV_MAX)
    return -FS_EINVAL;
  fs_size_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    fs_size_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (len < 0)
      return -FS_EINVAL;
    fs_size_t limit = FS_RW_MAX - totalLen;
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
  fs_off_t end = inode->size - offset;
  if (end < totalLen)
    totalLen = end;
  DataIterator it((struct RegularINode*)inode, offset);
  for (fs_size_t iovIdx = 0, amountRead = 0, count = 0; count != totalLen; it.Next()) {
    struct fs_iovec curr = iov[iovIdx];
    fs_size_t amount;
    fs_size_t end = totalLen - count;
    fs_size_t iovEnd = curr.iov_len - amountRead;
    fs_size_t currEnd = offset + count;
    if (it.IsInData()) {
      struct DataRange* range = it.GetRange();
      amount = Min<fs_size_t>(
        Min<fs_size_t>(
          (range->offset + range->size) - currEnd,
          iovEnd),
        end
      );
      MemCpy((char*)curr.iov_base + amountRead, range->data + (currEnd - range->offset), amount);
    } else {
      struct HoleRange hole = it.GetHole();
      amount = Min<fs_size_t>(
        Min<fs_size_t>(
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
  if (!(fd->flags & FS_O_NOATIME))
    GetTime(&inode->atime);
  return totalLen;
}
fs_ssize_t FileSystem::Write(unsigned int fdNum, const char* buf, fs_size_t count) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || !(fd->flags & (FS_O_WRONLY | FS_O_RDWR)))
    return -FS_EBADF;
  if (count == 0)
    return 0;
  if (count > FS_RW_MAX)
    count = FS_RW_MAX;
  struct BaseINode* inode = fd->inode;
  fs_off_t seekOff = fd->flags & FS_O_APPEND
    ? inode->size
    : fd->seekOff;
  fs_off_t seekEnd;
  if (AddOverflow(seekOff, count, &seekEnd))
    return -FS_EFBIG;
  struct DataRange* range = ((struct RegularINode*)inode)->AllocData(seekOff, count);
  if (!range)
    return -FS_ENOMEM;
  MemCpy(range->data + (seekOff - range->offset), buf, count);
  fd->seekOff = seekEnd;
  struct fs_timespec ts;
  GetTime(&ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
fs_ssize_t FileSystem::Writev(unsigned int fdNum, struct fs_iovec* iov, int iovcnt) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || !(fd->flags & (FS_O_WRONLY | FS_O_RDWR)))
    return -FS_EBADF;
  if (iovcnt == 0)
    return 0;
  if (iovcnt < 0 || iovcnt > FS_IOV_MAX)
    return -FS_EINVAL;
  fs_ssize_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    fs_ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (len < 0)
      return -FS_EINVAL;
    fs_size_t limit = FS_RW_MAX - totalLen;
    if (len > limit) {
      len = limit;
      iov[i].iov_len = len;
    }
    totalLen += len;
  }
  if (totalLen == 0)
    return 0;
  struct BaseINode* inode = fd->inode;
  fs_off_t seekOff = fd->flags & FS_O_APPEND
    ? inode->size
    : fd->seekOff;
  fs_off_t seekEnd;
  if (AddOverflow(seekOff, totalLen, &seekEnd))
    return -FS_EFBIG;
  struct DataRange* range = ((struct RegularINode*)inode)->AllocData(seekOff, totalLen);
  if (!range)
    return -FS_ENOMEM;
  fs_ssize_t count = 0;
  for (int i = 0; i != iovcnt; ++i) {
    fs_ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    MemCpy(range->data + (seekOff - range->offset) + count, iov[i].iov_base, len);
    count += len;
    if (count == totalLen)
      break;
  }
  fd->seekOff = seekEnd;
  struct fs_timespec ts;
  GetTime(&ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
fs_ssize_t FileSystem::PWrite(
  unsigned int fdNum,
  const char* buf,
  fs_size_t count,
  fs_off_t offset
) {
  if (offset < 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || !(fd->flags & (FS_O_WRONLY | FS_O_RDWR)))
    return -FS_EBADF;
  if (count == 0)
    return 0;
  if (count > FS_RW_MAX)
    count = FS_RW_MAX;
  struct BaseINode* inode = fd->inode;
  {
    fs_off_t res;
    if (AddOverflow(offset, count, &res))
      return -FS_EFBIG;
  }
  struct DataRange* range = ((struct RegularINode*)inode)->AllocData(offset, count);
  if (!range)
    return -FS_ENOMEM;
  MemCpy(range->data + (offset - range->offset), buf, count);
  struct fs_timespec ts;
  GetTime(&ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
fs_ssize_t FileSystem::PWritev(
  unsigned int fdNum,
  struct fs_iovec* iov,
  int iovcnt,
  fs_off_t offset
) {
  if (offset < 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)) || !(fd->flags & (FS_O_WRONLY | FS_O_RDWR)))
    return -FS_EBADF;
  if (iovcnt == 0)
    return 0;
  if (iovcnt < 0 || iovcnt > FS_IOV_MAX)
    return -FS_EINVAL;
  fs_ssize_t totalLen = 0;
  for (int i = 0; i != iovcnt; ++i) {
    fs_ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    if (len < 0)
      return -FS_EINVAL;
    fs_size_t limit = FS_RW_MAX - totalLen;
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
    fs_off_t res;
    if (AddOverflow(offset, totalLen, &res))
      return -FS_EFBIG;
  }
  struct DataRange* range = ((struct RegularINode*)inode)->AllocData(offset, totalLen);
  if (!range)
    return -FS_ENOMEM;
  fs_ssize_t count = 0;
  for (int i = 0; i != iovcnt; ++i) {
    fs_ssize_t len = iov[i].iov_len;
    if (len == 0)
      continue;
    MemCpy(range->data + (offset - range->offset) + count, iov[i].iov_base, len);
    count += len;
    if (count == totalLen)
      break;
  }
  struct fs_timespec ts;
  GetTime(&ts);
  inode->mtime = inode->ctime = ts;
  return count;
}
fs_ssize_t FileSystem::SendFile(
  unsigned int outFd,
  unsigned int inFd,
  fs_off_t* offset,
  fs_size_t count
) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fdIn;
  struct Fd* fdOut;
  if (!(fdIn = GetFd(fs, inFd)) || fdIn->flags & FS_O_WRONLY ||
      !(fdOut = GetFd(fs, outFd)) || !(fdOut->flags & (FS_O_WRONLY | FS_O_RDWR)))
    return -FS_EBADF;
  if (FS_S_ISDIR(fdIn->inode->mode) || fdOut->flags & FS_O_APPEND)
    return -FS_EINVAL;
  fs_off_t off;
  if (offset) {
    if ((off = *offset) < 0)
      return -FS_EINVAL;
  } else off = fdIn->seekOff;
  if (count == 0)
    return 0;
  if (count > FS_RW_MAX)
    count = FS_RW_MAX;
  struct BaseINode* inodeIn = fdIn->inode;
  struct BaseINode* inodeOut = fdOut->inode;
  fs_off_t outSeekEnd;
  if (AddOverflow(fdOut->seekOff, count, &outSeekEnd))
    return -FS_EFBIG;
  if (off >= inodeIn->size)
    return 0;
  fs_off_t end = inodeIn->size - off;
  if (end < count)
    count = end;
  if (!offset)
    fdIn->seekOff += count;
  else *offset += count;
  if (outSeekEnd > inodeOut->size)
    inodeOut->size = outSeekEnd;
  DataIterator itIn((struct RegularINode*)inodeIn, off);
  DataIterator itOut((struct RegularINode*)inodeOut, fdOut->seekOff);
  for (fs_size_t amountRead = 0; amountRead != count;) {
    fs_size_t amount;
    fs_size_t amountToRead = count - amountRead;
    fs_size_t currEndIn = off + amountRead;
    fs_size_t currEndOut = fdOut->seekOff + amountRead;
    if (!itIn.IsInData()) {
      struct HoleRange holeIn = itIn.GetHole();
      fs_off_t holeInEnd = holeIn.offset + holeIn.size;
      if (!itOut.IsInData()) {
        struct HoleRange holeOut = itOut.GetHole();
        amount = (holeOut.offset + holeOut.size) - currEndOut;
        {
          fs_size_t newAmount = holeInEnd - currEndIn;
          if (amount > newAmount) {
            amount = newAmount;
            itIn.Next();
          } else itOut.Next();
        }
        if (amount == 0)
          continue;
        amountRead += Min<fs_size_t>(amount, amountToRead);
        continue;
      }
      struct DataRange* rangeOut = itOut.GetRange();
      amount = (rangeOut->offset + rangeOut->size) - currEndOut;
      {
        fs_size_t newAmount = holeInEnd - currEndIn;
        if (amount > newAmount) {
          amount = newAmount;
          itIn.Next();
        } else itOut.Next();
      }
      if (amount == 0)
        continue;
      if (amount > amountToRead)
        amount = amountToRead;
      amountRead += amount;
      continue;
    }
    struct DataRange* rangeIn = itIn.GetRange();
    amount = Min<fs_size_t>((rangeIn->offset + rangeIn->size) - currEndIn, amountToRead);
    struct DataRange* rangeOut = ((struct RegularINode*)inodeOut)->AllocData(currEndOut, amount);
    if (!rangeOut)
      return -FS_ENOMEM;
    MemCpy(
      rangeOut->data + (currEndOut - rangeOut->offset),
      rangeIn->data + (currEndIn - rangeIn->offset),
      amount
    );
    amountRead += amount;
    itIn.Next();
    itOut.SeekTo(currEndOut);
  }
  struct fs_timespec ts;
  GetTime(&ts);
  if (!(fdIn->flags & FS_O_NOATIME))
    inodeIn->atime = ts;
  inodeOut->mtime = inodeOut->ctime = ts;
  return count;
}
int FileSystem::FTruncate(unsigned int fdNum, fs_off_t length) {
  if (length < 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  if (!FS_S_ISREG(inode->mode) || !(fd->flags & (FS_O_WRONLY | FS_O_RDWR)))
    return -FS_EINVAL;
  if (fd->flags & FS_O_APPEND)
    return -FS_EPERM;
  ((struct RegularINode*)inode)->TruncateData(length);
  struct fs_timespec ts;
  GetTime(&ts);
  inode->ctime = inode->mtime = ts;
  return 0;
}
int FileSystem::Truncate(const char* path, fs_off_t length) {
  if (length < 0)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  if (FS_S_ISDIR(inode->mode))
    return -FS_EISDIR;
  if (!FS_S_ISREG(inode->mode))
    return -FS_EINVAL;
  if (!inode->CanUse(FS_W_OK))
    return -FS_EACCES;
  ((struct RegularINode*)inode)->TruncateData(length);
  struct fs_timespec ts;
  GetTime(&ts);
  inode->ctime = inode->mtime = ts;
  return 0;
}
int FileSystem::FChModAt(int dirFd, const char* path, fs_mode_t mode) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  inode->mode = (mode & 0777) | (inode->mode & FS_S_IFMT);
  GetTime(&inode->ctime);
  return 0;
}
int FileSystem::FChMod(unsigned int fdNum, fs_mode_t mode) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  inode->mode = (mode & 0777) | (inode->mode & FS_S_IFMT);
  GetTime(&inode->ctime);
  return 0;
}
int FileSystem::ChMod(const char* path, fs_mode_t mode) {
  return FChModAt(FS_AT_FDCWD, path, mode);
}
int FileSystem::ChDir(const char* path) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  struct DirectoryINode* parent;
  int res = GetINode(fs, path, &inode, &parent, true);
  if (res != 0)
    return res;
  if (!FS_S_ISDIR(inode->mode))
    return -FS_ENOTDIR;
  const char* absPath = AbsolutePath(fs, path);
  if (!absPath)
    return -FS_ENOMEM;
  Delete(fs->cwd.path);
  fs->cwd.path = absPath;
  fs->cwd.inode = (struct DirectoryINode*)inode;
  fs->cwd.parent = parent;
  return 0;
}
int FileSystem::GetCwd(char* buf, fs_size_t size) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  if (fs->cwd.inode != fs->inodes[0]) {
    struct BaseINode* inode;
    struct DirectoryINode* parent;
    int res = GetINode(fs, fs->cwd.path, &inode, &parent, true);
    if (res != 0)
      return res;
  }
  fs_size_t cwdLen = strlen(fs->cwd.path);
  if (size <= cwdLen)
    return -FS_ERANGE;
  if (buf) {
    MemCpy(buf, fs->cwd.path, cwdLen);
    buf[cwdLen] = '\0';
  }
  return cwdLen;
}
int FileSystem::Stat(const char* path, struct fs_stat* buf) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  inode->FillStat(buf);
  return 0;
}
int FileSystem::FStat(unsigned int fdNum, struct fs_stat* buf) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  fd->inode->FillStat(buf);
  return 0;
}
int FileSystem::LStat(const char* path, struct fs_stat* buf) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  inode->FillStat(buf);
  return 0;
}
int FileSystem::Statx(int dirFd, const char* path, int flags, int mask, struct fs_statx* buf) {
  if (flags & ~(FS_AT_SYMLINK_NOFOLLOW | FS_AT_EMPTY_PATH) || mask & ~FS_STATX_ALL ||
      (flags & FS_AT_EMPTY_PATH && path[0] != '\0'))
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = 0;
  if (flags & FS_AT_EMPTY_PATH)
    inode = fs->cwd.inode;
  else res = GetINode(fs, path, &inode, NULL, !(flags & FS_AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  inode->FillStatx(buf, mask);
  return 0;
}
int FileSystem::GetXAttr(const char* path, const char* name, void* value, fs_size_t size) {
  if (size != 0 && (strlen(name) > FS_XATTR_NAME_MAX || size > FS_XATTR_SIZE_MAX))
    return -FS_ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  for (fs_size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (size != 0) {
        if (size < attrib->size)
          return -FS_ERANGE;
        if (attrib->size != 0)
          MemCpy(value, attrib->data, attrib->size);
      }
      return 0;
    }
  }
  return -FS_ENODATA;
}
int FileSystem::LGetXAttr(const char* path, const char* name, void* value, fs_size_t size) {
  if (size != 0 && (strlen(name) > FS_XATTR_NAME_MAX || size > FS_XATTR_SIZE_MAX))
    return -FS_ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  for (fs_size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (size != 0) {
        if (size < attrib->size)
          return -FS_ERANGE;
        if (attrib->size != 0)
          MemCpy(value, attrib->data, attrib->size);
      }
      return 0;
    }
  }
  return -FS_ENODATA;
}
int FileSystem::FGetXAttr(int fdNum, const char* name, void* value, fs_size_t size) {
  if (size != 0 && (strlen(name) > FS_XATTR_NAME_MAX || size > FS_XATTR_SIZE_MAX))
    return -FS_ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  for (fs_size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (size != 0) {
        if (size < attrib->size)
          return -FS_ERANGE;
        if (attrib->size != 0)
          MemCpy(value, attrib->data, attrib->size);
      }
      return 0;
    }
  }
  return -FS_ENODATA;
}
int FileSystem::SetXAttr(
  const char* path,
  const char* name,
  void* value,
  fs_size_t size,
  int flags
) {
  if (strlen(name) > FS_XATTR_NAME_MAX || size > FS_XATTR_SIZE_MAX)
    return -FS_ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  struct Attributes* attribs = &inode->attribs;
  for (fs_size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (flags & FS_XATTR_CREATE)
        return -FS_EEXIST;
      const char* kname;
      if (!(kname = strdup(name)))
        return -FS_ENOMEM;
      char* data = NULL;
      if (size != 0) {
        if (!Alloc(&data, size)) {
          Delete(kname);
          return -FS_ENOMEM;
        }
        MemCpy(data, value, size);
      }
      attribs->list[i] = {
        kname,
        size,
        data
      };
      return 0;
    }
  }
  if (flags & FS_XATTR_REPLACE)
    return -FS_ENODATA;
  const char* kname;
  if (!(kname = strdup(name)))
    return -FS_ENOMEM;
  char* data = NULL;
  if (size != 0) {
    if (!Alloc(&data, size)) {
      Delete(kname);
      return -FS_ENOMEM;
    }
    MemCpy(data, value, size);
  }
  if (!Realloc(&attribs->list, attribs->count, attribs->count + 1)) {
    Delete(kname);
    if (size != 0)
      Delete(data);
    return -FS_ENOMEM;
  }
  attribs->list[attribs->count] = {
    kname,
    size,
    data
  };
  ++attribs->count;
  return 0;
}
int FileSystem::LSetXAttr(
  const char* path,
  const char* name,
  void* value,
  fs_size_t size,
  int flags
) {
  if (strlen(name) > FS_XATTR_NAME_MAX || size > FS_XATTR_SIZE_MAX)
    return -FS_ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  struct Attributes* attribs = &inode->attribs;
  for (fs_size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (flags & FS_XATTR_CREATE)
        return -FS_EEXIST;
      const char* kname;
      if (!(kname = strdup(name)))
        return -FS_ENOMEM;
      char* data = NULL;
      if (size != 0) {
        if (!Alloc(&data, size)) {
          Delete(kname);
          return -FS_ENOMEM;
        }
        MemCpy(data, value, size);
      }
      attribs->list[i] = {
        kname,
        size,
        data
      };
      return 0;
    }
  }
  if (flags & FS_XATTR_REPLACE)
    return -FS_ENODATA;
  const char* kname;
  if (!(kname = strdup(name)))
    return -FS_ENOMEM;
  char* data = NULL;
  if (size != 0) {
    if (!Alloc(&data, size)) {
      Delete(kname);
      return -FS_ENOMEM;
    }
    MemCpy(data, value, size);
  }
  if (!Realloc(&attribs->list, attribs->count, attribs->count + 1)) {
    Delete(kname);
    if (size != 0)
      Delete(data);
    return -FS_ENOMEM;
  }
  attribs->list[attribs->count] = {
    kname,
    size,
    data
  };
  ++attribs->count;
  return 0;
}
int FileSystem::FSetXAttr(int fdNum, const char* name, void* value, fs_size_t size, int flags) {
  if (strlen(name) > FS_XATTR_NAME_MAX || size > FS_XATTR_SIZE_MAX)
    return -FS_ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  struct Attributes* attribs = &inode->attribs;
  for (fs_size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (flags & FS_XATTR_CREATE)
        return -FS_EEXIST;
      const char* kname;
      if (!(kname = strdup(name)))
        return -FS_ENOMEM;
      char* data = NULL;
      if (size != 0) {
        if (!Alloc(&data, size)) {
          Delete(kname);
          return -FS_ENOMEM;
        }
        MemCpy(data, value, size);
      }
      attribs->list[i] = {
        kname,
        size,
        data
      };
      return 0;
    }
  }
  if (flags & FS_XATTR_REPLACE)
    return -FS_ENODATA;
  const char* kname;
  if (!(kname = strdup(name)))
    return -FS_ENOMEM;
  char* data = NULL;
  if (size != 0) {
    if (!Alloc(&data, size)) {
      Delete(kname);
      return -FS_ENOMEM;
    }
    MemCpy(data, value, size);
  }
  if (!Realloc(&attribs->list, attribs->count, attribs->count + 1)) {
    Delete(kname);
    if (size != 0)
      Delete(data);
    return -FS_ENOMEM;
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
  if (strlen(name) > FS_XATTR_NAME_MAX)
    return -FS_ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  struct Attributes* attribs = &inode->attribs;
  for (fs_size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (attribs->count == 1) {
        Delete(attribs->list);
        attribs->list = NULL;
        attribs->count = 0;
        return 0;
      }
      if (attrib->size != 0)
        Delete(attrib->data);
      if (i != attribs->count - 1)
        MemMove(
          &attribs->list[i],
          &attribs->list[i + 1],
          sizeof(struct Attribute) * (attribs->count - i)
        );
      Realloc(&attribs->list, attribs->count, --attribs->count);
      return 0;
    }
  }
  return -FS_ENODATA;
}
int FileSystem::LRemoveXAttr(const char* path, const char* name) {
  if (strlen(name) > FS_XATTR_NAME_MAX)
    return -FS_ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  struct Attributes* attribs = &inode->attribs;
  for (fs_size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (attribs->count == 1) {
        Delete(attribs->list);
        attribs->list = NULL;
        attribs->count = 0;
        return 0;
      }
      if (attrib->size != 0)
        Delete(attrib->data);
      if (i != attribs->count - 1)
        MemMove(
          &attribs->list[i],
          &attribs->list[i + 1],
          sizeof(struct Attribute) * (attribs->count - i)
        );
      Realloc(&attribs->list, attribs->count, --attribs->count);
      return 0;
    }
  }
  return -FS_ENODATA;
}
int FileSystem::FRemoveXAttr(int fdNum, const char* name) {
  if (strlen(name) > FS_XATTR_NAME_MAX)
    return -FS_ERANGE;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  struct Attributes* attribs = &inode->attribs;
  for (fs_size_t i = 0; i != attribs->count; ++i) {
    struct Attribute* attrib = &attribs->list[i];
    if (strcmp(name, attrib->name) == 0) {
      if (attribs->count == 1) {
        Delete(attribs->list);
        attribs->list = NULL;
        attribs->count = 0;
        return 0;
      }
      if (attrib->size != 0)
        Delete(attrib->data);
      if (i != attribs->count - 1)
        MemMove(
          &attribs->list[i],
          &attribs->list[i + 1],
          sizeof(struct Attribute) * (attribs->count - i)
        );
      Realloc(&attribs->list, attribs->count, --attribs->count);
      return 0;
    }
  }
  return -FS_ENODATA;
}
fs_ssize_t FileSystem::ListXAttr(const char* path, char* list, fs_size_t size) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  if (res != 0)
    return res;
  fs_size_t listLen = 0;
  for (fs_size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    listLen += strlen(attrib->name) + 1;
    if ((size != 0 && listLen > size) || listLen > FS_XATTR_LIST_MAX)
      return -FS_ERANGE;
  }
  if (size == 0)
    return listLen;
  fs_size_t i = 0;
  for (fs_size_t j = 0; j != inode->attribs.count; ++j) {
    struct Attribute* attrib = &inode->attribs.list[j];
    fs_size_t nameLen = strlen(attrib->name) + 1;
    MemCpy(&list[i], attrib->name, nameLen);
    i += nameLen;
  }
  return listLen;
}
fs_ssize_t FileSystem::LListXAttr(const char* path, char* list, fs_size_t size) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode);
  if (res != 0)
    return res;
  fs_size_t listLen = 0;
  for (fs_size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    listLen += strlen(attrib->name) + 1;
    if (listLen > size || listLen > FS_XATTR_LIST_MAX)
      return -FS_ERANGE;
  }
  if (size == 0)
    return listLen;
  fs_size_t i = 0;
  for (fs_size_t j = 0; j != inode->attribs.count; ++j) {
    struct Attribute* attrib = &inode->attribs.list[j];
    fs_size_t nameLen = strlen(attrib->name) + 1;
    MemCpy(&list[i], attrib->name, nameLen);
    i += nameLen;
  }
  return listLen;
}
fs_ssize_t FileSystem::FListXAttr(int fdNum, char* list, fs_size_t size) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct Fd* fd;
  if (!(fd = GetFd(fs, fdNum)))
    return -FS_EBADF;
  struct BaseINode* inode = fd->inode;
  fs_size_t listLen = 0;
  for (fs_size_t i = 0; i != inode->attribs.count; ++i) {
    struct Attribute* attrib = &inode->attribs.list[i];
    listLen += strlen(attrib->name) + 1;
    if (listLen > size || listLen > FS_XATTR_LIST_MAX)
      return -FS_ERANGE;
  }
  if (size == 0)
    return listLen;
  fs_size_t i = 0;
  for (fs_size_t j = 0; j != inode->attribs.count; ++j) {
    struct Attribute* attrib = &inode->attribs.list[j];
    fs_size_t nameLen = strlen(attrib->name) + 1;
    MemCpy(&list[i], attrib->name, nameLen);
    i += nameLen;
  }
  return listLen;
}
int FileSystem::UTimeNsAt(int dirFd, const char* path, const struct fs_timespec* times, int flags) {
  if (flags & ~FS_AT_SYMLINK_NOFOLLOW)
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, !(flags & FS_AT_SYMLINK_NOFOLLOW));
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  struct fs_timespec ts;
  GetTime(&ts);
  if (times) {
    if (times[0].tv_nsec != FS_UTIME_OMIT) {
      if (times[0].tv_nsec == FS_UTIME_NOW)
        inode->atime = ts;
      else inode->atime = times[0];
    }
    if (times[1].tv_nsec != FS_UTIME_OMIT) {
      if (times[1].tv_nsec == FS_UTIME_NOW)
        inode->mtime = ts;
      else inode->mtime = times[1];
    }
  } else inode->atime = inode->mtime = ts;
  inode->ctime = ts;
  return 0;
}
int FileSystem::FUTimesAt(unsigned int dirFd, const char* path, const struct fs_timeval* times) {
  if (times && (
        (times[0].tv_usec < 0 || times[0].tv_usec >= 1000000) ||
        (times[1].tv_usec < 0 || times[1].tv_usec >= 1000000)
      ))
    return -FS_EINVAL;
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  struct DirectoryINode* origCwd = fs->cwd.inode;
  if (dirFd != FS_AT_FDCWD) {
    struct Fd* fd;
    if (!(fd = GetFd(fs, dirFd)))
      return -FS_EBADF;
    if (!FS_S_ISDIR(fd->inode->mode))
      return -FS_ENOTDIR;
    fs->cwd.inode = (struct DirectoryINode*)fd->inode;
  }
  struct BaseINode* inode;
  int res = GetINode(fs, path, &inode, NULL, true);
  fs->cwd.inode = origCwd;
  if (res != 0)
    return res;
  struct fs_timespec ts;
  GetTime(&ts);
  if (times) {
    inode->atime.tv_sec = times[0].tv_sec;
    inode->atime.tv_nsec = times[0].tv_usec * 1000;
    inode->mtime.tv_sec = times[1].tv_sec;
    inode->mtime.tv_nsec = times[1].tv_usec * 1000;
  } else inode->atime = inode->mtime = ts;
  inode->ctime = ts;
  return 0;
}
int FileSystem::UTimes(const char* path, const struct fs_timeval* times) {
  return FUTimesAt(FS_AT_FDCWD, path, times);
}
int FileSystem::UTime(const char* path, const struct fs_utimbuf* times) {
  struct fs_timeval ts[2];
  if (times) {
    ts[0].tv_sec = times->actime;
    ts[0].tv_usec = 0;
    ts[1].tv_sec = times->modtime;
    ts[1].tv_usec = 0;
  }
  return FUTimesAt(FS_AT_FDCWD, path, times ? ts : NULL);
}
int FileSystem::UMask(int mask) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  int prev = fs->umask;
  fs->umask = mask & 0777;
  return prev;
}

#ifdef __linux__
#define fd_t int
#define INVALID_FD -1
#else
#define fd_t HANDLE
#define INVALID_FD INVALID_HANDLE_VALUE
#define write WriteToFile
#define read ReadFromFile
#define close CloseHandle
#define unlink DeleteFileA
#endif

/**
 * format:
 *   magic number ("\x7FVFS")
 *   is64Bit
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
bool FileSystem::DumpToFile(const char* filename) {
  struct FSInternal* fs = (struct FSInternal*)data;
  ScopedLock lock(fs->mtx);
  char is64Bit = (sizeof(fs_size_t) / 4) - 1;
  fd_t fd;
#ifdef __linux__
  fd = creat(filename, 0644);
#else
  fd = CreateFileA(
    filename,
    GENERIC_WRITE,
    0,
    NULL,
    OPEN_ALWAYS,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );
#endif
  if (fd == INVALID_FD)
    goto err1;
#ifdef _WIN32
  if (SetFilePointer(fd, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
      !SetEndOfFile(fd))
    goto err2;
#endif
  if (write(fd, "\x7FVFS", 4) != 4 ||
      write(fd, &is64Bit, 1) != 1 ||
      write(fd, &fs->inodeCount, sizeof(fs_ino_t)) != sizeof(fs_ino_t))
    goto err2;
  for (fs_ino_t i = 0; i != fs->inodeCount; ++i) {
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
    if (write(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode) ||
        write(fd, &inode->attribs.count, sizeof(fs_size_t)) != sizeof(fs_size_t))
      goto err2;
    if (inode->attribs.count != 0)
      for (fs_size_t j = 0; j != inode->attribs.count; ++j) {
        struct Attribute* attrib = &inode->attribs.list[j];
        fs_size_t nameLen = strlen(attrib->name) + 1;
        if (write(fd, (void*)attrib->name, nameLen) != nameLen ||
            write(fd, &attrib->size, sizeof(fs_size_t)) != sizeof(fs_size_t) ||
            (attrib->size != 0 && write(fd, attrib->data, attrib->size) != attrib->size))
          goto err2;
      }
    if (FS_S_ISLNK(inode->mode)) {
      fs_size_t targetLen = strlen(((struct SymLinkINode*)inode)->target) + 1;
      fs_off_t dataLen = inode->size;
      if (write(fd, (void*)((struct SymLinkINode*)inode)->target, targetLen) != targetLen ||
          write(fd, ((struct SymLinkINode*)inode)->data, dataLen) != dataLen)
        goto err2;
    }
    if (FS_S_ISDIR(inode->mode)) {
      if (write(
            fd,
            &((struct DirectoryINode*)inode)->dentCount,
            sizeof(fs_off_t)
          ) != sizeof(fs_off_t) ||
          write(
            fd,
            &((struct DirectoryINode*)inode)->dents[1].inode->ndx,
            sizeof(fs_ino_t)
          ) != sizeof(fs_ino_t))
        goto err2;
      for (fs_off_t j = 2; j != ((struct DirectoryINode*)inode)->dentCount; ++j) {
        struct Dent* dent = &((struct DirectoryINode*)inode)->dents[j];
        fs_size_t nameLen = strlen(dent->name) + 1;
        if (write(fd, &dent->inode->ndx, sizeof(fs_ino_t)) != sizeof(fs_ino_t) ||
            write(fd, (void*)dent->name, nameLen) != nameLen)
          goto err2;
      }
    } else if (inode->size != 0) {
      if (write(
            fd,
            &((struct RegularINode*)inode)->dataRangeCount,
            sizeof(fs_off_t)
          ) != sizeof(fs_off_t))
        goto err2;
      for (fs_off_t j = 0; j != ((struct RegularINode*)inode)->dataRangeCount; ++j) {
        struct DataRange* range = ((struct RegularINode*)inode)->dataRanges[j];
        if (write(fd, &range->offset, sizeof(fs_off_t)) != sizeof(fs_off_t) ||
            write(fd, &range->size, sizeof(fs_off_t)) != sizeof(fs_off_t))
          goto err2;
        fs_ssize_t written = 0;
        while (written != range->size) {
          fs_size_t amount = range->size - written;
          if (amount > FS_RW_MAX)
            amount = FS_RW_MAX;
          fs_ssize_t count = write(fd, range->data + written, amount);
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
  unlink(filename);
 err1:
  return false;
}
FileSystem* FileSystem::LoadFromFile(const char* filename) {
  fd_t fd;
#ifdef __linux__
  fd = open(filename, FS_O_RDONLY);
#else
  fd = CreateFileA(
    filename,
    GENERIC_READ,
    0,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );
#endif
  if (fd == INVALID_FD)
    goto err_at_open;
  char magic[4];
  char is64Bit;
  fs_ino_t inodeCount;
  struct BaseINode** inodes;
  if (read(fd, magic, 4) != 4 ||
      memcmp(magic, "\x7FVFS", 4) != 0 ||
      read(fd, &is64Bit, 1) != 1 || (is64Bit + 1) * 4 != sizeof(fs_size_t) ||
      read(fd, &inodeCount, sizeof(fs_ino_t)) != sizeof(fs_ino_t))
    goto err_after_open;
  if (!Alloc(&inodes, inodeCount))
    goto err_after_open;
  for (fs_ino_t i = 0; i != inodeCount; ++i) {
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
    inode->attribs = {};
    fs_size_t attribCount;
    if (read(fd, &attribCount, sizeof(fs_size_t)) != sizeof(fs_size_t))
      goto err_after_inode_init;
    if (attribCount != 0) {
      inode->attribs.count = attribCount;
      if (!Alloc(&inode->attribs.list, attribCount))
        goto err_after_inode_init;
      for (fs_size_t j = 0; j != attribCount; ++j) {
        struct Attribute attr = {};
        char name[FS_XATTR_NAME_MAX];
        fs_size_t nameLen = 0;
        fs_size_t size;
        do {
          if (read(fd, &name[nameLen], 1) != 1)
            goto err_after_attribs_init;
        } while (name[nameLen++] != '\0');
        if (!(attr.name = strdup(name)))
          goto err_after_attribs_init;
        if (read(fd, &size, sizeof(fs_size_t)) != sizeof(fs_size_t))
          goto err_after_attrib_name;
        if (size != 0) {
          attr.size = size;
          if (!Alloc(&attr.data, size))
            goto err_after_attrib_name;
          if (read(fd, attr.data, size) != size)
            goto err_after_attrib_data_init;
        }
        inode->attribs.list[j] = attr;
        goto success_attrib;

       err_after_attrib_data_init:
        Delete<false>(attr.data);
       err_after_attrib_name:
        Delete<false>(attr.name);
       err_after_attribs_init:
        for (fs_size_t k = 0; k != j; ++k) {
          struct Attribute* attrib = &inode->attribs.list[k];
          Delete<false>(attrib->name);
          if (attrib->size != 0)
            Delete<false>(attrib->data);
        }
        Delete<false>(inode->attribs.list);
        goto err_after_inode_init;
       success_attrib: {}
      }
    }
    if (FS_S_ISLNK(inode->mode)) {
      char target[FS_PATH_MAX];
      fs_size_t targetLen = 0;
      do {
        if (read(fd, &target[targetLen], 1) != 1)
          goto err_after_inode_init;
      } while (target[targetLen++] != '\0');
      if (!(((struct SymLinkINode*)inode)->target = strdup(target)))
        goto err_after_inode_init;
      char data[FS_PATH_MAX];
      fs_size_t dataLen = 0;
      do {
        if (read(fd, &data[dataLen], 1) != 1)
          goto err_after_target_alloc;
      } while (data[dataLen++] != '\0');
      if (!Alloc(&((struct SymLinkINode*)inode)->data, dataLen))
        goto err_after_target_alloc;
      MemCpy(((struct SymLinkINode*)inode)->data, data, dataLen);
      goto success_symlink;

     err_after_target_alloc:
      Delete<false>(((struct SymLinkINode*)inode)->target);
      goto err_after_inode_init;
     success_symlink: {}
    }
    if (FS_S_ISDIR(inode->mode)) {
      fs_off_t dentCount;
      if (read(fd, &dentCount, sizeof(fs_off_t)) != sizeof(fs_off_t))
        goto err_after_inode_init;
      if (!Alloc(&((struct DirectoryINode*)inode)->dents, dentCount))
        goto err_after_inode_init;
      ((struct DirectoryINode*)inode)->dents[0] = { ".", inode };
      ((struct DirectoryINode*)inode)->dents[1].name = "..";
      if (read(
            fd,
            &((struct DirectoryINode*)inode)->dents[1].inode,
            sizeof(fs_ino_t)
          ) != sizeof(fs_ino_t))
        goto err_after_dent_alloc;
      for (fs_off_t j = 2; j != dentCount; ++j) {
        struct Dent* dent = &((struct DirectoryINode*)inode)->dents[j];
        char name[FS_PATH_MAX];
        fs_size_t nameLen = 0;
        if (read(fd, &dent->inode, sizeof(fs_ino_t)) != sizeof(fs_ino_t))
          goto err_after_dent_list_init;
        do {
          if (read(fd, &name[nameLen], 1) != 1)
            goto err_after_dent_list_init;
        } while (name[nameLen++] != '\0');
        if (!(dent->name = strdup(name)))
          goto err_after_dent_list_init;
        goto success_dent;

       err_after_dent_list_init:
        for (fs_off_t k = 2; k != j; ++k)
          Delete<false>(((struct DirectoryINode*)inode)->dents[k].name);
        goto err_after_dent_alloc;
       success_dent: {}
      }
      ((struct DirectoryINode*)inode)->dentCount = dentCount;
      goto success_dents;

     err_after_dent_alloc:
      Delete<false>(((struct DirectoryINode*)inode)->dents);
      goto err_after_inode_init;
     success_dents: {}
    }
    if (FS_S_ISREG(inode->mode)) {
      if (inode->size != 0) {
        fs_off_t dataRangeCount;
        if (read(fd, &dataRangeCount, sizeof(fs_off_t)) != sizeof(fs_off_t))
          goto err_after_inode_init;
        if (!Alloc(&((struct RegularINode*)inode)->dataRanges, dataRangeCount))
          goto err_after_inode_init;
        for (fs_off_t j = 0; j != dataRangeCount; ++j) {
          struct DataRange* range;
          fs_ssize_t nread = 0;
          fs_off_t offset;
          fs_off_t size;
          if (read(fd, &offset, sizeof(fs_off_t)) != sizeof(fs_off_t) ||
              read(fd, &size, sizeof(fs_off_t)) != sizeof(fs_off_t))
            goto err_after_dataranges_init;
          if (offset < 0 || offset > inode->size - size ||
              size   < 0 || size   > inode->size - offset)
            goto err_after_dataranges_init;
          if (!Alloc(&range))
            goto err_after_dataranges_init;
          range->offset = offset;
          range->size = size;
          if (!Alloc(&range->data, size))
            goto err_after_range_alloc;
          while (nread != size) {
            fs_size_t amount = size - nread;
            if (amount > FS_RW_MAX)
              amount = FS_RW_MAX;
            fs_ssize_t count = read(fd, range->data + nread, amount);
            if (count != amount)
              goto err_after_range_data_alloc;
            nread += count;
          }
          ((struct RegularINode*)inode)->dataRanges[j] = range;
          goto success_data_range;

         err_after_range_data_alloc:
          Delete<false>(range->data);
         err_after_range_alloc:
          Delete<false>(range);
          goto err_after_dataranges_init;
         success_data_range: {}
        }
        ((struct RegularINode*)inode)->dataRangeCount = dataRangeCount;
        goto success_data;

       err_after_dataranges_init:
        for (fs_off_t k = 0; k != dataRangeCount; ++k)
          Delete(((struct RegularINode*)inode)->dataRanges[k]);
        Delete<false>(((struct RegularINode*)inode)->dataRanges);
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
    Delete<false>(inode);
   err_at_inode_loop:
    for (fs_ino_t j = 0; j != i; ++i)
      DeleteINode(inodes[j]);
    goto err_after_inode_list_init;
   success_inode: {}
  }
  for (fs_ino_t i = 0; i != inodeCount; ++i) {
    struct BaseINode* inode = inodes[i];
    if (FS_S_ISDIR(inode->mode)) {
      for (fs_off_t j = 1; j != ((struct DirectoryINode*)inode)->dentCount; ++j) {
        struct Dent* dent = &((struct DirectoryINode*)inode)->dents[j];
        fs_ino_t ino = (fs_ino_t)dent->inode;
        if (ino >= inodeCount)
          goto err_after_inodes;
        dent->inode = inodes[ino];
      }
    }
  }
  for (fs_ino_t i = 0; i != inodeCount;) {
    struct BaseINode* inode = inodes[i];
    if (inode->nlink == 0) {
      DeleteINode(inode);
      if (i != inodeCount - 1) {
        MemMove(inodes + i, inodes + i + 1, sizeof(struct INode*) * (inodeCount - i));
        for (fs_ino_t j = i; j != inodeCount - 1; ++j)
          --inodes[j]->ndx;
      }
      Realloc(&inodes, inodeCount, --inodeCount);
    } else ++i;
  }
  close(fd);
  FileSystem* fs;
  struct FSInternal* data;
  if (!Alloc(&fs))
    goto err_after_inodes;
  if (!Alloc<true>(&data))
    goto err_after_fs_init;
  data->inodes = inodes;
  data->inodeCount = inodeCount;
  if (!(data->cwd.path = strdup("/")))
    goto err_after_fsdata_init;
  data->cwd.inode = data->cwd.parent = (struct DirectoryINode*)inodes[0];
  fs->data = data;
  return fs;

 err_after_fsdata_init:
  Delete<false>(data);
 err_after_fs_init:
  Delete<false>(fs);
 err_after_inodes:
  for (fs_ino_t i = 0; i != inodeCount; ++i)
    DeleteINode(inodes[i]);
 err_after_inode_list_init:
  Delete<false>(inodes);
 err_after_open:
  close(fd);
 err_at_open:
  return NULL;
}

#endif