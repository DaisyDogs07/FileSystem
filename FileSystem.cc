#include <iostream>
#include <cassert>
#include "./FileSystem.h"
#include "node.h"
#include "node_buffer.h"

using namespace node;
using namespace v8;

void FileSystemCleanup(void* data) {
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
  AtExit(GetCurrentEnvironment(isolate->GetCurrentContext()), FileSystemCleanup, fs);
  args.This()->SetInternalField(0, External::New(isolate, fs));
  args.GetReturnValue().Set(args.This());
}

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
    : static_cast<char*>( \
        x.As<ArrayBufferView>()->Buffer()->GetBackingStore()->Data() \
      ) + x.As<ArrayBufferView>()->ByteOffset())
#define StringLen(x) \
  (x->IsString() \
    ? x.As<String>()->Utf8Length(isolate) \
    : x.As<ArrayBufferView>()->ByteLength())

#define THROWIFERR(expr, res) \
  do { \
    if ((res = (expr)) < 0) { \
      isolate->ThrowException( \
        Exception::Error( \
          String::NewFromUtf8( \
            isolate, \
            strerror(-res) \
          ).ToLocalChecked() \
        ) \
      ); \
      return; \
    } \
  } while (0)
#define IsNumeric(x) \
  (x->IsNumber() || x->IsBigInt())
#define IsStrOrBuf(x) \
  (x->IsString() || x->IsArrayBufferView())

void FileSystemFAccessAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 4);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(IsNumeric(args[3]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->FAccessAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      Uint32Val(args[3])
    ), res
  );
}
void FileSystemAccess(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->Access(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Int32Val(args[1])
    ), res
  );
}
void FileSystemOpenAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 4);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(IsNumeric(args[3]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->OpenAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      Uint32Val(args[3])
    ), res
  );
  args.GetReturnValue().Set(
    Int32::New(
      isolate,
      res
    )
  );
}
void FileSystemOpen(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 3);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  assert(IsNumeric(args[2]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->Open(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Int32Val(args[1]),
      Uint32Val(args[2])
    ), res
  );
  args.GetReturnValue().Set(
    Int32::New(
      isolate,
      res
    )
  );
}
void FileSystemClose(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 1);
  assert(IsNumeric(args[0]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->Close(
      Int32Val(args[0])
    ), res
  );
}
void FileSystemMkNodAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->MkNodAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Uint32Val(args[2]),
      0
    ), res
  );
}
void FileSystemMkNod(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->MkNod(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Uint32Val(args[1]),
      0
    ), res
  );
}
void FileSystemMkDirAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->MkDirAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Uint32Val(args[2])
    ), res
  );
}
void FileSystemMkDir(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->MkDir(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Uint32Val(args[1])
    ), res
  );
}
void FileSystemSymlinkAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 3);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  assert(args[2]->IsString());
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->SymlinkAt(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Int32Val(args[1]),
      *String::Utf8Value(isolate, args[2].As<String>())
    ), res
  );
}
void FileSystemSymlink(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(args[1]->IsString());
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->Symlink(
      *String::Utf8Value(isolate, args[0].As<String>()),
      *String::Utf8Value(isolate, args[1].As<String>())
    ), res
  );
}
void FileSystemReadLinkAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int bufLen = Int32Val(args[2]);
  char* buf = new char[bufLen + 1];
  int res;
  THROWIFERR(
    fs->ReadLinkAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      buf,
      bufLen
    ), res
  );
  if (res < bufLen)
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
void FileSystemReadLink(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int bufLen = Int32Val(args[1]);
  char* buf = new char[bufLen + 1];
  int res;
  THROWIFERR(
    fs->ReadLink(
      *String::Utf8Value(isolate, args[0].As<String>()),
      buf,
      bufLen
    ), res
  );
  if (res < bufLen)
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
void FileSystemGetDents(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 1);
  assert(IsNumeric(args[0]));
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  Local<Array> dentArr = Array::New(isolate);
  char buf[1024];
  unsigned int fdNum = Int32Val(args[0]);
  while (true) {
    int nread;
    THROWIFERR(
      fs->GetDents(
        fdNum,
        (struct linux_dirent*)buf,
        1024
      ), nread
    );
    if (nread < 0) {
      args.GetReturnValue().Set(Int32::New(isolate, nread));
      return;
    }
    if (nread == 0) {
      args.GetReturnValue().Set(dentArr);
      return;
    }
    for (int i = 0; i < nread;) {
      struct linux_dirent* dent = (struct linux_dirent*)(buf + i);
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
        Int32::New(isolate, (buf + i)[dent->d_reclen - 1])
      ).Check();
      dentArr->Set(
        context,
        dentArr->Length(),
        dentObj
      ).Check();
      i += dent->d_reclen;
    }
  }
}
void FileSystemLinkAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 5);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(args[3]->IsString());
  assert(IsNumeric(args[4]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->LinkAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      *String::Utf8Value(isolate, args[3].As<String>()),
      Int32Val(args[4])
    ), res
  );
}
void FileSystemLink(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(args[1]->IsString());
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->Link(
      *String::Utf8Value(isolate, args[0].As<String>()),
      *String::Utf8Value(isolate, args[1].As<String>())
    ), res
  );
}
void FileSystemUnlinkAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->UnlinkAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2])
    ), res
  );
}
void FileSystemUnlink(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->Unlink(
      *String::Utf8Value(isolate, args[0].As<String>())
    ), res
  );
}
void FileSystemRmDir(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->RmDir(
      *String::Utf8Value(isolate, args[0].As<String>())
    ), res
  );
}
void FileSystemRenameAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 5);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(args[3]->IsString());
  assert(IsNumeric(args[4]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->RenameAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      *String::Utf8Value(isolate, args[3].As<String>()),
      Int32Val(args[4])
    ), res
  );
}
void FileSystemRename(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(args[2]->IsString());
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->Rename(
      *String::Utf8Value(isolate, args[0].As<String>()),
      *String::Utf8Value(isolate, args[1].As<String>())
    ), res
  );
}
void FileSystemLSeek(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  assert(IsNumeric(args[2]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  off_t res;
  THROWIFERR(
    fs->LSeek(
      Uint32Val(args[0]),
      Int64Val(args[1]),
      Uint32Val(args[2])
    ), res
  );
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemRead(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  size_t bufLen = Uint64Val(args[1]);
  char* buf = new char[bufLen + 1];
  ssize_t res;
  THROWIFERR(
    fs->Read(
      Uint32Val(args[0]),
      buf,
      bufLen
    ), res
  );
  if (res < bufLen)
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

void FileSystemWrite(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(IsStrOrBuf(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  ssize_t res;
  THROWIFERR(
    fs->Write(
      Uint32Val(args[0]),
      StringVal(args[1]),
      StringLen(args[1])
    ), res
  );
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemSendFile(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 4);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  assert(args[2]->IsNull() || IsNumeric(args[2]));
  assert(IsNumeric(args[3]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  off_t* off = NULL;
  if (!args[2]->IsNull())
    *off = Int64Val(args[2]);
  ssize_t res;
  THROWIFERR(
    fs->SendFile(
      Uint32Val(args[0]),
      Uint32Val(args[1]),
      off,
      Uint64Val(args[3])
    ), res
  );
  args.GetReturnValue().Set(BigInt::New(isolate, res));
}
void FileSystemTruncate(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->Truncate(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Int64Val(args[1])
    ), res
  );
}
void FileSystemFTruncate(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->FTruncate(
      Uint32Val(args[0]),
      Int64Val(args[1])
    ), res
  );
}
void FileSystemFChModAt(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 3);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->FChModAt(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Uint32Val(args[2])
    ), res
  );
}
void FileSystemChMod(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(args[0]->IsString());
  assert(IsNumeric(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->ChMod(
      *String::Utf8Value(isolate, args[0].As<String>()),
      Uint32Val(args[1])
    ), res
  );
}
void FileSystemFChMod(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 2);
  assert(IsNumeric(args[0]));
  assert(IsNumeric(args[1]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->FChMod(
      Uint32Val(args[0]),
      Uint32Val(args[1])
    ), res
  );
}
void FileSystemChDir(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  int res;
  THROWIFERR(
    fs->ChDir(
      *String::Utf8Value(isolate, args[0].As<String>())
    ), res
  );
}
void FileSystemGetCwd(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 0);
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  char buf[PATH_MAX];
  int res;
  THROWIFERR(fs->GetCwd(buf, PATH_MAX), res);
  args.GetReturnValue().Set(
    String::NewFromUtf8(
      isolate,
      buf,
      NewStringType::kNormal,
      res
    ).ToLocalChecked()
  );
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
    BigInt::NewFromUnsigned(isolate, s.st_mode)
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
  Local<Object> atimObj = Object::New(isolate);
  atimObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_sec"),
    Integer::NewFromUnsigned(isolate, s.st_atim.tv_sec)
  ).Check();
  atimObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_nsec"),
    Integer::NewFromUnsigned(isolate, s.st_atim.tv_nsec)
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_atim"),
    atimObj
  ).Check();
  Local<Object> mtimObj = Object::New(isolate);
  mtimObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_sec"),
    Integer::NewFromUnsigned(isolate, s.st_mtim.tv_sec)
  ).Check();
  mtimObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_nsec"),
    Integer::NewFromUnsigned(isolate, s.st_mtim.tv_nsec)
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_mtim"),
    mtimObj
  ).Check();
  Local<Object> ctimObj = Object::New(isolate);
  ctimObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_sec"),
    Integer::NewFromUnsigned(isolate, s.st_ctim.tv_sec)
  ).Check();
  ctimObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_nsec"),
    Integer::NewFromUnsigned(isolate, s.st_ctim.tv_nsec)
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_ctim"),
    ctimObj
  ).Check();
  statObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "st_blocks"),
    BigInt::NewFromUnsigned(isolate, s.st_blocks)
  ).Check();
  return statObj;
}

void FileSystemStat(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  struct stat s;
  int res;
  THROWIFERR(
    fs->Stat(
      *String::Utf8Value(isolate, args[0].As<String>()),
      &s
    ), res
  );
  args.GetReturnValue().Set(
    StatToObj(isolate, s)
  );
}
void FileSystemLStat(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 1);
  assert(args[0]->IsString());
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  struct stat s;
  int res;
  THROWIFERR(
    fs->LStat(
      *String::Utf8Value(isolate, args[0].As<String>()),
      &s
    ), res
  );
  args.GetReturnValue().Set(
    StatToObj(isolate, s)
  );
}
void FileSystemFStat(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 1);
  assert(IsNumeric(args[0]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  struct stat s;
  int res;
  THROWIFERR(
    fs->FStat(
      Uint32Val(args[0]),
      &s
    ), res
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
    BigInt::NewFromUnsigned(isolate, s.stx_mode)
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
  Local<Object> atimeObj = Object::New(isolate);
  atimeObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_sec"),
    Integer::NewFromUnsigned(isolate, s.stx_atime.tv_sec)
  ).Check();
  atimeObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_nsec"),
    Integer::NewFromUnsigned(isolate, s.stx_atime.tv_nsec)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_atime"),
    atimeObj
  ).Check();
  Local<Object> mtimeObj = Object::New(isolate);
  mtimeObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_sec"),
    Integer::NewFromUnsigned(isolate, s.stx_mtime.tv_sec)
  ).Check();
  mtimeObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_nsec"),
    Integer::NewFromUnsigned(isolate, s.stx_mtime.tv_nsec)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_mtime"),
    mtimeObj
  ).Check();
  Local<Object> ctimeObj = Object::New(isolate);
  ctimeObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_sec"),
    Integer::NewFromUnsigned(isolate, s.stx_ctime.tv_sec)
  ).Check();
  ctimeObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_nsec"),
    Integer::NewFromUnsigned(isolate, s.stx_ctime.tv_nsec)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_ctime"),
    ctimeObj
  ).Check();
  Local<Object> btimeObj = Object::New(isolate);
  btimeObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_sec"),
    Integer::NewFromUnsigned(isolate, s.stx_btime.tv_sec)
  ).Check();
  btimeObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "tv_nsec"),
    Integer::NewFromUnsigned(isolate, s.stx_btime.tv_nsec)
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_btime"),
    btimeObj
  ).Check();
  statxObj->Set(
    context,
    String::NewFromUtf8Literal(isolate, "stx_blocks"),
    BigInt::NewFromUnsigned(isolate, s.stx_blocks)
  ).Check();
  return statxObj;
}

void FileSystemStatx(const FunctionCallbackInfo<Value>& args) {
  assert(args.Length() == 4);
  assert(IsNumeric(args[0]));
  assert(args[1]->IsString());
  assert(IsNumeric(args[2]));
  assert(IsNumeric(args[3]));
  Isolate* isolate = args.GetIsolate();
  FileSystem* fs = reinterpret_cast<FileSystem*>(
    args.This()->GetInternalField(0).As<External>()->Value()
  );
  struct statx s;
  int res;
  THROWIFERR(
    fs->Statx(
      Int32Val(args[0]),
      *String::Utf8Value(isolate, args[1].As<String>()),
      Int32Val(args[2]),
      Int32Val(args[3]),
      &s
    ), res
  );
  args.GetReturnValue().Set(
    StatxToObj(isolate, s)
  );
}

void DefineConstants(Isolate* isolate, Local<FunctionTemplate> func) {
#define DefineFlag(v) \
  func->Set( \
    String::NewFromUtf8Literal(isolate, #v), \
    Integer::New(isolate, v) \
  )
  DefineFlag(      AT_FDCWD);
  DefineFlag(      AT_REMOVEDIR);
  DefineFlag(      AT_SYMLINK_FOLLOW);
  DefineFlag(      AT_SYMLINK_NOFOLLOW);
  DefineFlag(      DT_DIR);
  DefineFlag(      DT_LNK);
  DefineFlag(      DT_REG);
  DefineFlag(      O_APPEND);
  DefineFlag(      O_CREAT);
  DefineFlag(      O_DIRECTORY);
  DefineFlag(      O_EXCL);
  DefineFlag(      O_NOATIME);
  DefineFlag(      O_NOFOLLOW);
  DefineFlag(      O_RDONLY);
  DefineFlag(      O_RDWR);
  DefineFlag(      O_TRUNC);
  DefineFlag(      O_WRONLY);
  DefineFlag(      RENAME_NOREPLACE);
  DefineFlag(      S_IFDIR);
  DefineFlag(      S_IFLNK);
  DefineFlag(      S_IFMT);
  DefineFlag(      S_IFREG);
  DefineFlag(      SEEK_CUR);
  DefineFlag(      SEEK_END);
  DefineFlag(      SEEK_SET);
  DefineFlag(      STATX_ATIME);
  DefineFlag(      STATX_BASIC_STATS);
  DefineFlag(      STATX_BLOCKS);
  DefineFlag(      STATX_BTIME);
  DefineFlag(      STATX_CTIME);
  DefineFlag(      STATX_INO);
  DefineFlag(      STATX_MODE);
  DefineFlag(      STATX_MTIME);
  DefineFlag(      STATX_NLINK);
  DefineFlag(      STATX_SIZE);
  DefineFlag(      STATX_TYPE);
  DefineFlag(      F_OK);
  DefineFlag(      R_OK);
  DefineFlag(      W_OK);
  DefineFlag(      X_OK);
#undef DefineErrCode
#undef DefineFlag
#undef DefineFlagBigInt
}

template<size_t N>
void DefineFunction(
  Isolate* isolate,
  Local<ObjectTemplate> obj,
  const char (&prop)[N],
  FunctionCallback fn,
  int argc = 0,
  bool hasSideEffect = true
) {
  Local<FunctionTemplate> funcTmpl = FunctionTemplate::New(
    isolate,
    fn,
    Local<Value>(),
    Local<Signature>(),
    argc,
    ConstructorBehavior::kThrow,
    hasSideEffect ? SideEffectType::kHasSideEffect : SideEffectType::kHasNoSideEffect
  );
  Local<String> name = String::NewFromUtf8Literal(isolate, prop);
  funcTmpl->SetClassName(name);
  obj->Set(
    name,
    funcTmpl,
    PropertyAttribute::DontEnum
  );
}

NODE_MODULE_INIT() {
  Isolate* isolate = context->GetIsolate();
  Local<FunctionTemplate> FSTmpl = FunctionTemplate::New(
    isolate,
    FileSystemConstructor
  );
  FSTmpl->SetClassName(String::NewFromUtf8Literal(isolate, "FileSystem"));
  DefineConstants(isolate, FSTmpl);
  Local<ObjectTemplate> instTmpl = FSTmpl->InstanceTemplate();
  instTmpl->SetInternalFieldCount(1);
  DefineFunction(isolate, instTmpl, "FAccessAt",  FileSystemFAccessAt,  4, false);
  DefineFunction(isolate, instTmpl, "Access",     FileSystemAccess,     2, false);
  DefineFunction(isolate, instTmpl, "OpenAt",     FileSystemOpenAt,     4);
  DefineFunction(isolate, instTmpl, "Open",       FileSystemOpen,       3);
  DefineFunction(isolate, instTmpl, "Close",      FileSystemClose,      1);
  DefineFunction(isolate, instTmpl, "MkNodAt",    FileSystemMkNodAt,    3);
  DefineFunction(isolate, instTmpl, "MkNod",      FileSystemMkNod,      2);
  DefineFunction(isolate, instTmpl, "MkDirAt",    FileSystemMkDirAt,    3);
  DefineFunction(isolate, instTmpl, "MkDir",      FileSystemMkDir,      2);
  DefineFunction(isolate, instTmpl, "SymlinkAt",  FileSystemSymlinkAt,  3);
  DefineFunction(isolate, instTmpl, "Symlink",    FileSystemSymlink,    2);
  DefineFunction(isolate, instTmpl, "ReadLinkAt", FileSystemReadLinkAt, 3, false);
  DefineFunction(isolate, instTmpl, "ReadLink",   FileSystemReadLink,   2, false);
  DefineFunction(isolate, instTmpl, "GetDents",   FileSystemGetDents,   1, false);
  DefineFunction(isolate, instTmpl, "LinkAt",     FileSystemLinkAt,     5);
  DefineFunction(isolate, instTmpl, "Link",       FileSystemLink,       2);
  DefineFunction(isolate, instTmpl, "UnlinkAt",   FileSystemUnlinkAt,   3);
  DefineFunction(isolate, instTmpl, "Unlink",     FileSystemUnlink,     1);
  DefineFunction(isolate, instTmpl, "RmDir",      FileSystemRmDir,      1);
  DefineFunction(isolate, instTmpl, "RenameAt",   FileSystemRenameAt,   4);
  DefineFunction(isolate, instTmpl, "Rename",     FileSystemRename,     2);
  DefineFunction(isolate, instTmpl, "LSeek",      FileSystemLSeek,      3);
  DefineFunction(isolate, instTmpl, "Read",       FileSystemRead,       2);//, false);
  DefineFunction(isolate, instTmpl, "Write",      FileSystemWrite,      2);
  DefineFunction(isolate, instTmpl, "SendFile",   FileSystemSendFile,   4);
  DefineFunction(isolate, instTmpl, "Truncate",   FileSystemTruncate,   2);
  DefineFunction(isolate, instTmpl, "FTruncate",  FileSystemFTruncate,  2);
  DefineFunction(isolate, instTmpl, "FChModAt",   FileSystemFChModAt,   3);
  DefineFunction(isolate, instTmpl, "ChMod",      FileSystemChMod,      2);
  DefineFunction(isolate, instTmpl, "FChMod",     FileSystemFChMod,     2);
  DefineFunction(isolate, instTmpl, "ChDir",      FileSystemChDir,      1);
  DefineFunction(isolate, instTmpl, "GetCwd",     FileSystemGetCwd,     0, false);
  DefineFunction(isolate, instTmpl, "Stat",       FileSystemStat,       1, false);
  DefineFunction(isolate, instTmpl, "LStat",      FileSystemLStat,      1, false);
  DefineFunction(isolate, instTmpl, "FStat",      FileSystemFStat,      1, false);
  DefineFunction(isolate, instTmpl, "Statx",      FileSystemStatx,      4, false);
  Local<Function> FSFunc = FSTmpl->GetFunction(context).ToLocalChecked();
  module.As<Object>()->Set(
    context,
    String::NewFromUtf8Literal(isolate, "exports"),
    FSFunc
  ).Check();
}