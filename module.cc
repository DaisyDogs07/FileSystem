#include "FileSystemCpp.h"
#include "node.h"
#include "node_buffer.h"
#include <type_traits>

#define LIKELY(expr) __builtin_expect(!!(expr), 1)
#define UNLIKELY(expr) __builtin_expect(!!(expr), 0)

using namespace node;
using namespace v8;

namespace {
  template<typename T>
  bool TryAlloc(T** ptr, size_t length = 1) {
    T* newPtr = reinterpret_cast<T*>(malloc(sizeof(T) * length));
    if (UNLIKELY(!newPtr))
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

#define IsNumeric(x) \
  (x->IsNumber() || x->IsBigInt())
#define IsStrOrBuf(x) \
  (x->IsString() || Buffer::HasInstance(x))

#define THROWIFNOTFS(self, method) \
  do { \
    if (UNLIKELY(self.IsEmpty())) { \
      isolate->ThrowException( \
        Exception::TypeError( \
          String::NewFromUtf8Literal( \
            isolate, \
            method " requires that 'this' be a FileSystem", \
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
  if (UNLIKELY(!(expr))) { \
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
  if (UNLIKELY(!args.IsConstructCall())) {
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
  if (UNLIKELY(!fs))
    THROWERR(-errno);
  std::unique_ptr<BackingStore> ab = ArrayBuffer::NewBackingStore(
    fs,
    sizeof(FileSystem),
    FileSystemCleanup,
    NULL
  );
  Local<ArrayBuffer> abuf = ArrayBuffer::New(isolate, std::move(ab));
  args.This()->SetInternalField(0, External::New(isolate, fs));
  args.This()->SetInternalField(1, abuf);
}

void FileSystemFAccessAt2(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.faccessat2");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
  ASSERT(IsNumeric(args[3]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.faccessat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.access");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumeric(args[1]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.openat");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
  ASSERT(IsNumeric(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    res = fs->OpenAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<int>(args[2]),
      Val<mode_t>(args[3])
    )
  );
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
  THROWIFNOTFS(self, "FileSystem.prototype.open");
  ASSERT(args.Length() == 3);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumeric(args[1]));
  ASSERT(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    res = fs->Open(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Val<int>(args[1]),
      Val<mode_t>(args[2])
    )
  );
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
  THROWIFNOTFS(self, "FileSystem.prototype.creat");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    res = fs->Creat(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Val<mode_t>(args[1])
    )
  );
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
  THROWIFNOTFS(self, "FileSystem.prototype.close");
  ASSERT(args.Length() == 1);
  ASSERT(IsNumeric(args[0]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.close_range");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsNumeric(args[1]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.mknodat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->MkNodAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<mode_t>(args[2]),
      0
    )
  );
}
void FileSystemMkNod(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.mknod");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->MkNod(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Val<mode_t>(args[1]),
      0
    )
  );
}
void FileSystemMkDirAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.mkdirat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->MkDirAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<mode_t>(args[2])
    )
  );
}
void FileSystemMkDir(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.mkdir");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumeric(args[1]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->MkDir(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Val<mode_t>(args[1])
    )
  );
}
void FileSystemSymLinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.symlinkat");
  ASSERT(args.Length() == 3);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumeric(args[1]));
  ASSERT(args[2]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->SymLinkAt(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Val<int>(args[1]),
      *String::Utf8Value(isolate, args[2].As<String>())
    )
  );
}
void FileSystemSymLink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.symlink");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->SymLink(
      *String::Utf8Value(isolate, args[0].As<String>()),
      *String::Utf8Value(isolate, args[1].As<String>())
    )
  );
}
void FileSystemReadLinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.readlinkat");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char* buf;
  if (UNLIKELY(!TryAlloc(&buf, PATH_MAX)))
    THROWERR(-ENOMEM);
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
  THROWIFNOTFS(self, "FileSystem.prototype.readlink");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  char* buf;
  if (UNLIKELY(!TryAlloc(&buf, PATH_MAX)))
    THROWERR(-ENOMEM);
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
  THROWIFNOTFS(self, "FileSystem.prototype.getdents");
  ASSERT(args.Length() == 1 || args.Length() == 2);
  ASSERT(IsNumeric(args[0]));
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  uint32_t count = std::numeric_limits<uint32_t>::max();
  if (args.Length() == 2) {
    ASSERT(IsNumeric(args[1]));
    count = Val<uint32_t>(args[1]);
  }
  Local<Array> dentArr = Array::New(isolate);
  char* buf;
  if (UNLIKELY(!TryAlloc(&buf, 1024)))
    THROWERR(-ENOMEM);
  unsigned int fdNum = Val<unsigned int>(args[0]);
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
      dentArr->Set(
        context,
        dentArr->Length(),
        dentObj
      ).Check();
      j += dent->d_reclen;
    }
  }
  delete buf;
  fs->LSeek(fdNum, off, SEEK_SET);
  args.GetReturnValue().Set(dentArr);
}
void FileSystemLinkAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.linkat");
  ASSERT(args.Length() == 5);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
  ASSERT(args[3]->IsString());
  ASSERT(IsNumeric(args[4]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->LinkAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<int>(args[2]),
      *String::Utf8Value(isolate, args[3].As<String>()),
      Val<int>(args[4])
    )
  );
}
void FileSystemLink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.link");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
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
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->UnlinkAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<int>(args[2])
    )
  );
}
void FileSystemUnlink(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.unlink");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
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
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->RmDir(
      *String::Utf8Value(isolate, args[0].As<String>())
    )
  );
}
void FileSystemRenameAt2(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.renameat2");
  ASSERT(args.Length() == 5);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
  ASSERT(args[3]->IsString());
  ASSERT(IsNumeric(args[4]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->RenameAt2(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<int>(args[2]),
      *String::Utf8Value(isolate, args[3].As<String>()),
      Val<unsigned int>(args[4])
    )
  );
}
void FileSystemRenameAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.renameat");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
  ASSERT(args[3]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  THROWIFERR(
    fs->RenameAt(
      Val<int>(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Val<int>(args[2]),
      *String::Utf8Value(isolate, args[3].As<String>())
    )
  );
}
void FileSystemRename(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.rename");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsString());
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
void FileSystemFAllocate(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.fallocate");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsNumeric(args[1]));
  ASSERT(IsNumeric(args[2]));
  ASSERT(IsNumeric(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  off_t res;
  THROWIFERR(
    res = fs->FAllocate(
      Val<int>(args[0]),
      Val<int>(args[1]),
      Val<off_t>(args[2]),
      Val<off_t>(args[3])
    )
  );
}
void FileSystemLSeek(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.lseek");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsNumeric(args[1]));
  ASSERT(IsNumeric(args[2]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.read");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsNumeric(args[1]));
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
  if (UNLIKELY(!TryAlloc(&buf, bufLen + 1)))
    THROWERR(-ENOMEM);
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
  THROWIFNOTFS(self, "FileSystem.prototype.readv");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsArray());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> buffers = args[1].As<Array>();
  uint32_t length = std::min(buffers->Length(), (uint32_t)1024);
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    ASSERT(Buffer::HasInstance(buf));
  }
  struct iovec* iov;
  if (UNLIKELY(!TryAlloc(&iov, length)))
    THROWERR(-ENOMEM);
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    ASSERT(Buffer::HasInstance(buf));
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
  THROWIFNOTFS(self, "FileSystem.prototype.pread");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsNumeric(args[1]));
  ASSERT(IsNumeric(args[2]));
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
  if (UNLIKELY(!TryAlloc(&buf, bufLen + 1)))
    THROWERR(-ENOMEM);
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
  THROWIFNOTFS(self, "FileSystem.prototype.preadv");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsArray());
  ASSERT(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> buffers = args[1].As<Array>();
  uint32_t length = std::min(buffers->Length(), (uint32_t)1024);
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    ASSERT(Buffer::HasInstance(buf));
  }
  struct iovec* iov;
  if (UNLIKELY(!TryAlloc(&iov, length)))
    THROWERR(-ENOMEM);
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
  THROWIFNOTFS(self, "FileSystem.prototype.write");
  ASSERT(args.Length() == 2 || args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsStrOrBuf(args[1]));
  if (args.Length() == 3)
    ASSERT(IsNumeric(args[2]));
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
    if (UNLIKELY(!TryAlloc(&buf, count)))
      THROWERR(-ENOMEM);
    Local<String> str = args[1].As<String>();
    str->WriteUtf8(isolate, buf, count);
  } else buf = Buffer::Data(args[1]);
  ssize_t res;
  THROWIFERR(
    res = fs->Write(
      Val<unsigned int>(args[0]),
      buf,
      count
    )
  );
  if (isString)
    delete buf;
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemWritev(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.writev");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsArray());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> buffers = args[1].As<Array>();
  uint32_t length = std::min(buffers->Length(), (uint32_t)1024);
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    ASSERT(Buffer::HasInstance(buf));
  }
  struct iovec* iov;
  if (UNLIKELY(!TryAlloc(&iov, length)))
    THROWERR(-ENOMEM);
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
  delete iov;
  if (res < 0)
    THROWERR(res);
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemPWrite(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.pwrite");
  ASSERT(args.Length() == 3 || args.Length() == 4);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsStrOrBuf(args[1]));
  ASSERT(IsNumeric(args[2]));
  if (args.Length() == 4)
    ASSERT(IsNumeric(args[3]));
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
    if (UNLIKELY(!TryAlloc(&buf, count)))
      THROWERR(-ENOMEM);
    Local<String> str = args[1].As<String>();
    str->WriteUtf8(isolate, buf, count);
  } else buf = Buffer::Data(args[1]);
  ssize_t res;
  THROWIFERR(
    res = fs->PWrite(
      Val<unsigned int>(args[0]),
      buf,
      count,
      Val<off_t>(args[2])
    )
  );
  if (isString)
    delete buf;
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemPWritev(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.pwritev");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsArray());
  ASSERT(IsNumeric(args[2]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> buffers = args[1].As<Array>();
  uint32_t length = std::min(buffers->Length(), (uint32_t)1024);
  for (uint32_t i = 0; i != length; ++i) {
    Local<Value> buf = buffers->Get(
      context,
      i
    ).ToLocalChecked();
    ASSERT(Buffer::HasInstance(buf));
  }
  struct iovec* iov;
  if (UNLIKELY(!TryAlloc(&iov, length)))
    THROWERR(-ENOMEM);
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
  delete iov;
  if (res < 0)
    THROWERR(res);
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemSendFile(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.sendfile");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsNumeric(args[1]));
  ASSERT(args[2]->IsNull() || IsNumeric(args[2]));
  ASSERT(IsNumeric(args[3]));
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  off_t off;
  if (!args[2]->IsNull())
    off = Val<off_t>(args[2]);
  ssize_t res;
  THROWIFERR(
    res = fs->SendFile(
      Val<unsigned int>(args[0]),
      Val<unsigned int>(args[1]),
      args[2]->IsNull() ? NULL : &off,
      Val<size_t>(args[3])
    )
  );
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemFTruncate(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.ftruncate");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsNumeric(args[1]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.truncate");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumeric(args[1]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.fchmodat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.fchmod");
  ASSERT(args.Length() == 2);
  ASSERT(IsNumeric(args[0]));
  ASSERT(IsNumeric(args[1]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.chmod");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(IsNumeric(args[1]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.chdir");
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
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
  return statObj;
}

void FileSystemFStat(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.fstat");
  ASSERT(args.Length() == 1);
  ASSERT(IsNumeric(args[0]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.stat");
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
  THROWIFNOTFS(self, "FileSystem.prototype.lstat");
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
  return statxObj;
}

void FileSystemStatx(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.statx");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(IsNumeric(args[2]));
  ASSERT(IsNumeric(args[3]));
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
void FileSystemUTimeNsAt(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Object> self = args.This()->FindInstanceInPrototypeChain(FSConstructorTmpl.Get(isolate));
  THROWIFNOTFS(self, "FileSystem.prototype.utimensat");
  ASSERT(args.Length() == 4);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(args[2]->IsNull() || args[2]->IsArray());
  if (args[2]->IsArray())
    ASSERT(args[2].As<Array>()->Length() == 2);
  ASSERT(IsNumeric(args[3]));
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
  THROWIFNOTFS(self, "FileSystem.prototype.futimesat");
  ASSERT(args.Length() == 3);
  ASSERT(IsNumeric(args[0]));
  ASSERT(args[1]->IsString());
  ASSERT(args[2]->IsNull() || args[2]->IsArray());
  if (args[2]->IsArray())
    ASSERT(args[2].As<Array>()->Length() == 2);
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
    ASSERT(atime->IsNumber());
    ASSERT(mtime->IsNumber());
    double atimeVal = atime.As<Number>()->Value();
    double mtimeVal = mtime.As<Number>()->Value();
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
  THROWIFNOTFS(self, "FileSystem.prototype.utimes");
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsNull() || args[1]->IsArray());
  if (args[1]->IsArray())
    ASSERT(args[1].As<Array>()->Length() == 2);
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
    ASSERT(atime->IsNumber());
    ASSERT(mtime->IsNumber());
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
  ASSERT(args.Length() == 2);
  ASSERT(args[0]->IsString());
  ASSERT(args[1]->IsNull() || args[1]->IsArray());
  if (args[1]->IsArray())
    ASSERT(args[1].As<Array>()->Length() == 2);
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
    ASSERT(atime->IsNumber());
    ASSERT(mtime->IsNumber());
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
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    self->GetInternalField(0).As<External>()->Value()
  );
  if (!fs->DumpToFile(
        *String::Utf8Value(isolate, args[0].As<String>())
      )) {
    isolate->ThrowException(
      Exception::Error(
        String::NewFromUtf8(isolate, strerror(errno), NewStringType::kInternalized).ToLocalChecked()
      )
    );
    errno = 0;
  }
}

void FileSystemLoadFrom(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  ASSERT(args.Length() == 1);
  ASSERT(args[0]->IsString());
  errno = 0;
  FileSystem* fs = fs->LoadFromFile(
    *String::Utf8Value(isolate, args[0].As<String>())
  );
  if (!fs) {
    if (errno) {
      isolate->ThrowException(
        Exception::Error(
          String::NewFromUtf8(
            isolate,
            strerror(errno),
            NewStringType::kInternalized
          ).ToLocalChecked()
        )
      );
    } else {
      isolate->ThrowException(
        Exception::Error(
          String::NewFromUtf8Literal(
            isolate,
            "FileSystem corrupt or invalid",
            NewStringType::kInternalized
          )
        )
      );
    }
    return;
  }
  Local<Context> context = isolate->GetCurrentContext();
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
  DefineFlag(DT_REG);
  DefineFlag(DT_DIR);
  DefineFlag(DT_LNK);
  DefineFlag(FALLOC_FL_KEEP_SIZE);
  DefineFlag(FALLOC_FL_PUNCH_HOLE);
  DefineFlag(FALLOC_FL_COLLAPSE_RANGE);
  DefineFlag(FALLOC_FL_ZERO_RANGE);
  DefineFlag(FALLOC_FL_INSERT_RANGE);
  DefineFlag(O_APPEND);
  DefineFlag(O_CREAT);
  DefineFlag(O_DIRECTORY);
  DefineFlag(O_EXCL);
  DefineFlag(O_NOATIME);
  DefineFlag(O_NOFOLLOW);
  DefineFlag(O_TRUNC);
  DefineFlag(O_TMPFILE);
  DefineFlag(O_RDONLY);
  DefineFlag(O_WRONLY);
  DefineFlag(O_RDWR);
  DefineFlag(RENAME_EXCHANGE);
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
  DefineFlag(SEEK_DATA);
  DefineFlag(SEEK_HOLE);
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
  DefineFlag(UTIME_NOW);
  DefineFlag(UTIME_OMIT);
  DefineFlag(R_OK);
  DefineFlag(W_OK);
  DefineFlag(X_OK);
  DefineFlag(F_OK);

  DefineFlag(EPERM);
  DefineFlag(ENOENT);
  DefineFlag(EIO);
  DefineFlag(EBADF);
  DefineFlag(ENOMEM);
  DefineFlag(EACCES);
  DefineFlag(EBUSY);
  DefineFlag(EEXIST);
  DefineFlag(ENODEV);
  DefineFlag(ENOTDIR);
  DefineFlag(EISDIR);
  DefineFlag(EINVAL);
  DefineFlag(EFBIG);
  DefineFlag(ERANGE);
  DefineFlag(ENAMETOOLONG);
  DefineFlag(ENOTEMPTY);
  DefineFlag(ELOOP);
  DefineFlag(EOVERFLOW);
  DefineFlag(EOPNOTSUPP);
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
  Local<String> name = String::NewFromUtf8Literal(isolate, prop, NewStringType::kInternalized);
  obj->Set(
    name,
    funcTmpl,
    attr
  );
}
void DefineTemplateFunctions(Isolate* isolate, Local<ObjectTemplate> tmpl) {
  DefineFunction(isolate, tmpl, "faccessat2",  FileSystemFAccessAt2, 4);
  DefineFunction(isolate, tmpl, "faccessat",   FileSystemFAccessAt,  3);
  DefineFunction(isolate, tmpl, "access",      FileSystemAccess,     2);
  DefineFunction(isolate, tmpl, "openat",      FileSystemOpenAt,     4);
  DefineFunction(isolate, tmpl, "open",        FileSystemOpen,       3);
  DefineFunction(isolate, tmpl, "creat",       FileSystemCreat,      2);
  DefineFunction(isolate, tmpl, "close",       FileSystemClose,      1);
  DefineFunction(isolate, tmpl, "close_range", FileSystemCloseRange, 2);
  DefineFunction(isolate, tmpl, "mknodat",     FileSystemMkNodAt,    3);
  DefineFunction(isolate, tmpl, "mknod",       FileSystemMkNod,      2);
  DefineFunction(isolate, tmpl, "mkdirat",     FileSystemMkDirAt,    3);
  DefineFunction(isolate, tmpl, "mkdir",       FileSystemMkDir,      2);
  DefineFunction(isolate, tmpl, "symlinkat",   FileSystemSymLinkAt,  3);
  DefineFunction(isolate, tmpl, "symlink",     FileSystemSymLink,    2);
  DefineFunction(isolate, tmpl, "readlinkat",  FileSystemReadLinkAt, 2);
  DefineFunction(isolate, tmpl, "readlink",    FileSystemReadLink,   1);
  DefineFunction(isolate, tmpl, "getdents",    FileSystemGetDents,   1);
  DefineFunction(isolate, tmpl, "linkat",      FileSystemLinkAt,     5);
  DefineFunction(isolate, tmpl, "link",        FileSystemLink,       2);
  DefineFunction(isolate, tmpl, "unlinkat",    FileSystemUnlinkAt,   3);
  DefineFunction(isolate, tmpl, "unlink",      FileSystemUnlink,     1);
  DefineFunction(isolate, tmpl, "rmdir",       FileSystemRmDir,      1);
  DefineFunction(isolate, tmpl, "renameat2",   FileSystemRenameAt2,  5);
  DefineFunction(isolate, tmpl, "renameat",    FileSystemRenameAt,   4);
  DefineFunction(isolate, tmpl, "rename",      FileSystemRename,     2);
  DefineFunction(isolate, tmpl, "fallocate",   FileSystemFAllocate,  4);
  DefineFunction(isolate, tmpl, "lseek",       FileSystemLSeek,      3);
  DefineFunction(isolate, tmpl, "read",        FileSystemRead,       2);
  DefineFunction(isolate, tmpl, "readv",       FileSystemReadv,      2);
  DefineFunction(isolate, tmpl, "pread",       FileSystemPRead,      3);
  DefineFunction(isolate, tmpl, "preadv",      FileSystemPReadv,     3);
  DefineFunction(isolate, tmpl, "write",       FileSystemWrite,      2);
  DefineFunction(isolate, tmpl, "writev",      FileSystemWritev,     2);
  DefineFunction(isolate, tmpl, "pwrite",      FileSystemPWrite,     3);
  DefineFunction(isolate, tmpl, "pwritev",     FileSystemPWritev,    3);
  DefineFunction(isolate, tmpl, "sendfile",    FileSystemSendFile,   4);
  DefineFunction(isolate, tmpl, "ftruncate",   FileSystemFTruncate,  2);
  DefineFunction(isolate, tmpl, "truncate",    FileSystemTruncate,   2);
  DefineFunction(isolate, tmpl, "fchmodat",    FileSystemFChModAt,   3);
  DefineFunction(isolate, tmpl, "fchmod",      FileSystemFChMod,     2);
  DefineFunction(isolate, tmpl, "chmod",       FileSystemChMod,      2);
  DefineFunction(isolate, tmpl, "chdir",       FileSystemChDir,      1);
  DefineFunction(isolate, tmpl, "getcwd",      FileSystemGetCwd,     0);
  DefineFunction(isolate, tmpl, "fstat",       FileSystemFStat,      1);
  DefineFunction(isolate, tmpl, "stat",        FileSystemStat,       1);
  DefineFunction(isolate, tmpl, "lstat",       FileSystemLStat,      1);
  DefineFunction(isolate, tmpl, "statx",       FileSystemStatx,      4);
  DefineFunction(isolate, tmpl, "utimensat",   FileSystemUTimeNsAt,  4);
  DefineFunction(isolate, tmpl, "futimesat",   FileSystemFUTimesAt,  3);
  DefineFunction(isolate, tmpl, "utimes",      FileSystemUTimes,     2);
  DefineFunction(isolate, tmpl, "utime",       FileSystemUTime,      2);
  DefineFunction(isolate, tmpl, "dumpTo",      FileSystemDumpTo,     1);
}

NODE_MODULE_INIT() {
  Isolate* isolate = context->GetIsolate();
  Local<FunctionTemplate> FSTmpl = FunctionTemplate::New(
    isolate,
    FileSystemConstructor
  );
  FSTmpl->SetClassName(
    String::NewFromUtf8Literal(isolate, "FileSystem", NewStringType::kInternalized)
  );
  DefineConstants(isolate, FSTmpl);
  DefineFunction(isolate, FSTmpl, "loadFrom", FileSystemLoadFrom, 1, PropertyAttribute::None);
  FSConstructorTmpl.Reset(isolate, FSTmpl);
  Local<ObjectTemplate> instTmpl = FSTmpl->InstanceTemplate();
  instTmpl->SetInternalFieldCount(2);
  FSInstanceTmpl.Reset(isolate, instTmpl);
  DefineTemplateFunctions(isolate, FSTmpl->PrototypeTemplate());
  Local<Function> FSFunc = FSTmpl->GetFunction(context).ToLocalChecked();
  module.As<Object>()->Set(
    context,
    String::NewFromUtf8Literal(isolate, "exports", NewStringType::kInternalized),
    FSFunc
  ).Check();
}