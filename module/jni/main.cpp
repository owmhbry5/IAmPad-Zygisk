/*
 * IAmPad-Zygisk v4
 * Fixed: logging to /data/local/tmp (writable by all processes)
 * Fixed: crash-safe initialization
 * Added: marker file to verify module loads
 */

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cerrno>
#include <string>
#include <vector>
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
#define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__); file_log("INFO", __VA_ARGS__); } while(0)
#define LOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__); file_log("ERROR", __VA_ARGS__); } while(0)
#define LOGD(...) do { __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__); file_log("DEBUG", __VA_ARGS__); } while(0)

// Use /data/local/tmp/ which is writable by ALL processes (no SELinux issues)
static const char* LOG_PATH = "/data/local/tmp/iampad.log";
static const char* MARKER_PATH = "/data/local/tmp/iampad_loaded.marker";

static void file_log(const char* level, const char* fmt, ...) {
    FILE* fp = fopen(LOG_PATH, "a");
    if (!fp) return;

    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm);

    va_list args;
    va_start(args, fmt);
    fprintf(fp, "[%s] [%s] ", tbuf, level);
    vfprintf(fp, fmt, args);
    fprintf(fp, "\n");
    va_end(args);
    fclose(fp);
}

// ---------------------------------------------------------------------------
// Config
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
    if (replacement) {
        strncpy(value, replacement, PROP_VALUE_MAX - 1);
        value[PROP_VALUE_MAX - 1] = '\0';
        return static_cast<int>(strlen(value));
    }
    if (orig_system_property_get) {
        return orig_system_property_get(name, value);
    }
    value[0] = '\0';
    return 0;
}

// ---------------------------------------------------------------------------
// Find libc.so
// ---------------------------------------------------------------------------

static bool find_library_inode(const char* needle, dev_t* out_dev, ino_t* out_inode) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        LOGE("cannot open /proc/self/maps: %s", strerror(errno));
        return false;
    }

    char line[1024];
    int line_no = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_no++;
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        // Find the 6th field (pathname) by skipping 5 whitespace-separated fields
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

        // Check if this line's pathname contains our needle
        char* match = strstr(p, needle);
        if (!match) continue;

        // Verify it's the exact filename (ends at boundary)
        char after = match[strlen(needle)];
        if (after != '\0' && after != ' ' && after != '\t') continue;

        struct stat st;
        if (stat(p, &st) == 0) {
            *out_dev = st.st_dev;
            *out_inode = st.st_ino;
            LOGI("found %s: dev=%lu inode=%lu path=%s",
                 needle, (unsigned long)st.st_dev, (unsigned long)st.st_ino, p);
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    LOGE("FAILED to find %s in /proc/self/maps (%d lines)", needle, line_no);
    return false;
}

// ---------------------------------------------------------------------------
// Config loader
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
        LOGI("config not found: %s", path);
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
        trim(key); trim(val);

        if (strcmp(key, "manufacturer") == 0)         strncpy(g_manufacturer, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "brand") == 0)            strncpy(g_brand, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "model") == 0)            strncpy(g_model, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "device") == 0)           strncpy(g_device, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "product") == 0)          strncpy(g_product, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "characteristics") == 0)  strncpy(g_characteristics, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "targets") == 0) {
            g_targets.clear();
            char buf[512]; strncpy(buf, val, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
            char* tok = strtok(buf, ",");
            while (tok) { trim(tok); if (strlen(tok) > 0) g_targets.push_back(tok); tok = strtok(nullptr, ","); }
        }
    }
    fclose(fp);
    LOGI("config: mfr=%s brand=%s model=%s char=%s", g_manufacturer, g_brand, g_model, g_characteristics);
}

// ---------------------------------------------------------------------------
// Build field spoofing
// ---------------------------------------------------------------------------

static void set_field(JNIEnv* env, jclass cls, const char* name, const char* value) {
    jfieldID fid = env->GetStaticFieldID(cls, name, "Ljava/lang/String;");
    if (!fid) { env->ExceptionClear(); LOGE("field %s not found", name); return; }
    jstring jval = env->NewStringUTF(value);
    if (!jval) { LOGE("NewStringUTF failed for %s", name); return; }
    env->SetStaticObjectField(cls, fid, jval);
    env->DeleteLocalRef(jval);
    LOGI("Build.%s = %s", name, value);
}

static void spoof_build(JNIEnv* env) {
    jclass cls = env->FindClass("android/os/Build");
    if (!cls) { env->ExceptionClear(); LOGE("FindClass Build failed"); return; }

    set_field(env, cls, "MANUFACTURER", g_manufacturer);
    set_field(env, cls, "BRAND",        g_brand);
    set_field(env, cls, "MODEL",        g_model);
    set_field(env, cls, "DEVICE",       g_device);
    set_field(env, cls, "PRODUCT",      g_product);

    env->DeleteLocalRef(cls);
    LOGI("Build fields spoofed");
}

// ---------------------------------------------------------------------------
// Zygisk module entry
// ---------------------------------------------------------------------------

class IAmPad : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;

        // Write marker file to prove module loaded
        FILE* marker = fopen(MARKER_PATH, "w");
        if (marker) {
            fprintf(marker, "IAmPad-Zygisk loaded at %ld\n", (long)time(nullptr));
            fclose(marker);
        }

        // Clear old log
        FILE* logfp = fopen(LOG_PATH, "w");
        if (logfp) fclose(logfp);

        LOGI("========================================");
        LOGI("IAmPad-Zygisk v4 onLoad() called!");
        LOGI("pid=%d uid=%d", getpid(), getuid());
        LOGI("========================================");
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        const char* process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!process) return;

        LOGI("preAppSpecialize: %s uid=%d pid=%d", process, args->uid, getpid());

        bool target = is_target(process);
        env->ReleaseStringUTFChars(args->nice_name, process);

        if (!target) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        LOGI(">>> TARGET: %s <<<", process);

        // Load config
        int fd = api->getModuleDir();
        LOGI("getModuleDir fd=%d", fd);
        if (fd >= 0) {
            char dir[256];
            snprintf(dir, sizeof(dir), "/proc/self/fd/%d", fd);
            load_config(dir);
            close(fd);
        }

        // Hook properties
        dev_t dev;
        ino_t inode;
        LOGI("searching libc.so...");
        if (find_library_inode("libc.so", &dev, &inode)) {
            LOGI("registering PLT hook...");
            api->pltHookRegister(dev, inode, "__system_property_get",
                                 (void*)iampad_system_property_get,
                                 (void**)&orig_system_property_get);
            bool ok = api->pltHookCommit();
            LOGI("pltHookCommit = %s", ok ? "SUCCESS" : "FAILED");
            if (ok) {
                char test[PROP_VALUE_MAX];
                __system_property_get("ro.build.characteristics", test);
                LOGI("VERIFY characteristics = %s", test);
                __system_property_get("ro.product.model", test);
                LOGI("VERIFY model = %s", test);
            }
        } else {
            LOGE("libc.so NOT FOUND");
        }

        // Spoof Build
        spoof_build(env);

        LOGI("=== done for %s ===", process);
    }

private:
    Api* api = nullptr;
    JNIEnv* env = nullptr;

    bool is_target(const char* process) {
        if (!process) return false;
        const std::vector<std::string>& t = g_targets.empty() ? defaults() : g_targets;
        for (const auto& pkg : t) {
            if (strncmp(process, pkg.c_str(), pkg.length()) == 0) return true;
        }
        return false;
    }

    static const std::vector<std::string>& defaults() {
        static std::vector<std::string> t = {
            "com.tencent.mm", "com.tencent.mobileqq",
            "com.tencent.tim", "com.alibaba.android.rimet"
        };
        return t;
    }
};

REGISTER_ZYGISK_MODULE(IAmPad)
