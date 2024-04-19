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
#include <chrono>

using namespace llvm;
using namespace llvm::orc;

// LLVM 相关组件
ExitOnError ExitOnErr;
LLVMContext TheContext;
IRBuilder<> Builder(TheContext);
std::unique_ptr<Module> TheModule;
std::unique_ptr<LLJIT> TheJIT;

// C++ 结构体，对比 IR 结构体 构造 js 对象速度
struct CppStruct {
    CppStruct() : ival(0), fval(0.f) {}
    CppStruct(int i, float f) : ival(i), fval(f) {}
    int ival;
    float fval;
};

// IR 结构体，用来保存 IR Struct 的名称和指针
struct LLVMStruct {
    LLVMStruct() : name(""), LLVMStructType(nullptr) {}
    LLVMStruct(const LLVMStruct &myStruct) : name(myStruct.name), LLVMStructType(myStruct.LLVMStructType) {}
    LLVMStruct(std::string name, StructType *type) : name(name), LLVMStructType(type) {}
    std::string name;
    StructType *LLVMStructType;
};


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

// 包装函数，用于从 void* 转换为 int32_t 并调用 napi_create_int32()
napi_status create_napi_int32(napi_env env, void* dataPtr, napi_value* result) {
    int32_t value = *static_cast<int32_t*>(dataPtr);
    return napi_create_int32(env, value, result);
}

// 包装函数，用于从 void* 转换为 double 并调用 napi_create_double()
napi_status create_napi_double(napi_env env, void* dataPtr, napi_value* result) {
    double value = *static_cast<double*>(dataPtr);
    return napi_create_double(env, value, result);
}

// 包装函数，用于从 void* 转换为 float 并调用 napi_create_double()
napi_status create_napi_float(napi_env env, void* dataPtr, napi_value* result) {
    float value = *static_cast<float*>(dataPtr);
    return napi_create_double(env, value, result);
}

// 建立映射表，将生产者对应的结构体名称映射到 IR 结构体
std::unordered_map<std::string, LLVMStruct> map;
// 建立映射表，将IR结构体名称映射到其成员名称列表
std::unordered_map<std::string, std::vector<std::string>> structMemberNamesMap;

// json 类型到 IR 结构体成员类型的映射
std::map<std::string, llvm::Type *> typeMap = {
    {"i32", Builder.getInt32Ty()},
    {"float", Builder.getFloatTy()},
    {"double", Builder.getDoubleTy()}
};

// llvm type 到 napi_create_func 映射
std::unordered_map<llvm::Type::TypeID, std::function<napi_status(napi_env, void*, napi_value*)>> napi_function_map = {
    { llvm::Type::IntegerTyID, create_napi_int32 },
    { llvm::Type::DoubleTyID, create_napi_double },
    { llvm::Type::FloatTyID, create_napi_float },
    // ... 为其他LLVM类型添加映射 ...
};

void InitLLVMStructFronJSON() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();
    TheModule = std::make_unique<Module>("MyModule", TheContext);
    auto JTMB = ExitOnErr(JITTargetMachineBuilder::detectHost());
    auto DL = ExitOnErr(JTMB.getDefaultDataLayoutForTarget());
    TheModule->setDataLayout(DL);

    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
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
        std::vector<std::string> fieldNames; // 保存该结构体的成员名称列表
        for (const auto &fieldDef : structDef["fields"]) {
            std::string fieldType = fieldDef["type"].get<std::string>();
            std::string fieldName = fieldDef["name"].get<std::string>();
            fieldTypes.push_back(typeMap[fieldType]);
            fieldNames.push_back(fieldName); // 添加成员名称到列表
            //outs() << "field name: " << fieldName << "\n";
        }

        StructType *structType = StructType::create(TheContext, fieldTypes, structName);
        LLVMStruct llvmStruct = LLVMStruct(structName, structType);
        map.emplace(structName, llvmStruct);
        // 将结构体名称和成员名称列表添加到映射表中
        structMemberNamesMap.emplace(structName, fieldNames);
    }

    // 打印一下都保存了哪些结构体
    //for (auto &lmStruct : map) {
    //    outs() << "llvm struct name: " << lmStruct.first << "\n";
    //}

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    std::cout << "[1] Create LLVM Struct From Json Cost: " << std::chrono::duration_cast<std::chrono::nanoseconds> (end - begin).count() << "[ns] = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[µs]" << std::endl;
}

// 定义一个生产者生产的数据对象，这里写死对应的结构体名称为 MyStruct，向内存区域 void* 写入了一个 int 和一个 float
MyStruct member = {"MyStruct", 42, 3.14};

napi_value CreateObjectFromMyStruct(napi_env env, napi_callback_info info) {
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    napi_value obj;
    napi_create_object(env, &obj);

    // 拿到生产者数据后，根据结构体名称从 map 中取出 IR 结构体类型
    LLVMStruct llvmMember = map[member.type];
    // 获取成员名称列表
    const auto &memberNames = structMemberNamesMap[member.type];
    // 遍历 IR 结构体类型中的成员，并根据类型设置 node 成员的数据
    size_t currentOffset = 0;
    for (unsigned i = 0, e = llvmMember.LLVMStructType->getNumElements(); i != e; ++i) {
        napi_value nValue;
        auto *ElementType = llvmMember.LLVMStructType->getElementType(i);
        llvm::Type::TypeID typeID = ElementType->getTypeID();

        // 查找对应的N-API函数指针
        auto it = napi_function_map.find(typeID);
        if (it != napi_function_map.end()) {
            // 打印一下类型ID
            //outs() << "typeID: " << typeID << "\n";
            // 打印一下当前偏移量
            //outs() << "currentOffset: " << currentOffset << "\n";

            // 根据类型取得对应的元素大小
            size_t elementSize = TheModule->getDataLayout().getTypeAllocSize(ElementType);
            // 计算当前成员的地址
            void* dataPtrWithOffset = static_cast<char*>(member.dataPtr) + currentOffset;

            // 调用映射表中的函数，它会转换类型并创建相应的N-API值
            napi_status status = it->second(env, dataPtrWithOffset, &nValue);

            // 处理错误情况
            //if (status != napi_ok) {

            //}

            // 使用成员名称设置 JavaScript 对象的属性
            napi_set_named_property(env, obj, memberNames[i].c_str(), nValue);

            // 更新偏移量
            currentOffset += elementSize;
        } else {
            // 错误处理：找不到对应的类型ID
        }
    }
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    auto nsTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    double usTime = nsTime / 1000.0;
    std::cout << "[2] Create js member by LLVM struct cost: " << nsTime << "[ns] = " << usTime << "[µs]" << std::endl;
    return obj;
}

// 用于构造 js 对象的 C++ 结构体对象
CppStruct cst = CppStruct{42, 3.14};
napi_value CreateObjectFromCppStruct(napi_env env, napi_callback_info info) {
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value iValue;
    napi_value fValue;
    napi_create_int32(env, cst.ival, &iValue);
    napi_create_double(env, cst.fval, &fValue);
    // 使用成员名称设置 JavaScript 对象的属性
    napi_set_named_property(env, obj, "ival", iValue);
    napi_set_named_property(env, obj, "fval", fValue);

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    auto nsTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    double usTime = nsTime / 1000.0;
    std::cout << "[3] Create js member by cpp native  cost: " << nsTime << "[ns] = " << usTime << "[µs]\n"<< std::endl;
    return obj;
}

napi_value Init(napi_env env, napi_value exports) {
    InitLLVMStructFronJSON();
    napi_value fn;

    napi_create_function(env, nullptr, 0, CreateObjectFromMyStruct, nullptr, &fn);
    napi_set_named_property(env, exports, "createObjectFromMyStruct", fn);
    napi_create_function(env, nullptr, 0, CreateObjectFromCppStruct, nullptr, &fn);
    napi_set_named_property(env, exports, "createObjectFromCppStruct", fn);

    return exports;
}

// 此处确保宏调用格式正确
NAPI_MODULE(mynodeaddon, Init);
