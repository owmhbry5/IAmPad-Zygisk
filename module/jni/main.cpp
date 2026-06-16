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

#define TAG "IAmPad"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Configurable values
// ---------------------------------------------------------------------------

static char g_manufacturer[PROP_VALUE_MAX]    = "Xiaomi";
static char g_brand[PROP_VALUE_MAX]           = "Xiaomi";
static char g_model[PROP_VALUE_MAX]           = "23046RP50C";
static char g_device[PROP_VALUE_MAX]          = "23046RP50C";
static char g_product[PROP_VALUE_MAX]         = "23046RP50C";
static char g_characteristics[PROP_VALUE_MAX] = "tablet";

static std::vector<std::string> g_targets;

static const char* DEFAULT_TARGETS[] = {
    "com.tencent.mm",
    "com.tencent.mobileqq",
    "com.tencent.tim",
    "com.alibaba.android.rimet",
    nullptr
};

// ---------------------------------------------------------------------------
// Property hooks via inline hook (dlsym fallback)
// ---------------------------------------------------------------------------

static int (*orig_system_property_get)(const char*, char*) = nullptr;

static const char* map_property(const char* name) {
    if (!name) return nullptr;
    if (strcmp(name, "ro.product.manufacturer") == 0) return g_manufacturer;
    if (strcmp(name, "ro.product.brand") == 0)        return g_brand;
    if (strcmp(name, "ro.product.model") == 0)        return g_model;
    if (strcmp(name, "ro.product.device") == 0)       return g_device;
    if (strcmp(name, "ro.product.name") == 0)         return g_product;
    if (strcmp(name, "ro.build.characteristics") == 0) return g_characteristics;
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
// Helper: locate libc.so in /proc/self/maps
// ---------------------------------------------------------------------------

static bool find_library_inode(const char* needle, dev_t* out_dev, ino_t* out_inode) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        LOGE("cannot open /proc/self/maps");
        return false;
    }

    char line[1024];
    bool found = false;
    int line_no = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_no++;
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        // Find the pathname part (after the 5th space-separated field)
        // Format: start-end perms offset dev inode [pathname]
        // The pathname starts after inode, which is the 5th field
        char* p = line;
        int fields = 0;
        while (*p && fields < 5) {
            if (*p == ' ' || *p == '\t') {
                fields++;
                // Skip consecutive whitespace
                while (*p == ' ' || *p == '\t') p++;
            } else {
                p++;
            }
        }

        // p now points to the pathname (6th field)
        if (fields < 5 || !*p) continue;

        // Check if this line contains our target library
        if (strstr(p, needle) == nullptr) continue;

        // Verify it ends with the exact library name (not just contains it)
        char* suffix = strstr(p, needle);
        while (suffix) {
            char after = suffix[strlen(needle)];
            if (after == '\0' || after == ' ' || after == '\t') {
                // Found exact match
                struct stat st;
                if (stat(p, &st) == 0) {
                    *out_dev = st.st_dev;
                    *out_inode = st.st_ino;
                    LOGI("found %s at line %d: dev=%lu inode=%lu path=%s",
                         needle, line_no, (unsigned long)st.st_dev,
                         (unsigned long)st.st_ino, p);
                    found = true;
                    goto done;
                }
            }
            suffix = strstr(suffix + 1, needle);
        }
    }

done:
    fclose(fp);
    if (!found) {
        LOGE("failed to find %s in /proc/self/maps after %d lines", needle, line_no);
    }
    return found;
}

// ---------------------------------------------------------------------------
// Helper: load config from module directory
// ---------------------------------------------------------------------------

static void trim(char* s) {
    if (!s) return;
    // Leading
    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    // Trailing
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

static void load_config(const char* module_dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config.conf", module_dir);

    FILE* fp = fopen(path, "r");
    if (!fp) {
        LOGI("config not found: %s, using defaults", path);
        return;
    }

    LOGI("loading config from %s", path);

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* val = eq + 1;
        trim(key);
        trim(val);

        if (strcmp(key, "manufacturer") == 0)         strncpy(g_manufacturer, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "brand") == 0)            strncpy(g_brand, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "model") == 0)            strncpy(g_model, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "device") == 0)           strncpy(g_device, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "product") == 0)          strncpy(g_product, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "characteristics") == 0)  strncpy(g_characteristics, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "targets") == 0) {
            g_targets.clear();
            char buf[512];
            strncpy(buf, val, sizeof(buf)-1);
            buf[sizeof(buf)-1] = '\0';
            char* tok = strtok(buf, ",");
            while (tok) {
                trim(tok);
                if (strlen(tok) > 0) g_targets.push_back(tok);
                tok = strtok(nullptr, ",");
            }
            LOGI("loaded %zu custom targets", g_targets.size());
        }
    }
    fclose(fp);

    LOGI("config: manufacturer=%s brand=%s model=%s device=%s product=%s characteristics=%s",
         g_manufacturer, g_brand, g_model, g_device, g_product, g_characteristics);
}

// ---------------------------------------------------------------------------
// Helper: spoof Build static fields via JNI reflection
// ---------------------------------------------------------------------------

static void set_static_string_field(JNIEnv* env, jclass cls, const char* name, const char* value) {
    jfieldID fid = env->GetStaticFieldID(cls, name, "Ljava/lang/String;");
    if (fid == nullptr) {
        // Clear exception if field not found
        env->ExceptionClear();
        LOGE("Build.%s not found", name);
        return;
    }
    jstring jval = env->NewStringUTF(value);
    if (jval == nullptr) {
        LOGE("NewStringUTF failed for Build.%s", name);
        return;
    }
    env->SetStaticObjectField(cls, fid, jval);
    env->DeleteLocalRef(jval);
    LOGI("set Build.%s = %s", name, value);
}

static void spoof_build_fields(JNIEnv* env) {
    jclass build_cls = env->FindClass("android/os/Build");
    if (build_cls == nullptr) {
        env->ExceptionClear();
        LOGE("android.os.Build class not found!");
        return;
    }

    set_static_string_field(env, build_cls, "MANUFACTURER", g_manufacturer);
    set_static_string_field(env, build_cls, "BRAND",        g_brand);
    set_static_string_field(env, build_cls, "MODEL",        g_model);
    set_static_string_field(env, build_cls, "DEVICE",       g_device);
    set_static_string_field(env, build_cls, "PRODUCT",      g_product);
    set_static_string_field(env, build_cls, "FINGERPRINT", "");  // Clear to avoid mismatch

    // Also spoof Build.VERSION fields for better compatibility
    jclass version_cls = env->FindClass("android/os/Build$VERSION");
    if (version_cls) {
        // SDK_INT check - some apps check this
        jfieldID sdk_fid = env->GetStaticFieldID(version_cls, "SDK_INT", "I");
        if (sdk_fid) {
            // Don't change SDK_INT, just log it
            jint sdk = env->GetStaticIntField(version_cls, sdk_fid);
            LOGI("current SDK_INT = %d", sdk);
        }
        env->ExceptionClear();
    }

    env->DeleteLocalRef(build_cls);
    if (version_cls) env->DeleteLocalRef(version_cls);
}

// ---------------------------------------------------------------------------
// Zygisk module
// ---------------------------------------------------------------------------

class IAmPad : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
        LOGI("=== IAmPad module loaded ===");
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        const char* process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!process) {
            LOGE("failed to get process name");
            return;
        }

        LOGD("preAppSpecialize: process=%s", process);

        bool target = is_target(process);
        env->ReleaseStringUTFChars(args->nice_name, process);

        if (!target) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        LOGI("=== Target process detected, applying hooks ===");

        // Load config
        int fd = api->getModuleDir();
        if (fd >= 0) {
            char module_dir[256];
            snprintf(module_dir, sizeof(module_dir), "/proc/self/fd/%d", fd);
            load_config(module_dir);
            close(fd);
        } else {
            LOGW("getModuleDir() returned %d, using defaults", fd);
        }

        // 1. Hook native system properties
        hook_system_properties();

        // 2. Spoof Build static fields via JNI
        spoof_build_fields(env);

        LOGI("=== Hooks applied ===");
    }

private:
    Api* api = nullptr;
    JNIEnv* env = nullptr;

    bool is_target(const char* process) {
        if (!process) return false;

        if (!g_targets.empty()) {
            for (const auto& pkg : g_targets) {
                if (strncmp(process, pkg.c_str(), pkg.length()) == 0) {
                    return true;
                }
            }
            return false;
        }

        for (int i = 0; DEFAULT_TARGETS[i] != nullptr; i++) {
            if (strncmp(process, DEFAULT_TARGETS[i], strlen(DEFAULT_TARGETS[i])) == 0) {
                return true;
            }
        }
        return false;
    }

    void hook_system_properties() {
        dev_t dev;
        ino_t inode;

        LOGI("looking for libc.so in /proc/self/maps...");
        if (!find_library_inode("libc.so", &dev, &inode)) {
            LOGE("!!! FAILED to locate libc.so - PLT hook will not work !!!");
            // Try dlsym fallback
            LOGI("trying dlsym fallback...");
            void* sym = dlsym(RTLD_DEFAULT, "__system_property_get");
            if (sym) {
                LOGI("found __system_property_get via dlsym at %p", sym);
                // Can't easily hook via dlsym without a hooking framework
                // But at least we confirmed the symbol exists
            } else {
                LOGE("__system_property_get not found via dlsym either");
            }
            return;
        }

        LOGI("libc.so found: dev=%lu inode=%lu", (unsigned long)dev, (unsigned long)inode);
        LOGI("registering PLT hook for __system_property_get...");

        api->pltHookRegister(dev, inode, "__system_property_get",
                             reinterpret_cast<void*>(iampad_system_property_get),
                             reinterpret_cast<void**>(&orig_system_property_get));

        if (!api->pltHookCommit()) {
            LOGE("!!! pltHookCommit FAILED - property hook not active !!!");
        } else {
            LOGI("pltHookCommit SUCCESS - __system_property_get hooked");
            // Verify hook is working
            char test_val[PROP_VALUE_MAX];
            __system_property_get("ro.build.characteristics", test_val);
            LOGI("verification: ro.build.characteristics = %s", test_val);
        }
    }
};

REGISTER_ZYGISK_MODULE(IAmPad)
