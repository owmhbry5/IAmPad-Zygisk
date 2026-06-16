/*
 * IAmPad-Zygisk v7
 * Fixed: hook SystemProperties.native_get (used by many ROMs/WeChat builds)
 * Fixed: add ro.vendor.build.characteristics and ro.product.characteristics
 * Based on analysis of working QQ-伪装小米平板模式 and 平板模块1.1
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
#include <jni.h>

#include "zygisk.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;

#define TAG "IAmPad"
#define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__); file_log("INFO", __VA_ARGS__); } while(0)
#define LOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__); file_log("ERROR", __VA_ARGS__); } while(0)
#define LOGD(...) do { __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__); file_log("DEBUG", __VA_ARGS__); } while(0)

static const char* LOG_PATH = "/data/local/tmp/iampad.log";

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
// Config - default Xiaomi Pad 6 Pro
// ---------------------------------------------------------------------------

static char g_manufacturer[PROP_VALUE_MAX] = "Xiaomi";
static char g_brand[PROP_VALUE_MAX]        = "Xiaomi";
static char g_model[PROP_VALUE_MAX]        = "23046RP50C";
static char g_device[PROP_VALUE_MAX]       = "pipa";
static char g_product[PROP_VALUE_MAX]      = "pipa";
static char g_marketname[PROP_VALUE_MAX]   = "Xiaomi Pad 6 Pro";
static char g_characteristics[PROP_VALUE_MAX] = "tablet";

static std::vector<std::string> g_targets;
static const char* DEFAULT_TARGETS[] = {
    "com.tencent.mm", "com.tencent.mobileqq",
    "com.tencent.tim", "com.alibaba.android.rimet", nullptr
};

// ---------------------------------------------------------------------------
// ALL properties to spoof (from reference module analysis)
// ---------------------------------------------------------------------------

struct PropMapping {
    const char* name;
    char* value;
};

static std::vector<PropMapping> g_prop_map;

static void build_prop_map() {
    g_prop_map.clear();

    // Manufacturer
    g_prop_map.push_back({"ro.product.manufacturer", g_manufacturer});
    g_prop_map.push_back({"ro.product.system.manufacturer", g_manufacturer});
    g_prop_map.push_back({"ro.product.system_ext.manufacturer", g_manufacturer});
    g_prop_map.push_back({"ro.product.vendor.manufacturer", g_manufacturer});
    g_prop_map.push_back({"ro.product.odm.manufacturer", g_manufacturer});
    g_prop_map.push_back({"ro.product.product.manufacturer", g_manufacturer});

    // Brand
    g_prop_map.push_back({"ro.product.brand", g_brand});
    g_prop_map.push_back({"ro.product.system.brand", g_brand});
    g_prop_map.push_back({"ro.product.system_ext.brand", g_brand});
    g_prop_map.push_back({"ro.product.vendor.brand", g_brand});
    g_prop_map.push_back({"ro.product.odm.brand", g_brand});
    g_prop_map.push_back({"ro.product.product.brand", g_brand});

    // Model
    g_prop_map.push_back({"ro.product.model", g_model});
    g_prop_map.push_back({"ro.product.system.model", g_model});
    g_prop_map.push_back({"ro.product.system_ext.model", g_model});
    g_prop_map.push_back({"ro.product.vendor.model", g_model});
    g_prop_map.push_back({"ro.product.odm.model", g_model});
    g_prop_map.push_back({"ro.product.product.model", g_model});

    // Device
    g_prop_map.push_back({"ro.product.device", g_device});
    g_prop_map.push_back({"ro.product.system.device", g_device});
    g_prop_map.push_back({"ro.product.system_ext.device", g_device});
    g_prop_map.push_back({"ro.product.vendor.device", g_device});
    g_prop_map.push_back({"ro.product.odm.device", g_device});
    g_prop_map.push_back({"ro.product.product.device", g_device});

    // Product name
    g_prop_map.push_back({"ro.product.name", g_product});
    g_prop_map.push_back({"ro.product.system.name", g_product});
    g_prop_map.push_back({"ro.product.system_ext.name", g_product});
    g_prop_map.push_back({"ro.product.vendor.name", g_product});
    g_prop_map.push_back({"ro.product.odm.name", g_product});
    g_prop_map.push_back({"ro.product.product.name", g_product});

    // Market name
    g_prop_map.push_back({"ro.product.marketname", g_marketname});
    g_prop_map.push_back({"ro.product.system.marketname", g_marketname});
    g_prop_map.push_back({"ro.product.vendor.marketname", g_marketname});
    g_prop_map.push_back({"ro.product.product.marketname", g_marketname});

    // Characteristics (tablet)
    g_prop_map.push_back({"ro.build.characteristics", g_characteristics});
    g_prop_map.push_back({"ro.system.build.characteristics", g_characteristics});
    g_prop_map.push_back({"ro.vendor.build.characteristics", g_characteristics});
    g_prop_map.push_back({"ro.product.characteristics", g_characteristics});
}

// ---------------------------------------------------------------------------
// Property hook
// ---------------------------------------------------------------------------

static int (*orig_system_property_get)(const char*, char*) = nullptr;

extern "C"
int iampad_system_property_get(const char* name, char* value) {
    if (name) {
        for (const auto& pm : g_prop_map) {
            if (strcmp(name, pm.name) == 0) {
                strncpy(value, pm.value, PROP_VALUE_MAX - 1);
                value[PROP_VALUE_MAX - 1] = '\0';
                return static_cast<int>(strlen(value));
            }
        }
    }
    if (orig_system_property_get) {
        return orig_system_property_get(name, value);
    }
    value[0] = '\0';
    return 0;
}

// ---------------------------------------------------------------------------
// Java SystemProperties.native_get hook
// Some ROMs/wechat builds read props through android.os.SystemProperties
// instead of going through libc's __system_property_get directly.
// ---------------------------------------------------------------------------

static const char* lookup_prop_value(const char* name) {
    if (!name) return nullptr;
    for (const auto& pm : g_prop_map) {
        if (strcmp(name, pm.name) == 0) return pm.value;
    }
    return nullptr;
}

static jstring iampad_native_get(JNIEnv* env, jclass /*cls*/, jstring keyJ, jstring defJ) {
    if (!keyJ) return defJ;
    const char* key = env->GetStringUTFChars(keyJ, nullptr);
    if (!key) return defJ;
    const char* val = lookup_prop_value(key);
    jstring ret;
    if (val) {
        ret = env->NewStringUTF(val);
    } else {
        char buf[PROP_VALUE_MAX];
        if (__system_property_get(key, buf) > 0) {
            ret = env->NewStringUTF(buf);
        } else {
            ret = defJ;
        }
    }
    env->ReleaseStringUTFChars(keyJ, key);
    return ret;
}

static void hook_system_properties_jni(JNIEnv* env) {
    jclass cls = env->FindClass("android/os/SystemProperties");
    if (!cls) {
        env->ExceptionClear();
        LOGE("SystemProperties class not found");
        return;
    }
    static JNINativeMethod methods[] = {
        {"native_get", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
         (void*)iampad_native_get}
    };
    if (env->RegisterNatives(cls, methods, 1) != 0) {
        env->ExceptionClear();
        LOGE("RegisterNatives(native_get) failed");
    } else {
        LOGI("SystemProperties.native_get hooked");
    }
    env->DeleteLocalRef(cls);
}

// ---------------------------------------------------------------------------
// Find libc.so
// ---------------------------------------------------------------------------

static bool find_library_inode(const char* needle, dev_t* out_dev, ino_t* out_inode) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        char* p = line;
        int fields = 0;
        while (*p && fields < 5) {
            if (*p == ' ' || *p == '\t') { fields++; while (*p == ' ' || *p == '\t') p++; }
            else p++;
        }
        if (fields < 5 || !*p) continue;
        char* match = strstr(p, needle);
        if (!match) continue;
        char after = match[strlen(needle)];
        if (after != '\0' && after != ' ' && after != '\t') continue;
        struct stat st;
        if (stat(p, &st) == 0) {
            *out_dev = st.st_dev;
            *out_inode = st.st_ino;
            fclose(fp);
            return true;
        }
    }
    fclose(fp);
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
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n')) s[--len] = '\0';
}

static void load_config(const char* module_dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config.conf", module_dir);
    FILE* fp = fopen(path, "r");
    if (!fp) { LOGI("no config, using defaults"); return; }
    LOGI("loading config from %s", path);
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line; char* val = eq + 1;
        trim(key); trim(val);
        if (strcmp(key, "manufacturer") == 0)         strncpy(g_manufacturer, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "brand") == 0)            strncpy(g_brand, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "model") == 0)            strncpy(g_model, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "device") == 0)           strncpy(g_device, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "product") == 0)          strncpy(g_product, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "marketname") == 0)       strncpy(g_marketname, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "characteristics") == 0)  strncpy(g_characteristics, val, PROP_VALUE_MAX-1);
        else if (strcmp(key, "targets") == 0) {
            g_targets.clear();
            char buf[512]; strncpy(buf, val, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
            char* tok = strtok(buf, ",");
            while (tok) { trim(tok); if (strlen(tok) > 0) g_targets.push_back(tok); tok = strtok(nullptr, ","); }
        }
    }
    fclose(fp);
}

// ---------------------------------------------------------------------------
// Build field spoofing
// ---------------------------------------------------------------------------

static void set_field(JNIEnv* env, jclass cls, const char* name, const char* value) {
    jfieldID fid = env->GetStaticFieldID(cls, name, "Ljava/lang/String;");
    if (!fid) { env->ExceptionClear(); return; }
    jstring jval = env->NewStringUTF(value);
    if (!jval) return;
    env->SetStaticObjectField(cls, fid, jval);
    env->DeleteLocalRef(jval);
}

static void spoof_build(JNIEnv* env) {
    jclass cls = env->FindClass("android/os/Build");
    if (!cls) { env->ExceptionClear(); return; }
    set_field(env, cls, "MANUFACTURER", g_manufacturer);
    set_field(env, cls, "BRAND", g_brand);
    set_field(env, cls, "MODEL", g_model);
    set_field(env, cls, "DEVICE", g_device);
    set_field(env, cls, "PRODUCT", g_product);
    char fp_str[256];
    snprintf(fp_str, sizeof(fp_str), "%s/%s/%s:user/release-keys", g_brand, g_product, g_model);
    set_field(env, cls, "FINGERPRINT", fp_str);
    env->DeleteLocalRef(cls);
}

// ---------------------------------------------------------------------------
// Zygisk module
// ---------------------------------------------------------------------------

class IAmPad : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
        FILE* fp = fopen(LOG_PATH, "w");
        if (fp) fclose(fp);
        LOGI("IAmPad v7 onLoad pid=%d", getpid());
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        const char* process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!process) return;
        bool target = is_target(process);
        env->ReleaseStringUTFChars(args->nice_name, process);
        if (!target) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        LOGI("TARGET: %s uid=%d", process, args->uid);
        int fd = api->getModuleDir();
        if (fd >= 0) {
            char dir[256];
            snprintf(dir, sizeof(dir), "/proc/self/fd/%d", fd);
            load_config(dir);
            close(fd);
        }
        build_prop_map();
        LOGI("spoofing %zu properties", g_prop_map.size());
        dev_t dev; ino_t inode;
        if (find_library_inode("libc.so", &dev, &inode)) {
            api->pltHookRegister(dev, inode, "__system_property_get",
                                 (void*)iampad_system_property_get,
                                 (void**)&orig_system_property_get);
            bool ok = api->pltHookCommit();
            LOGI("pltHookCommit = %s", ok ? "OK" : "FAIL");
            if (ok) {
                char test[PROP_VALUE_MAX];
                __system_property_get("ro.build.characteristics", test);
                LOGI("VERIFY characteristics = %s", test);
            }
        } else {
            LOGE("libc.so not found");
        }
        spoof_build(env);
        hook_system_properties_jni(env);
        LOGI("done for %s", process);
    }

private:
    Api* api = nullptr;
    JNIEnv* env = nullptr;
    bool is_target(const char* process) {
        if (!process) return false;
        const auto& t = g_targets.empty() ? defaults() : g_targets;
        for (const auto& pkg : t)
            if (strncmp(process, pkg.c_str(), pkg.length()) == 0) return true;
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
