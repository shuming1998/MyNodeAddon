#include <node_api.h>

// 假设的结构体和函数
struct MyStruct {
    int name;
    float score;
};

MyStruct myStructInstance = {42, 3.14};

napi_value CreateObjectFromMyStruct(napi_env env, napi_callback_info info) {
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value name, score;
    napi_create_int32(env, myStructInstance.name, &name);
    napi_create_double(env, myStructInstance.score, &score);
    napi_set_named_property(env, obj, "name", name);
    napi_set_named_property(env, obj, "score", score);

    return obj;
}

napi_value Init(napi_env env, napi_value exports) {
    napi_value fn;
    napi_create_function(env, nullptr, 0, CreateObjectFromMyStruct, nullptr, &fn);
    napi_set_named_property(env, exports, "createObjectFromMyStruct", fn);

    return exports;
}

// 此处确保宏调用格式正确
NAPI_MODULE(mynodeaddon, Init);
