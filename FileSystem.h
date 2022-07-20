#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct linux_dirent {
  ino_t d_ino;
  off_t d_off;
  unsigned short d_reclen;
  char d_name[1];
};

class FileSystem {
 public:
  FileSystem() {
    INode* root = new INode;
    root->mode = 0755 | S_IFDIR;
    root->parent = root;
    root->dents = new INode::Dent[2];
    root->dents[0] = { ".", root };
    root->dents[1] = { "..", root };
    root->dentCount = root->nlink = 2;
    PushINode(root);
    char* rootPath = new char[2];
    rootPath[0] = '/';
    rootPath[1] = '\0';
    cwd = { rootPath, root };
  }
  ~FileSystem() {
    for (ino_t i = 0; i != inodeCount; ++i)
      delete inodes[i];
    delete inodes;
    delete fds;
  }

  int FAccessAt(int dirFd, const char* path, int mode, int flags) {
    if (mode & ~S_IRWXO || flags & ~AT_SYMLINK_NOFOLLOW)
      return -EINVAL;
    INode* origCwd = cwd.inode;
    if (dirFd != AT_FDCWD) {
      Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd.inode = fd->inode;
    }
    INode* inode;
    INode* parent = NULL;
    int res = GetINode(path, &inode, &parent, !(flags & AT_SYMLINK_NOFOLLOW));
    cwd.inode = origCwd;
    if (res != 0)
      return res;
    if ((mode & X_OK && !(inode->mode & 0111)) ||
        (mode & W_OK && !(inode->mode & 0222)) ||
        (mode & R_OK && !(inode->mode & 0444)))
      return -EACCES;
    return 0;
  }
  int Access(const char* path, int mode) {
    return FAccessAt(AT_FDCWD, path, mode, 0);
  }
  int OpenAt(int dirFd, const char* path, int flags, mode_t mode) {
    if (flags & ~(O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_APPEND | O_TRUNC | O_DIRECTORY | O_NOFOLLOW | O_NOATIME))
      return -EINVAL;
    if (flags & O_CREAT) {
      if (mode & ~0777)
        return -EINVAL;
      mode |= S_IFREG;
    } else if (mode != 0)
      return -EINVAL;
    INode* origCwd = cwd.inode;
    if (dirFd != AT_FDCWD) {
      Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd.inode = fd->inode;
    }
    if (flags & O_CREAT && flags & O_EXCL)
      flags |= O_NOFOLLOW;
    if (flags & O_WRONLY && flags & O_RDWR)
      flags &= ~O_RDWR;
    INode* inode;
    INode* parent = NULL;
    int res = GetINode(path, &inode, &parent, !(flags & O_NOFOLLOW));
    cwd.inode = origCwd;
    if (parent == NULL)
      return res;
    if (res == 0) {
      if (flags & O_NOFOLLOW && S_ISLNK(inode->mode))
        return -ELOOP;
      if (flags & O_CREAT && flags & O_EXCL)
        return -EEXIST;
    } else {
      if (flags & O_CREAT && res == -ENOENT) {
        if (inodeCount == std::numeric_limits<ino_t>::max())
          return -EDQUOT;
        const char* absPath = AbsolutePath(path);
        const char* name = GetName(absPath);
        delete absPath;
        if (parent->size > std::numeric_limits<size_t>::max() - (strlen(name) * 2)) {
          delete name;
          return -ENOSPC;
        }
        INode* x = new INode;
        x->parent = parent;
        x->mode = mode;
        x->nlink = 1;
        PushINode(x);
        parent->PushDent(name, x);
        parent->ctime = parent->mtime = x->btime;
        inode = x;
      } else return res;
    }
    if (S_ISDIR(inode->mode)) {
      if (flags & (O_WRONLY | O_RDWR))
        return -EISDIR;
    } else {
      if (flags & O_DIRECTORY)
        return -ENOTDIR;
      if (flags & O_TRUNC && inode->size != 0) {
        inode->data = reinterpret_cast<char*>(
          realloc(inode->data, 1)
        );
        inode->data[0] = '\0';
        inode->size = 0;
      }
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
    return RemoveFd(fd);
  }
  int MkNodAt(int dirFd, const char* path, mode_t mode, dev_t) {
    if (inodeCount == std::numeric_limits<ino_t>::max())
      return -EDQUOT;
    if ((mode & S_IFMT) == 0)
      mode |= S_IFREG;
    if ((mode & S_IFMT) != S_IFREG)
      return -EINVAL;
    INode* origCwd = cwd.inode;
    if (dirFd != AT_FDCWD) {
      Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd.inode = fd->inode;
    }
    INode* inode;
    INode* parent = NULL;
    int res = GetINode(path, &inode, &parent);
    cwd.inode = origCwd;
    if (parent == NULL)
      return res;
    if (res == 0)
      return -EEXIST;
    const char* absPath = AbsolutePath(path);
    const char* name = GetName(absPath);
    delete absPath;
    if (parent->size > std::numeric_limits<size_t>::max() - (strlen(name) * 2)) {
      delete name;
      return -ENOSPC;
    }
    INode* x = new INode;
    x->parent = parent;
    x->mode = (mode & ~S_IFMT) | S_IFREG;
    x->data = new char;
    x->nlink = 1;
    PushINode(x);
    parent->PushDent(name, x);
    parent->ctime = parent->mtime = x->btime;
    return 0;
  }
  int MkNod(const char* path, mode_t mode, dev_t) {
    return MkNodAt(AT_FDCWD, path, mode, 0);
  }
  int MkDirAt(int dirFd, const char* path, mode_t mode) {
    if (inodeCount == std::numeric_limits<ino_t>::max())
      return -EDQUOT;
    INode* origCwd = cwd.inode;
    if (dirFd != AT_FDCWD) {
      Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd.inode = fd->inode;
    }
    INode* inode;
    INode* parent = NULL;
    int res = GetINode(path, &inode, &parent);
    cwd.inode = origCwd;
    if (parent == NULL)
      return res;
    if (res == 0)
      return -EEXIST;
    const char* absPath = AbsolutePath(path);
    const char* name = GetName(absPath);
    delete absPath;
    if (parent->size > std::numeric_limits<size_t>::max() - (strlen(name) * 2)) {
      delete name;
      return -ENOSPC;
    }
    INode* x = new INode;
    x->parent = parent;
    x->mode = (mode & ~S_IFMT) | S_IFDIR;
    x->nlink = 2;
    x->dents = new INode::Dent[2];
    x->dents[0] = { ".", x };
    x->dents[1] = { "..", parent };
    x->dentCount = 2;
    PushINode(x);
    parent->PushDent(name, x);
    ++parent->nlink;
    parent->ctime = parent->mtime = x->btime;
    return 0;
  }
  int MkDir(const char* path, mode_t mode) {
    return MkDirAt(AT_FDCWD, path, mode);
  }
  int SymlinkAt(const char* oldPath, int newDirFd, const char* newPath) {
    if (inodeCount == std::numeric_limits<ino_t>::max())
      return -EDQUOT;
    INode* oldInode;
    INode* parent;
    int res = GetINode(oldPath, &oldInode, &parent);
    if (res != 0)
      return res;
    INode* origCwd = cwd.inode;
    if (newDirFd != AT_FDCWD) {
      Fd* fd;
      if (!(fd = GetFd(newDirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd.inode = fd->inode;
    }
    INode* newInode;
    parent = NULL;
    res = GetINode(newPath, &newInode, &parent);
    cwd.inode = origCwd;
    if (parent == NULL)
      return res;
    const char* absPath = AbsolutePath(newPath);
    const char* name = GetName(absPath);
    delete absPath;
    if (parent->size > std::numeric_limits<size_t>::max() - (strlen(name) * 2)) {
      delete name;
      return -ENOSPC;
    }
    INode* x = new INode;
    x->parent = parent;
    x->mode = 0777 | S_IFLNK;
    x->target = oldInode;
    x->nlink = 1;
    size_t oldPathLen = strlen(oldPath);
    x->data = new char[oldPathLen + 1];
    memcpy(x->data, oldPath, oldPathLen);
    x->data[oldPathLen] = '\0';
    x->size = oldPathLen;
    PushINode(x);
    parent->PushDent(name, x);
    ++oldInode->nsymlink;
    parent->ctime = parent->mtime = x->btime;
    return 0;
  }
  int Symlink(const char* oldPath, const char* newPath) {
    return SymlinkAt(oldPath, AT_FDCWD, newPath);
  }
  int ReadLinkAt(int dirFd, const char* path, char* buf, int bufLen) {
    if (bufLen <= 0)
      return -EINVAL;
    INode* origCwd = cwd.inode;
    if (dirFd != AT_FDCWD) {
      Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      if (!S_ISDIR(fd->inode->mode))
        return -ENOTDIR;
      cwd.inode = fd->inode;
    }
    INode* inode;
    INode* parent;
    int res = GetINode(path, &inode, &parent);
    cwd.inode = origCwd;
    if (res != 0)
      return res;
    if (!S_ISLNK(inode->mode))
      return -EINVAL;
    if (inode->size < bufLen)
      bufLen = inode->size;
    memcpy(buf, inode->data, bufLen);
    clock_gettime(CLOCK_REALTIME, &inode->atime);
    return bufLen;
  }
  int ReadLink(const char* path, char* buf, int bufLen) {
    return ReadLinkAt(AT_FDCWD, path, buf, bufLen);
  }
  int GetDents(unsigned int fdNum, struct linux_dirent* dirp, unsigned int count) {
    Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    if (!S_ISDIR(fd->inode->mode))
      return -ENOTDIR;
    if (fd->seekOff >= fd->inode->dentCount)
      return 0;
    unsigned int nread = 0;
    char* dirpData = (char*)dirp;
    for (unsigned int j = 0; j != count && fd->seekOff != fd->inode->dentCount; ++j, ++fd->seekOff) {
      INode::Dent d = fd->inode->dents[fd->seekOff];
      size_t nameLen = strlen(d.name);
#define ALIGN(x, a) (((x) + ((typeof(x))(a) - 1)) & ~((typeof(x))(a) - 1))
      unsigned short reclen = ALIGN(__builtin_offsetof(struct linux_dirent, d_name) + nameLen + 2, sizeof(long));
#undef ALIGN
      if (j == 0 && count < reclen)
        return -EINVAL;
      if (nread > count - reclen)
        return nread;
      struct linux_dirent* dent = (struct linux_dirent*)dirpData;
      dent->d_ino = d.inode->id;
      dent->d_off = fd->seekOff + 1;
      dent->d_reclen = reclen;
      memcpy(dent->d_name, d.name, nameLen);
      dent->d_name[nameLen] = '\0';
      dirpData[reclen - 1] = (d.inode->mode & S_IFMT) >> 12;
      dirpData += reclen;
      nread += reclen;
    }
    if (!(fd->flags & O_NOATIME))
      clock_gettime(CLOCK_REALTIME, &fd->inode->atime);
    return nread;
  }
  int LinkAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, int flags) {
    if ((flags & ~AT_SYMLINK_FOLLOW) != 0)
      return -EINVAL;
    Fd* oldFd;
    Fd* newFd;
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
    INode* origCwd = cwd.inode;
    if (oldDirFd != AT_FDCWD)
      cwd.inode = oldFd->inode;
    INode* oldInode;
    INode* parent;
    int res = GetINode(oldPath, &oldInode, &parent, flags & AT_SYMLINK_FOLLOW);
    cwd.inode = origCwd;
    if (res != 0)
      return res;
    if (newDirFd != AT_FDCWD)
      cwd.inode = newFd->inode;
    INode* newInode;
    parent = NULL;
    res = GetINode(newPath, &newInode, &parent);
    cwd.inode = origCwd;
    if (parent == NULL)
      return res;
    if (res == 0)
      return -EEXIST;
    if (S_ISDIR(oldInode->mode))
      return -EPERM;
    const char* absPath = AbsolutePath(newPath);
    const char* name = GetName(absPath);
    delete absPath;
    if (parent->size > std::numeric_limits<size_t>::max() - (strlen(name) * 2)) {
      delete name;
      return -ENOSPC;
    }
    parent->PushDent(name, oldInode);
    ++oldInode->nlink;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    oldInode->ctime = ts;
    parent->ctime = parent->mtime = ts;
    return 0;
  }
  int Link(const char* oldPath, const char* newPath) {
    return LinkAt(AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
  }
  int UnlinkAt(int dirFd, const char* path, int flags) {
    if ((flags & ~AT_REMOVEDIR) != 0)
      return -EINVAL;
    INode* origCwd = cwd.inode;
    if (dirFd != AT_FDCWD) {
      Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      cwd.inode = fd->inode;
    }
    int res = flags & AT_REMOVEDIR ? RmDir(path) : Unlink(path);
    cwd.inode = origCwd;
    return res;
  }
  int Unlink(const char* path) {
    INode* inode;
    INode* parent;
    int res = GetINode(path, &inode, &parent);
    if (res != 0)
      return res;
    if (S_ISDIR(inode->mode))
      return -EISDIR;
    for (int i = 0; i != fdCount; ++i)
      if (fds[i]->inode == inode)
        return -EBUSY;
    const char* absPath = AbsolutePath(path);
    const char* name = GetName(absPath);
    delete absPath;
    parent->RemoveDent(name);
    delete name;
    if (S_ISLNK(inode->mode) &&
        --inode->target->nsymlink == 0 && inode->target->nlink == 0)
      RemoveINode(inode->target);
    if (--inode->nlink == 0 && inode->nsymlink == 0)
      RemoveINode(inode);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    parent->ctime = parent->mtime = ts;
    return 0;
  }
  int RmDir(const char* path) {
    INode* inode;
    INode* parent;
    int res = GetINode(path, &inode, &parent);
    if (res != 0)
      return res;
    if (!S_ISDIR(inode->mode))
      return -ENOTDIR;
    if (inode == inodes[0])
      return -EBUSY;
    for (int i = 0; i != fdCount; ++i)
      if (fds[i]->inode == inode)
        return -EBUSY;
    const char* last = GetName(path);
    bool isDot = strcmp(last, ".") == 0;
    delete last;
    if (isDot)
      return -EINVAL;
    if (inode->dentCount != 2)
      return -ENOTEMPTY;
    const char* absPath = AbsolutePath(path);
    const char* name = GetName(absPath);
    delete absPath;
    parent->RemoveDent(name);
    delete name;
    if (--inode->nlink == 0 && inode->nsymlink == 0)
      RemoveINode(inode);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    parent->ctime = parent->mtime = ts;
    return 0;
  }
  int RenameAt(int oldDirFd, const char* oldPath, int newDirFd, const char* newPath, unsigned int flags) {
    if (flags & ~RENAME_NOREPLACE)
      return -EINVAL;
    const char* last = GetName(oldPath);
    bool isDot = strcmp(last, ".") == 0 || strcmp(last, "..") == 0;
    delete last;
    if (isDot)
      return -EBUSY;
    Fd* oldFd;
    Fd* newFd;
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
    INode* origCwd = cwd.inode;
    if (oldDirFd != AT_FDCWD)
      cwd.inode = oldFd->inode;
    INode* oldInode;
    INode* oldParent;
    int res = GetINode(oldPath, &oldInode, &oldParent);
    cwd.inode = origCwd;
    if (res != 0)
      return res;
    if (newDirFd != AT_FDCWD)
      cwd.inode = newFd->inode;
    INode* newInode;
    INode* newParent = NULL;
    res = GetINode(newPath, &newInode, &newParent);
    cwd.inode = origCwd;
    if (newParent == NULL)
      return res;
    if (S_ISDIR(oldInode->mode)) {
      if (res >= 0 && !S_ISDIR(newInode->mode))
        return -ENOTDIR;
      if (oldInode == inodes[0] || oldInode == cwd.inode)
        return -EBUSY;
      if (oldInode->dentCount > 2)
        return -ENOTEMPTY;
    } else if (res >= 0 && S_ISDIR(newInode->mode))
      return -EISDIR;
    if (flags & RENAME_NOREPLACE && res >= 0)
      return -EEXIST;
    const char* newAbs = AbsolutePath(newPath);
    const char* newName = GetName(newAbs);
    delete newAbs;
    if (newParent->size > std::numeric_limits<size_t>::max() - (strlen(newName) * 2)) {
      delete newName;
      return -ENOSPC;
    }
    const char* oldAbs = AbsolutePath(oldPath);
    const char* oldName = GetName(oldAbs);
    delete oldAbs;
    oldParent->RemoveDent(oldName);
    delete oldName;
    newParent->RemoveDent(newName);
    newParent->PushDent(newName, oldInode);
    oldInode->parent = newParent;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    oldParent->ctime = oldParent->mtime = ts;
    newParent->ctime = newParent->mtime = ts;
    return 0;
  }
  int Rename(const char* oldPath, const char* newPath) {
    return RenameAt(AT_FDCWD, oldPath, AT_FDCWD, newPath, 0);
  }
  off_t LSeek(unsigned int fdNum, off_t offset, unsigned int whence) {
    if (offset < 0)
      return -EINVAL;
    Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    switch (whence) {
      case SEEK_SET:
        return fd->seekOff = offset;
      case SEEK_CUR:
        if (fd->seekOff > std::numeric_limits<off_t>::max() - offset)
          return -EOVERFLOW;
        return fd->seekOff += offset;
      case SEEK_END:
        if (S_ISDIR(fd->inode->mode))
          return -EINVAL;
        if (fd->inode->size > std::numeric_limits<off_t>::max() - offset)
          return -EOVERFLOW;
        return fd->seekOff = fd->inode->size + offset;
      default:
        return -EINVAL;
    }
  }
  ssize_t Read(unsigned int fdNum, char* buf, size_t count) {
    Fd* fd;
    if (!(fd = GetFd(fdNum)) || fd->flags & O_WRONLY)
      return -EBADF;
    INode* inode = fd->inode;
    if (S_ISDIR(inode->mode))
      return -EISDIR;
    if (fd->seekOff >= inode->size)
      return 0;
    size_t end = inode->size - fd->seekOff;
    if (end < count)
      count = end;
    memcpy(buf, inode->data + fd->seekOff, count);
    buf[count] = '\0';
    if (!(fd->flags & O_NOATIME))
      clock_gettime(CLOCK_REALTIME, &inode->atime);
    return count;
  }
  ssize_t Write(unsigned int fdNum, const char* buf, size_t count) {
    Fd* fd;
    if (!(fd = GetFd(fdNum)) || !(fd->flags & (O_WRONLY | O_RDWR)))
      return -EBADF;
    INode* inode = fd->inode;
    off_t seekOff = fd->flags & O_APPEND
      ? inode->size
      : fd->seekOff;
    if (seekOff > std::numeric_limits<size_t>::max() - count)
      return -EFBIG;
    if (seekOff + count > inode->size) {
      inode->data = reinterpret_cast<char*>(
        realloc(inode->data, seekOff + count + 1)
      );
      if (inode->size < seekOff)
        memset(inode->data + inode->size, '\0', seekOff - inode->size);
      inode->size = seekOff + count;
    }
    memcpy(inode->data + seekOff, buf, count);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode->mtime = inode->ctime = ts;
    return count;
  }
  ssize_t SendFile(unsigned int outFd, unsigned int inFd, off_t* offset, size_t count) {
    Fd* fdIn;
    Fd* fdOut;
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
    INode* inodeIn = fdIn->inode;
    INode* inodeOut = fdOut->inode;
    if (fdOut->seekOff > std::numeric_limits<size_t>::max() - count)
      return -EFBIG;
    // read
    if (off >= inodeIn->size)
      return 0;
    size_t end = inodeIn->size - off;
    if (end < count)
      count = end;
    if (!offset)
      fdIn->seekOff += count;
    else *offset += count;
    // write
    if (fdOut->seekOff + count > inodeOut->size) {
      inodeOut->data = reinterpret_cast<char*>(
        realloc(inodeOut->data, fdOut->seekOff + count + 1)
      );
      if (inodeOut->size < fdOut->seekOff)
        memset(inodeOut->data + inodeOut->size, '\0', fdOut->seekOff - inodeOut->size);
      inodeOut->size = fdOut->seekOff + count;
    }
    memcpy(inodeOut->data + fdOut->seekOff, inodeIn->data + off, count);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (!(fdIn->flags & O_NOATIME))
      inodeIn->atime = ts;
    inodeOut->mtime = inodeOut->ctime = ts;
    return count;
  }
  int Truncate(const char* path, off_t length) {
    if (length < 0)
      return -EINVAL;
    INode* inode;
    INode* parent;
    int res = GetINode(path, &inode, &parent, true);
    if (res != 0)
      return res;
    if (S_ISDIR(inode->mode))
      return -EISDIR;
    inode->data = reinterpret_cast<char*>(
      realloc(inode->data, length + 1)
    );
    if (inode->size < length)
      memset(inode->data + inode->size, '\0', length - inode->size);
    inode->size = length;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode->ctime = inode->mtime = ts;
    return 0;
  }
  int FTruncate(unsigned int fdNum, off_t length) {
    if (length < 0)
      return -EINVAL;
    Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    if (fd->flags != O_WRONLY && !(fd->flags & (O_WRONLY | O_RDWR)))
      return -EINVAL;
    INode* inode = fd->inode;
    if (S_ISDIR(inode->mode))
      return -EISDIR;
    inode->data = reinterpret_cast<char*>(
      realloc(inode->data, length + 1)
    );
    if (inode->size < length)
      memset(inode->data + inode->size, '\0', length - inode->size);
    inode->size = length;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    inode->ctime = inode->mtime = ts;
    return 0;
  }
  int FChModAt(int dirFd, const char* path, mode_t mode) {
    INode* origCwd = cwd.inode;
    if (dirFd != AT_FDCWD) {
      Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      cwd.inode = fd->inode;
    }
    INode* inode;
    INode* parent;
    int res = GetINode(path, &inode, &parent);
    cwd.inode = origCwd;
    if (res != 0)
      return res;
    inode->mode = (mode & ~S_IFMT) | (inode->mode & S_IFMT);
    clock_gettime(CLOCK_REALTIME, &inode->ctime);
    return 0;
  }
  int ChMod(const char* path, mode_t mode) {
    return FChModAt(AT_FDCWD, path, mode);
  }
  int FChMod(unsigned int fdNum, mode_t mode) {
    Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    fd->inode->mode = (mode & ~S_IFMT) | (fd->inode->mode & S_IFMT);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    fd->inode->ctime = ts;
    return 0;
  }
  int ChDir(const char* path) {
    INode* inode;
    INode* parent;
    int res = GetINode(path, &inode, &parent, true);
    if (res != 0)
      return res;
    if (!S_ISDIR(inode->mode))
      return -ENOTDIR;
    cwd = { AbsolutePath(path), inode };
    return 0;
  }
  int GetCwd(char* buf, size_t size) {
    if (cwd.inode != inodes[0]) {
      INode* inode;
      INode* parent;
      int res = GetINode(cwd.path, &inode, &parent, true);
      if (res != 0)
        return res;
    }
    size_t cwdLen = strlen(cwd.path);
    if (size <= cwdLen)
      return -ERANGE;
    if (buf != NULL) {
      memcpy(buf, cwd.path, cwdLen);
      buf[cwdLen] = '\0';
    }
    return cwdLen;
  }
  int Stat(const char* path, struct stat* buf) {
    INode* inode;
    INode* parent;
    int res = GetINode(path, &inode, &parent, true);
    if (res != 0)
      return res;
    FillStat(inode, buf);
    return 0;
  }
  int LStat(const char* path, struct stat* buf) {
    INode* inode;
    INode* parent;
    int res = GetINode(path, &inode, &parent);
    if (res != 0)
      return res;
    FillStat(inode, buf);
    return 0;
  }
  int FStat(unsigned int fdNum, struct stat* buf) {
    Fd* fd;
    if (!(fd = GetFd(fdNum)))
      return -EBADF;
    FillStat(fd->inode, buf);
    return 0;
  }
  int Statx(int dirFd, const char* path, int flags, int mask, struct statx* buf) {
    if (flags != 0 || mask & ~(STATX_BASIC_STATS | STATX_BTIME))
      return -EINVAL;
    INode* origCwd = cwd.inode;
    if (dirFd != AT_FDCWD) {
      Fd* fd;
      if (!(fd = GetFd(dirFd)))
        return -EBADF;
      cwd.inode = fd->inode;
    }
    INode* inode;
    INode* parent;
    int res = GetINode(path, &inode, &parent, !(flags & AT_SYMLINK_NOFOLLOW));
    cwd.inode = origCwd;
    if (res != 0)
      return res;
    FillStatx(inode, buf, mask);
    return 0;
  }

 private:
  struct INode {
    INode() {
      clock_gettime(CLOCK_REALTIME, &btime);
      ctime = mtime = atime = btime;
    }
    ~INode() {
      if (data != NULL)
        delete data;
      if (dents != NULL) {
        while (dentCount != 2)
          delete dents[--dentCount].name;
        delete dents;
      }
    }
    ino_t id;
    INode* parent;
    INode* target;
    struct Dent {
      const char* name;
      INode* inode;
    };
    struct Dent* dents = NULL;
    size_t dentCount = 0;
    void PushDent(const char* name, INode* inode) {
      dents = reinterpret_cast<struct Dent*>(
        realloc(dents, sizeof(struct Dent) * (dentCount + 1))
      );
      dents[dentCount++] = { name, inode };
      size += strlen(name) * 2;
    }
    void RemoveDent(const char* name) {
      for (size_t i = 0; i != dentCount; ++i)
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
    char* data = NULL;
    size_t size = 0;
    nlink_t nsymlink = 0;
    nlink_t nlink = 0;
    mode_t mode;
    struct timespec btime;
    struct timespec ctime;
    struct timespec mtime;
    struct timespec atime;
  };
  struct Fd {
    INode* inode;
    int fd;
    int flags;
    off_t seekOff = 0;
  };
  struct Cwd {
    ~Cwd() {
      delete path;
    }
    const char* path;
    INode* inode;
  };
  Cwd cwd;
  INode** inodes = {};
  ino_t inodeCount = 0;
  Fd** fds = {};
  int fdCount = 0;
  bool PushINode(INode* inode) {
    if (inodeCount == std::numeric_limits<ino_t>::max())
      return false;
    inodes = reinterpret_cast<INode**>(
      realloc(inodes, sizeof(INode*) * (inodeCount + 1))
    );
    inode->id = inodeCount;
    inodes[inodeCount++] = inode;
    return true;
  }
  void RemoveINode(INode* inode) {
    for (ino_t i = 0; i != inodeCount; ++i) {
      if (inodes[i] == inode) {
        delete inode;
        if (i != inodeCount - 1) {
          memmove(inodes + i, inodes + i + 1, sizeof(INode*) * (inodeCount - i));
          while (i != inodeCount - 1)
            --inodes[i++]->id;
        }
        inodes = reinterpret_cast<INode**>(
          realloc(inodes, sizeof(INode*) * --inodeCount)
        );
        break;
      }
    }
  }
  int PushFd(INode* inode, int flags) {
    if (fdCount == std::numeric_limits<int>::max())
      return -ENFILE;
    int fdNum = fdCount;
    for (int i = 0; i != fdCount; ++i) {
      if (fds[i]->fd != i) {
        fdNum = i;
        break;
      }
    }
    fds = reinterpret_cast<Fd**>(
      realloc(fds, sizeof(Fd*) * (fdCount + 1))
    );
    Fd* fd = new Fd;
    fd->inode = inode;
    fd->flags = flags;
    fd->fd = fdNum;
    fds[fdCount++] = fd;
    return fdNum;
  }
  int RemoveFd(unsigned int fd) {
    for (int i = 0; i != fdCount; ++i)
      if (fds[i]->fd == fd) {
        delete fds[i];
        if (i != fdCount - 1)
          memmove(fds + i, fds + i + 1, sizeof(Fd*) * (fdCount - i));
        fds = reinterpret_cast<Fd**>(
          realloc(fds, sizeof(Fd*) * --fdCount)
        );
        return 0;
      }
    return -EBADF;
  }
  Fd* GetFd(unsigned int fdNum) {
    for (int i = 0; i != fdCount; ++i)
      if (fds[i]->fd == fdNum)
        return fds[i];
    return NULL;
  }
  const char* GetName(const char* path) {
    size_t pathLen = strlen(path);
    char* name = new char[NAME_MAX + 1];
    size_t nameLen = 0;
    for (size_t i = pathLen; i != -1; --i) {
      if (path[i] == '/') {
        if (nameLen != 0)
          break;
      } else {
        memmove(name + 1, name, nameLen++);
        name[0] = path[i];
      }
    }
    name[nameLen] = '\0';
    return reinterpret_cast<char*>(
      realloc(name, nameLen + 1)
    );
  }
  const char* AbsolutePath(const char* path) {
    char* absPath = new char[PATH_MAX];
    size_t absPathLen = 0;
    if (path[0] != '/') {
      size_t cwdPathLen = strlen(cwd.path);
      if (cwdPathLen != 1) {
        memcpy(absPath, cwd.path, cwdPathLen);
        absPath[cwdPathLen] = '/';
        absPathLen = cwdPathLen + 1;
      } else absPath[absPathLen++] = '/';
    }
    size_t pathLen = strlen(path);
    for (size_t i = 0; i != pathLen; ++i) {
      if (path[i] == '/') {
        if (absPath[absPathLen - 1] != '/')
          absPath[absPathLen++] = '/';
      } else if (path[i] == '.' && absPath[absPathLen - 1] == '/') {
        if (path[i + 1] == '.') {
          if (path[i + 2] == '/' || path[i + 2] == '\0') {
            --absPathLen;
            while (absPathLen > 0 && absPath[--absPathLen - 1] != '/');
            if (absPathLen == 0)
              absPathLen = 1;
            if (path[i + 2] == '\0')
              ++i;
            else i += 2;
          }
        } else if (path[i + 1] == '/' || path[i + 1] == '\0') {
          if (path[i + 1] != '\0')
            ++i;
        } else absPath[absPathLen++] = '.';
      } else absPath[absPathLen++] = path[i];
    }
    if (absPath[absPathLen - 1] == '/')
      --absPathLen;
    absPath[absPathLen] = '\0';
    return reinterpret_cast<char*>(
      realloc(absPath, absPathLen + 1)
    );
  }
  int GetINode(const char* path, INode** inode, INode** parent, bool followResolved = false) {
    size_t pathLen = strlen(path);
    if (pathLen == 0)
      return -ENOENT;
    if (pathLen >= PATH_MAX)
      return -ENAMETOOLONG;
    INode* current = path[0] == '/'
      ? inodes[0]
      : cwd.inode;
    INode* currParent = current->parent;
    int err = 0;
    char name[NAME_MAX + 1];
    size_t nameLen = 0;
    int followCount = 0;
    for (size_t i = 0; i != pathLen; ++i) {
      if (path[i] == '/') {
        if (nameLen == 0)
          continue;
        if (err)
          return err;
        size_t j = 0;
        for (; j != current->dentCount; ++j)
          if (strcmp(current->dents[j].name, name) == 0)
            break;
        if (j == current->dentCount) {
          err = -ENOENT;
          goto resetName;
        }
        currParent = current;
        current = current->dents[j].inode;
        while (S_ISLNK(current->mode)) {
          if (followCount++ == 40) {
            err = -ELOOP;
            goto resetName;
          }
          current = current->target;
          if (current->nlink == 0) {
            err = -ENOENT;
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
    *parent = currParent;
    if (err)
      return err;
    if (nameLen != 0) {
      *parent = current;
      for (size_t i = 0; i != current->dentCount; ++i)
        if (strcmp(current->dents[i].name, name) == 0) {
          current = current->dents[i].inode;
          goto out;
        }
      return -ENOENT;
    }
   out:
    if (followResolved) {
      while (S_ISLNK(current->mode)) {
        if (followCount++ == 40)
          return -ELOOP;
        current = current->target;
        if (current->nlink == 0)
          return -ENOENT;
      }
    }
    *inode = current;
    return 0;
  }
  void FillStat(INode* inode, struct stat* buf) {
    memset(buf, '\0', sizeof(struct stat));
    buf->st_ino = inode->id;
    buf->st_mode = inode->mode;
    buf->st_nlink = inode->nlink;
    buf->st_size = inode->size;
    buf->st_atim = inode->atime;
    buf->st_mtim = inode->mtime;
    buf->st_ctim = inode->ctime;
    buf->st_blocks = inode->size / 512;
  }
  void FillStatx(INode* inode, struct statx* buf, int mask) {
    memset(buf, '\0', sizeof(struct statx));
    buf->stx_mask = mask & (STATX_BASIC_STATS | STATX_BTIME);
    buf->stx_attributes = buf->stx_attributes_mask = STATX_ATTR_NODUMP;
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
    if (mask & STATX_BLOCKS)
      buf->stx_blocks = inode->size / 512;
  }
};