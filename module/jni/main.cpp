/*
 * IAmPad-Zygisk
 * A minimal open-source Zygisk module to enable pad/tablet login mode
 * for WeChat, QQ, TIM and DingTalk on Android phones.
 *
 * License: MIT
 */

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <android/log.h>
#include <sys/system_properties.h>

#include "zygisk.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "IAmPad", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "IAmPad", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "IAmPad", __VA_ARGS__)

// ---------------------------------------------------------------------------
// Configurable values (can be loaded from /data/adb/modules/<id>/config.conf)
// ---------------------------------------------------------------------------

static std::string g_manufacturer    = "Xiaomi";
static std::string g_brand           = "Xiaomi";
static std::string g_model           = "23046RP50C";
static std::string g_device          = "23046RP50C";
static std::string g_product         = "23046RP50C";
static std::string g_characteristics = "tablet";

static std::vector<std::string> g_targets;

// ---------------------------------------------------------------------------
// Property hooks
// ---------------------------------------------------------------------------

static int (*orig_system_property_get)(const char*, char*) = nullptr;

static const char* map_property(const char* name) {
    if (strcmp(name, "ro.product.manufacturer") == 0) return g_manufacturer.c_str();
    if (strcmp(name, "ro.product.brand") == 0)        return g_brand.c_str();
    if (strcmp(name, "ro.product.model") == 0)        return g_model.c_str();
    if (strcmp(name, "ro.product.device") == 0)       return g_device.c_str();
    if (strcmp(name, "ro.product.name") == 0)         return g_product.c_str();
    if (strcmp(name, "ro.build.characteristics") == 0) return g_characteristics.c_str();
    return nullptr;
}

extern "C"
int iampad_system_property_get(const char* name, char* value) {
    const char* replacement = map_property(name);
    if (replacement != nullptr) {
        strncpy(value, replacement, PROP_VALUE_MAX - 1);
        value[PROP_VALUE_MAX - 1] = '\0';
        LOGD("hook prop %s -> %s", name, value);
        return static_cast<int>(strlen(value));
    }
    if (orig_system_property_get != nullptr) {
        return orig_system_property_get(name, value);
    }
    value[0] = '\0';
    return 0;
}

// ---------------------------------------------------------------------------
// Helper: locate a shared library in /proc/self/maps and get its dev/inode
// ---------------------------------------------------------------------------

static bool find_library_inode(const char* library_name, dev_t* out_dev, ino_t* out_inode) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (fp == nullptr) return false;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char path[256] = {0};
        // format: addr-addr perm offset dev:inode pathname
        if (sscanf(line, "%*x-%*x %*s %*s %*s %*s %255s", path) != 1) continue;

        // Match "libc.so", "libc.so.6", "/apex/.../libc.so", etc.
        if (strstr(path, library_name) != nullptr) {
            struct stat st;
            if (stat(path, &st) == 0) {
                *out_dev = st.st_dev;
                *out_inode = st.st_ino;
                fclose(fp);
                return true;
            }
        }
    }
    fclose(fp);
    return false;
}

// ---------------------------------------------------------------------------
// Helper: load config from module directory
// ---------------------------------------------------------------------------

static void trim(std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) { s.clear(); return; }
    size_t end = s.find_last_not_of(" \t\r\n");
    s = s.substr(start, end - start + 1);
}

static void load_config(const char* module_dir) {
    std::string path = std::string(module_dir) + "/config.conf";
    std::ifstream file(path);
    if (!file.is_open()) {
        LOGI("config not found: %s, using defaults", path.c_str());
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        trim(key);
        trim(val);

        if (key == "manufacturer")    g_manufacturer    = val;
        else if (key == "brand")      g_brand           = val;
        else if (key == "model")      g_model           = val;
        else if (key == "device")     g_device          = val;
        else if (key == "product")    g_product         = val;
        else if (key == "characteristics") g_characteristics = val;
        else if (key == "targets") {
            g_targets.clear();
            std::stringstream ss(val);
            std::string pkg;
            while (std::getline(ss, pkg, ',')) {
                trim(pkg);
                if (!pkg.empty()) g_targets.push_back(pkg);
            }
        }
    }
    file.close();
    LOGI("loaded config from %s", path.c_str());
}

// ---------------------------------------------------------------------------
// Helper: reflectively overwrite Build static String fields
// ---------------------------------------------------------------------------

static void set_static_string_field(JNIEnv* env, jclass cls, const char* name, const char* value) {
    jfieldID fid = env->GetStaticFieldID(cls, name, "Ljava/lang/String;");
    if (fid == nullptr) {
        LOGE("Build.%s not found", name);
        return;
    }
    jstring jval = env->NewStringUTF(value);
    env->SetStaticObjectField(cls, fid, jval);
    env->DeleteLocalRef(jval);
    LOGD("set Build.%s = %s", name, value);
}

static void spoof_build_fields(JNIEnv* env) {
    jclass build_cls = env->FindClass("android/os/Build");
    if (build_cls == nullptr) {
        LOGE("android.os.Build not found");
        return;
    }

    set_static_string_field(env, build_cls, "MANUFACTURER", g_manufacturer.c_str());
    set_static_string_field(env, build_cls, "BRAND",        g_brand.c_str());
    set_static_string_field(env, build_cls, "MODEL",        g_model.c_str());
    set_static_string_field(env, build_cls, "DEVICE",       g_device.c_str());
    set_static_string_field(env, build_cls, "PRODUCT",      g_product.c_str());

    env->DeleteLocalRef(build_cls);
}

// ---------------------------------------------------------------------------
// Zygisk module
// ---------------------------------------------------------------------------

class IAmPad : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        const char* process = env->GetStringUTFChars(args->nice_name, nullptr);
        bool target = is_target(process);
        LOGD("process: %s target=%d", process, target ? 1 : 0);
        env->ReleaseStringUTFChars(args->nice_name, process);

        if (!target) {
            // We are not interested in this process; let Magisk unload us.
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        // Load user config from the module directory (runs as root here).
        char module_dir[256];
        int fd = api->getModuleDir();
        if (fd >= 0) {
            snprintf(module_dir, sizeof(module_dir), "/proc/self/fd/%d", fd);
            load_config(module_dir);
            close(fd);
        }

        // 1. Hook native system properties.
        hook_system_properties();

        // 2. Spoof Build static fields via JNI.
        spoof_build_fields(env);
    }

private:
    Api* api;
    JNIEnv* env;

    bool is_target(const char* process) {
        // Use configured targets if available, otherwise use defaults.
        const std::vector<std::string>& targets = g_targets.empty() ? default_targets() : g_targets;
        for (const auto& pkg : targets) {
            if (strncmp(process, pkg.c_str(), pkg.length()) == 0) return true;
        }
        return false;
    }

    static const std::vector<std::string>& default_targets() {
        static std::vector<std::string> targets = {
            "com.tencent.mm",           // WeChat
            "com.tencent.mobileqq",     // QQ
            "com.tencent.tim",          // TIM
            "com.alibaba.android.rimet" // DingTalk
        };
        return targets;
    }

    void hook_system_properties() {
        dev_t dev;
        ino_t inode;
        if (!find_library_inode("libc.so", &dev, &inode)) {
            LOGE("failed to locate libc.so");
            return;
        }

        api->pltHookRegister(dev, inode, "__system_property_get",
                             reinterpret_cast<void*>(iampad_system_property_get),
                             reinterpret_cast<void**>(&orig_system_property_get));
        if (!api->pltHookCommit()) {
            LOGE("pltHookCommit failed");
        } else {
            LOGI("__system_property_get hooked");
        }
    }
};

REGISTER_ZYGISK_MODULE(IAmPad)
