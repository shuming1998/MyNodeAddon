#include <node_api.h>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/IR/Verifier.h"

#include <nlohmann/json.hpp>

#include <cstddef> // for offsetof
#include <iostream>
#include <fstream>
#include <system_error>
#include <vector>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unordered_map>

using namespace llvm;
using namespace llvm::orc;

// IR 结构体，用来保存 IR Struct 的名称和指针
struct LLVMStruct {
    LLVMStruct() : name(""), LLVMStructType(nullptr) {}
    LLVMStruct(const LLVMStruct &myStruct) : name(myStruct.name), LLVMStructType(myStruct.LLVMStructType) {}
    LLVMStruct(std::string name, StructType *type) : name(name), LLVMStructType(type) {}
    std::string name;
    StructType *LLVMStructType;
};

// 定义一个通用的函数指针类型
typedef napi_status (*napi_create_func)(napi_env, ...);

// 建立映射，将生产者对应的结构体名称映射到 IR 结构体
std::unordered_map<std::string, LLVMStruct> map;
// N-API 函数指针映射表，将 LLVM 变量类型映射到 Nodejs 创建变量类型的函数指针
std::unordered_map<llvm::Type::TypeID, napi_create_func> napi_function_map;

// 初始化 N-API 函数指针映射表
void InitializeNapiFunctionMap(napi_env env) {
    napi_function_map[llvm::Type::TypeID::IntegerTyID] = reinterpret_cast<napi_create_func>(napi_create_int32);
    napi_function_map[llvm::Type::TypeID::IntegerTyID] = reinterpret_cast<napi_create_func>(napi_create_int64);
    napi_function_map[llvm::Type::TypeID::FloatTyID] = reinterpret_cast<napi_create_func>(napi_create_double);
    napi_function_map[llvm::Type::TypeID::DoubleTyID] = reinterpret_cast<napi_create_func>(napi_create_double);

    //napi_function_map[4] = reinterpret_cast<napi_create_func>(napi_create_uint32);
    //napi_function_map[5] = reinterpret_cast<napi_create_func>(napi_create_string_utf8);
    //napi_function_map[6] = reinterpret_cast<napi_create_func>(napi_create_string_latin1);
    //napi_function_map[7] = reinterpret_cast<napi_create_func>(napi_create_boolean);
    //napi_function_map[8] = reinterpret_cast<napi_create_func>(napi_create_null);
    //napi_function_map[9] = reinterpret_cast<napi_create_func>(napi_create_undefined);
}

// 定义结构体，模拟生产者生成的数据
struct MyStruct {
    std::string type; // 成员1：json 中的结构体类型
    void* dataPtr;    // 成员2：写入数据内存区域的指针

    // 构造函数
    MyStruct(std::string t, int intValue, float floatValue) : type(t) {
        // 为dataPtr分配足够的空间存储一个int和一个float
        dataPtr = operator new(sizeof(int) + sizeof(float));

        // 将int值和float值复制到分配的空间
        *static_cast<int*>(dataPtr) = intValue;
        *reinterpret_cast<float*>(static_cast<int*>(dataPtr) + 1) = floatValue;
    }

    ~MyStruct() {
        operator delete(dataPtr); // 析构函数中释放分配的空间
    }

    // 用于打印存储的int和float值的成员函数
    void printValues() const {
        // 从void*中提取int和float值
        int intValue = *static_cast<int*>(dataPtr);
        float floatValue = *reinterpret_cast<float*>(static_cast<int*>(dataPtr) + 1);

        // 打印值
        std::cout << "Type: " << type << "\n";
        std::cout << "Int Value: " << intValue << "\n";
        std::cout << "Float Value: " << floatValue << "\n";
    }
};

// LLVM 相关组件
ExitOnError ExitOnErr;
LLVMContext TheContext;
IRBuilder<> Builder(TheContext);
std::unique_ptr<Module> TheModule;
std::unique_ptr<LLJIT> TheJIT;

// json 类型到 IR 结构体成员类型的映射
std::map<std::string, llvm::Type *> typeMap = {
    {"i32", Builder.getInt32Ty()},
    {"float", Builder.getFloatTy()},
    {"double", Builder.getDoubleTy()}
};

napi_value CreateObjectFromMyStruct(napi_env env, napi_callback_info info) {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
    TheModule = std::make_unique<Module>("MyModule", TheContext);
    auto JTMB = ExitOnErr(JITTargetMachineBuilder::detectHost());
    auto DL = ExitOnErr(JTMB.getDefaultDataLayoutForTarget());
    TheModule->setDataLayout(DL);

    // 读取和解析 json 文件
    std::ifstream configStream("structs.json");
    if (!configStream.is_open()) {
        errs() << "Failed to open structs.json\n";
    }
    nlohmann::json structsJson;
    configStream >> structsJson;

    // 构造 IR 结构体，并保存到 map 中
    for (const auto &structDef : structsJson) {
        std::string structName = structDef["name"].get<std::string>();
        std::vector<Type *> fieldTypes;
        for (const auto &fieldDef : structDef["fields"]) {
            std::string fieldType = fieldDef["type"].get<std::string>();
            fieldTypes.push_back(typeMap[fieldType]);
        }

        StructType *structType = StructType::create(TheContext, fieldTypes, structName);
        LLVMStruct llvmStruct = LLVMStruct(structName, structType);
        map.emplace(structName, llvmStruct);
    }

    // 打印一下都保存了哪些结构体
    for (auto &lmStruct : map) {
        outs() << "llvm struct name: " << lmStruct.first << "\n";
    }

    napi_value obj;
    napi_create_object(env, &obj);

    // 定义一个生产者生产的数据对象，这里写死对应的结构体名称为 MyStruct，向内存区域 void* 写入了一个 int 和一个 float
    MyStruct member = {"MyStruct", 42, 3.14};

    // 拿到生产者数据后，根据结构体名称从 map 中取出 IR 结构体类型
    LLVMStruct llvmMember = map[member.type];
    // 遍历 IR 结构体类型中的成员，并根据类型设置 node 成员的数据
    for (unsigned i = 0, e = llvmMember.LLVMStructType->getNumElements(); i != e; ++i) {
        napi_value nValue;
        auto *ElementType = llvmMember.LLVMStructType->getElementType(i);
        llvm::Type::TypeID typeID =  ElementType->getTypeID();
        outs() << "element type id: " << typeID << "\n";

        // TODO: 根据类型设置 node 成员：1.需要根据成员类型布局计算偏移量，2.需要获取json中成员名称，代替硬编码 "name" 和 "score"
        if (ElementType->isIntegerTy()) {
            napi_create_int32(env, *static_cast<int*>(member.dataPtr), &nValue);
            napi_set_named_property(env, obj, "name", nValue);
        } else if (ElementType->isFloatingPointTy()) {
            napi_create_double(env, *reinterpret_cast<float*>(static_cast<int*>(member.dataPtr) + 1), &nValue);
            napi_set_named_property(env, obj, "score", nValue);
        }
    }

    return obj;
}

napi_value Init(napi_env env, napi_value exports) {
    napi_value fn;

    // 初始化 N-API 函数映射表
    InitializeNapiFunctionMap(env);

    napi_create_function(env, nullptr, 0, CreateObjectFromMyStruct, nullptr, &fn);
    napi_set_named_property(env, exports, "createObjectFromMyStruct", fn);

    return exports;
}

// 此处确保宏调用格式正确
NAPI_MODULE(mynodeaddon, Init);
