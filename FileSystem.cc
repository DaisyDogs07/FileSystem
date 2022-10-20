#include <cassert>
#include "FileSystem.h"
#include "node.h"
#include "node_buffer.h"

using namespace node;
using namespace v8;

Persistent<FunctionTemplate> FSConstructorTmpl;
Persistent<ObjectTemplate> FSInstanceTmpl;

void FileSystemCleanup(void*, size_t, void* data) {
  delete reinterpret_cast<FileSystem*>(data);
}

void FileSystemConstructor(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  if (!args.IsConstructCall()) {
    isolate->ThrowException(
      Exception::TypeError(
        String::NewFromUtf8Literal(
          isolate,
          "Constructor FileSystem requires 'new'"
        )
      )
    );
    return;
  }
  FileSystem* fs = new FileSystem;
  std::unique_ptr<BackingStore> ab = ArrayBuffer::NewBackingStore(fs, sizeof(FileSystem), FileSystemCleanup, fs);
  Local<ArrayBuffer> abuf = ArrayBuffer::New(isolate, std::move(ab));
  args.This()->SetInternalField(0, External::New(isolate, fs));
  args.This()->SetInternalField(1, abuf);
}

#define IsNumeric(x) \
  (x->IsNumber() || x->IsBigInt())
#define IsStrOrBuf(x) \
  (x->IsString() || Buffer::HasInstance(x))
#define Int32Val(x) \
  (x->IsBigInt() \
    ? static_cast<int32_t>(x.As<BigInt>()->Int64Value()) \
    : static_cast<int32_t>(x.As<Number>()->Value()))
#define Uint32Val(x) \
  (x->IsBigInt() \
    ? static_cast<uint32_t>(x.As<BigInt>()->Uint64Value()) \
    : static_cast<uint32_t>(x.As<Number>()->Value()))
#define Int64Val(x) \
  (x->IsBigInt() \
    ? x.As<BigInt>()->Int64Value() \
    : static_cast<int64_t>(x.As<Number>()->Value()))
#define Uint64Val(x) \
  (x->IsBigInt() \
    ? x.As<BigInt>()->Uint64Value() \
    : static_cast<uint64_t>(x.As<Number>()->Value()))
#define StringVal(x) \
  (x->IsString() \
    ? *String::Utf8Value(isolate, x.As<String>()) \
    : Buffer::Data(x))
#define StringLen(x) \
  (x->IsString() \
    ? static_cast<size_t>(x.As<String>()->Utf8Length(isolate)) \
    : Buffer::Length(x))

#define THROWIFNOTFS(self, method) \
  do { \
    if (self.IsEmpty()) { \
      isolate->ThrowException( \
        Exception::TypeError( \
          String::NewFromUtf8Literal( \
            isolate, \
            method " requires that 'this' be a FileSystem" \
          ) \
        ) \
      ); \
      return; \
    }\
  } while (0)
#define THROWERR(code) \
  do { \
    isolate->ThrowException( \
      Exception::Error( \
        String::NewFromUtf8( \
          isolate, \
          strerror(-code) \
        ).ToLocalChecked() \
      ) \
    ); \
    return; \
  } while (0)
#define THROWIFERR(res) \
  do { \
    auto tmp = (res); \
    if (tmp < 0) \
      THROWERR(tmp); \
  } while (0)

void FileSystemFAccessAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.faccessat");
  assert(args.Length() == 4);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(IsNumeric(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FAccessAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      Uint32Val(args[3])
    )
  );
}
void FileSystemAccess(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.access");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Access(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Int32Val(args[1])
    )
  );
}
void FileSystemOpenAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.openat");
  assert(args.Length() == 4);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(IsNumeric(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    res = fs->OpenAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      Uint32Val(args[3])
    )
  );
  args.GetReturnValue().Set(
    Int32::New(
      isolate,
      res
    )
  );
}
void FileSystemOpen(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.open");
  assert(args.Length() == 3);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    res = fs->Open(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Int32Val(args[1]),
      Uint32Val(args[2])
    )
  );
  args.GetReturnValue().Set(
    Int32::New(
      isolate,
      res
    )
  );
}
void FileSystemCreat(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.creat");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    res = fs->Creat(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Uint32Val(args[1])
    )
  );
  args.GetReturnValue().Set(
    Int32::New(
      isolate,
      res
    )
  );
}
void FileSystemClose(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.close");
  assert(args.Length() == 1);
  assert(IsNumeric(args[0]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Close(
      Int32Val(args[0])
    )
  );
}
void FileSystemMkNodAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.mknodat");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->MkNodAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Uint32Val(args[2]),
      0
    )
  );
}
void FileSystemMkNod(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.mknod");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->MkNod(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Uint32Val(args[1]),
      0
    )
  );
}
void FileSystemMkDirAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.mkdirat");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->MkDirAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Uint32Val(args[2])
    )
  );
}
void FileSystemMkDir(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.mkdir");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->MkDir(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Uint32Val(args[1])
    )
  );
}
void FileSystemSymlinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.symlinkat");
  assert(args.Length() == 3);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  assert(args[2]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->SymlinkAt(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Int32Val(args[1]),
      *String::Utf8Value(isolate, args[2].As<String>())
    )
  );
}
void FileSystemSymlink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.symlink");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Symlink(
      *String::Utf8Value(isolate, args[0].As<String>()),
      *String::Utf8Value(isolate, args[1].As<String>())
    )
  );
}
void FileSystemReadLinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.readlinkat");
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char buf[PATH_MAX];
  int res;
  THROWIFERR(
    res = fs->ReadLinkAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      buf,
      PATH_MAX
    )
  );
  buf[res] = '\0';
  args.GetReturnValue().Set(
    String::NewFromUtf8(isolate, buf).ToLocalChecked()
  );
}
void FileSystemReadLink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.readlink");
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char buf[PATH_MAX];
  int res;
  THROWIFERR(
    res = fs->ReadLink(
      *String::Utf8Value(isolate, args[0].As<String>()),
      buf,
      PATH_MAX
    )
  );
  buf[res] = '\0';
  args.GetReturnValue().Set(
    String::NewFromUtf8(isolate, buf).ToLocalChecked()
  );
}
void FileSystemGetDents(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.getdents");
  assert(args.Length() == 1 || args.Length() == 2);
  assert(IsNumeric(args[0]));
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  uint32_t count = std::numeric_limits<uint32_t>::max();
  if (args.Length() == 2) {
    assert(IsNumeric(args[1]));
    count = Uint32Val(args[1]);
  }
  Local<Array> dentArr = Array::New(isolate);
  char buf[1024];
  unsigned int fdNum = Uint32Val(args[0]);
  off_t off = fs->LSeek(fdNum, 0, SEEK_CUR);
  uint32_t i = 0;
  while (i < count) {
    int nread;
    THROWIFERR(
      nread = fs->GetDents(
        fdNum,
        (struct linux_dirent*)buf,
        1024
      )
    );
    if (nread == 0)
      break;
    for (int j = 0; i < count && j < nread; ++i) {
      struct linux_dirent* dent = (struct linux_dirent*)(buf + j);
      Local<Object> dentObj = Object::New(isolate);
      dentObj->Set(
        context,
        String::NewFromUtf8Literal(isolate, "d_ino"),
        BigInt::NewFromUnsigned(isolate, dent->d_ino)
      ).Check();
      dentObj->Set(
        context,
        String::NewFromUtf8Literal(isolate, "d_off"),
        BigInt::NewFromUnsigned(isolate, dent->d_off)
      ).Check();
      dentObj->Set(
        context,
        String::NewFromUtf8Literal(isolate, "d_name"),
        String::NewFromUtf8(isolate, dent->d_name).ToLocalChecked()
      ).Check();
      dentObj->Set(
        context,
        String::NewFromUtf8Literal(isolate, "d_type"),
        Integer::New(isolate, (buf + j)[dent->d_reclen - 1])
      ).Check();
      dentArr->Set(
        context,
        dentArr->Length(),
        dentObj
      ).Check();
      j += dent->d_reclen;
    }
  }
  fs->LSeek(fdNum, off, SEEK_SET);
  args.GetReturnValue().Set(dentArr);
}
void FileSystemLinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.linkat");
  assert(args.Length() == 5);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(args[3]->IsString());
  assert(IsNumeric(args[4]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->LinkAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      *String::Utf8Value(isolate, args[3].As<String>()),
      Int32Val(args[4])
    )
  );
}
void FileSystemLink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.link");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Link(
      *String::Utf8Value(isolate, args[0].As<String>()),
      *String::Utf8Value(isolate, args[1].As<String>())
    )
  );
}
void FileSystemUnlinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.unlinkat");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->UnlinkAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2])
    )
  );
}
void FileSystemUnlink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.unlink");
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Unlink(
      *String::Utf8Value(isolate, args[0].As<String>())
    )
  );
}
void FileSystemRmDir(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.rmdir");
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->RmDir(
      *String::Utf8Value(isolate, args[0].As<String>())
    )
  );
}
void FileSystemRenameAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.renameat");
  assert(args.Length() == 5);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(args[3]->IsString());
  assert(IsNumeric(args[4]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->RenameAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      *String::Utf8Value(isolate, args[3].As<String>()),
      Int32Val(args[4])
    )
  );
}
void FileSystemRename(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.rename");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Rename(
      *String::Utf8Value(isolate, args[0].As<String>()),
      *String::Utf8Value(isolate, args[1].As<String>())
    )
  );
}
void FileSystemLSeek(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.lseek");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  off_t res;
  THROWIFERR(
    res = fs->LSeek(
      Uint32Val(args[0]),
      Int64Val(args[1]),
      Uint32Val(args[2])
    )
  );
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemRead(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.read");
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  unsigned int fdNum = Uint32Val(args[0]);
  struct stat s;
  ssize_t res;
  THROWIFERR(res = fs->FStat(fdNum, &s));
  size_t bufLen = std::min(
    std::min(
      Uint64Val(args[1]),
      (size_t)s.st_size
    ),
    (size_t)std::numeric_limits<int>::max()
  );
  char* buf = new char[bufLen + 1];
  res = fs->Read(
    fdNum,
    buf,
    bufLen
  );
  if (res < 0) {
    delete buf;
    THROWERR(res);
  }
  buf = reinterpret_cast<char*>(
    realloc(buf, res)
  );
  args.GetReturnValue().Set(
    Buffer::New(
      isolate,
      buf,
      res
    ).ToLocalChecked()
  );
}
void FileSystemReadv(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.readv");
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsArray());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> buffers = args[1].As<Array>();
  struct iovec* iov = new iovec[buffers->Length()];
  for (uint32_t i = 0; i != buffers->Length(); ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    assert(Buffer::HasInstance(buf));
    iov[i].iov_base = Buffer::Data(buf);
    iov[i].iov_len = Buffer::Length(buf);
  }
  ssize_t res = fs->Readv(
    Uint32Val(args[0]),
    iov,
    buffers->Length()
  );
  delete[] iov;
  if (res < 0)
    THROWERR(res);
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemPRead(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.pread");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  unsigned int fdNum = Uint32Val(args[0]);
  struct stat s;
  ssize_t res;
  THROWIFERR(res = fs->FStat(fdNum, &s));
  size_t bufLen = std::min(
    std::min(
      Uint64Val(args[2]),
      (size_t)s.st_size
    ),
    (size_t)std::numeric_limits<int>::max()
  );
  char* buf = new char[bufLen + 1];
  res = fs->PRead(
    fdNum,
    buf,
    bufLen,
    Int64Val(args[1])
  );
  if (res < 0) {
    delete buf;
    THROWERR(res);
  }
  buf = reinterpret_cast<char*>(
    realloc(buf, res)
  );
  args.GetReturnValue().Set(
    Buffer::New(
      isolate,
      buf,
      res
    ).ToLocalChecked()
  );
}
void FileSystemPReadv(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.preadv");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsArray());
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> buffers = args[1].As<Array>();
  struct iovec* iov = new iovec[buffers->Length()];
  for (uint32_t i = 0; i != buffers->Length(); ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    assert(Buffer::HasInstance(buf));
    iov[i].iov_base = Buffer::Data(buf);
    iov[i].iov_len = Buffer::Length(buf);
  }
  ssize_t res = fs->PReadv(
    Uint32Val(args[0]),
    iov,
    buffers->Length(),
    Int64Val(args[2])
  );
  delete[] iov;
  if (res < 0)
    THROWERR(res);
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemWrite(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.write");
  assert(args.Length() == 2 || args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(IsStrOrBuf(args[1]));
  if (args.Length() == 3)
    assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  ssize_t res;
  THROWIFERR(
    res = fs->Write(
      Uint32Val(args[0]),
      StringVal(args[1]),
      args.Length() == 3
        ? Uint64Val(args[2])
        : StringLen(args[1])
    )
  );
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemWritev(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.writev");
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsArray());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> buffers = args[1].As<Array>();
  struct iovec* iov = new iovec[buffers->Length()];
  for (uint32_t i = 0; i != buffers->Length(); ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    assert(Buffer::HasInstance(buf));
    iov[i].iov_base = Buffer::Data(buf);
    iov[i].iov_len = Buffer::Length(buf);
  }
  ssize_t res = fs->Writev(
    Uint32Val(args[0]),
    iov,
    buffers->Length()
  );
  delete[] iov;
  if (res < 0)
    THROWERR(res);
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemPWrite(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.pwrite");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  ssize_t res = fs->PWrite(
    Uint32Val(args[0]),
    StringVal(args[1]),
    Uint64Val(args[2]),
    Int64Val(args[3])
  );
  if (res < 0)
    THROWERR(res);
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemPWritev(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.pwritev");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsArray());
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> buffers = args[1].As<Array>();
  struct iovec* iov = new iovec[buffers->Length()];
  for (uint32_t i = 0; i != buffers->Length(); ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    assert(Buffer::HasInstance(buf));
    iov[i].iov_base = Buffer::Data(buf);
    iov[i].iov_len = Buffer::Length(buf);
  }
  ssize_t res = fs->PWritev(
    Uint32Val(args[0]),
    iov,
    buffers->Length(),
    Int64Val(args[2])
  );
  delete[] iov;
  if (res < 0)
    THROWERR(res);
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemSendFile(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.sendfile");
  assert(args.Length() == 4);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  assert(args[2]->IsNull() || IsNumeric(args[2]));
  assert(IsNumeric(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  off_t off;
  if (!args[2]->IsNull())
    off = Int64Val(args[2]);
  ssize_t res;
  THROWIFERR(
    res = fs->SendFile(
      Uint32Val(args[0]),
      Uint32Val(args[1]),
      args[2]->IsNull() ? NULL : &off,
      Uint64Val(args[3])
    )
  );
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemFTruncate(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.ftruncate");
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FTruncate(
      Uint32Val(args[0]),
      Int64Val(args[1])
    )
  );
}
void FileSystemTruncate(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.truncate");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Truncate(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Int64Val(args[1])
    )
  );
}
void FileSystemFChModAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.fchmodat");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FChModAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Uint32Val(args[2])
    )
  );
}
void FileSystemFChMod(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.fchmod");
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FChMod(
      Uint32Val(args[0]),
      Uint32Val(args[1])
    )
  );
}
void FileSystemChMod(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.chmod");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->ChMod(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Uint32Val(args[1])
    )
  );
}
void FileSystemChDir(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.chdir");
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->ChDir(
      *String::Utf8Value(isolate, args[0].As<String>())
    )
  );
}
void FileSystemGetCwd(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.getcwd");
  assert(args.Length() == 0);
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char buf[PATH_MAX];
  THROWIFERR(fs->GetCwd(buf, PATH_MAX));
  args.GetReturnValue().Set(
    String::NewFromUtf8(isolate, buf).ToLocalChecked()
  );
}

void SetTimeProp(Isolate* isolate, Local<Object> obj, const char* prop, time_t sec, long nsec) {
  Local<Context> context = isolate->GetCurrentContext();
  Local<Object> tim = Object::New(isolate);
  tim->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_sec"),
    Integer::NewFromUnsigned(isolate, sec)
  ).Check();
  tim->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_nsec"),
    Integer::NewFromUnsigned(isolate, nsec)
  ).Check();
  obj->Set(
    context,
    String::NewFromUtf8(isolate, prop).ToLocalChecked(),
    tim
  ).Check();
}

Local<Object> StatToObj(Isolate* isolate, struct stat s) {
  Local<Context> context = isolate->GetCurrentContext();
  Local<Object> statObj = Object::New(isolate);
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_ino"),
    BigInt::NewFromUnsigned(isolate, s.st_ino)
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_mode"),
    Integer::NewFromUnsigned(isolate, s.st_mode)
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_nlink"),
    BigInt::NewFromUnsigned(isolate, s.st_nlink)
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_size"),
    BigInt::NewFromUnsigned(isolate, s.st_size)
  ).Check();
  SetTimeProp(isolate, statObj, "st_atim", s.st_atim.tv_sec, s.st_atim.tv_nsec);
  SetTimeProp(isolate, statObj, "st_mtim", s.st_mtim.tv_sec, s.st_mtim.tv_nsec);
  SetTimeProp(isolate, statObj, "st_ctim", s.st_ctim.tv_sec, s.st_ctim.tv_nsec);
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_blocks"),
    BigInt::NewFromUnsigned(isolate, s.st_blocks)
  ).Check();
  return statObj;
}

void FileSystemFStat(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.fstat");
  assert(args.Length() == 1);
  assert(IsNumeric(args[0]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  struct stat s;
  THROWIFERR(
    fs->FStat(
      Uint32Val(args[0]),
      &s
    )
  );
  args.GetReturnValue().Set(
    StatToObj(isolate, s)
  );
}
void FileSystemStat(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.stat");
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  struct stat s;
  THROWIFERR(
    fs->Stat(
      *String::Utf8Value(isolate, args[0].As<String>()),
      &s
    )
  );
  args.GetReturnValue().Set(
    StatToObj(isolate, s)
  );
}
void FileSystemLStat(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.lstat");
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  struct stat s;
  THROWIFERR(
    fs->LStat(
      *String::Utf8Value(isolate, args[0].As<String>()),
      &s
    )
  );
  args.GetReturnValue().Set(
    StatToObj(isolate, s)
  );
}

Local<Object> StatxToObj(Isolate* isolate, struct statx s) {
  Local<Context> context = isolate->GetCurrentContext();
  Local<Object> statxObj = Object::New(isolate);
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_ino"),
    BigInt::NewFromUnsigned(isolate, s.stx_ino)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_mode"),
    Integer::NewFromUnsigned(isolate, s.stx_mode)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_nlink"),
    BigInt::NewFromUnsigned(isolate, s.stx_nlink)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_size"),
    BigInt::NewFromUnsigned(isolate, s.stx_size)
  ).Check();
  SetTimeProp(isolate, statxObj, "stx_atime", s.stx_atime.tv_sec, s.stx_atime.tv_nsec);
  SetTimeProp(isolate, statxObj, "stx_mtime", s.stx_mtime.tv_sec, s.stx_mtime.tv_nsec);
  SetTimeProp(isolate, statxObj, "stx_ctime", s.stx_ctime.tv_sec, s.stx_ctime.tv_nsec);
  SetTimeProp(isolate, statxObj, "stx_btime", s.stx_btime.tv_sec, s.stx_btime.tv_nsec);
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_blocks"),
    BigInt::NewFromUnsigned(isolate, s.stx_blocks)
  ).Check();
  return statxObj;
}

void FileSystemStatx(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.statx");
  assert(args.Length() == 4);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(IsNumeric(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  struct statx s;
  THROWIFERR(
    fs->Statx(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      Int32Val(args[3]),
      &s
    )
  );
  args.GetReturnValue().Set(
    StatxToObj(isolate, s)
  );
}
void FileSystemUTimeNsAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.utimensat");
  assert(args.Length() == 4);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(args[2]->IsNull() || args[2]->IsArray());
  if (args[2]->IsArray())
    assert(args[2].As<Array>()->Length() == 2);
  assert(IsNumeric(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  struct timespec times[2];
  if (!args[2]->IsNull()) {
    double atime = args[2].As<Array>()->Get(
      context,
      0
    ).ToLocalChecked().As<Number>()->Value();
    double mtime = args[2].As<Array>()->Get(
      context,
      1
    ).ToLocalChecked().As<Number>()->Value();
    if (atime != -1) {
      times[0].tv_sec = atime / 1000.0;
      times[0].tv_nsec = atime * 1000000.0;
    } else {
      times[0].tv_sec = 0;
      times[0].tv_nsec = UTIME_OMIT;
    }
    if (mtime != -1) {
      times[1].tv_sec = mtime / 1000.0;
      times[1].tv_nsec = mtime * 1000000.0;
    } else {
      times[1].tv_sec = 0;
      times[1].tv_nsec = UTIME_OMIT;
    }
  }
  THROWIFERR(
    fs->UTimeNsAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      args[2]->IsNull() ? NULL : times,
      Int32Val(args[3])
    )
  );
}
void FileSystemFUTimesAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.futimesat");
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(args[2]->IsNull() || args[2]->IsArray());
  if (args[2]->IsArray())
    assert(args[2].As<Array>()->Length() == 2);
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  struct timeval times[2];
  if (!args[2]->IsNull()) {
    Local<Value> atime = args[2].As<Array>()->Get(
      context,
      0
    ).ToLocalChecked();
    Local<Value> mtime = args[2].As<Array>()->Get(
      context,
      1
    ).ToLocalChecked();
    assert(atime->IsNumber());
    assert(mtime->IsNumber());
    double atimeVal = atime.As<Number>()->Value();
    double mtimeVal = mtime.As<Number>()->Value();
    times[0].tv_sec = atimeVal / 1000.0;
    times[0].tv_usec = atimeVal * 1000.0;
    times[1].tv_sec = mtimeVal / 1000.0;
    times[1].tv_usec = mtimeVal * 1000.0;
  }
  THROWIFERR(
    fs->FUTimesAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      args[2]->IsNull() ? NULL : times
    )
  );
}
void FileSystemUTimes(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.utimes");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(args[1]->IsNull() || args[1]->IsArray());
  if (args[1]->IsArray())
    assert(args[1].As<Array>()->Length() == 2);
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  struct timeval times[2];
  if (!args[1]->IsNull()) {
    Local<Value> atime = args[1].As<Array>()->Get(
      context,
      0
    ).ToLocalChecked();
    Local<Value> mtime = args[1].As<Array>()->Get(
      context,
      1
    ).ToLocalChecked();
    assert(atime->IsNumber());
    assert(mtime->IsNumber());
    double atimeVal = atime.As<Number>()->Value();
    double mtimeVal = mtime.As<Number>()->Value();
    times[0].tv_sec = atimeVal / 1000.0;
    times[0].tv_usec = atimeVal * 1000.0;
    times[1].tv_sec = mtimeVal / 1000.0;
    times[1].tv_usec = mtimeVal * 1000.0;
  }
  THROWIFERR(
    fs->UTimes(
      *String::Utf8Value(isolate, args[0].As<String>()),
      args[1]->IsNull() ? NULL : times
    )
  );
}
void FileSystemUTime(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.utime");
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(args[1]->IsNull() || args[1]->IsArray());
  if (args[1]->IsArray())
    assert(args[1].As<Array>()->Length() == 2);
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  struct utimbuf times;
  if (!args[1]->IsNull()) {
    Local<Value> atime = args[1].As<Array>()->Get(
      context,
      0
    ).ToLocalChecked();
    Local<Value> mtime = args[1].As<Array>()->Get(
      context,
      1
    ).ToLocalChecked();
    assert(atime->IsNumber());
    assert(mtime->IsNumber());
    times.actime = atime.As<Number>()->Value() / 1000.0;
    times.modtime = mtime.As<Number>()->Value() / 1000.0;
  }
  THROWIFERR(
    fs->UTime(
      *String::Utf8Value(isolate, args[0].As<String>()),
      args[1]->IsNull() ? NULL : &times
    )
  );
}

void FileSystemDumpTo(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.dumpTo");
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  if (!fs->DumpToFile(
        *String::Utf8Value(isolate, args[0].As<String>())
      )) {
    isolate->ThrowException(
      Exception::Error(
        String::NewFromUtf8(isolate, strerror(errno)).ToLocalChecked()
      )
    );
    errno = 0;
  }
}

void FileSystemLoadFrom(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  FileSystem* fs = FileSystem::LoadFromFile(
    *String::Utf8Value(isolate, args[0].As<String>())
  );
  if (!fs) {
    if (errno) {
      isolate->ThrowException(
        Exception::Error(
          String::NewFromUtf8(isolate, strerror(errno)).ToLocalChecked()
        )
      );
      errno = 0;
    } else {
      isolate->ThrowException(
        Exception::Error(
          String::NewFromUtf8Literal(isolate, "FileSystem corrupt or invalid")
        )
      );
    }
    return;
  }
  Local<Context> context = isolate->GetCurrentContext();
  Local<Object> fsObj = FSInstanceTmpl.Get(isolate)->NewInstance(context).ToLocalChecked();
  std::unique_ptr<BackingStore> ab = ArrayBuffer::NewBackingStore(fs, sizeof(FileSystem), FileSystemCleanup, fs);
  Local<ArrayBuffer> abuf = ArrayBuffer::New(isolate, std::move(ab));
  fsObj->SetInternalField(0, External::New(isolate, fs));
  fsObj->SetInternalField(1, abuf);
  args.GetReturnValue().Set(fsObj);
}

void DefineConstants(Isolate* isolate, Local<FunctionTemplate> func) {
#define DefineFlag(v) \
  do { \
    func->Set( \
      String::NewFromUtf8Literal(isolate, #v), \
      Integer::New(isolate, v) \
    ); \
  } while (0)
  DefineFlag(AT_FDCWD);
  DefineFlag(AT_REMOVEDIR);
  DefineFlag(AT_SYMLINK_FOLLOW);
  DefineFlag(AT_SYMLINK_NOFOLLOW);
  DefineFlag(DT_REG);
  DefineFlag(DT_DIR);
  DefineFlag(DT_LNK);
  DefineFlag(O_APPEND);
  DefineFlag(O_CREAT);
  DefineFlag(O_DIRECTORY);
  DefineFlag(O_EXCL);
  DefineFlag(O_NOATIME);
  DefineFlag(O_NOFOLLOW);
  DefineFlag(O_TRUNC);
  DefineFlag(O_RDONLY);
  DefineFlag(O_WRONLY);
  DefineFlag(O_RDWR);
  DefineFlag(RENAME_NOREPLACE);
  DefineFlag(S_IFMT);
  DefineFlag(S_IFREG);
  DefineFlag(S_IFDIR);
  DefineFlag(S_IFLNK);
  DefineFlag(S_IRWXU);
  DefineFlag(S_IRUSR);
  DefineFlag(S_IWUSR);
  DefineFlag(S_IXUSR);
  DefineFlag(S_IRWXG);
  DefineFlag(S_IRGRP);
  DefineFlag(S_IWGRP);
  DefineFlag(S_IXGRP);
  DefineFlag(S_IRWXO);
  DefineFlag(S_IROTH);
  DefineFlag(S_IWOTH);
  DefineFlag(S_IXOTH);
  DefineFlag(SEEK_SET);
  DefineFlag(SEEK_CUR);
  DefineFlag(SEEK_END);
  DefineFlag(STATX_ALL);
  DefineFlag(STATX_ATIME);
  DefineFlag(STATX_BASIC_STATS);
  DefineFlag(STATX_BLOCKS);
  DefineFlag(STATX_BTIME);
  DefineFlag(STATX_CTIME);
  DefineFlag(STATX_INO);
  DefineFlag(STATX_MODE);
  DefineFlag(STATX_MTIME);
  DefineFlag(STATX_NLINK);
  DefineFlag(STATX_SIZE);
  DefineFlag(STATX_TYPE);
  DefineFlag(R_OK);
  DefineFlag(W_OK);
  DefineFlag(X_OK);
  DefineFlag(F_OK);
#undef DefineFlag
}

template<typename T, size_t N>
void DefineFunction(
  Isolate* isolate,
  Local<T> obj,
  const char (&prop)[N],
  FunctionCallback fn,
  int argc = 0,
  PropertyAttribute attr = PropertyAttribute::DontEnum
) {
  Local<FunctionTemplate> funcTmpl = FunctionTemplate::New(
    isolate,
    fn,
    Local<Value>(),
    Local<Signature>(),
    argc,
    ConstructorBehavior::kThrow,
    SideEffectType::kHasSideEffect
  );
  Local<String> name = String::NewFromUtf8Literal(isolate, prop);
  obj->Set(
    name,
    funcTmpl,
    attr
  );
}
void DefineTemplateFunctions(Isolate* isolate, Local<ObjectTemplate> tmpl) {
  tmpl->SetInternalFieldCount(2);
  DefineFunction(isolate, tmpl, "faccessat",  FileSystemFAccessAt,  4);
  DefineFunction(isolate, tmpl, "access",     FileSystemAccess,     2);
  DefineFunction(isolate, tmpl, "openat",     FileSystemOpenAt,     4);
  DefineFunction(isolate, tmpl, "open",       FileSystemOpen,       3);
  DefineFunction(isolate, tmpl, "creat",      FileSystemCreat,      2);
  DefineFunction(isolate, tmpl, "close",      FileSystemClose,      1);
  DefineFunction(isolate, tmpl, "mknodat",    FileSystemMkNodAt,    3);
  DefineFunction(isolate, tmpl, "mknod",      FileSystemMkNod,      2);
  DefineFunction(isolate, tmpl, "mkdirat",    FileSystemMkDirAt,    3);
  DefineFunction(isolate, tmpl, "mkdir",      FileSystemMkDir,      2);
  DefineFunction(isolate, tmpl, "symlinkat",  FileSystemSymlinkAt,  3);
  DefineFunction(isolate, tmpl, "symlink",    FileSystemSymlink,    2);
  DefineFunction(isolate, tmpl, "readlinkat", FileSystemReadLinkAt, 2);
  DefineFunction(isolate, tmpl, "readlink",   FileSystemReadLink,   1);
  DefineFunction(isolate, tmpl, "getdents",   FileSystemGetDents,   1);
  DefineFunction(isolate, tmpl, "linkAt",     FileSystemLinkAt,     5);
  DefineFunction(isolate, tmpl, "link",       FileSystemLink,       2);
  DefineFunction(isolate, tmpl, "unlinkat",   FileSystemUnlinkAt,   3);
  DefineFunction(isolate, tmpl, "unlink",     FileSystemUnlink,     1);
  DefineFunction(isolate, tmpl, "rmdir",      FileSystemRmDir,      1);
  DefineFunction(isolate, tmpl, "renameat",   FileSystemRenameAt,   4);
  DefineFunction(isolate, tmpl, "rename",     FileSystemRename,     2);
  DefineFunction(isolate, tmpl, "lseek",      FileSystemLSeek,      3);
  DefineFunction(isolate, tmpl, "read",       FileSystemRead,       2);
  DefineFunction(isolate, tmpl, "readv",      FileSystemReadv,      2);
  DefineFunction(isolate, tmpl, "pread",      FileSystemPRead,      3);
  DefineFunction(isolate, tmpl, "preadv",     FileSystemPReadv,     3);
  DefineFunction(isolate, tmpl, "write",      FileSystemWrite,      2);
  DefineFunction(isolate, tmpl, "writev",     FileSystemWritev,     2);
  DefineFunction(isolate, tmpl, "pwrite",     FileSystemPWrite,     3);
  DefineFunction(isolate, tmpl, "pwritev",    FileSystemPWritev,    3);
  DefineFunction(isolate, tmpl, "sendfile",   FileSystemSendFile,   4);
  DefineFunction(isolate, tmpl, "ftruncate",  FileSystemFTruncate,  2);
  DefineFunction(isolate, tmpl, "truncate",   FileSystemTruncate,   2);
  DefineFunction(isolate, tmpl, "fchmodat",   FileSystemFChModAt,   3);
  DefineFunction(isolate, tmpl, "fchmod",     FileSystemFChMod,     2);
  DefineFunction(isolate, tmpl, "chmod",      FileSystemChMod,      2);
  DefineFunction(isolate, tmpl, "chdir",      FileSystemChDir,      1);
  DefineFunction(isolate, tmpl, "getcwd",     FileSystemGetCwd,     0);
  DefineFunction(isolate, tmpl, "fstat",      FileSystemFStat,      1);
  DefineFunction(isolate, tmpl, "stat",       FileSystemStat,       1);
  DefineFunction(isolate, tmpl, "lstat",      FileSystemLStat,      1);
  DefineFunction(isolate, tmpl, "statx",      FileSystemStatx,      4);
  DefineFunction(isolate, tmpl, "utimensat",  FileSystemUTimeNsAt,  4);
  DefineFunction(isolate, tmpl, "futimesat",  FileSystemFUTimesAt,  3);
  DefineFunction(isolate, tmpl, "utimes",     FileSystemUTimes,     2);
  DefineFunction(isolate, tmpl, "utime",      FileSystemUTime,      2);
  DefineFunction(isolate, tmpl, "dumpTo",     FileSystemDumpTo,     1);
}

NODE_MODULE_INIT() {
  Isolate* isolate = context->GetIsolate();
  Local<FunctionTemplate> FSTmpl = FunctionTemplate::New(
    isolate,
    FileSystemConstructor
  );
  FSTmpl->SetClassName(String::NewFromUtf8Literal(isolate, "FileSystem"));
  DefineConstants(isolate, FSTmpl);
  FSConstructorTmpl.Reset(isolate, FSTmpl);
  Local<ObjectTemplate> instTmpl = FSTmpl->InstanceTemplate();
  DefineTemplateFunctions(isolate, instTmpl);
  DefineTemplateFunctions(isolate, FSTmpl->PrototypeTemplate());
  DefineFunction(isolate, FSTmpl, "loadFrom", FileSystemLoadFrom, 1, PropertyAttribute::None);
  FSInstanceTmpl.Reset(isolate, instTmpl);
  Local<Function> FSFunc = FSTmpl->GetFunction(context).ToLocalChecked();
  module.As<Object>()->Set(
    context,
    String::NewFromUtf8Literal(isolate, "exports"),
    FSFunc
  ).Check();
}