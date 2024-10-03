#include "FileSystem.h"
#include "node.h"
#include "node_buffer.h"
#include <type_traits>

using namespace node;
using namespace v8;

namespace {
  template<typename T>
  bool TryAlloc(T** ptr, size_t length = 1) {
    T* newPtr = reinterpret_cast<T*>(malloc(sizeof(T) * length));
    if (!newPtr)
      return false;
    *ptr = newPtr;
    return true;
  }
  template<typename T>
  T Val(Local<Value> x) {
    if (x->IsBigInt()) {
      if constexpr (std::is_signed<T>::value)
        return x.As<BigInt>()->Int64Value();
      return x.As<BigInt>()->Uint64Value();
    }
    return x.As<Number>()->Value();
  }
  Persistent<FunctionTemplate> FSConstructorTmpl;
  Persistent<ObjectTemplate> FSInstanceTmpl;
}

#define IsBuffer(x) Buffer::HasInstance(x)
#define IsNumberLike(x) \
  (x->IsNumber() || x->IsBigInt())
#define IsDataLike(x) \
  (x->IsString() || Buffer::HasInstance(x))

#define THROWIFNOTFS(self, method) \
  do { \
    if (self.IsEmpty()) { \
      isolate->ThrowException( \
        Exception::TypeError( \
          String::NewFromUtf8Literal( \
            isolate, \
            "FileSystem.prototype." method " requires that 'this' be a FileSystem", \
            NewStringType::kInternalized \
          ) \
        ) \
      ); \
      return; \
    } \
  } while (0)
#define THROWERR(code) \
  do { \
    int errnum = -(code); \
    Local<Value> err = Exception::Error( \
      String::NewFromUtf8( \
        isolate, \
        strerror(errnum), \
        NewStringType::kInternalized \
      ).ToLocalChecked() \
    ); \
    err.As<Object>()->Set( \
      isolate->GetCurrentContext(), \
      String::NewFromUtf8Literal(isolate, "errno", NewStringType::kInternalized), \
      Integer::New(isolate, errnum) \
    ).ToChecked(); \
    isolate->ThrowException(err); \
    return; \
  } while (0)
#define THROWIFERR(res) \
  do { \
    auto tmp = (res); \
    if (tmp < 0) \
      THROWERR(tmp); \
  } while (0)

#define ASSERT(expr) { \
  if (!(expr)) { \
    isolate->ThrowException( \
      Exception::Error( \
        String::NewFromUtf8Literal( \
          isolate, \
          "Assertion \"" #expr "\" failed", \
          NewStringType::kInternalized \
        ) \
      ) \
    ); \
    return; \
  } \
}

void FileSystemCleanup(void* data, size_t, void*) {
  delete reinterpret_cast<FileSystem*>(data);
}

void FileSystemConstructor(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  if (!args.IsConstructCall()) {
    isolate->ThrowException(
      Exception::TypeError(
        String::NewFromUtf8Literal(
          isolate,
          "Constructor FileSystem requires 'new'",
          NewStringType::kInternalized
        )
      )
    );
    return;
  }
  FileSystem* fs = FileSystem::New();
  if (!fs) {
    isolate->LowMemoryNotification();
    fs = FileSystem::New();
    if (!fs)
      THROWERR(-ENOMEM);
  }
  std::unique_ptr<BackingStore> ab = ArrayBuffer::NewBackingStore(
    fs,
    sizeof(FileSystem),
    FileSystemCleanup,
    NULL
  );
  Local<ArrayBuffer> abuf = ArrayBuffer::New(isolate, std::move(ab));
  args.This()->SetInternalField(0, External::New(isolate, fs));
  args.This()->SetInternalField(1, abuf);
  args.This()->SetIntegrityLevel(isolate->GetCurrentContext(), IntegrityLevel::kFrozen);
}

void FileSystemFAccessAt2(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "faccessat2");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  ASSERT(IsNumberLike(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FAccessAt2(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<int>(args[2]),
      Val<int>(args[3])
    )
  );
}
void FileSystemFAccessAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "faccessat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FAccessAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<int>(args[2])
    )
  );
}
void FileSystemAccess(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "access");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Access(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Val<int>(args[1])
    )
  );
}
void FileSystemOpenAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "openat");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  ASSERT(IsNumberLike(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->OpenAt(
    Val<int>(args[0]),
    *String::Utf8Value(isolate, args[1].As<String>()),
    Val<int>(args[2]),
    Val<mode_t>(args[3])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->OpenAt(
        Val<int>(args[0]),
        *String::Utf8Value(isolate, args[1].As<String>()),
        Val<int>(args[2]),
        Val<mode_t>(args[3])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
  args.GetReturnValue().Set(
    Integer::New(
      isolate,
      res
    )
  );
}
void FileSystemOpen(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "open");
  ASSERT(args.Length() == 3);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumberLike(args[1]));
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->Open(
    *String::Utf8Value(isolate, args[0].As<String>()),
    Val<int>(args[1]),
    Val<mode_t>(args[2])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->Open(
        *String::Utf8Value(isolate, args[0].As<String>()),
        Val<int>(args[1]),
        Val<mode_t>(args[2])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
  args.GetReturnValue().Set(
    Integer::New(
      isolate,
      res
    )
  );
}
void FileSystemCreat(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "creat");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->Creat(
    *String::Utf8Value(isolate, args[0].As<String>()),
    Val<mode_t>(args[1])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->Creat(
        *String::Utf8Value(isolate, args[0].As<String>()),
        Val<mode_t>(args[1])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
  args.GetReturnValue().Set(
    Integer::New(
      isolate,
      res
    )
  );
}
void FileSystemClose(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "close");
  ASSERT(args.Length() == 1);
  ASSERT(IsNumberLike(args[0]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Close(Val<unsigned int>(args[0]))
  );
}
void FileSystemCloseRange(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "close_range");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->CloseRange(
      Val<unsigned int>(args[0]),
      Val<unsigned int>(args[1]),
      0
    )
  );
}
void FileSystemMkNodAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "mknodat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->MkNodAt(
    Val<int>(args[0]),
    *String::Utf8Value(isolate, args[1].As<String>()),
    Val<mode_t>(args[2]),
    0
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->MkNodAt(
        Val<int>(args[0]),
        *String::Utf8Value(isolate, args[1].As<String>()),
        Val<mode_t>(args[2]),
        0
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemMkNod(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "mknod");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->MkNod(
    *String::Utf8Value(isolate, args[0].As<String>()),
    Val<mode_t>(args[1]),
    0
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->MkNod(
        *String::Utf8Value(isolate, args[0].As<String>()),
        Val<mode_t>(args[1]),
        0
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemMkDirAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "mkdirat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->MkDirAt(
    Val<int>(args[0]),
    *String::Utf8Value(isolate, args[1].As<String>()),
    Val<mode_t>(args[2])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->MkDirAt(
        Val<int>(args[0]),
        *String::Utf8Value(isolate, args[1].As<String>()),
        Val<mode_t>(args[2])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemMkDir(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "mkdir");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->MkDir(
    *String::Utf8Value(isolate, args[0].As<String>()),
    Val<mode_t>(args[1])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->MkDir(
        *String::Utf8Value(isolate, args[0].As<String>()),
        Val<mode_t>(args[1])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemSymLinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "symlinkat");
  ASSERT(args.Length() == 3);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumberLike(args[1]));
  ASSERT(args[2]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->SymLinkAt(
    *String::Utf8Value(isolate, args[0].As<String>()),
    Val<int>(args[1]),
    *String::Utf8Value(isolate, args[2].As<String>())
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->SymLinkAt(
        *String::Utf8Value(isolate, args[0].As<String>()),
        Val<int>(args[1]),
        *String::Utf8Value(isolate, args[2].As<String>())
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemSymLink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "symlink");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->SymLink(
    *String::Utf8Value(isolate, args[0].As<String>()),
    *String::Utf8Value(isolate, args[1].As<String>())
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemReadLinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "readlinkat");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char* buf;
  if (!TryAlloc(&buf, PATH_MAX)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&buf, PATH_MAX))
      THROWERR(-ENOMEM);
  }
  int res;
  THROWIFERR(
    res = fs->ReadLinkAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      buf,
      PATH_MAX
    )
  );
  buf = reinterpret_cast<char*>(
    realloc(buf, res)
  );
  args.GetReturnValue().Set(
    String::NewFromUtf8(isolate, buf, NewStringType::kInternalized, res).ToLocalChecked()
  );
  delete buf;
}
void FileSystemReadLink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "readlink");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char* buf;
  if (!TryAlloc(&buf, PATH_MAX)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&buf, PATH_MAX))
      THROWERR(-ENOMEM);
  }
  int res;
  THROWIFERR(
    res = fs->ReadLink(
      *String::Utf8Value(isolate, args[0].As<String>()),
      buf,
      PATH_MAX
    )
  );
  buf = reinterpret_cast<char*>(
    realloc(buf, res)
  );
  args.GetReturnValue().Set(
    String::NewFromUtf8(isolate, buf, NewStringType::kInternalized, res).ToLocalChecked()
  );
  delete buf;
}
void FileSystemGetDents(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "getdents");
  ASSERT(args.Length() == 1 || args.Length() == 2);
  ASSERT(IsNumberLike(args[0]));
  if (args.Length() == 2)
    ASSERT(IsNumberLike(args[1]));
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  size_t count = -1;
  if (args.Length() == 2)
    count = Val<size_t>(args[1]);
  Local<Array> dentArr = Array::New(isolate);
  char* buf;
  if (!TryAlloc(&buf, 1024)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&buf, 1024))
      THROWERR(-ENOMEM);
  }
  unsigned int fdNum = Val<unsigned int>(args[0]);
  off_t off = fs->LSeek(fdNum, 0, SEEK_CUR);
  size_t i = 0;
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
    for (int j = 0; i < count && j != nread; ++i) {
      struct linux_dirent* dent = (struct linux_dirent*)(buf + j);
      Local<Object> dentObj = Object::New(isolate);
      dentObj->Set(
        context,
        String::NewFromUtf8Literal(isolate, "d_ino", NewStringType::kInternalized),
        BigInt::NewFromUnsigned(isolate, dent->d_ino)
      ).Check();
      dentObj->Set(
        context,
        String::NewFromUtf8Literal(isolate, "d_off", NewStringType::kInternalized),
        BigInt::NewFromUnsigned(isolate, dent->d_off)
      ).Check();
      dentObj->Set(
        context,
        String::NewFromUtf8Literal(isolate, "d_name", NewStringType::kInternalized),
        String::NewFromUtf8(isolate, dent->d_name).ToLocalChecked()
      ).Check();
      dentObj->Set(
        context,
        String::NewFromUtf8Literal(isolate, "d_type", NewStringType::kInternalized),
        Integer::New(isolate, (buf + j)[dent->d_reclen - 1])
      ).Check();
      dentObj->SetIntegrityLevel(context, IntegrityLevel::kFrozen);
      dentArr->Set(
        context,
        dentArr->Length(),
        dentObj
      ).Check();
      j += dent->d_reclen;
    }
  }
  dentArr->SetIntegrityLevel(context, IntegrityLevel::kFrozen);
  delete buf;
  fs->LSeek(fdNum, off, SEEK_SET);
  args.GetReturnValue().Set(dentArr);
}
void FileSystemLinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "linkat");
  ASSERT(args.Length() == 5);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  ASSERT(args[3]->IsString());
  ASSERT(IsNumberLike(args[4]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->LinkAt(
    Val<int>(args[0]),
    *String::Utf8Value(isolate, args[1].As<String>()),
    Val<int>(args[2]),
    *String::Utf8Value(isolate, args[3].As<String>()),
    Val<int>(args[4])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->LinkAt(
        Val<int>(args[0]),
        *String::Utf8Value(isolate, args[1].As<String>()),
        Val<int>(args[2]),
        *String::Utf8Value(isolate, args[3].As<String>()),
        Val<int>(args[4])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemLink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "link");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->Link(
    *String::Utf8Value(isolate, args[0].As<String>()),
    *String::Utf8Value(isolate, args[1].As<String>())
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->Link(
        *String::Utf8Value(isolate, args[0].As<String>()),
        *String::Utf8Value(isolate, args[1].As<String>())
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemUnlinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "unlinkat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->UnlinkAt(
    Val<int>(args[0]),
    *String::Utf8Value(isolate, args[1].As<String>()),
    Val<int>(args[2])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->UnlinkAt(
        Val<int>(args[0]),
        *String::Utf8Value(isolate, args[1].As<String>()),
        Val<int>(args[2])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemUnlink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "unlink");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->Unlink(
    *String::Utf8Value(isolate, args[0].As<String>())
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->Unlink(
        *String::Utf8Value(isolate, args[0].As<String>())
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemRmDir(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "rmdir");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->RmDir(
    *String::Utf8Value(isolate, args[0].As<String>())
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->RmDir(
        *String::Utf8Value(isolate, args[0].As<String>())
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemRenameAt2(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "renameat2");
  ASSERT(args.Length() == 5);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  ASSERT(args[3]->IsString());
  ASSERT(IsNumberLike(args[4]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->RenameAt2(
    Val<int>(args[0]),
    *String::Utf8Value(isolate, args[1].As<String>()),
    Val<int>(args[2]),
    *String::Utf8Value(isolate, args[3].As<String>()),
    Val<unsigned int>(args[4])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->RenameAt2(
        Val<int>(args[0]),
        *String::Utf8Value(isolate, args[1].As<String>()),
        Val<int>(args[2]),
        *String::Utf8Value(isolate, args[3].As<String>()),
        Val<unsigned int>(args[4])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemRenameAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "renameat");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  ASSERT(args[3]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->RenameAt(
    Val<int>(args[0]),
    *String::Utf8Value(isolate, args[1].As<String>()),
    Val<int>(args[2]),
    *String::Utf8Value(isolate, args[3].As<String>())
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->RenameAt(
        Val<int>(args[0]),
        *String::Utf8Value(isolate, args[1].As<String>()),
        Val<int>(args[2]),
        *String::Utf8Value(isolate, args[3].As<String>())
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemRename(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "rename");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->Rename(
    *String::Utf8Value(isolate, args[0].As<String>()),
    *String::Utf8Value(isolate, args[1].As<String>())
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->Rename(
        *String::Utf8Value(isolate, args[0].As<String>()),
        *String::Utf8Value(isolate, args[1].As<String>())
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemFAllocate(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "fallocate");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsNumberLike(args[1]));
  ASSERT(IsNumberLike(args[2]));
  ASSERT(IsNumberLike(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->FAllocate(
    Val<int>(args[0]),
    Val<int>(args[1]),
    Val<off_t>(args[2]),
    Val<off_t>(args[3])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->FAllocate(
        Val<int>(args[0]),
        Val<int>(args[1]),
        Val<off_t>(args[2]),
        Val<off_t>(args[3])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemLSeek(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "lseek");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsNumberLike(args[1]));
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  off_t res;
  THROWIFERR(
    res = fs->LSeek(
      Val<unsigned int>(args[0]),
      Val<off_t>(args[1]),
      Val<unsigned int>(args[2])
    )
  );
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemRead(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "read");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  unsigned int fdNum = Val<unsigned int>(args[0]);
  struct stat s;
  off_t off;
  THROWIFERR(fs->FStat(fdNum, &s));
  THROWIFERR(off = fs->LSeek(fdNum, 0, SEEK_CUR));
  size_t bufLen = std::min(
    std::min(
      Val<size_t>(args[1]),
      (size_t)(s.st_size - off)
    ),
    (size_t)0x7ffff000
  );
  char* buf;
  if (!TryAlloc(&buf, bufLen + 1)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&buf, bufLen + 1))
      THROWERR(-ENOMEM);
  }
  ssize_t res = fs->Read(
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
  THROWIFNOTFS(self, "readv");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsArray());
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Array> buffers = args[1].As<Array>();
  uint32_t length = std::min(buffers->Length(), (uint32_t)1024);
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    ASSERT(IsBuffer(buf));
  }
  struct iovec* iov;
  if (!TryAlloc(&iov, length)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&iov, length))
      THROWERR(-ENOMEM);
  }
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    iov[i].iov_base = Buffer::Data(buf);
    iov[i].iov_len = Buffer::Length(buf);
  }
  ssize_t res = fs->Readv(
    Val<unsigned int>(args[0]),
    iov,
    length
  );
  delete iov;
  if (res < 0)
    THROWERR(res);
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemPRead(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "pread");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsNumberLike(args[1]));
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  unsigned int fdNum = Val<unsigned int>(args[0]);
  struct stat s;
  off_t off;
  THROWIFERR(fs->FStat(fdNum, &s));
  THROWIFERR(off = fs->LSeek(fdNum, 0, SEEK_CUR));
  size_t bufLen = std::min(
    std::min(
      Val<size_t>(args[1]),
      (size_t)(s.st_size - off)
    ),
    (size_t)0x7ffff000
  );
  char* buf;
  if (!TryAlloc(&buf, bufLen + 1)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&buf, bufLen + 1))
      THROWERR(-ENOMEM);
  }
  ssize_t res = fs->PRead(
    fdNum,
    buf,
    bufLen,
    Val<off_t>(args[2])
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
  THROWIFNOTFS(self, "preadv");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsArray());
  ASSERT(IsNumberLike(args[2]));
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Array> buffers = args[1].As<Array>();
  uint32_t length = std::min(buffers->Length(), (uint32_t)1024);
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    ASSERT(IsBuffer(buf));
  }
  struct iovec* iov;
  if (!TryAlloc(&iov, length)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&iov, length))
      THROWERR(-ENOMEM);
  }
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    iov[i].iov_base = Buffer::Data(buf);
    iov[i].iov_len = Buffer::Length(buf);
  }
  ssize_t res = fs->PReadv(
    Val<unsigned int>(args[0]),
    iov,
    length,
    Val<off_t>(args[2])
  );
  delete iov;
  if (res < 0)
    THROWERR(res);
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemWrite(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "write");
  ASSERT(args.Length() == 2 || args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsDataLike(args[1]));
  if (args.Length() == 3)
    ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  bool isString = args[1]->IsString();
  size_t strLen = isString
    ? args[1].As<String>()->Utf8Length(isolate)
    : Buffer::Length(args[1]);
  size_t count;
  if (args.Length() == 3) {
    count = Val<size_t>(args[2]);
    if (count > strLen)
      count = strLen;
  } else count = strLen;
  char* buf;
  if (isString) {
    if (!TryAlloc(&buf, count)) {
      isolate->LowMemoryNotification();
      if (!TryAlloc(&buf, count))
        THROWERR(-ENOMEM);
    }
    Local<String> str = args[1].As<String>();
    str->WriteUtf8(isolate, buf, count);
  } else buf = Buffer::Data(args[1]);
  ssize_t res = fs->Write(
    Val<unsigned int>(args[0]),
    buf,
    count
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->Write(
        Val<unsigned int>(args[0]),
        buf,
        count
      );
      if (res < 0) {
        if (isString)
          delete buf;
        THROWERR(res);
      }
    } else {
      if (isString)
        delete buf;
      THROWERR(res);
    }
  }
  if (isString)
    delete buf;
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemWritev(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "writev");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsArray());
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Array> buffers = args[1].As<Array>();
  uint32_t length = std::min(buffers->Length(), (uint32_t)1024);
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    ASSERT(IsBuffer(buf));
  }
  struct iovec* iov;
  if (!TryAlloc(&iov, length)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&iov, length))
      THROWERR(-ENOMEM);
  }
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    iov[i].iov_base = Buffer::Data(buf);
    iov[i].iov_len = Buffer::Length(buf);
  }
  ssize_t res = fs->Writev(
    Val<unsigned int>(args[0]),
    iov,
    length
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->Writev(
        Val<unsigned int>(args[0]),
        iov,
        length
      );
      if (res < 0) {
        delete iov;
        THROWERR(res);
      }
    } else {
      delete iov;
      THROWERR(res);
    }
  }
  delete iov;
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemPWrite(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "pwrite");
  ASSERT(args.Length() == 3 || args.Length() == 4);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsDataLike(args[1]));
  ASSERT(IsNumberLike(args[2]));
  if (args.Length() == 4)
    ASSERT(IsNumberLike(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  bool isString = args[1]->IsString();
  size_t strLen = isString
    ? args[1].As<String>()->Utf8Length(isolate)
    : Buffer::Length(args[1]);
  size_t count;
  if (args.Length() == 4) {
    count = Val<size_t>(args[3]);
    if (count > strLen)
      count = strLen;
  } else count = strLen;
  char* buf;
  if (isString) {
    if (!TryAlloc(&buf, count)) {
      isolate->LowMemoryNotification();
      if (!TryAlloc(&buf, count))
        THROWERR(-ENOMEM);
    }
    Local<String> str = args[1].As<String>();
    str->WriteUtf8(isolate, buf, count);
  } else buf = Buffer::Data(args[1]);
  ssize_t res = fs->PWrite(
    Val<unsigned int>(args[0]),
    buf,
    count,
    Val<off_t>(args[2])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->PWrite(
        Val<unsigned int>(args[0]),
        buf,
        count,
        Val<off_t>(args[2])
      );
      if (res < 0) {
        if (isString)
          delete buf;
        THROWERR(res);
      }
    } else {
      if (isString)
        delete buf;
      THROWERR(res);
    }
  }
  if (isString)
    delete buf;
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemPWritev(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "pwritev");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsArray());
  ASSERT(IsNumberLike(args[2]));
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Array> buffers = args[1].As<Array>();
  uint32_t length = std::min(buffers->Length(), (uint32_t)1024);
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    ASSERT(IsBuffer(buf));
  }
  struct iovec* iov;
  if (!TryAlloc(&iov, length)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&iov, length))
      THROWERR(-ENOMEM);
  }
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    iov[i].iov_base = Buffer::Data(buf);
    iov[i].iov_len = Buffer::Length(buf);
  }
  ssize_t res = fs->PWritev(
    Val<unsigned int>(args[0]),
    iov,
    length,
    Val<off_t>(args[2])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->PWritev(
        Val<unsigned int>(args[0]),
        iov,
        length,
        Val<off_t>(args[2])
      );
      if (res < 0) {
        delete iov;
        THROWERR(res);
      }
    } else {
      delete iov;
      THROWERR(res);
    }
  }
  delete iov;
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemSendFile(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "sendfile");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsNumberLike(args[1]));
  ASSERT(args[2]->IsNull() || IsNumberLike(args[2]));
  ASSERT(IsNumberLike(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  off_t off;
  if (!args[2]->IsNull())
    off = Val<off_t>(args[2]);
  ssize_t res = fs->SendFile(
    Val<unsigned int>(args[0]),
    Val<unsigned int>(args[1]),
    args[2]->IsNull() ? NULL : &off,
    Val<size_t>(args[3])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->SendFile(
        Val<unsigned int>(args[0]),
        Val<unsigned int>(args[1]),
        args[2]->IsNull() ? NULL : &off,
        Val<size_t>(args[3])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemFTruncate(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "ftruncate");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FTruncate(
      Val<unsigned int>(args[0]),
      Val<off_t>(args[1])
    )
  );
}
void FileSystemTruncate(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "truncate");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->Truncate(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Val<off_t>(args[1])
    )
  );
}
void FileSystemFChModAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "fchmodat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FChModAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<mode_t>(args[2])
    )
  );
}
void FileSystemFChMod(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "fchmod");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FChMod(
      Val<unsigned int>(args[0]),
      Val<mode_t>(args[1])
    )
  );
}
void FileSystemChMod(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "chmod");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumberLike(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->ChMod(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Val<mode_t>(args[1])
    )
  );
}
void FileSystemChDir(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "chdir");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res = fs->ChDir(
    *String::Utf8Value(isolate, args[0].As<String>())
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->ChDir(
        *String::Utf8Value(isolate, args[0].As<String>())
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemGetCwd(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "getcwd");
  ASSERT(args.Length() == 0);
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char buf[PATH_MAX];
  THROWIFERR(fs->GetCwd(buf, PATH_MAX));
  args.GetReturnValue().Set(
    String::NewFromUtf8(isolate, buf, NewStringType::kInternalized).ToLocalChecked()
  );
}

void SetTimeProp(Isolate* isolate, Local<Object> obj, const char* prop, time_t sec, long nsec) {
  Local<Context> context = isolate->GetCurrentContext();
  Local<Object> tim = Object::New(isolate);
  tim->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_sec", NewStringType::kInternalized),
    Integer::NewFromUnsigned(isolate, sec)
  ).Check();
  tim->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_nsec", NewStringType::kInternalized),
    Integer::NewFromUnsigned(isolate, nsec)
  ).Check();
  tim->SetIntegrityLevel(context, IntegrityLevel::kFrozen);
  obj->Set(
    context,
    String::NewFromUtf8(isolate, prop, NewStringType::kInternalized).ToLocalChecked(),
    tim
  ).Check();
}

Local<Object> StatToObj(Isolate* isolate, struct stat s) {
  Local<Context> context = isolate->GetCurrentContext();
  Local<Object> statObj = Object::New(isolate);
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_ino", NewStringType::kInternalized),
    BigInt::NewFromUnsigned(isolate, s.st_ino)
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_mode", NewStringType::kInternalized),
    Integer::NewFromUnsigned(isolate, s.st_mode)
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_nlink", NewStringType::kInternalized),
    BigInt::NewFromUnsigned(isolate, s.st_nlink)
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_size", NewStringType::kInternalized),
    BigInt::NewFromUnsigned(isolate, s.st_size)
  ).Check();
  SetTimeProp(isolate, statObj, "st_atim", s.st_atim.tv_sec, s.st_atim.tv_nsec);
  SetTimeProp(isolate, statObj, "st_mtim", s.st_mtim.tv_sec, s.st_mtim.tv_nsec);
  SetTimeProp(isolate, statObj, "st_ctim", s.st_ctim.tv_sec, s.st_ctim.tv_nsec);
  statObj->SetIntegrityLevel(context, IntegrityLevel::kFrozen);
  return statObj;
}

void FileSystemFStat(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "fstat");
  ASSERT(args.Length() == 1);
  ASSERT(IsNumberLike(args[0]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  struct stat s;
  THROWIFERR(
    fs->FStat(
      Val<unsigned int>(args[0]),
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
  THROWIFNOTFS(self, "stat");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
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
  THROWIFNOTFS(self, "lstat");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
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
    String::NewFromUtf8Literal(isolate, "stx_ino", NewStringType::kInternalized),
    BigInt::NewFromUnsigned(isolate, s.stx_ino)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_mode", NewStringType::kInternalized),
    Integer::NewFromUnsigned(isolate, s.stx_mode)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_nlink", NewStringType::kInternalized),
    BigInt::NewFromUnsigned(isolate, s.stx_nlink)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_size", NewStringType::kInternalized),
    BigInt::NewFromUnsigned(isolate, s.stx_size)
  ).Check();
  SetTimeProp(isolate, statxObj, "stx_atime", s.stx_atime.tv_sec, s.stx_atime.tv_nsec);
  SetTimeProp(isolate, statxObj, "stx_mtime", s.stx_mtime.tv_sec, s.stx_mtime.tv_nsec);
  SetTimeProp(isolate, statxObj, "stx_ctime", s.stx_ctime.tv_sec, s.stx_ctime.tv_nsec);
  SetTimeProp(isolate, statxObj, "stx_btime", s.stx_btime.tv_sec, s.stx_btime.tv_nsec);
  statxObj->SetIntegrityLevel(context, IntegrityLevel::kFrozen);
  return statxObj;
}

void FileSystemStatx(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "statx");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  ASSERT(IsNumberLike(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  struct statx s;
  THROWIFERR(
    fs->Statx(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<int>(args[2]),
      Val<int>(args[3]),
      &s
    )
  );
  args.GetReturnValue().Set(
    StatxToObj(isolate, s)
  );
}
void FileSystemGetXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "getxattr");
  ASSERT(args.Length() == 3);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char* value = NULL;
  size_t valSize = Val<size_t>(args[2]);
  if (valSize != 0) {
    if (!TryAlloc(&value, valSize)) {
      isolate->LowMemoryNotification();
      if (!TryAlloc(&value, valSize))
        THROWERR(-ENOMEM);
    }
  }
  int res = fs->GetXAttr(
    *String::Utf8Value(isolate, args[0].As<String>()),
    *String::Utf8Value(isolate, args[1].As<String>()),
    value,
    valSize
  );
  if (res != 0) {
    if (valSize == 0) {
      if (res == -ENODATA) {
        args.GetReturnValue().Set(False(isolate));
        return;
      }
    } else delete value;
    THROWERR(res);
  }
  if (valSize == 0) {
    args.GetReturnValue().Set(True(isolate));
    return;
  }
  args.GetReturnValue().Set(
    Buffer::New(isolate, value, valSize).ToLocalChecked()
  );
}
void FileSystemLGetXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "lgetxattr");
  ASSERT(args.Length() == 3);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char* value = NULL;
  size_t valSize = Val<size_t>(args[2]);
  if (valSize != 0) {
    if (!TryAlloc(&value, valSize)) {
      isolate->LowMemoryNotification();
      if (!TryAlloc(&value, valSize))
        THROWERR(-ENOMEM);
    }
  }
  int res = fs->LGetXAttr(
    *String::Utf8Value(isolate, args[0].As<String>()),
    *String::Utf8Value(isolate, args[1].As<String>()),
    value,
    valSize
  );
  if (res != 0) {
    if (valSize == 0) {
      if (res == -ENODATA) {
        args.GetReturnValue().Set(False(isolate));
        return;
      }
    } else delete value;
    THROWERR(res);
  }
  if (valSize == 0) {
    args.GetReturnValue().Set(True(isolate));
    return;
  }
  args.GetReturnValue().Set(
    Buffer::New(isolate, value, valSize).ToLocalChecked()
  );
}
void FileSystemFGetXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "fgetxattr");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumberLike(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char* value = NULL;
  size_t valSize = Val<size_t>(args[2]);
  if (valSize != 0) {
    if (!TryAlloc(&value, valSize)) {
      isolate->LowMemoryNotification();
      if (!TryAlloc(&value, valSize))
        THROWERR(-ENOMEM);
    }
  }
  int res = fs->FGetXAttr(
    Val<int>(args[0]),
    *String::Utf8Value(isolate, args[1].As<String>()),
    value,
    valSize
  );
  if (res != 0) {
    if (valSize == 0) {
      if (res == -ENODATA) {
        args.GetReturnValue().Set(False(isolate));
        return;
      }
    } else delete value;
    THROWERR(res);
  }
  if (valSize == 0) {
    args.GetReturnValue().Set(True(isolate));
    return;
  }
  args.GetReturnValue().Set(
    Buffer::New(isolate, value, valSize).ToLocalChecked()
  );
}
void FileSystemSetXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "setxattr");
  ASSERT(args.Length() == 4 || args.Length() == 5);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  ASSERT(IsDataLike(args[2]));
  ASSERT(IsNumberLike(args[3]));
  if (args.Length() == 5)
    ASSERT(IsNumberLike(args[4]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  bool isString = args[2]->IsString();
  int res = fs->SetXAttr(
    *String::Utf8Value(isolate, args[0].As<String>()),
    *String::Utf8Value(isolate, args[1].As<String>()),
    isString ? *String::Utf8Value(isolate, args[2].As<String>()) : Buffer::Data(args[2]),
    args.Length() == 5
      ? Val<size_t>(args[3])
      : isString
        ? args[2].As<String>()->Utf8Length(isolate)
        : Buffer::Length(args[2]),
    args.Length() == 4 ? Val<size_t>(args[3]) : Val<size_t>(args[4])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->SetXAttr(
        *String::Utf8Value(isolate, args[0].As<String>()),
        *String::Utf8Value(isolate, args[1].As<String>()),
        isString ? *String::Utf8Value(isolate, args[2].As<String>()) : Buffer::Data(args[2]),
        args.Length() == 5
          ? Val<size_t>(args[3])
          : isString
            ? args[2].As<String>()->Utf8Length(isolate)
            : Buffer::Length(args[2]),
        args.Length() == 4 ? Val<size_t>(args[3]) : Val<size_t>(args[4])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemLSetXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "lsetxattr");
  ASSERT(args.Length() == 4 || args.Length() == 5);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  ASSERT(IsDataLike(args[2]));
  ASSERT(IsNumberLike(args[3]));
  if (args.Length() == 5)
    ASSERT(IsNumberLike(args[4]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  bool isString = args[2]->IsString();
  int res = fs->LSetXAttr(
    *String::Utf8Value(isolate, args[0].As<String>()),
    *String::Utf8Value(isolate, args[1].As<String>()),
    isString ? *String::Utf8Value(isolate, args[2].As<String>()) : Buffer::Data(args[2]),
    args.Length() == 5
      ? Val<size_t>(args[3])
      : isString
        ? args[2].As<String>()->Utf8Length(isolate)
        : Buffer::Length(args[2]),
    args.Length() == 4 ? Val<size_t>(args[3]) : Val<size_t>(args[4])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->LSetXAttr(
        *String::Utf8Value(isolate, args[0].As<String>()),
        *String::Utf8Value(isolate, args[1].As<String>()),
        isString ? *String::Utf8Value(isolate, args[2].As<String>()) : Buffer::Data(args[2]),
        args.Length() == 5
          ? Val<size_t>(args[3])
          : isString
            ? args[2].As<String>()->Utf8Length(isolate)
            : Buffer::Length(args[2]),
        args.Length() == 4 ? Val<size_t>(args[3]) : Val<size_t>(args[4])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemFSetXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "fsetxattr");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsDataLike(args[2]));
  ASSERT(IsNumberLike(args[3]));
  if (args.Length() == 5)
    ASSERT(IsNumberLike(args[4]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  bool isString = args[2]->IsString();
  int res = fs->FSetXAttr(
    Val<int>(args[0]),
    *String::Utf8Value(isolate, args[1].As<String>()),
    isString ? *String::Utf8Value(isolate, args[2].As<String>()) : Buffer::Data(args[2]),
    args.Length() == 5
      ? Val<size_t>(args[3])
      : isString
        ? args[2].As<String>()->Utf8Length(isolate)
        : Buffer::Length(args[2]),
    args.Length() == 4 ? Val<size_t>(args[3]) : Val<size_t>(args[4])
  );
  if (res < 0) {
    if (res == -ENOMEM) {
      isolate->LowMemoryNotification();
      res = fs->FSetXAttr(
        Val<int>(args[0]),
        *String::Utf8Value(isolate, args[1].As<String>()),
        isString ? *String::Utf8Value(isolate, args[2].As<String>()) : Buffer::Data(args[2]),
        args.Length() == 5
          ? Val<size_t>(args[3])
          : isString
            ? args[2].As<String>()->Utf8Length(isolate)
            : Buffer::Length(args[2]),
        args.Length() == 4 ? Val<size_t>(args[3]) : Val<size_t>(args[4])
      );
      if (res < 0)
        THROWERR(res);
    } else THROWERR(res);
  }
}
void FileSystemRemoveXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "removexattr");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->RemoveXAttr(
      *String::Utf8Value(isolate, args[0].As<String>()),
      *String::Utf8Value(isolate, args[1].As<String>())
    )
  );
}
void FileSystemLRemoveXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "lremovexattr");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->LRemoveXAttr(
      *String::Utf8Value(isolate, args[0].As<String>()),
      *String::Utf8Value(isolate, args[1].As<String>())
    )
  );
}
void FileSystemFRemoveXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "fremovexattr");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->FRemoveXAttr(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>())
    )
  );
}
void FileSystemListXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "listxattr");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  String::Utf8Value utf8Val = String::Utf8Value(isolate, args[0].As<String>());
  char* val = *utf8Val;
  ssize_t res = fs->ListXAttr(val, NULL, 0);
  if (res < 0)
    THROWERR(res);
  char* list;
  if (!TryAlloc(&list, res)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&list, res))
      THROWERR(-ENOMEM);
  }
  res = fs->ListXAttr(
    val,
    list,
    res
  );
  if (res < 0) {
    delete list;
    THROWERR(res);
  }
  Local<Array> listArr = Array::New(isolate);
  size_t i = 0;
  while (i != res) {
    size_t entryLen = strlen(&list[i]);
    listArr->Set(
      context,
      listArr->Length(),
      String::NewFromUtf8(
        isolate,
        &list[i],
        NewStringType::kNormal,
        entryLen
      ).ToLocalChecked()
    ).Check();
    i += entryLen + 1;
  }
  args.GetReturnValue().Set(listArr);
}
void FileSystemLListXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "listxattr");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  String::Utf8Value utf8Val = String::Utf8Value(isolate, args[0].As<String>());
  char* val = *utf8Val;
  ssize_t res = fs->LListXAttr(val, NULL, 0);
  if (res < 0)
    THROWERR(res);
  char* list;
  if (!TryAlloc(&list, res)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&list, res))
      THROWERR(-ENOMEM);
  }
  res = fs->LListXAttr(
    val,
    list,
    res
  );
  if (res < 0) {
    delete list;
    THROWERR(res);
  }
  Local<Array> listArr = Array::New(isolate);
  size_t i = 0;
  while (i != res) {
    size_t entryLen = strlen(&list[i]);
    listArr->Set(
      context,
      listArr->Length(),
      String::NewFromUtf8(
        isolate,
        &list[i],
        NewStringType::kNormal,
        entryLen
      ).ToLocalChecked()
    ).Check();
    i += entryLen + 1;
  }
  args.GetReturnValue().Set(listArr);
}
void FileSystemFListXAttr(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "listxattr");
  ASSERT(args.Length() == 1);
  ASSERT(IsNumberLike(args[0]));
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int val = Val<int>(args[0]);
  ssize_t res = fs->FListXAttr(val, NULL, 0);
  if (res < 0)
    THROWERR(res);
  char* list;
  if (!TryAlloc(&list, res)) {
    isolate->LowMemoryNotification();
    if (!TryAlloc(&list, res))
      THROWERR(-ENOMEM);
  }
  res = fs->FListXAttr(
    val,
    list,
    res
  );
  if (res < 0) {
    delete list;
    THROWERR(res);
  }
  Local<Array> listArr = Array::New(isolate);
  size_t i = 0;
  while (i != res) {
    size_t entryLen = strlen(&list[i]);
    listArr->Set(
      context,
      listArr->Length(),
      String::NewFromUtf8(
        isolate,
        &list[i],
        NewStringType::kNormal,
        entryLen
      ).ToLocalChecked()
    ).Check();
    i += entryLen + 1;
  }
  args.GetReturnValue().Set(listArr);
}
void FileSystemUTimeNsAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "utimensat");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(args[2]->IsNull() || args[2]->IsArray());
  ASSERT(IsNumberLike(args[3]));
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  struct timespec times[2];
  if (!args[2]->IsNull()) {
    Local<Value> atime = args[2].As<Array>()->Get(
      context,
      0
    ).ToLocalChecked();
    Local<Value> mtime = args[2].As<Array>()->Get(
      context,
      1
    ).ToLocalChecked();
    ASSERT(IsNumberLike(atime));
    ASSERT(IsNumberLike(mtime));
    time_t atimeVal = Val<time_t>(atime);
    time_t mtimeVal = Val<time_t>(atime);
    if (atimeVal != -1) {
      times[0].tv_sec = atimeVal / 1000.0;
      times[0].tv_nsec = atimeVal * 1000000.0;
    } else {
      times[0].tv_sec = 0;
      times[0].tv_nsec = UTIME_OMIT;
    }
    if (mtimeVal != -1) {
      times[1].tv_sec = mtimeVal / 1000.0;
      times[1].tv_nsec = mtimeVal * 1000000.0;
    } else {
      times[1].tv_sec = 0;
      times[1].tv_nsec = UTIME_OMIT;
    }
  }
  THROWIFERR(
    fs->UTimeNsAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      args[2]->IsNull() ? NULL : times,
      Val<int>(args[3])
    )
  );
}
void FileSystemFUTimesAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "futimesat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumberLike(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(args[2]->IsNull() || args[2]->IsArray());
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
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
    ASSERT(IsNumberLike(atime));
    ASSERT(IsNumberLike(mtime));
    double atimeVal = Val<time_t>(atime);
    double mtimeVal = Val<time_t>(mtime);
    times[0].tv_sec = atimeVal / 1000.0;
    times[0].tv_usec = atimeVal * 1000.0;
    times[1].tv_sec = mtimeVal / 1000.0;
    times[1].tv_usec = mtimeVal * 1000.0;
  }
  THROWIFERR(
    fs->FUTimesAt(
      Val<unsigned int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      args[2]->IsNull() ? NULL : times
    )
  );
}
void FileSystemUTimes(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "utimes");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsNull() || args[1]->IsArray());
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
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
    ASSERT(IsNumberLike(atime));
    ASSERT(IsNumberLike(mtime));
    double atimeVal = Val<time_t>(atime);
    double mtimeVal = Val<time_t>(mtime);
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
  THROWIFNOTFS(self, "utime");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsNull() || args[1]->IsArray());
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
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
    ASSERT(IsNumberLike(atime));
    ASSERT(IsNumberLike(mtime));
    times.actime = Val<time_t>(atime) / 1000.0;
    times.modtime = Val<time_t>(mtime) / 1000.0;
  }
  THROWIFERR(
    fs->UTime(
      *String::Utf8Value(isolate, args[0].As<String>()),
      args[1]->IsNull() ? NULL : &times
    )
  );
}
void FileSystemUMask(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "umask");
  ASSERT(args.Length() == 1);
  ASSERT(IsNumberLike(args[0]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  args.GetReturnValue().Set(
    Integer::New(
      isolate,
      fs->UMask(Val<int>(args[0]))
    )
  );
}

void FileSystemDumpTo(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "dumpTo");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  if (!fs->DumpToFile(*String::Utf8Value(isolate, args[0].As<String>())))
    THROWERR(-errno);
}

void FileSystemLoadFrom(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = fs->LoadFromFile(
    *String::Utf8Value(isolate, args[0].As<String>())
  );
  if (!fs) {
    if (errno == ENOMEM) {
      isolate->LowMemoryNotification();
      fs = fs->LoadFromFile(
        *String::Utf8Value(isolate, args[0].As<String>())
      );
    }
    if (!fs)
      THROWERR(-errno);
  }
  Local<Object> fsObj = FSInstanceTmpl.Get(isolate)->NewInstance(context).ToLocalChecked();
  std::unique_ptr<BackingStore> ab = ArrayBuffer::NewBackingStore(
    fs,
    sizeof(FileSystem),
    FileSystemCleanup,
    NULL
  );
  Local<ArrayBuffer> abuf = ArrayBuffer::New(isolate, std::move(ab));
  fsObj->SetInternalField(0, External::New(isolate, fs));
  fsObj->SetInternalField(1, abuf);
  fsObj->SetIntegrityLevel(context, IntegrityLevel::kFrozen);
  args.GetReturnValue().Set(fsObj);
}

void DefineConstants(Isolate* isolate, Local<FunctionTemplate> func) {
#define DefineFlag(v) \
  do { \
    func->Set( \
      String::NewFromUtf8Literal(isolate, #v, NewStringType::kInternalized), \
      Integer::New(isolate, v) \
    ); \
  } while (0)
  DefineFlag(AT_EMPTY_PATH);
  DefineFlag(AT_FDCWD);
  DefineFlag(AT_REMOVEDIR);
  DefineFlag(AT_SYMLINK_FOLLOW);
  DefineFlag(AT_SYMLINK_NOFOLLOW);
  DefineFlag(DT_DIR);
  DefineFlag(DT_LNK);
  DefineFlag(DT_REG);
  DefineFlag(FALLOC_FL_COLLAPSE_RANGE);
  DefineFlag(FALLOC_FL_INSERT_RANGE);
  DefineFlag(FALLOC_FL_KEEP_SIZE);
  DefineFlag(FALLOC_FL_PUNCH_HOLE);
  DefineFlag(FALLOC_FL_ZERO_RANGE);
  DefineFlag(O_ACCMODE);
  DefineFlag(O_APPEND);
  DefineFlag(O_CREAT);
  DefineFlag(O_DIRECTORY);
  DefineFlag(O_EXCL);
  DefineFlag(O_NOATIME);
  DefineFlag(O_NOFOLLOW);
  DefineFlag(O_RDONLY);
  DefineFlag(O_RDWR);
  DefineFlag(O_TMPFILE);
  DefineFlag(O_TRUNC);
  DefineFlag(O_WRONLY);
  DefineFlag(F_OK);
  DefineFlag(R_OK);
  DefineFlag(W_OK);
  DefineFlag(X_OK);
  DefineFlag(NAME_MAX);
  DefineFlag(PATH_MAX);
  DefineFlag(RENAME_EXCHANGE);
  DefineFlag(RENAME_NOREPLACE);
  DefineFlag(SEEK_CUR);
  DefineFlag(SEEK_DATA);
  DefineFlag(SEEK_END);
  DefineFlag(SEEK_HOLE);
  DefineFlag(SEEK_SET);
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
  DefineFlag(S_IFDIR);
  DefineFlag(S_IFLNK);
  DefineFlag(S_IFMT);
  DefineFlag(S_IFREG);
  DefineFlag(S_IRGRP);
  DefineFlag(S_IROTH);
  DefineFlag(S_IRUSR);
  DefineFlag(S_IRWXG);
  DefineFlag(S_IRWXO);
  DefineFlag(S_IRWXU);
  DefineFlag(S_IWGRP);
  DefineFlag(S_IWOTH);
  DefineFlag(S_IWUSR);
  DefineFlag(S_IXGRP);
  DefineFlag(S_IXOTH);
  DefineFlag(S_IXUSR);
  DefineFlag(UTIME_NOW);
  DefineFlag(UTIME_OMIT);
  DefineFlag(XATTR_CREATE);
  DefineFlag(XATTR_NAME_MAX);
  DefineFlag(XATTR_REPLACE);
  DefineFlag(XATTR_SIZE_MAX);

  DefineFlag(EACCES);
  DefineFlag(EBADF);
  DefineFlag(EBUSY);
  DefineFlag(EEXIST);
  DefineFlag(EFBIG);
  DefineFlag(EINVAL);
  DefineFlag(EISDIR);
  DefineFlag(ELOOP);
  DefineFlag(ENAMETOOLONG);
  DefineFlag(ENODATA);
  DefineFlag(ENODEV);
  DefineFlag(ENOENT);
  DefineFlag(ENOMEM);
  DefineFlag(ENOTDIR);
  DefineFlag(ENOTEMPTY);
  DefineFlag(EOPNOTSUPP);
  DefineFlag(EOVERFLOW);
  DefineFlag(EPERM);
  DefineFlag(ERANGE);
#undef DefineFlag
}

template<typename T, size_t N>
void DefineFunction(
  Isolate* isolate,
  Local<T> obj,
  const char (&prop)[N],
  FunctionCallback fn,
  int argc,
  PropertyAttribute attr = PropertyAttribute::DontEnum
) {
  obj->Set(
    String::NewFromUtf8Literal(isolate, prop, NewStringType::kInternalized),
    FunctionTemplate::New(
      isolate,
      fn,
      Local<Value>(),
      Local<Signature>(),
      argc,
      ConstructorBehavior::kThrow,
      SideEffectType::kHasSideEffect
    ),
    attr
  );
}
void DefineTemplateFunctions(Isolate* isolate, Local<ObjectTemplate> tmpl) {
  DefineFunction(isolate, tmpl, "faccessat2",   FileSystemFAccessAt2,   4);
  DefineFunction(isolate, tmpl, "faccessat",    FileSystemFAccessAt,    3);
  DefineFunction(isolate, tmpl, "access",       FileSystemAccess,       2);
  DefineFunction(isolate, tmpl, "openat",       FileSystemOpenAt,       4);
  DefineFunction(isolate, tmpl, "open",         FileSystemOpen,         3);
  DefineFunction(isolate, tmpl, "creat",        FileSystemCreat,        2);
  DefineFunction(isolate, tmpl, "close",        FileSystemClose,        1);
  DefineFunction(isolate, tmpl, "close_range",  FileSystemCloseRange,   2);
  DefineFunction(isolate, tmpl, "mknodat",      FileSystemMkNodAt,      3);
  DefineFunction(isolate, tmpl, "mknod",        FileSystemMkNod,        2);
  DefineFunction(isolate, tmpl, "mkdirat",      FileSystemMkDirAt,      3);
  DefineFunction(isolate, tmpl, "mkdir",        FileSystemMkDir,        2);
  DefineFunction(isolate, tmpl, "symlinkat",    FileSystemSymLinkAt,    3);
  DefineFunction(isolate, tmpl, "symlink",      FileSystemSymLink,      2);
  DefineFunction(isolate, tmpl, "readlinkat",   FileSystemReadLinkAt,   2);
  DefineFunction(isolate, tmpl, "readlink",     FileSystemReadLink,     1);
  DefineFunction(isolate, tmpl, "getdents",     FileSystemGetDents,     1);
  DefineFunction(isolate, tmpl, "linkat",       FileSystemLinkAt,       5);
  DefineFunction(isolate, tmpl, "link",         FileSystemLink,         2);
  DefineFunction(isolate, tmpl, "unlinkat",     FileSystemUnlinkAt,     3);
  DefineFunction(isolate, tmpl, "unlink",       FileSystemUnlink,       1);
  DefineFunction(isolate, tmpl, "rmdir",        FileSystemRmDir,        1);
  DefineFunction(isolate, tmpl, "renameat2",    FileSystemRenameAt2,    5);
  DefineFunction(isolate, tmpl, "renameat",     FileSystemRenameAt,     4);
  DefineFunction(isolate, tmpl, "rename",       FileSystemRename,       2);
  DefineFunction(isolate, tmpl, "fallocate",    FileSystemFAllocate,    4);
  DefineFunction(isolate, tmpl, "lseek",        FileSystemLSeek,        3);
  DefineFunction(isolate, tmpl, "read",         FileSystemRead,         2);
  DefineFunction(isolate, tmpl, "readv",        FileSystemReadv,        2);
  DefineFunction(isolate, tmpl, "pread",        FileSystemPRead,        3);
  DefineFunction(isolate, tmpl, "preadv",       FileSystemPReadv,       3);
  DefineFunction(isolate, tmpl, "write",        FileSystemWrite,        2);
  DefineFunction(isolate, tmpl, "writev",       FileSystemWritev,       2);
  DefineFunction(isolate, tmpl, "pwrite",       FileSystemPWrite,       3);
  DefineFunction(isolate, tmpl, "pwritev",      FileSystemPWritev,      3);
  DefineFunction(isolate, tmpl, "sendfile",     FileSystemSendFile,     4);
  DefineFunction(isolate, tmpl, "ftruncate",    FileSystemFTruncate,    2);
  DefineFunction(isolate, tmpl, "truncate",     FileSystemTruncate,     2);
  DefineFunction(isolate, tmpl, "fchmodat",     FileSystemFChModAt,     3);
  DefineFunction(isolate, tmpl, "fchmod",       FileSystemFChMod,       2);
  DefineFunction(isolate, tmpl, "chmod",        FileSystemChMod,        2);
  DefineFunction(isolate, tmpl, "chdir",        FileSystemChDir,        1);
  DefineFunction(isolate, tmpl, "getcwd",       FileSystemGetCwd,       0);
  DefineFunction(isolate, tmpl, "fstat",        FileSystemFStat,        1);
  DefineFunction(isolate, tmpl, "stat",         FileSystemStat,         1);
  DefineFunction(isolate, tmpl, "lstat",        FileSystemLStat,        1);
  DefineFunction(isolate, tmpl, "statx",        FileSystemStatx,        4);
  DefineFunction(isolate, tmpl, "getxattr",     FileSystemGetXAttr,     3);
  DefineFunction(isolate, tmpl, "lgetxattr",    FileSystemLGetXAttr,    3);
  DefineFunction(isolate, tmpl, "fgetxattr",    FileSystemFGetXAttr,    3);
  DefineFunction(isolate, tmpl, "setxattr",     FileSystemSetXAttr,     4);
  DefineFunction(isolate, tmpl, "lsetxattr",    FileSystemLSetXAttr,    4);
  DefineFunction(isolate, tmpl, "fsetxattr",    FileSystemFSetXAttr,    4);
  DefineFunction(isolate, tmpl, "removexattr",  FileSystemRemoveXAttr,  2);
  DefineFunction(isolate, tmpl, "lremovexattr", FileSystemLRemoveXAttr, 2);
  DefineFunction(isolate, tmpl, "fremovexattr", FileSystemFRemoveXAttr, 2);
  DefineFunction(isolate, tmpl, "listxattr",    FileSystemListXAttr,    1);
  DefineFunction(isolate, tmpl, "llistxattr",   FileSystemLListXAttr,   1);
  DefineFunction(isolate, tmpl, "flistxattr",   FileSystemFListXAttr,   1);
  DefineFunction(isolate, tmpl, "utimensat",    FileSystemUTimeNsAt,    4);
  DefineFunction(isolate, tmpl, "futimesat",    FileSystemFUTimesAt,    3);
  DefineFunction(isolate, tmpl, "utimes",       FileSystemUTimes,       2);
  DefineFunction(isolate, tmpl, "utime",        FileSystemUTime,        2);
  DefineFunction(isolate, tmpl, "umask",        FileSystemUMask,        1);
  DefineFunction(isolate, tmpl, "dumpTo",       FileSystemDumpTo,       1);
}

NODE_MODULE_INIT() {
  Isolate* isolate = context->GetIsolate();
  Local<FunctionTemplate> FSTmpl = FunctionTemplate::New(
    isolate,
    FileSystemConstructor
  );
  DefineConstants(isolate, FSTmpl);
  DefineFunction(isolate, FSTmpl, "loadFrom", FileSystemLoadFrom, 1, PropertyAttribute::None);
  FSConstructorTmpl.Reset(isolate, FSTmpl);
  Local<ObjectTemplate> instTmpl = FSTmpl->InstanceTemplate();
  instTmpl->SetInternalFieldCount(2);
  FSInstanceTmpl.Reset(isolate, instTmpl);
  DefineTemplateFunctions(isolate, FSTmpl->PrototypeTemplate());
  Local<Function> FSFunc = FSTmpl->GetFunction(context).ToLocalChecked();
  FSFunc->SetName(String::NewFromUtf8Literal(isolate, "FileSystem", NewStringType::kInternalized));
  FSFunc->SetIntegrityLevel(context, IntegrityLevel::kFrozen);
  module.As<Object>()->Set(
    context,
    String::NewFromUtf8Literal(isolate, "exports", NewStringType::kInternalized),
    FSFunc
  ).Check();
}