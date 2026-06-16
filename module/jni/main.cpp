/*
 * IAmPad-Zygisk v3
 * Open-source Zygisk tablet mode module with file-based logging
 */

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
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
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// File-based logger (writes to /data/adb/modules/iampad/iampad.log)
// ---------------------------------------------------------------------------

static FILE* g_logfile = nullptr;
static char g_logpath[256] = "/data/adb/modules/iampad/iampad.log";

static void log_to_file(const char* level, const char* fmt, va_list args) {
    if (!g_logfile) return;

    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(g_logfile, "[%s] [%s] ", timebuf, level);
    vfprintf(g_logfile, fmt, args);
    fprintf(g_logfile, "\n");
    fflush(g_logfile);
}

static void FILE_LOGI(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_to_file("INFO", fmt, args);
    va_end(args);
    // Also log to logcat
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, TAG, fmt, args);
    va_end(args);
}

static void FILE_LOGE(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_to_file("ERROR", fmt, args);
    va_end(args);
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_ERROR, TAG, fmt, args);
    va_end(args);
}

static void FILE_LOGD(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_to_file("DEBUG", fmt, args);
    va_end(args);
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_DEBUG, TAG, fmt, args);
    va_end(args);
}

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
// Property hook
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
        FILE_LOGD("hook prop %s -> %s", name, value);
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
        FILE_LOGE("cannot open /proc/self/maps");
        return false;
    }

    char line[1024];
    bool found = false;
    int line_no = 0;
    int match_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_no++;
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        // Parse: start-end perms offset dev inode pathname
        // Find the 6th field (pathname) by counting spaces
        char* p = line;
        int fields = 0;
        while (*p && fields < 5) {
            if (*p == ' ' || *p == '\t') {
                fields++;
                while (*p == ' ' || *p == '\t') p++;
            } else {
                p++;
            }
        }

        if (fields < 5 || !*p) continue;

        // Check if pathname contains our target
        if (strstr(p, needle) == nullptr) continue;

        match_count++;
        FILE_LOGD("maps match #%d line %d: %s", match_count, line_no, p);

        // Verify exact match (ends with needle or needle followed by nothing)
        char* suffix = strstr(p, needle);
        if (suffix) {
            char after = suffix[strlen(needle)];
            if (after == '\0' || after == ' ' || after == '\t') {
                struct stat st;
                if (stat(p, &st) == 0) {
                    *out_dev = st.st_dev;
                    *out_inode = st.st_ino;
                    FILE_LOGI("FOUND %s: dev=%lu inode=%lu path=%s",
                              needle, (unsigned long)st.st_dev,
                              (unsigned long)st.st_ino, p);
                    found = true;
                    break;
                } else {
                    FILE_LOGE("stat failed for %s: %s", p, strerror(errno));
                }
            }
        }
    }

    fclose(fp);
    if (!found) {
        FILE_LOGE("FAILED to find %s (scanned %d lines, %d matches)",
                  needle, line_no, match_count);
    }
    return found;
}

// ---------------------------------------------------------------------------
// Helper: load config
// ---------------------------------------------------------------------------

static void trim(char* s) {
    if (!s) return;
    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n')) s[--len] = '\0';
}

static void load_config(const char* module_dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config.conf", module_dir);

    FILE* fp = fopen(path, "r");
    if (!fp) {
        FILE_LOGI("config not found: %s, using defaults", path);
        return;
    }

    FILE_LOGI("loading config from %s", path);

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
        }
    }
    fclose(fp);
    FILE_LOGI("config loaded: mfr=%s brand=%s model=%s", g_manufacturer, g_brand, g_model);
}

// ---------------------------------------------------------------------------
// Helper: spoof Build fields
// ---------------------------------------------------------------------------

static void set_static_string_field(JNIEnv* env, jclass cls, const char* name, const char* value) {
    jfieldID fid = env->GetStaticFieldID(cls, name, "Ljava/lang/String;");
    if (fid == nullptr) {
        env->ExceptionClear();
        FILE_LOGE("Build.%s field not found", name);
        return;
    }
    jstring jval = env->NewStringUTF(value);
    if (!jval) {
        FILE_LOGE("NewStringUTF failed for %s", name);
        return;
    }
    env->SetStaticObjectField(cls, fid, jval);
    env->DeleteLocalRef(jval);
    FILE_LOGI("JNI set Build.%s = %s", name, value);
}

static void spoof_build_fields(JNIEnv* env) {
    jclass build_cls = env->FindClass("android/os/Build");
    if (!build_cls) {
        env->ExceptionClear();
        FILE_LOGE("FindClass android/os/Build FAILED");
        return;
    }

    FILE_LOGI("spoofing Build fields...");

    set_static_string_field(env, build_cls, "MANUFACTURER", g_manufacturer);
    set_static_string_field(env, build_cls, "BRAND",        g_brand);
    set_static_string_field(env, build_cls, "MODEL",        g_model);
    set_static_string_field(env, build_cls, "DEVICE",       g_device);
    set_static_string_field(env, build_cls, "PRODUCT",      g_product);

    // FINGERPRINT is important - some apps check it
    char fp[256];
    snprintf(fp, sizeof(fp), "%s/%s/%s:%s/test-keys",
             g_brand, g_product, g_model, "user");
    set_static_string_field(env, build_cls, "FINGERPRINT", fp);

    env->DeleteLocalRef(build_cls);
    FILE_LOGI("Build fields spoofed successfully");
}

// ---------------------------------------------------------------------------
// Zygisk module
// ---------------------------------------------------------------------------

class IAmPad : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;

        // Open log file immediately
        g_logfile = fopen(g_logpath, "a");
        if (g_logfile) {
            FILE_LOGI("========================================");
            FILE_LOGI("IAmPad-Zygisk v3 LOADED");
            FILE_LOGI("========================================");
        }
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        const char* process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!process) {
            FILE_LOGE("GetStringUTFChars returned null");
            return;
        }

        FILE_LOGI("preAppSpecialize: %s uid=%d", process, args->uid);

        bool target = is_target(process);
        if (!target) {
            env->ReleaseStringUTFChars(args->nice_name, process);
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        FILE_LOGI(">>> TARGET PROCESS: %s <<<", process);
        env->ReleaseStringUTFChars(args->nice_name, process);

        // Load config
        int fd = api->getModuleDir();
        FILE_LOGI("getModuleDir() = %d", fd);
        if (fd >= 0) {
            char module_dir[256];
            snprintf(module_dir, sizeof(module_dir), "/proc/self/fd/%d", fd);
            load_config(module_dir);
            close(fd);
        }

        // Dump current Build values BEFORE spoofing
        dump_build_fields("BEFORE");

        // 1. Hook system properties
        hook_system_properties();

        // 2. Spoof Build fields
        spoof_build_fields(env);

        // 3. Dump Build values AFTER spoofing
        dump_build_fields("AFTER");

        FILE_LOGI("=== hooks applied for %s ===", process);
    }

private:
    Api* api = nullptr;
    JNIEnv* env = nullptr;

    void dump_build_fields(const char* phase) {
        jclass build_cls = env->FindClass("android/os/Build");
        if (!build_cls) {
            env->ExceptionClear();
            return;
        }

        const char* fields[] = {"MANUFACTURER", "BRAND", "MODEL", "DEVICE", "PRODUCT", "FINGERPRINT", nullptr};
        for (int i = 0; fields[i]; i++) {
            jfieldID fid = env->GetStaticFieldID(build_cls, fields[i], "Ljava/lang/String;");
            if (fid) {
                jstring jval = (jstring)env->GetStaticObjectField(build_cls, fid);
                if (jval) {
                    const char* val = env->GetStringUTFChars(jval, nullptr);
                    FILE_LOGI("Build.%s [%s] = %s", fields[i], phase, val);
                    env->ReleaseStringUTFChars(jval, val);
                    env->DeleteLocalRef(jval);
                }
            } else {
                env->ExceptionClear();
            }
        }
        env->DeleteLocalRef(build_cls);
    }

    bool is_target(const char* process) {
        if (!process) return false;
        if (!g_targets.empty()) {
            for (const auto& pkg : g_targets) {
                if (strncmp(process, pkg.c_str(), pkg.length()) == 0) return true;
            }
            return false;
        }
        for (int i = 0; DEFAULT_TARGETS[i]; i++) {
            if (strncmp(process, DEFAULT_TARGETS[i], strlen(DEFAULT_TARGETS[i])) == 0) return true;
        }
        return false;
    }

    void hook_system_properties() {
        dev_t dev;
        ino_t inode;

        FILE_LOGI("searching for libc.so...");
        if (!find_library_inode("libc.so", &dev, &inode)) {
            FILE_LOGE("!!! libc.so NOT FOUND - hook will not work !!!");
            return;
        }

        FILE_LOGI("registering PLT hook...");
        api->pltHookRegister(dev, inode, "__system_property_get",
                             reinterpret_cast<void*>(iampad_system_property_get),
                             reinterpret_cast<void**>(&orig_system_property_get));

        bool ok = api->pltHookCommit();
        FILE_LOGI("pltHookCommit = %s", ok ? "SUCCESS" : "FAILED");

        if (ok) {
            // Verify
            char test[PROP_VALUE_MAX];
            __system_property_get("ro.build.characteristics", test);
            FILE_LOGI("VERIFY: ro.build.characteristics = %s", test);

            __system_property_get("ro.product.model", test);
            FILE_LOGI("VERIFY: ro.product.model = %s", test);
        }
    }
};

REGISTER_ZYGISK_MODULE(IAmPad)
