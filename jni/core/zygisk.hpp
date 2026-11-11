// ============================================================================
// File          : zygisk.hpp
// Architecture  : ELF64 AArch64 (arm64-v8a)
// Standard      : C++20
// Purpose       : Minimal, self-contained Zygisk ABI v4 interface header for
//                 targeting com.netease.dbdena in preAppSpecialize/postAppSpecialize
// ============================================================================
#pragma once

#include "visibility.hpp"
#include <jni.h>
#include <cstdint>

namespace zygisk {

struct AppSpecializeArgs {
    jint&        uid;
    jint&        gid;
    jintArray&   gids;
    jint&        runtime_flags;
    jobjectArray& rlimits;
    jint&        mount_external;
    jstring&     se_info;
    jstring&     nice_name;
    jstring&     instruction_set;
    jstring&     app_data_dir;
    jintArray*   fds_to_ignore;
    jboolean*    is_child_zygote;
    jboolean*    is_top_app;
    jobjectArray* pkg_data_info_list;
    jobjectArray* whitelisted_data_info_list;
    jboolean*    mount_data_dirs;
    jboolean*    mount_storage_dirs;
};

struct ServerSpecializeArgs {
    jint&     uid;
    jint&     gid;
    jintArray& gids;
    jint&     runtime_flags;
    jlong&    permitted_capabilities;
    jlong&    effective_capabilities;
};

enum Option : int {
    FORCE_DENYLIST_UNMOUNT = 0,
    DLCLOSE_MODULE_LIBRARY = 1,
};

enum StateFlags : uint32_t {
    PROCESS_GRANTED_ROOT = (1u << 0),
    PROCESS_ON_DENYLIST  = (1u << 1),
};

namespace internal {
struct api_table;
template <class T> void entry_impl(api_table*, JNIEnv*);
} // namespace internal

class ModuleBase {
public:
    virtual void onLoad([[maybe_unused]] void* api, [[maybe_unused]] JNIEnv* env) {}
    virtual void preAppSpecialize([[maybe_unused]] AppSpecializeArgs* args) {}
    virtual void postAppSpecialize([[maybe_unused]] const AppSpecializeArgs* args) {}
    virtual void preServerSpecialize([[maybe_unused]] ServerSpecializeArgs* args) {}
    virtual void postServerSpecialize([[maybe_unused]] const ServerSpecializeArgs* args) {}
    virtual ~ModuleBase() = default;
};

struct Api {
    void setOption(Option opt) noexcept;
    uint32_t getFlags() noexcept;
private:
    internal::api_table* tbl_;
    template <class T> friend void internal::entry_impl(internal::api_table*, JNIEnv*);
};

namespace internal {

struct module_abi {
    long api_version;
    ModuleBase* impl;
    void (*preAppSpecialize)(ModuleBase*, AppSpecializeArgs*);
    void (*postAppSpecialize)(ModuleBase*, const AppSpecializeArgs*);
    void (*preServerSpecialize)(ModuleBase*, ServerSpecializeArgs*);
    void (*postServerSpecialize)(ModuleBase*, const ServerSpecializeArgs*);
};

struct api_table {
    void* impl;
    bool (*registerModule)(api_table*, module_abi*);
    void (*hookJniNativeMethods)(JNIEnv*, const char*, JNINativeMethod*, int);
    void (*pltHookRegister)(dev_t, ino_t, const char*, void*, void**);
    bool (*exemptFd)(int);
    bool (*pltHookCommit)();
    int  (*connectCompanion)(void*);
    void (*setOption)(void*, int);
    int  (*getModuleDir)(void*);
    uint32_t (*getFlags)(void*);
    bool (*umbountNamespace)(void*);
};

template <class T>
void entry_impl(api_table* table, JNIEnv* env) {
    static Api api;
    api.tbl_ = table;
    static T module;
    ModuleBase* base = &module;
    static module_abi abi = {
        .api_version          = 4,
        .impl                 = base,
        .preAppSpecialize     = [](ModuleBase* m, AppSpecializeArgs* args) { m->preAppSpecialize(args); },
        .postAppSpecialize    = [](ModuleBase* m, const AppSpecializeArgs* args) { m->postAppSpecialize(args); },
        .preServerSpecialize  = [](ModuleBase* m, ServerSpecializeArgs* args) { m->preServerSpecialize(args); },
        .postServerSpecialize = [](ModuleBase* m, const ServerSpecializeArgs* args) { m->postServerSpecialize(args); },
    };
    if (!table->registerModule(table, &abi)) {
        return;
    }
    base->onLoad(&api, env);
}

} // namespace internal

inline void Api::setOption(Option opt) noexcept {
    if (tbl_ && tbl_->setOption) {
        tbl_->setOption(tbl_->impl, static_cast<int>(opt));
    }
}

inline uint32_t Api::getFlags() noexcept {
    if (tbl_ && tbl_->getFlags) {
        return tbl_->getFlags(tbl_->impl);
    }
    return 0;
}

} // namespace zygisk

#define REGISTER_ZYGISK_MODULE(clazz) \
    INSTRUMENT_C_EXPORT void zygisk_module_entry(zygisk::internal::api_table* table, JNIEnv* env) { \
        zygisk::internal::entry_impl<clazz>(table, env); \
    }

