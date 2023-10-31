#ifndef __linux__
#error FileSystem is only available in Linux
#endif
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <string.h>
#include <utime.h>
#include <unistd.h>

struct linux_dirent {
  unsigned long	d_ino;
  unsigned long	d_off;
  unsigned short d_reclen;
  char d_name[1];
};

class FileSystem {
 public:
  FileSystem() {
    struct INode* root;
    if (!TryAlloc(&root) ||
        !TryAlloc(&root->dents, 2))
      abort();
    root->mode = 0755 | S_IFDIR;
    root->dents[0] = { ".", root };
    root->dents[1] = { "..", root };
    root->dentCount = root->nlink = 2;
    if (!PushINode(root) ||
        !TryAlloc(&cwd) ||
        !(cwd->path = strdup("/")))
      abort();
    cwd->inode = root;
    cwd->parent = root;
  }
  ~FileSystem() {
    while (inodeCount--)
      delete inodes[inodeCount];
    while (fdCount--)
      delete fds[fdCount];
    delete inodes;
    delete fds;
    delete cwd;
  }

  int FAccessAt2(int dirFd, const char* path, int mode, int flags) {
    std::lock_guard<std::mutex> lock(mtx);
    if (mode & ~S_IRWXO || flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH) ||
        (flags & AT_EMPTY_PATH && path[0] != '\0'))
      return -EINVAL;
    struct INode* origCwd = cwd->inode;
    if (dirFd != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode) && !(flags & AT_EMPTY_PATH))
        return -ENOTDIR;
      cwd->inode = fd->inode;
    }
    struct INode* inode;
    int res = 0;
    if (flags & AT_EMPTY_PATH)
      inode = cwd->inode;
    else res = GetINode(path, &inode, NULL, !(flags & AT_SYMLINK_NOFOLLOW));
    cwd->inode = origCwd;
    if (res != 0)
      return res;
    int check = 0;
    if (mode & R_OK)
      check |= 0444;
    if (mode & W_OK)
      check |= 0222;
    if (mode & X_OK)
      check |= 0111;
    if (check != F_OK && !(inode->mode & check))
      return -EACCES;
    return 0;
  }
  int FAccessAt(int dirFd, const char* path, int mode) {
    return FAccessAt2(dirFd, path, mode, F_OK);
  }
  int Access(const char* path, int mode) {
    return FAccessAt2(AT_FDCWD, path, mode, F_OK);
  }
  int OpenAt(int dirFd, const char* path, int flags, mode_t mode) {
    std::lock_guard<std::mutex> lock(mtx);
    if (flags & ~(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_APPEND | O_TRUNC | O_TMPFILE | O_DIRECTORY | O_NOFOLLOW | O_NOATIME))
      return -EINVAL;
    if ((flags & O_TMPFILE) == O_TMPFILE) {
      if (flags & O_CREAT || !(flags & (O_WRONLY | O_RDWR)) || mode == 0)
        return -EINVAL;
      struct INode* inode;
      if (!TryAlloc(&inode))
        return -EIO;
      if (!PushINode(inode)) {
        delete inode;
        return -EIO;
      }
      inode->mode = (mode & ~S_IFMT) | S_IFREG;
      int res = PushFd(inode, flags);
      if (res < 0)
        RemoveINode(inode);
      return res;
    } else if (flags & O_CREAT) {
      if (flags & O_DIRECTORY || mode & ~0777)
        return -EINVAL;
      mode |= S_IFREG;
    } else if (mode != 0)
      return -EINVAL;
    struct INode* origCwd = cwd->inode;
    if (dirFd != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd->inode = fd->inode;
    }
    if (flags & O_CREAT && flags & O_EXCL)
      flags |= O_NOFOLLOW;
    if (flags & O_WRONLY && flags & O_RDWR)
      flags &= ~O_RDWR;
    struct INode* inode;
    struct INode* parent = NULL;
    int res = GetINode(path, &inode, &parent, !(flags & O_NOFOLLOW));
    cwd->inode = origCwd;
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
    } else {
      if (flags & O_CREAT && res == -ENOENT) {
        if (inodeCount == std::numeric_limits<ino_t>::max())
          return -EDQUOT;
        flags &= ~O_TRUNC;
        const char* name = GetAbsoluteLast(path);
        if (!name)
          return -ENOMEM;
        if (parent->size > std::numeric_limits<off_t>::max() - (strlen(name) * 2)) {
          delete name;
          return -ENOSPC;
        }
        struct INode* x;
        if (!TryAlloc(&x)) {
          delete name;
          return -EIO;
        }
        if (!PushINode(x)) {
          delete name;
          delete x;
          return -EIO;
        }
        if (!parent->PushDent(name, x)) {
          RemoveINode(x);
          delete name;
          return -EIO;
        }
        x->mode = mode;
        x->nlink = 1;
        parent->ctime = parent->mtime = x->btime;
        int res = PushFd(x, flags);
        if (res < 0) {
          parent->RemoveDent(name);
          RemoveINode(x);
          delete name;
        }
        return res;
      } else return res;
    }
    if (S_ISDIR(inode->mode)) {
      if (flags & (O_WRONLY | O_RDWR))
        return -EISDIR;
    } else {
      if (flags & O_DIRECTORY)
        return -ENOTDIR;
      if (flags & O_TRUNC && inode->size != 0)
        inode->TruncateData(0);
    }
    return PushFd(inode, flags);
  }
  int Open(const char* path, int flags, mode_t mode) {
    return OpenAt(AT_FDCWD, path, flags, mode);
  }
  int Creat(const char* path, mode_t mode) {
    return OpenAt(AT_FDCWD, path, O_CREAT | O_WRONLY | O_TRUNC, mode);
  }
  int Close(unsigned int fd) {
    std::lock_guard<std::mutex> lock(mtx);
    return RemoveFd(fd);
  }
  int CloseRange(unsigned int fd, unsigned int maxFd, unsigned int flags) {
    std::lock_guard<std::mutex> lock(mtx);
    if (flags != 0 || fd > maxFd)
      return -EINVAL;
    for (int i = 0; i != fdCount; ++i)
      if (fds[i]->fd > fd && fds[i]->fd < maxFd)
        RemoveFd(fds[i]->fd);
    return 0;
  }
  int MkNodAt(int dirFd, const char* path, mode_t mode, dev_t) {
    std::lock_guard<std::mutex> lock(mtx);
    if (inodeCount == std::numeric_limits<ino_t>::max())
      return -EDQUOT;
    if (!(mode & S_IFMT))
      mode |= S_IFREG;
    else if (S_ISDIR(mode))
      return -EPERM;
    else if (!S_ISREG(mode))
      return -EINVAL;
    struct INode* origCwd = cwd->inode;
    if (dirFd != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd->inode = fd->inode;
    }
    struct INode* inode;
    struct INode* parent = NULL;
    int res = GetINode(path, &inode, &parent);
    cwd->inode = origCwd;
    if (!parent || res != -ENOENT)
      return res;
    if (res == 0)
      return -EEXIST;
    const char* name = GetAbsoluteLast(path);
    if (!name)
      return -ENOMEM;
    if (parent->size > std::numeric_limits<off_t>::max() - (strlen(name) * 2)) {
      delete name;
      return -ENOSPC;
    }
    struct INode* x;
    if (!TryAlloc(&x)) {
      delete name;
      return -EIO;
    }
    if (!PushINode(x)) {
      delete name;
      delete x;
      return -EIO;
    }
    if (!parent->PushDent(name, x)) {
      RemoveINode(x);
      delete name;
      return -EIO;
    }
    x->mode = mode;
    x->nlink = 1;
    parent->ctime = parent->mtime = x->btime;
    return 0;
  }
  int MkNod(const char* path, mode_t mode, dev_t) {
    return MkNodAt(AT_FDCWD, path, mode, 0);
  }
  int MkDirAt(int dirFd, const char* path, mode_t mode) {
    std::lock_guard<std::mutex> lock(mtx);
    if (inodeCount == std::numeric_limits<ino_t>::max())
      return -EDQUOT;
    struct INode* origCwd = cwd->inode;
    if (dirFd != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd->inode = fd->inode;
    }
    struct INode* inode;
    struct INode* parent = NULL;
    int res = GetINode(path, &inode, &parent);
    cwd->inode = origCwd;
    if (!parent || res != -ENOENT)
      return res;
    if (res == 0)
      return -EEXIST;
    const char* name = GetAbsoluteLast(path);
    if (!name)
      return -ENOMEM;
    if (parent->size > std::numeric_limits<off_t>::max() - (strlen(name) * 2)) {
      delete name;
      return -ENOSPC;
    }
    struct INode* x;
    if (!TryAlloc(&x)) {
      delete name;
      return -EIO;
    }
    if (!TryAlloc(&x->dents, 2) ||
        !PushINode(x)) {
      delete name;
      delete x;
      return -EIO;
    }
    if (!parent->PushDent(name, x)) {
      RemoveINode(x);
      delete name;
      return -EIO;
    }
    x->mode = (mode & ~S_IFMT) | S_IFDIR;
    x->nlink = 2;
    x->dents[0] = { ".", x };
    x->dents[1] = { "..", parent };
    x->dentCount = 2;
    ++parent->nlink;
    parent->ctime = parent->mtime = x->btime;
    return 0;
  }
  int MkDir(const char* path, mode_t mode) {
    return MkDirAt(AT_FDCWD, path, mode);
  }
  int SymLinkAt(const char* oldPath, int newDirFd, const char* newPath) {
    std::lock_guard<std::mutex> lock(mtx);
    if (inodeCount == std::numeric_limits<ino_t>::max())
      return -EDQUOT;
    struct Fd* fd;
    if (newDirFd != AT_FDCWD) {
      if (!(fd = GetFd(newDirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
    }
    struct INode* oldInode;
    int res = GetINode(oldPath, &oldInode);
    if (res != 0)
      return res;
    struct INode* origCwd = cwd->inode;
    if (newDirFd != AT_FDCWD)
      cwd->inode = fd->inode;
    struct INode* newInode;
    struct INode* newParent = NULL;
    res = GetINode(newPath, &newInode, &newParent);
    cwd->inode = origCwd;
    if (!newParent)
      return res;
    if (res == 0)
      return -EEXIST;
    if (res != -ENOENT)
      return res;
    const char* name = GetAbsoluteLast(newPath);
    if (!name)
      return -ENOMEM;
    if (newParent->size > std::numeric_limits<off_t>::max() - (strlen(name) * 2)) {
      delete name;
      return -ENOSPC;
    }
    struct INode* x;
    if (!TryAlloc(&x)) {
      delete name;
      return -EIO;
    }
    size_t oldPathLen = strlen(oldPath);
    struct INode::DataRange* range = x->AllocData(0, oldPathLen);
    if (!range || !PushINode(x)) {
      delete name;
      delete x;
      return -EIO;
    }
    memcpy(range->data, oldPath, oldPathLen);
    if (!newParent->PushDent(name, x)) {
      RemoveINode(x);
      delete name;
      return -EIO;
    }
    x->mode = 0777 | S_IFLNK;
    x->target = AbsolutePath(oldPath);
    x->nlink = 1;
    x->size = oldPathLen;
    newParent->ctime = newParent->mtime = x->btime;
    return 0;
  }
  int SymLink(const char* oldPath, const char* newPath) {
    return SymLinkAt(oldPath, AT_FDCWD, newPath);
  }
  int ReadLinkAt(int dirFd, const char* path, char* buf, int bufLen) {
    std::lock_guard<std::mutex> lock(mtx);
    if (bufLen <= 0)
      return -EINVAL;
    struct INode* origCwd = cwd->inode;
    if (dirFd != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd->inode = fd->inode;
    }
    struct INode* inode;
    int res = GetINode(path, &inode);
    cwd->inode = origCwd;
    if (res != 0)
      return res;
    if (!S_ISLNK(inode->mode))
      return -EINVAL;
    if (inode->size < bufLen)
      bufLen = inode->size;
    memcpy(buf, inode->dataRanges[0]->data, bufLen);
    clock_gettime(CLOCK_REALTIME, &inode->atime);
    return bufLen;
  }
  int ReadLink(const char* path, char* buf, int bufLen) {
    return ReadLinkAt(AT_FDCWD, path, buf, bufLen);
  }
  int GetDents(unsigned int fdNum, struct linux_dirent* dirp, unsigned int count) {
    std::lock_guard<std::mutex> lock(mtx);
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    struct INode* inode = fd->inode;
    if (!S_ISDIR(inode->mode))
      return -ENOTDIR;
    if (fd->seekOff >= inode->dentCount)
      return 0;
    unsigned int nread = 0;
    char* dirpData = (char*)dirp;
    do {
      struct INode::Dent d = inode->dents[fd->seekOff];
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
      dirpData[reclen - 1] = (d.inode->mode & S_IFMT) >> 12;
      dirpData += reclen;
      nread += reclen;
    } while (++fd->seekOff != inode->dentCount);
    if (nread == 0)
      return -EINVAL;
    if (!(fd->flags & O_NOATIME))
      clock_gettime(CLOCK_REALTIME, &inode->atime);
    return nread;
  }
  int LinkAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, int flags) {
    std::lock_guard<std::mutex> lock(mtx);
    if (flags & ~(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH) ||
        (flags & AT_EMPTY_PATH && oldPath[0] != '\0'))
      return -EINVAL;
    struct Fd* oldFd;
    struct Fd* newFd;
    if (oldDirFd != AT_FDCWD || flags & AT_EMPTY_PATH) {
      if (!(oldFd = GetFd(oldDirFd)))
        return -EBADF;
      if (!S_ISDIR(oldFd->inode->mode)) {
        if (!(flags & AT_EMPTY_PATH))
          return -ENOTDIR;
      } else if (flags & AT_EMPTY_PATH)
        return -EPERM;
    }
    if (newDirFd != AT_FDCWD) {
      if (!(newFd = GetFd(newDirFd)))
        return -EBADF;
      if (!S_ISDIR(newFd->inode->mode))
        return -ENOTDIR;
    }
    struct INode* origCwd = cwd->inode;
    if (oldDirFd != AT_FDCWD)
      cwd->inode = oldFd->inode;
    struct INode* oldInode;
    int res = 0;
    if (flags & AT_EMPTY_PATH)
      oldInode = cwd->inode;
    else res = GetINode(oldPath, &oldInode, NULL, flags & AT_SYMLINK_FOLLOW);
    cwd->inode = origCwd;
    if (res != 0)
      return res;
    if (newDirFd != AT_FDCWD)
      cwd->inode = newFd->inode;
    struct INode* newInode;
    struct INode* newParent = NULL;
    res = GetINode(newPath, &newInode, &newParent);
    cwd->inode = origCwd;
    if (!newParent)
      return res;
    if (res == 0)
      return -EEXIST;
    if (res != -ENOENT)
      return res;
    if (S_ISDIR(oldInode->mode))
      return -EPERM;
    const char* name = GetAbsoluteLast(newPath);
    if (!name)
      return -ENOMEM;
    if (newParent->size > std::numeric_limits<off_t>::max() - (strlen(name) * 2)) {
      delete name;
      return -ENOSPC;
    }
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
  int Link(const char* oldPath, const char* newPath) {
    return LinkAt(AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
  }
  int UnlinkAt(int dirFd, const char* path, int flags) {
    std::lock_guard<std::mutex> lock(mtx);
    if (flags & ~AT_REMOVEDIR)
      return -EINVAL;
    struct INode* origCwd = cwd->inode;
    if (dirFd != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd->inode = fd->inode;
    }
    struct INode* inode;
    struct INode* parent;
    int res = GetINode(path, &inode, &parent);
    cwd->inode = origCwd;
    if (res != 0)
      return res;
    if (flags & AT_REMOVEDIR) {
      if (!S_ISDIR(inode->mode))
        return -ENOTDIR;
      if (inode == inodes[0])
        return -EBUSY;
    } else if (S_ISDIR(inode->mode))
      return -EISDIR;
    for (int i = 0; i != fdCount; ++i)
      if (fds[i]->inode == inode)
        return -EBUSY;
    if (flags & AT_REMOVEDIR) {
      const char* last = GetLast(path);
      if (!last)
        return -ENOMEM;
      bool isDot = strcmp(last, ".") == 0;
      delete last;
      if (isDot)
        return -EINVAL;
      if (inode->dentCount != 2)
        return -ENOTEMPTY;
    }
    const char* name = GetAbsoluteLast(path);
    if (!name)
      return -ENOMEM;
    parent->RemoveDent(name);
    delete name;
    if (flags & AT_REMOVEDIR)
      --parent->nlink;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (--inode->nlink == 0)
      RemoveINode(inode);
    else inode->ctime = ts;
    parent->ctime = parent->mtime = ts;
    return 0;
  }
  int Unlink(const char* path) {
    return UnlinkAt(AT_FDCWD, path, 0);
  }
  int RmDir(const char* path) {
    return UnlinkAt(AT_FDCWD, path, AT_REMOVEDIR);
  }
  int RenameAt2(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, unsigned int flags) {
    std::lock_guard<std::mutex> lock(mtx);
    if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE) ||
        (flags & RENAME_NOREPLACE && flags & RENAME_EXCHANGE))
      return -EINVAL;
    const char* last = GetLast(oldPath);
    if (!last)
      return -ENOMEM;
    bool isDot = strcmp(last, ".") == 0 || strcmp(last, "..") == 0;
    delete last;
    if (isDot)
      return -EBUSY;
    struct Fd* oldFd;
    struct Fd* newFd;
    if (oldDirFd != AT_FDCWD) {
      if (!(oldFd = GetFd(oldDirFd)))
        return -EBADF;
      if (!S_ISDIR(oldFd->inode->mode))
        return -ENOTDIR;
    }
    if (newDirFd != AT_FDCWD) {
      if (!(newFd = GetFd(newDirFd)))
        return -EBADF;
      if (!S_ISDIR(newFd->inode->mode))
        return -ENOTDIR;
    }
    struct INode* origCwd = cwd->inode;
    if (oldDirFd != AT_FDCWD)
      cwd->inode = oldFd->inode;
    struct INode* oldInode;
    struct INode* oldParent;
    int res = GetINode(oldPath, &oldInode, &oldParent);
    cwd->inode = origCwd;
    if (res != 0)
      return res;
    if (newDirFd != AT_FDCWD)
      cwd->inode = newFd->inode;
    struct INode* newInode = NULL;
    struct INode* newParent = NULL;
    res = GetINode(newPath, &newInode, &newParent);
    cwd->inode = origCwd;
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
        if (newInode->dentCount > 2)
          return -ENOTEMPTY;
      }
      if (oldInode == inodes[0] || oldInode == cwd->inode)
        return -EBUSY;
    } else if (newInode && S_ISDIR(newInode->mode))
      return -EISDIR;
    if (IsInDir(oldParent, newParent))
      return -EINVAL;
    const char* oldName = GetAbsoluteLast(oldPath);
    if (!oldName)
      return -ENOMEM;
    const char* newName = GetAbsoluteLast(newPath);
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
      if (newParent->size > std::numeric_limits<off_t>::max() - (strlen(newName) * 2)) {
        delete newName;
        return -ENOSPC;
      }
      if (!newParent->PushDent(newName, oldInode)) {
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
        if (--newInode->nlink == 0)
          RemoveINode(newInode);
        else newInode->ctime = ts;
      }
    } else newInode->ctime = ts;
    oldInode->ctime = ts;
    oldParent->ctime = oldParent->mtime = ts;
    newParent->ctime = newParent->mtime = ts;
    return 0;
  }
  int RenameAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath) {
    return RenameAt2(oldDirFd, oldPath, newDirFd, newPath, 0);
  }
  int Rename(const char* oldPath, const char* newPath) {
    return RenameAt2(AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
  }
  off_t LSeek(unsigned int fdNum, off_t offset, unsigned int whence) {
    std::lock_guard<std::mutex> lock(mtx);
    if (offset < 0)
      return -EINVAL;
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    struct INode* inode = fd->inode;
    switch (whence) {
      case SEEK_SET:
        return fd->seekOff = offset;
      case SEEK_CUR:
        if (fd->seekOff > std::numeric_limits<off_t>::max() - offset)
          return -EOVERFLOW;
        return fd->seekOff += offset;
      case SEEK_END:
        if (S_ISDIR(inode->mode))
          return -EINVAL;
        if (inode->size > std::numeric_limits<off_t>::max() - offset)
          return -EOVERFLOW;
        return fd->seekOff = inode->size + offset;
      case SEEK_DATA: {
        INode::DataIterator it(inode, fd->seekOff);
        if (!it.IsInData()) {
          if (!it.Next()) {
            if (inode->size > std::numeric_limits<off_t>::max() - offset)
              return -EOVERFLOW;
            return fd->seekOff = inode->size + offset;
          }
          struct INode::DataRange* range = it.GetRange();
          if (range->offset > std::numeric_limits<off_t>::max() - offset)
            return -EOVERFLOW;
          return fd->seekOff = range->offset + offset;
        }
        it.Next();
        if (it.Next()) {
          struct INode::DataRange* range = it.GetRange();
          if (range->offset > std::numeric_limits<off_t>::max() - offset)
            return -EOVERFLOW;
          return fd->seekOff = range->offset + offset;
        }
        if (inode->size > std::numeric_limits<off_t>::max() - offset)
          return -EOVERFLOW;
        return fd->seekOff = inode->size + offset;
      }
      case SEEK_HOLE: {
        INode::DataIterator it(inode, fd->seekOff);
        if (it.IsInData()) {
          it.Next();
          struct INode::HoleRange hole = it.GetHole();
          if (hole.offset > std::numeric_limits<off_t>::max() - offset)
            return -EOVERFLOW;
          return fd->seekOff = hole.offset + offset;
        }
        if (it.Next()) {
          it.Next();
          struct INode::HoleRange hole = it.GetHole();
          if (hole.offset > std::numeric_limits<off_t>::max() - offset)
            return -EOVERFLOW;
          return fd->seekOff = hole.offset + offset;
        }
        if (inode->size > std::numeric_limits<off_t>::max() - offset)
          return -EOVERFLOW;
        return fd->seekOff = inode->size + offset;
      }
    }
    return -EINVAL;
  }
  ssize_t Read(unsigned int fdNum, char* buf, size_t count) {
    std::lock_guard<std::mutex> lock(mtx);
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)) || fd->flags & O_WRONLY)
      return -EBADF;
    struct INode* inode = fd->inode;
    if (S_ISDIR(inode->mode))
      return -EISDIR;
    if (count == 0)
      return 0;
    if (!buf)
      return -EFAULT;
    count = std::min(count, 0x7ffff000UL);
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
        amount = std::min((range->offset + range->size) - (fd->seekOff + amountRead), count - amountRead);
        memcpy(buf + amountRead, range->data + (fd->seekOff + amountRead) - range->offset, amount);
      } else {
        struct INode::HoleRange hole = it.GetHole();
        amount = std::min((hole.offset + hole.size) - (fd->seekOff + amountRead), count - amountRead);
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
  ssize_t Readv(unsigned int fdNum, struct iovec* iov, int iovcnt) {
    std::lock_guard<std::mutex> lock(mtx);
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)) || fd->flags & O_WRONLY)
      return -EBADF;
    struct INode* inode = fd->inode;
    if (S_ISDIR(inode->mode))
      return -EISDIR;
    if (iovcnt == 0)
      return 0;
    if (iovcnt < 0 || iovcnt > 1024)
      return -EINVAL;
    if (!iov)
      return -EFAULT;
    size_t totalLen = 0;
    for (int i = 0; i != iovcnt; ++i) {
      size_t len = iov[i].iov_len;
      if (len == 0)
        continue;
      if (len < 0)
        return -EINVAL;
      if (!iov[i].iov_base)
        return -EFAULT;
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
        amount = std::min(
          std::min(
            (range->offset + range->size) - (fd->seekOff + count),
            curr.iov_len - amountRead),
          totalLen - count
        );
        memcpy((char*)curr.iov_base + amountRead, range->data + (fd->seekOff + count) - range->offset, amount);
      } else {
        struct INode::HoleRange hole = it.GetHole();
        amount = std::min(
          std::min(
            (hole.offset + hole.size) - (fd->seekOff + count),
            curr.iov_len - amountRead),
          totalLen - count
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
  ssize_t PRead(unsigned int fdNum, char* buf, size_t count, off_t offset) {
    std::lock_guard<std::mutex> lock(mtx);
    if (offset < 0)
      return -EINVAL;
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)) || fd->flags & O_WRONLY)
      return -EBADF;
    struct INode* inode = fd->inode;
    if (S_ISDIR(inode->mode))
      return -EISDIR;
    if (count == 0)
      return 0;
    if (!buf)
      return -EFAULT;
    count = std::min(count, 0x7ffff000UL);
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
        amount = std::min((range->offset + range->size) - (offset + amountRead), count - amountRead);
        memcpy(buf + amountRead, range->data + (offset + amountRead) - range->offset, amount);
      } else {
        struct INode::HoleRange hole = it.GetHole();
        amount = std::min((hole.offset + hole.size) - (offset + amountRead), count - amountRead);
        memset(buf + amountRead, '\0', amount);
      }
      amountRead += amount;
    }
    if (!(fd->flags & O_NOATIME))
      clock_gettime(CLOCK_REALTIME, &inode->atime);
    return count;
  }
  ssize_t PReadv(unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
    std::lock_guard<std::mutex> lock(mtx);
    if (offset < 0)
      return -EINVAL;
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)) || fd->flags & O_WRONLY)
      return -EBADF;
    struct INode* inode = fd->inode;
    if (S_ISDIR(inode->mode))
      return -EISDIR;
    if (iovcnt == 0)
      return 0;
    if (iovcnt < 0 || iovcnt > 1024)
      return -EINVAL;
    if (!iov)
      return -EFAULT;
    size_t totalLen = 0;
    for (int i = 0; i != iovcnt; ++i) {
      size_t len = iov[i].iov_len;
      if (len == 0)
        continue;
      if (len < 0)
        return -EINVAL;
      if (!iov[i].iov_base)
        return -EFAULT;
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
        amount = std::min(
          std::min(
            (range->offset + range->size) - (fd->seekOff + count),
            curr.iov_len - amountRead),
          totalLen - count
        );
        memcpy((char*)curr.iov_base + amountRead, range->data + (fd->seekOff + count) - range->offset, amount);
      } else {
        struct INode::HoleRange hole = it.GetHole();
        amount = std::min(
          std::min(
            (hole.offset + hole.size) - (fd->seekOff + count),
            curr.iov_len - amountRead),
          totalLen - count
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
  ssize_t Write(unsigned int fdNum, const char* buf, size_t count) {
    std::lock_guard<std::mutex> lock(mtx);
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)) || !(fd->flags & (O_WRONLY | O_RDWR)))
      return -EBADF;
    if (count == 0)
      return 0;
    count = std::min(count, 0x7ffff000UL);
    if (!buf)
      return -EFAULT;
    struct INode* inode = fd->inode;
    off_t seekOff = fd->flags & O_APPEND
      ? inode->size
      : fd->seekOff;
    if (seekOff > std::numeric_limits<off_t>::max() - count)
      return -EFBIG;
    struct INode::DataRange* range = inode->AllocData(seekOff, count);
    if (!range)
      return -EIO;
    memcpy(range->data + (seekOff - range->offset), buf, count);
    if (seekOff + count > inode->size)
      inode->size = seekOff + count;
    fd->seekOff += count;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode->mtime = inode->ctime = ts;
    return count;
  }
  ssize_t Writev(unsigned int fdNum, struct iovec* iov, int iovcnt) {
    std::lock_guard<std::mutex> lock(mtx);
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)) || !(fd->flags & (O_WRONLY | O_RDWR)))
      return -EBADF;
    if (iovcnt == 0)
      return 0;
    if (iovcnt < 0 || iovcnt > 1024)
      return -EINVAL;
    if (!iov)
      return -EFAULT;
    ssize_t totalLen = 0;
    for (int i = 0; i != iovcnt; ++i) {
      ssize_t len = (ssize_t)iov[i].iov_len;
      if (len == 0)
        continue;
      if (len < 0)
        return -EINVAL;
      if (!iov[i].iov_base)
        break;
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
    if (seekOff > std::numeric_limits<off_t>::max() - totalLen)
      return -EFBIG;
    struct INode::DataRange* range = inode->AllocData(seekOff, totalLen);
    if (!range)
      return -EIO;
    ssize_t count = 0;
    for (int i = 0; i != iovcnt; ++i) {
      ssize_t len = (ssize_t)iov[i].iov_len;
      if (len == 0)
        continue;
      memcpy(range->data + (seekOff - range->offset) + count, iov[i].iov_base, len);
      count += len;
      if (count == totalLen)
        break;
    }
    if (seekOff + count > inode->size)
      inode->size = seekOff + count;
    fd->seekOff += count;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode->mtime = inode->ctime = ts;
    return count;
  }
  ssize_t PWrite(unsigned int fdNum, const char* buf, size_t count, off_t offset) {
    std::lock_guard<std::mutex> lock(mtx);
    if (offset < 0)
      return -EINVAL;
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)) || !(fd->flags & (O_WRONLY | O_RDWR)))
      return -EBADF;
    if (count == 0)
      return 0;
    count = std::min(count, 0x7ffff000UL);
    if (!buf)
      return -EFAULT;
    struct INode* inode = fd->inode;
    if (offset > std::numeric_limits<off_t>::max() - count)
      return -EFBIG;
    struct INode::DataRange* range = inode->AllocData(offset, count);
    if (!range)
      return -EIO;
    memcpy(range->data + (offset - range->offset), buf, count);
    if (offset + count > inode->size)
      inode->size = offset + count;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode->mtime = inode->ctime = ts;
    return count;
  }
  ssize_t PWritev(unsigned int fdNum, struct iovec* iov, int iovcnt, off_t offset) {
    std::lock_guard<std::mutex> lock(mtx);
    if (offset < 0)
      return -EINVAL;
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)) || !(fd->flags & (O_WRONLY | O_RDWR)))
      return -EBADF;
    if (iovcnt == 0)
      return 0;
    if (iovcnt < 0 || iovcnt > 1024)
      return -EINVAL;
    if (!iov)
      return -EFAULT;
    ssize_t totalLen = 0;
    for (int i = 0; i != iovcnt; ++i) {
      ssize_t len = (ssize_t)iov[i].iov_len;
      if (len == 0)
        continue;
      if (len < 0)
        return -EINVAL;
      if (!iov[i].iov_base)
        break;
      if (len > 0x7ffff000 - totalLen) {
        len = 0x7ffff000 - totalLen;
        iov[i].iov_len = len;
      }
      totalLen += len;
    }
    if (totalLen == 0)
      return 0;
    struct INode* inode = fd->inode;
    if (offset > std::numeric_limits<off_t>::max() - totalLen)
      return -EFBIG;
    struct INode::DataRange* range = inode->AllocData(offset, totalLen);
    if (!range)
      return -EIO;
    ssize_t count = 0;
    for (int i = 0; i != iovcnt; ++i) {
      ssize_t len = (ssize_t)iov[i].iov_len;
      if (len == 0)
        continue;
      memcpy(range->data + (offset - range->offset) + count, iov[i].iov_base, len);
      count += len;
      if (count == totalLen)
        break;
    }
    if (offset + count > inode->size)
      inode->size = offset + count;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode->mtime = inode->ctime = ts;
    return count;
  }
  ssize_t SendFile(unsigned int outFd, unsigned int inFd, off_t* offset, size_t count) {
    std::lock_guard<std::mutex> lock(mtx);
    struct Fd* fdIn;
    struct Fd* fdOut;
    if ((!(fdIn = GetFd(inFd)) || fdIn->flags & O_WRONLY) ||
        (!(fdOut = GetFd(outFd)) || !(fdOut->flags & (O_WRONLY | O_RDWR))))
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
    count = std::min(count, 0x7ffff000UL);
    struct INode* inodeIn = fdIn->inode;
    struct INode* inodeOut = fdOut->inode;
    if (fdOut->seekOff > std::numeric_limits<off_t>::max() - count)
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
          amountRead += std::min(amount, count - amountRead);
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
        amount = std::min(amount, count - amountRead);
        memset(rangeOut->data + (fdOut->seekOff + amountRead) - rangeOut->offset, '\0', amount);
        amountRead += amount;
        continue;
      }
      struct INode::DataRange* rangeIn = itIn.GetRange();
      amount = std::min((rangeIn->offset + rangeIn->size) - (off + amountRead), count - amountRead);
      struct INode::DataRange* rangeOut = inodeOut->AllocData(fdOut->seekOff + amountRead, amount);
      if (!rangeOut)
        return -EIO;
      memcpy(
        rangeOut->data + (fdOut->seekOff + amountRead) - rangeOut->offset,
        rangeIn->data + (off + amountRead) - rangeIn->offset,
        amount
      );
      amountRead += amount;
      itIn.Next();
      off_t rangeOutEnd;
      if (itOut.IsInData()) {
        struct INode::DataRange* rangeOut = itOut.GetRange();
        rangeOutEnd = rangeOut->offset + rangeOut->size;
      } else {
        struct INode::HoleRange holeOut = itOut.GetHole();
        rangeOutEnd = holeOut.offset + holeOut.size;
      }
      if (rangeOutEnd < off + amountRead) {
        while (itOut.Next()) {
          if (itOut.IsInData()) {
            struct INode::DataRange* rangeOut = itOut.GetRange();
            rangeOutEnd = rangeOut->offset + rangeOut->size;
          } else {
            struct INode::HoleRange holeOut = itOut.GetHole();
            rangeOutEnd = holeOut.offset + holeOut.size;
          }
          if (rangeOutEnd >= off + amountRead)
            break;
        }
      }
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (!(fdIn->flags & O_NOATIME))
      inodeIn->atime = ts;
    inodeOut->mtime = inodeOut->ctime = ts;
    return count;
  }
  int FTruncate(unsigned int fdNum, off_t length) {
    std::lock_guard<std::mutex> lock(mtx);
    if (length < 0)
      return -EINVAL;
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    struct INode* inode = fd->inode;
    if (!S_ISREG(inode->mode) || !(fd->flags & (O_WRONLY | O_RDWR)))
      return -EINVAL;
    if (fd->flags & O_APPEND)
      return -EPERM;
    inode->TruncateData(length);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode->ctime = inode->mtime = ts;
    return 0;
  }
  int Truncate(const char* path, off_t length) {
    std::lock_guard<std::mutex> lock(mtx);
    if (length < 0)
      return -EINVAL;
    struct INode* inode;
    int res = GetINode(path, &inode, NULL, true);
    if (res != 0)
      return res;
    if (S_ISDIR(inode->mode))
      return -EISDIR;
    if (!S_ISREG(inode->mode))
      return -EINVAL;
    if (!(inode->mode & 0222))
      return -EACCES;
    inode->TruncateData(length);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode->ctime = inode->mtime = ts;
    return 0;
  }
  int FChModAt(int dirFd, const char* path, mode_t mode) {
    std::lock_guard<std::mutex> lock(mtx);
    struct INode* origCwd = cwd->inode;
    if (dirFd != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      cwd->inode = fd->inode;
    }
    struct INode* inode;
    int res = GetINode(path, &inode);
    cwd->inode = origCwd;
    if (res != 0)
      return res;
    inode->mode = (mode & ~S_IFMT) | (inode->mode & S_IFMT);
    clock_gettime(CLOCK_REALTIME, &inode->ctime);
    return 0;
  }
  int FChMod(unsigned int fdNum, mode_t mode) {
    std::lock_guard<std::mutex> lock(mtx);
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    struct INode* inode = fd->inode;
    inode->mode = (mode & ~S_IFMT) | (inode->mode & S_IFMT);
    clock_gettime(CLOCK_REALTIME, &inode->ctime);
    return 0;
  }
  int ChMod(const char* path, mode_t mode) {
    return FChModAt(AT_FDCWD, path, mode);
  }
  int ChDir(const char* path) {
    std::lock_guard<std::mutex> lock(mtx);
    struct INode* inode;
    struct INode* parent;
    int res = GetINode(path, &inode, &parent, true);
    if (res != 0)
      return res;
    if (!S_ISDIR(inode->mode))
      return -ENOTDIR;
    const char* absPath = AbsolutePath(path);
    if (!absPath)
      return -ENOMEM;
    delete cwd->path;
    cwd->path = absPath;
    cwd->inode = inode;
    cwd->parent = parent;
    return 0;
  }
  int GetCwd(char* buf, size_t size) {
    std::lock_guard<std::mutex> lock(mtx);
    if (cwd->inode != inodes[0]) {
      struct INode* inode;
      struct INode* parent;
      int res = GetINode(cwd->path, &inode, &parent, true);
      if (res != 0)
        return res;
    }
    size_t cwdLen = strlen(cwd->path);
    if (size <= cwdLen)
      return -ERANGE;
    if (buf) {
      memcpy(buf, cwd->path, cwdLen);
      buf[cwdLen] = '\0';
    }
    return cwdLen;
  }
  int FStat(unsigned int fdNum, struct stat* buf) {
    std::lock_guard<std::mutex> lock(mtx);
    struct Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    FillStat(fd->inode, buf);
    return 0;
  }
  int Stat(const char* path, struct stat* buf) {
    std::lock_guard<std::mutex> lock(mtx);
    struct INode* inode;
    int res = GetINode(path, &inode, NULL, true);
    if (res != 0)
      return res;
    FillStat(inode, buf);
    return 0;
  }
  int LStat(const char* path, struct stat* buf) {
    std::lock_guard<std::mutex> lock(mtx);
    struct INode* inode;
    int res = GetINode(path, &inode);
    if (res != 0)
      return res;
    FillStat(inode, buf);
    return 0;
  }
  int Statx(int dirFd, const char* path, int flags, int mask, struct statx* buf) {
    std::lock_guard<std::mutex> lock(mtx);
    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH) || mask & ~STATX_ALL ||
        (flags & AT_EMPTY_PATH && path[0] != '\0'))
      return -EINVAL;
    struct INode* origCwd = cwd->inode;
    if (dirFd != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      cwd->inode = fd->inode;
    }
    struct INode* inode;
    int res = 0;
    if (flags & AT_EMPTY_PATH)
      inode = cwd->inode;
    else res = GetINode(path, &inode, NULL, !(flags & AT_SYMLINK_NOFOLLOW));
    cwd->inode = origCwd;
    if (res != 0)
      return res;
    FillStatx(inode, buf, mask);
    return 0;
  }
  int UTimeNsAt(int dirFd, const char* path, const struct timespec* times, int flags) {
    std::lock_guard<std::mutex> lock(mtx);
    if (flags & ~AT_SYMLINK_NOFOLLOW)
      return -EINVAL;
    struct INode* origCwd = cwd->inode;
    if (dirFd != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      cwd->inode = fd->inode;
    }
    struct INode* inode;
    int res = GetINode(path, &inode, NULL, !(flags & AT_SYMLINK_NOFOLLOW));
    cwd->inode = origCwd;
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
  int FUTimesAt(unsigned int fdNum, const char* path, const struct timeval* times) {
    std::lock_guard<std::mutex> lock(mtx);
    if (times && (
        times[0].tv_usec < 0 || times[0].tv_usec >= 1000000 ||
        times[1].tv_usec < 0 || times[1].tv_usec >= 1000000
      ))
      return -EINVAL;
    struct INode* origCwd = cwd->inode;
    if (fdNum != AT_FDCWD) {
      struct Fd* fd;
      if (!(fd = GetFd(fdNum)))
        return -EBADF;
      cwd->inode = fd->inode;
    }
    struct INode* inode;
    int res = GetINode(path, &inode, NULL, true);
    cwd->inode = origCwd;
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
  bool DumpToFile(const char* filename) {
    std::lock_guard<std::mutex> lock(mtx);
    int fd = creat(filename, 0644);
    if (fd < 0)
      return false;
    if (write(fd, "\x7FVFS", 4) != 4 ||
        write(fd, &inodeCount, sizeof(ino_t)) != sizeof(ino_t)) {
      close(fd);
      return false;
    }
    for (ino_t i = 0; i != inodeCount; ++i) {
      struct INode* inode = inodes[i];
      struct DumpedINode dumped;
      dumped.id = inode->id;
      dumped.size = inode->size;
      dumped.nlink = inode->nlink;
      dumped.mode = inode->mode;
      dumped.btime = inode->btime;
      dumped.ctime = inode->ctime;
      dumped.mtime = inode->mtime;
      dumped.atime = inode->atime;
      if (write(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode)) {
        close(fd);
        return false;
      }
      if (S_ISLNK(inode->mode)) {
        size_t targetLen = strlen(inode->target) + 1;
        if (write(fd, inode->target, targetLen) != targetLen) {
          close(fd);
          return false;
        }
        off_t dataLen = inode->size;
        if (write(fd, inode->dataRanges[0]->data, dataLen) != dataLen) {
          close(fd);
          return false;
        }
      }
      if (S_ISDIR(inode->mode)) {
        if (write(fd, &inode->dentCount, sizeof(off_t)) != sizeof(off_t)) {
          close(fd);
          return false;
        }
        if (write(fd, &inode->dents[1].inode->ndx, sizeof(ino_t)) != sizeof(ino_t)) {
          close(fd);
          return false;
        }
        for (off_t j = 2; j != inode->dentCount; ++j) {
          struct INode::Dent* dent = &inode->dents[j];
          if (write(fd, &dent->inode->ndx, sizeof(ino_t)) != sizeof(ino_t)) {
            close(fd);
            return false;
          }
          size_t nameLen = strlen(dent->name) + 1;
          if (write(fd, dent->name, nameLen) != nameLen) {
            close(fd);
            return false;
          }
        }
      } else if (inode->size != 0) {
        if (write(fd, &inode->dataRangeCount, sizeof(off_t)) != sizeof(off_t)) {
          close(fd);
          return false;
        }
        for (off_t j = 0; j != inode->dataRangeCount; ++j) {
          struct INode::DataRange* range = inode->dataRanges[j];
          if (write(fd, &range->offset, sizeof(off_t)) != sizeof(off_t)) {
            close(fd);
            return false;
          }
          if (write(fd, &range->size, sizeof(off_t)) != sizeof(off_t)) {
            close(fd);
            return false;
          }
          ssize_t written = 0;
          while (written != range->size) {
            size_t amount = range->size - written;
            if (amount > 0x7ffff000)
              amount = 0x7ffff000;
            ssize_t count = write(fd, range->data + written, amount);
            if (count < 0) {
              close(fd);
              return false;
            }
            written += count;
          }
        }
      }
    }
    close(fd);
    return true;
  }
  static FileSystem* LoadFromFile(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
      return NULL;
    char magic[4];
    ino_t inodeCount;
    if (read(fd, magic, 4) != 4 ||
        memcmp(magic, "\x7FVFS", 4) != 0 ||
        read(fd, &inodeCount, sizeof(ino_t)) != sizeof(ino_t)) {
      close(fd);
      return NULL;
    }
    struct INode** inodes = (struct INode**)malloc(sizeof(struct INode*) * inodeCount);
    if (!inodes) {
      close(fd);
      return NULL;
    }
    for (ino_t i = 0; i != inodeCount; ++i) {
      struct INode* inode = (struct INode*)malloc(sizeof(struct INode));
      if (!inode) {
        close(fd);
        for (ino_t j = 0; j != i; ++j)
          delete inodes[j];
        free(inodes);
        return NULL;
      }
      inode->ndx = i;
      struct DumpedINode dumped;
      if (read(fd, &dumped, sizeof(struct DumpedINode)) != sizeof(struct DumpedINode)) {
        close(fd);
        for (ino_t j = 0; j != i; ++j)
          delete inodes[j];
        free(inodes);
        free(inode);
        return NULL;
      }
      inode->id = dumped.id;
      inode->size = dumped.size;
      inode->nlink = dumped.nlink;
      inode->mode = dumped.mode;
      inode->btime = dumped.btime;
      inode->ctime = dumped.ctime;
      inode->mtime = dumped.mtime;
      inode->atime = dumped.atime;
      if (S_ISLNK(inode->mode)) {
        char target[PATH_MAX];
        size_t targetLen = 0;
        do {
          if (read(fd, &target[targetLen], 1) != 1) {
            close(fd);
            for (ino_t j = 0; j != i; ++j)
              delete inodes[j];
            free(inodes);
            free(inode);
            return NULL;
          }
        } while (target[targetLen++] != '\0');
        inode->target = strdup(target);
        if (!inode->target) {
          close(fd);
          for (ino_t j = 0; j != i; ++j)
            delete inodes[j];
          free(inodes);
          free(inode);
          return NULL;
        }
        char data[PATH_MAX];
        size_t dataLen = 0;
        do {
          if (read(fd, &data[dataLen], 1) != 1) {
            close(fd);
            free((void*)inode->target);
            for (ino_t j = 0; j != i; ++j)
              delete inodes[j];
            free(inodes);
            free(inode);
            return NULL;
          }
        } while (data[dataLen++] != '\0');
        if (!inode->AllocData(0, dataLen)) {
          close(fd);
          free((void*)inode->target);
          for (ino_t j = 0; j != i; ++j)
            delete inodes[j];
          free(inodes);
          free(inode);
          return NULL;
        }
        memcpy(inode->dataRanges[0]->data, data, dataLen);
      } else inode->target = NULL;
      if (S_ISDIR(inode->mode)) {
        if (read(fd, &inode->dentCount, sizeof(off_t)) != sizeof(off_t)) {
          close(fd);
          for (ino_t j = 0; j != i; ++j)
            delete inodes[j];
          free(inodes);
          free(inode);
          return NULL;
        }
        inode->dents = (struct INode::Dent*)malloc(sizeof(struct INode::Dent) * inode->dentCount);
        if (!inode->dents) {
          close(fd);
          for (ino_t j = 0; j != i; ++j)
            delete inodes[j];
          free(inodes);
          free(inode);
          return NULL;
        }
        inode->dents[0] = { ".", inode };
        inode->dents[1].name = "..";
        if (read(fd, &inode->dents[1].inode, sizeof(ino_t)) != sizeof(ino_t)) {
          close(fd);
          free(inode->dents);
          for (ino_t j = 0; j != i; ++j)
            delete inodes[j];
          free(inodes);
          free(inode);
          return NULL;
        }
        for (off_t j = 2; j != inode->dentCount; ++j) {
          struct INode::Dent* dent = &inode->dents[j];
          if (read(fd, &dent->inode, sizeof(ino_t)) != sizeof(ino_t)) {
            close(fd);
            for (off_t k = 2; k != j; ++k)
              free((void*)inode->dents[k].name);
            free(inode->dents);
            for (ino_t j = 0; j != i; ++j)
              delete inodes[j];
            free(inodes);
            free(inode);
            return NULL;
          }
          char name[PATH_MAX];
          size_t nameLen = 0;
          do {
            if (read(fd, &name[nameLen], 1) != 1) {
              close(fd);
              for (off_t k = 2; k != j; ++k)
                free((void*)inode->dents[k].name);
              free(inode->dents);
              for (ino_t j = 0; j != i; ++j)
                delete inodes[j];
              free(inodes);
              free(inode);
              return NULL;
            }
          } while (name[nameLen++] != '\0');
          dent->name = strdup(name);
          if (!dent->name) {
            close(fd);
            for (off_t k = 2; k != j; ++k)
              free((void*)inode->dents[k].name);
            free(inode->dents);
            for (ino_t j = 0; j != i; ++j)
              delete inodes[j];
            free(inodes);
            free(inode);
            return NULL;
          }
        }
        inode->dataRangeCount = 0;
        inode->dataRanges = NULL;
      } else  {
        inode->dents = NULL;
        inode->dentCount = 0;
      }
      if (S_ISREG(inode->mode)) {
        inode->dataRangeCount = 0;
        inode->dataRanges = NULL;
        if (inode->size != 0) {
          off_t dataRangeCount;
          if (read(fd, &dataRangeCount, sizeof(off_t)) != sizeof(off_t)) {
            close(fd);
            for (ino_t j = 0; j != i; ++j)
              delete inodes[j];
            free(inodes);
            free(inode);
            return NULL;
          }
          inode->dataRanges = (struct INode::DataRange**)malloc(sizeof(struct INode::DataRange*) * dataRangeCount);
          if (!inode->dataRanges) {
            close(fd);
            for (ino_t j = 0; j != i; ++j)
              delete inodes[j];
            free(inodes);
            free(inode);
            return NULL;
          }
          inode->dataRangeCount = dataRangeCount;
          for (off_t j = 0; j != dataRangeCount; ++j) {
            off_t offset;
            off_t size;
            if (read(fd, &offset, sizeof(off_t)) != sizeof(off_t) ||
                read(fd, &size, sizeof(off_t)) != sizeof(off_t)) {
              close(fd);
              for (ino_t j = 0; j != i; ++j)
                delete inodes[j];
              free(inodes);
              free(inode);
              return NULL;
            }
            struct INode::DataRange* range = (struct INode::DataRange*)malloc(sizeof(struct INode::DataRange));
            if (!range) {
              close(fd);
              for (ino_t j = 0; j != i; ++j)
                delete inodes[j];
              free(inodes);
              free(inode);
              return NULL;
            }
            range->offset = offset;
            range->size = size;
            range->data = new char[size];
            if (!range->data) {
              close(fd);
              free(range);
              for (ino_t j = 0; j != i; ++j)
                delete inodes[j];
              free(inodes);
              free(inode);
              return NULL;
            }
            ssize_t nread = 0;
            while (nread != size) {
              size_t amount = size - nread;
              if (amount > 0x7ffff000)
                amount = 0x7ffff000;
              ssize_t count = read(fd, range->data + nread, amount);
              if (count != amount) {
                close(fd);
                delete range;
                for (ino_t j = 0; j != i; ++j)
                  delete inodes[j];
                free(inodes);
                free(inode);
                return NULL;
              }
              nread += count;
            }
            inode->dataRanges[j] = range;
          }
        }
      }
      inodes[i] = inode;
    }
    for (ino_t i = 0; i != inodeCount; ++i) {
      struct INode* inode = inodes[i];
      if (S_ISDIR(inode->mode)) {
        for (off_t j = 1; j != inode->dentCount; ++j) {
          struct INode::Dent* dent = &inode->dents[j];
          if ((ino_t)dent->inode >= inodeCount) {
            close(fd);
            for (ino_t j = 0; j != inodeCount; ++j)
              delete inodes[j];
            free(inodes);
            return NULL;
          }
          dent->inode = inodes[(ino_t)dent->inode];
        }
      }
    }
    for (ino_t i = 0; i != inodeCount;) {
      struct INode* inode = inodes[i];
      if (inode->nlink == 0) {
        delete inode;
        if (i != inodeCount - 1) {
          memmove(inodes + i, inodes + i + 1, sizeof(struct INode*) * (inodeCount - i - 1));
          for (ino_t j = i; j != inodeCount - 1; ++j)
            --inodes[j]->ndx;
        }
        inodes = reinterpret_cast<struct INode**>(
          realloc(inodes, sizeof(struct INode*) * --inodeCount)
        );
      } else ++i;
    }
    close(fd);
    FileSystem* fs = (FileSystem*)malloc(sizeof(FileSystem));
    if (!fs) {
      for (ino_t i = 0; i != inodeCount; ++i)
        delete inodes[i];
      free(inodes);
      return NULL;
    }
    memset(fs, '\0', sizeof(FileSystem));
    fs->inodes = inodes;
    fs->inodeCount = inodeCount;
    fs->fds = {};
    struct Cwd* cwd = (struct Cwd*)malloc(sizeof(struct Cwd));
    if (!cwd) {
      for (ino_t i = 0; i != inodeCount; ++i)
        delete inodes[i];
      free(inodes);
      free(fs);
      return NULL;
    }
    cwd->path = strdup("/");
    if (!cwd->path) {
      free(cwd);
      for (ino_t i = 0; i != inodeCount; ++i)
        delete inodes[i];
      free(inodes);
      free(fs);
      return NULL;
    }
    cwd->inode = inodes[0];
    cwd->parent = inodes[0];
    fs->cwd = cwd;
    return fs;
  }

 private:
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
      if (!TryRealloc(&dents, dentCount + 1))
        return false;
      dents[dentCount++] = { name, inode };
      size += strlen(name) * 2;
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
          size -= strlen(name) * 2;
          break;
        }
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
          for (off_t i = 0; i != inode->dataRangeCount; ++i) {
            struct DataRange* range = inode->dataRanges[i];
            if (offset >= range->offset) {
              if (offset < range->offset + range->size) {
                rangeIdx_ = i;
                atData_ = true;
                break;
              }
            } else {
              rangeIdx_ = i;
              atData_ = false;
              break;
            }
          }
        }
      }
      bool IsInData() {
        return atData_;
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
          if (rangeIdx_ >= inode_->dataRangeCount - 1)
            return false;
          if (isBeforeFirstRange_)
            isBeforeFirstRange_ = false;
          else ++rangeIdx_;
        }
        atData_ = !atData_;
        return true;
      }
     private:
      struct INode* inode_;
      off_t rangeIdx_;
      bool atData_;
      bool isBeforeFirstRange_;
    };

    struct DataRange* InsertRange(off_t offset, off_t size, off_t* index) {
      struct DataRange* range = NULL;
      if (dataRangeCount == 0) {
        if (!TryAlloc(&range))
          return NULL;
        if (!TryAlloc(&range->data, size) ||
            !TryAlloc(&dataRanges, 1)) {
          delete range;
          return NULL;
        }
        range->offset = offset;
        range->size = size;
        *index = 0;
        dataRanges[0] = range;
        dataRangeCount = 1;
        return range;
      }
      if (offset > dataRanges[dataRangeCount - 1]->offset) {
        if (!TryAlloc(&range))
          return NULL;
        if (!TryAlloc(&range->data, size) ||
            !TryRealloc(&dataRanges, dataRangeCount + 1)) {
          delete range;
          return NULL;
        }
        range->offset = offset;
        range->size = size;
        *index = dataRangeCount;
        dataRanges[dataRangeCount++] = range;
        return range;
      }
      for (off_t i = 0; i != dataRangeCount; ++i)
        if (offset < dataRanges[i]->offset) {
          if (!TryAlloc(&range))
            return NULL;
          if (!TryAlloc(&range->data, size) ||
              !TryRealloc(&dataRanges, dataRangeCount + 1)) {
            delete range;
            return NULL;
          }
          range->offset = offset;
          range->size = size;
          memmove(dataRanges + i + 1, dataRanges + i, sizeof(struct DataRange*) * (dataRangeCount - i));
          *index = i;
          dataRanges[i] = range;
          ++dataRangeCount;
          break;
        }
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

    struct DataRange* AllocData(off_t offset, off_t length) {
      off_t rangeIdx = 0;
      bool createdRange = false;
      struct DataRange* range = NULL;
      for (off_t i = 0; i != dataRangeCount; ++i) {
        struct DataRange* range2 = dataRanges[i];
        if (offset >= range2->offset) {
          if (offset <= range2->offset + range2->size) {
            rangeIdx = i;
            range = range2;
            break;
          }
        } else break;
      }
      if (!range) {
        range = InsertRange(offset, length, &rangeIdx);
        if (!range)
          return NULL;
        createdRange = true;
      } else if (offset + length < range->offset + range->size)
        return range;
      off_t newRangeLength = range->size + ((offset + length) - (range->offset + range->size));
      for (off_t i = rangeIdx + 1; i < dataRangeCount; ++i) {
        struct DataRange* range2 = dataRanges[i];
        if (range2->offset < offset + length)
          newRangeLength = std::max(newRangeLength, (range2->offset + range2->size) - range->offset);
        else break;
      }
      if (!TryRealloc(&range->data, newRangeLength)) {
        if (createdRange)
          RemoveRange(rangeIdx);
        return NULL;
      }
      range->size = newRangeLength;
      if (offset + length > size)
        size = offset + length;
      for (off_t i = rangeIdx + 1; i < dataRangeCount;) {
        struct DataRange* range2 = dataRanges[i];
        if (range2->offset < offset + length) {
          memcpy(range->data + (range2->offset - range->offset), range2->data, range2->size);
          RemoveRange(i);
        } else break;
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
      for (off_t i = 0; i != dataRangeCount; ++i) {
        struct DataRange* range = dataRanges[i];
        if (length > range->offset &&
            length < range->offset + range->size) {
          for (off_t j = i + 1; j != dataRangeCount; ++j)
            delete dataRanges[j];
          dataRanges = reinterpret_cast<struct DataRange**>(
            realloc(dataRanges, sizeof(struct DataRange*) * (i + 1))
          );
          dataRangeCount = i + 1;
          range->data = reinterpret_cast<char*>(
            realloc(range->data, length - range->offset)
          );
          range->size = length - range->offset;
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
  };
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
  struct Fd {
    struct INode* inode;
    int fd;
    int flags;
    off_t seekOff = 0;
  };
  struct Cwd {
    ~Cwd() {
      delete path;
    }
    const char* path;
    struct INode* inode;
    struct INode* parent;
  };
  struct Cwd* cwd;
  struct INode** inodes = {};
  ino_t inodeCount = 0;
  struct Fd** fds = {};
  int fdCount = 0;
  std::mutex mtx;
  bool PushINode(struct INode* inode) {
    ino_t id = inodeCount;
    for (ino_t i = 0; i != inodeCount; ++i)
      if (inodes[i]->id != i) {
        id = i;
        break;
      }
    if (!TryRealloc(&inodes, sizeof(struct INode*) * (inodeCount + 1)))
      return false;
    if (id != inodeCount) {
      memmove(inodes + id + 1, inodes + id, sizeof(struct INode*) * (inodeCount - id));
      for (ino_t i = id + 1; i != inodeCount + 1; ++i)
        ++inodes[i]->ndx;
    }
    inode->ndx = id;
    inode->id = id;
    inodes[id] = inode;
    ++inodeCount;
    return true;
  }
  void RemoveINode(struct INode* inode) {
    ino_t i = inode->ndx;
    delete inode;
    if (i != inodeCount - 1) {
      memmove(inodes + i, inodes + i + 1, sizeof(struct INode*) * (inodeCount - i));
      do {
        --inodes[i++]->ndx;
      } while (i != inodeCount - 1);
    }
    inodes = reinterpret_cast<struct INode**>(
      realloc(inodes, sizeof(struct INode*) * --inodeCount)
    );
  }
  int PushFd(struct INode* inode, int flags) {
    if (fdCount == std::numeric_limits<int>::max())
      return -ENFILE;
    int fdNum = fdCount;
    for (int i = 0; i != fdCount; ++i)
      if (fds[i]->fd != i) {
        fdNum = i;
        break;
      }
    struct Fd* fd;
    if (!TryRealloc(&fds, fdCount + 1))
      return -ENOMEM;
    if (!TryAlloc(&fd)) {
      fds = reinterpret_cast<struct Fd**>(
        realloc(fds, sizeof(struct Fd*) * fdCount)
      );
      return -ENOMEM;
    }
    fd->inode = inode;
    fd->flags = flags;
    fd->fd = fdNum;
    fds[fdCount++] = fd;
    return fdNum;
  }
  int RemoveFd(unsigned int fd) {
    for (int i = 0; i != fdCount; ++i)
      if (fds[i]->fd == fd) {
        if (fds[i]->inode->nlink == 0)
          RemoveINode(fds[i]->inode);
        delete fds[i];
        if (i != fdCount - 1)
          memmove(fds + i, fds + i + 1, sizeof(struct Fd*) * (fdCount - i));
        fds = reinterpret_cast<struct Fd**>(
          realloc(fds, sizeof(struct Fd*) * --fdCount)
        );
        return 0;
      }
    return -EBADF;
  }
  struct Fd* GetFd(unsigned int fdNum) {
    for (int i = 0; i != fdCount; ++i)
      if (fds[i]->fd == fdNum)
        return fds[i];
    return NULL;
  }
  int GetINode(
    const char* path,
    struct INode** inode,
    struct INode** parent = NULL,
    bool followResolved = false,
    int followCount = 0
  ) {
    size_t pathLen = strlen(path);
    if (pathLen == 0)
      return -ENOENT;
    if (pathLen >= PATH_MAX)
      return -ENAMETOOLONG;
    bool isAbsolute = path[0] == '/';
    struct INode* current = isAbsolute
      ? inodes[0]
      : cwd->inode;
    struct INode* currParent = isAbsolute
      ? inodes[0]
      : cwd->parent;
    int err = 0;
    char name[NAME_MAX + 1];
    size_t nameLen = 0;
    for (size_t i = 0; i != pathLen; ++i) {
      if (path[i] == '/') {
        if (nameLen == 0)
          continue;
        if (err)
          return err;
        off_t j = 0;
        for (; j != current->dentCount; ++j)
          if (strcmp(current->dents[j].name, name) == 0)
            break;
        if (j == current->dentCount) {
          err = -ENOENT;
          goto resetName;
        }
        currParent = current;
        current = current->dents[j].inode;
        if (S_ISLNK(current->mode)) {
          if (followCount++ == 40) {
            err = -ELOOP;
            goto resetName;
          }
          struct INode* targetParent;
          int res = GetINode(current->target, &current, &targetParent, true, followCount);
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
        *parent = current;
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
        if (followCount++ == 40)
          return -ELOOP;
        struct INode* targetParent;
        int res = GetINode(current->target, &current, &targetParent, true, followCount);
        if (res != 0)
          return res;
      }
    }
    *inode = current;
    return 0;
  }
  static bool IsInDir(struct INode* dir, struct INode* inode) {
    for (off_t i = 2; i != dir->dentCount; ++i)
      if (dir->dents[i].inode == inode ||
          (S_ISDIR(dir->dents[i].inode->mode) && IsInDir(dir->dents[i].inode, inode)))
        return true;
    return false;
  }
  static const char* GetLast(const char* path) {
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
  const char* AbsolutePath(const char* path) {
    char absPath[PATH_MAX];
    int absPathLen = 0;
    if (path[0] != '/') {
      int cwdPathLen = strlen(cwd->path);
      if (cwdPathLen != 1) {
        memcpy(absPath, cwd->path, cwdPathLen);
        absPath[cwdPathLen] = '/';
        absPathLen = cwdPathLen + 1;
      } else absPath[absPathLen++] = '/';
    }
    int pathLen = strlen(path);
    for (int i = 0; i != pathLen; ++i) {
      if (path[i] == '/') {
        if (absPath[absPathLen - 1] != '/')
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
  const char* GetAbsoluteLast(const char* path) {
    const char* absPath = AbsolutePath(path);
    if (!absPath)
      return NULL;
    const char* last = GetLast(absPath);
    delete absPath;
    return last;
  }
  template<typename T>
  static bool TryAlloc(T** ptr, size_t length = 1) {
    T* newPtr;
    if (length == 1)
      newPtr = new T;
    else newPtr = new T[length];
    if (!newPtr)
      return false;
    *ptr = newPtr;
    return true;
  }
  template<typename T>
  static bool TryRealloc(T** ptr, size_t length) {
    T* newPtr = (T*)realloc(*ptr, sizeof(T) * length);
    if (!newPtr)
      return false;
    *ptr = newPtr;
    return true;
  }
  static void FillStat(struct INode* inode, struct stat* buf) {
    memset(buf, '\0', sizeof(struct stat));
    buf->st_ino = inode->id;
    buf->st_mode = inode->mode;
    buf->st_nlink = inode->nlink;
    buf->st_size = inode->size;
    buf->st_atim = inode->atime;
    buf->st_mtim = inode->mtime;
    buf->st_ctim = inode->ctime;
  }
  static void FillStatx(struct INode* inode, struct statx* buf, int mask) {
    memset(buf, '\0', sizeof(struct statx));
    buf->stx_mask = mask & (
      STATX_INO   | STATX_TYPE  | STATX_MODE  |
      STATX_NLINK | STATX_SIZE  | STATX_ATIME |
      STATX_MTIME | STATX_CTIME | STATX_BTIME
    );
    if (mask & STATX_INO)
      buf->stx_ino = inode->id;
    if (mask & STATX_TYPE)
      buf->stx_mode |= inode->mode & S_IFMT;
    if (mask & STATX_MODE)
      buf->stx_mode |= inode->mode & ~S_IFMT;
    if (mask & STATX_NLINK)
      buf->stx_nlink = inode->nlink;
    if (mask & STATX_SIZE)
      buf->stx_size = inode->size;
    if (mask & STATX_ATIME) {
      buf->stx_atime.tv_sec = inode->atime.tv_sec;
      buf->stx_atime.tv_nsec = inode->atime.tv_nsec;
    }
    if (mask & STATX_MTIME) {
      buf->stx_mtime.tv_sec = inode->mtime.tv_sec;
      buf->stx_mtime.tv_nsec = inode->mtime.tv_nsec;
    }
    if (mask & STATX_CTIME) {
      buf->stx_ctime.tv_sec = inode->ctime.tv_sec;
      buf->stx_ctime.tv_nsec = inode->ctime.tv_nsec;
    }
    if (mask & STATX_BTIME) {
      buf->stx_btime.tv_sec = inode->btime.tv_sec;
      buf->stx_btime.tv_nsec = inode->btime.tv_nsec;
    }
  }
};