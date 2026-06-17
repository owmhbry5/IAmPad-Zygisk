/*
 * IAmPad-Zygisk v11
 * Added: system_ext/odm marketname; hardware/board/locale/mod_device/cpu-abilist/serial
 *        matching the property set observed in 平板模块1.1
 * Fixed: hook SystemProperties.native_get (used by many ROMs/WeChat builds)
 * Based on analysis of QQ-伪装小米平板模式, 平板模块1.1, Houvven/I-Am-Pad and device_faker
 */

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <cerrno>
#include <string>
#include <utility>
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
static char g_board[PROP_VALUE_MAX]        = "pipa";
static char g_hardware[PROP_VALUE_MAX]     = "qcom";
static char g_locale[PROP_VALUE_MAX]       = "zh-CN";
static char g_mod_device[PROP_VALUE_MAX]   = "pipa";
static char g_build_product[PROP_VALUE_MAX] = "pipa";
static char g_cpu_abilist[PROP_VALUE_MAX]   = "arm64-v8a,armeabi-v7a,armeabi";
static char g_cpu_abilist32[PROP_VALUE_MAX] = "armeabi-v7a,armeabi";
static char g_cpu_abilist64[PROP_VALUE_MAX] = "arm64-v8a";
static char g_serialno[PROP_VALUE_MAX]     = "unknown";
static char g_boot_serialno[PROP_VALUE_MAX] = "unknown";
static char g_arch[PROP_VALUE_MAX]         = "arm64";
static char g_fingerprint[256] = "Xiaomi/pipa/pipa:13/TKQ1.221114.001/V14.0.8.0.TMYCNXM:user/release-keys";
static char g_build_id[PROP_VALUE_MAX] = "TKQ1.221114.001";
static char g_android_release[PROP_VALUE_MAX] = "13";
static char g_sdk_int[PROP_VALUE_MAX] = "33";
static char g_foldable_screen_support[PROP_VALUE_MAX] = "true";

static std::vector<std::string> g_targets;
static bool g_targets_configured = false;
static std::vector<std::pair<std::string, std::string>> g_custom_props;

// ---------------------------------------------------------------------------
// ALL properties to spoof (from reference module analysis)
// ---------------------------------------------------------------------------

struct PropMapping {
    const char* name;
    const char* value;
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
    g_prop_map.push_back({"ro.product.product", g_product});
    g_prop_map.push_back({"ro.product.system.product", g_product});
    g_prop_map.push_back({"ro.product.system_ext.product", g_product});
    g_prop_map.push_back({"ro.product.vendor.product", g_product});
    g_prop_map.push_back({"ro.product.odm.product", g_product});
    g_prop_map.push_back({"ro.product.name", g_product});
    g_prop_map.push_back({"ro.product.system.name", g_product});
    g_prop_map.push_back({"ro.product.system_ext.name", g_product});
    g_prop_map.push_back({"ro.product.vendor.name", g_product});
    g_prop_map.push_back({"ro.product.odm.name", g_product});
    g_prop_map.push_back({"ro.product.product.name", g_product});

    // Market name
    g_prop_map.push_back({"ro.product.marketname", g_marketname});
    g_prop_map.push_back({"ro.product.system.marketname", g_marketname});
    g_prop_map.push_back({"ro.product.system_ext.marketname", g_marketname});
    g_prop_map.push_back({"ro.product.vendor.marketname", g_marketname});
    g_prop_map.push_back({"ro.product.product.marketname", g_marketname});
    g_prop_map.push_back({"ro.product.odm.marketname", g_marketname});

    // Characteristics (tablet)
    g_prop_map.push_back({"ro.build.characteristics", g_characteristics});
    g_prop_map.push_back({"ro.system.build.characteristics", g_characteristics});
    g_prop_map.push_back({"ro.vendor.build.characteristics", g_characteristics});
    g_prop_map.push_back({"ro.product.build.characteristics", g_characteristics});
    g_prop_map.push_back({"ro.product.characteristics", g_characteristics});

    // Build identity
    g_prop_map.push_back({"ro.build.fingerprint", g_fingerprint});
    g_prop_map.push_back({"ro.system.build.fingerprint", g_fingerprint});
    g_prop_map.push_back({"ro.vendor.build.fingerprint", g_fingerprint});
    g_prop_map.push_back({"ro.product.build.fingerprint", g_fingerprint});
    g_prop_map.push_back({"ro.build.id", g_build_id});
    g_prop_map.push_back({"ro.system.build.id", g_build_id});
    g_prop_map.push_back({"ro.vendor.build.id", g_build_id});
    g_prop_map.push_back({"ro.product.build.id", g_build_id});
    g_prop_map.push_back({"ro.build.version.release", g_android_release});
    g_prop_map.push_back({"ro.system.build.version.release", g_android_release});
    g_prop_map.push_back({"ro.vendor.build.version.release", g_android_release});
    g_prop_map.push_back({"ro.product.build.version.release", g_android_release});
    g_prop_map.push_back({"ro.build.version.sdk", g_sdk_int});
    g_prop_map.push_back({"ro.system.build.version.sdk", g_sdk_int});
    g_prop_map.push_back({"ro.vendor.build.version.sdk", g_sdk_int});
    g_prop_map.push_back({"ro.product.build.version.sdk", g_sdk_int});

    // Hardware / board / product / locale / mod_device / cpu abilist / serial / foldable
    g_prop_map.push_back({"ro.hardware", g_hardware});
    g_prop_map.push_back({"ro.build.product", g_build_product});
    g_prop_map.push_back({"ro.product.mod_device", g_mod_device});
    g_prop_map.push_back({"ro.os_foldable_screen_support", g_foldable_screen_support});
    g_prop_map.push_back({"ro.product.locale", g_locale});
    g_prop_map.push_back({"ro.product.cpu.abilist", g_cpu_abilist});
    g_prop_map.push_back({"ro.product.cpu.abilist32", g_cpu_abilist32});
    g_prop_map.push_back({"ro.product.cpu.abilist64", g_cpu_abilist64});
    g_prop_map.push_back({"ro.system.product.cpu.abilist", g_cpu_abilist});
    g_prop_map.push_back({"ro.system.product.cpu.abilist32", g_cpu_abilist32});
    g_prop_map.push_back({"ro.system.product.cpu.abilist64", g_cpu_abilist64});
    g_prop_map.push_back({"ro.serialno", g_serialno});
    g_prop_map.push_back({"ro.boot.serialno", g_boot_serialno});
    g_prop_map.push_back({"ro.arch", g_arch});

    for (const auto& prop : g_custom_props) {
        g_prop_map.push_back({prop.first.c_str(), prop.second.c_str()});
    }
}

// ---------------------------------------------------------------------------
// Property hook
// ---------------------------------------------------------------------------

static int (*orig_system_property_get)(const char*, char*) = nullptr;
using NativeGet = jstring (*)(JNIEnv*, jclass, jstring);
using NativeGetDef = jstring (*)(JNIEnv*, jclass, jstring, jstring);
static NativeGet orig_native_get = nullptr;
static NativeGetDef orig_native_get_def = nullptr;

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

static jstring read_prop_or_default(JNIEnv* env, const char* key, jstring defJ) {
    const char* val = lookup_prop_value(key);
    if (val) {
        return env->NewStringUTF(val);
    }

    char buf[PROP_VALUE_MAX];
    if (__system_property_get(key, buf) > 0) {
        return env->NewStringUTF(buf);
    }

    return defJ ? defJ : env->NewStringUTF("");
}

static jstring iampad_native_get(JNIEnv* env, jclass cls, jstring keyJ) {
    if (!keyJ) return env->NewStringUTF("");
    const char* key = env->GetStringUTFChars(keyJ, nullptr);
    if (!key) return env->NewStringUTF("");
    std::string key_string(key);
    const char* val = lookup_prop_value(key);
    env->ReleaseStringUTFChars(keyJ, key);
    if (val) return env->NewStringUTF(val);
    if (orig_native_get) return orig_native_get(env, cls, keyJ);
    return read_prop_or_default(env, key_string.c_str(), nullptr);
}

static jstring iampad_native_get_def(JNIEnv* env, jclass cls, jstring keyJ, jstring defJ) {
    if (!keyJ) return defJ;
    const char* key = env->GetStringUTFChars(keyJ, nullptr);
    if (!key) return defJ;
    std::string key_string(key);
    const char* val = lookup_prop_value(key);
    env->ReleaseStringUTFChars(keyJ, key);
    if (val) return env->NewStringUTF(val);
    if (orig_native_get_def) return orig_native_get_def(env, cls, keyJ, defJ);
    return read_prop_or_default(env, key_string.c_str(), defJ);
}

static void hook_system_properties_jni(Api* api, JNIEnv* env) {
    jclass cls = env->FindClass("android/os/SystemProperties");
    if (!cls) {
        env->ExceptionClear();
        LOGE("SystemProperties class not found");
        return;
    }
    orig_native_get = nullptr;
    orig_native_get_def = nullptr;
    JNINativeMethod methods[] = {
        {"native_get", "(Ljava/lang/String;)Ljava/lang/String;",
         (void*)iampad_native_get},
        {"native_get", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
         (void*)iampad_native_get_def}
    };
    api->hookJniNativeMethods(env, "android/os/SystemProperties", methods, 2);
    if (methods[0].fnPtr && methods[0].fnPtr != (void*)iampad_native_get) {
        orig_native_get = reinterpret_cast<NativeGet>(methods[0].fnPtr);
    }
    if (methods[1].fnPtr && methods[1].fnPtr != (void*)iampad_native_get_def) {
        orig_native_get_def = reinterpret_cast<NativeGetDef>(methods[1].fnPtr);
    }
    LOGI("SystemProperties.native_get hooks: one_arg=%s two_arg=%s",
         orig_native_get ? "OK" : "MISS",
         orig_native_get_def ? "OK" : "MISS");
    env->DeleteLocalRef(cls);
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

static void copy_prop(char* dst, const char* src) {
    if (!dst || !src) return;
    strncpy(dst, src, PROP_VALUE_MAX - 1);
    dst[PROP_VALUE_MAX - 1] = '\0';
}

static void copy_string(char* dst, size_t dst_size, const char* src) {
    if (!dst || !src || dst_size == 0) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void reset_config_defaults() {
    copy_prop(g_manufacturer, "Xiaomi");
    copy_prop(g_brand, "Xiaomi");
    copy_prop(g_model, "23046RP50C");
    copy_prop(g_device, "pipa");
    copy_prop(g_product, "pipa");
    copy_prop(g_marketname, "Xiaomi Pad 6 Pro");
    copy_prop(g_characteristics, "tablet");
    copy_prop(g_board, "pipa");
    copy_prop(g_hardware, "qcom");
    copy_prop(g_locale, "zh-CN");
    copy_prop(g_mod_device, "pipa");
    copy_prop(g_build_product, "pipa");
    copy_prop(g_cpu_abilist, "arm64-v8a,armeabi-v7a,armeabi");
    copy_prop(g_cpu_abilist32, "armeabi-v7a,armeabi");
    copy_prop(g_cpu_abilist64, "arm64-v8a");
    copy_prop(g_serialno, "unknown");
    copy_prop(g_boot_serialno, "unknown");
    copy_prop(g_arch, "arm64");
    copy_string(g_fingerprint, sizeof(g_fingerprint),
                "Xiaomi/pipa/pipa:13/TKQ1.221114.001/V14.0.8.0.TMYCNXM:user/release-keys");
    copy_prop(g_build_id, "TKQ1.221114.001");
    copy_prop(g_android_release, "13");
    copy_prop(g_sdk_int, "33");
    copy_prop(g_foldable_screen_support, "true");
    g_targets_configured = false;
    g_targets.clear();
    g_custom_props.clear();
}

static bool value_enabled(const char* value) {
    return value &&
           strcmp(value, "0") != 0 &&
           strcmp(value, "false") != 0 &&
           strcmp(value, "off") != 0 &&
           strcmp(value, "no") != 0;
}

static const char* package_for_switch(const char* key) {
    if (strcmp(key, "wechat") == 0) return "com.tencent.mm";
    if (strcmp(key, "qq") == 0) return "com.tencent.mobileqq";
    if (strcmp(key, "tim") == 0) return "com.tencent.tim";
    if (strcmp(key, "dingtalk") == 0) return "com.alibaba.android.rimet";
    return nullptr;
}

static void load_config(const char* module_dir) {
    reset_config_defaults();
    char path[512];
    snprintf(path, sizeof(path), "%s/config.conf", module_dir);
    FILE* fp = fopen(path, "r");
    if (!fp) { LOGI("no config, using defaults"); return; }
    LOGI("loading config from %s", path);
    bool app_switch_seen = false;
    std::vector<std::string> app_switch_targets;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line; char* val = eq + 1;
        trim(key); trim(val);
        const char* switch_pkg = package_for_switch(key);
        if (switch_pkg) {
            app_switch_seen = true;
            if (value_enabled(val)) app_switch_targets.push_back(switch_pkg);
        }
        else if (strcmp(key, "manufacturer") == 0)    copy_prop(g_manufacturer, val);
        else if (strcmp(key, "brand") == 0)            copy_prop(g_brand, val);
        else if (strcmp(key, "model") == 0)            copy_prop(g_model, val);
        else if (strcmp(key, "device") == 0)           copy_prop(g_device, val);
        else if (strcmp(key, "product") == 0)          copy_prop(g_product, val);
        else if (strcmp(key, "marketname") == 0)       copy_prop(g_marketname, val);
        else if (strcmp(key, "characteristics") == 0)  copy_prop(g_characteristics, val);
        else if (strcmp(key, "board") == 0)            copy_prop(g_board, val);
        else if (strcmp(key, "hardware") == 0)         copy_prop(g_hardware, val);
        else if (strcmp(key, "locale") == 0)           copy_prop(g_locale, val);
        else if (strcmp(key, "mod_device") == 0)       copy_prop(g_mod_device, val);
        else if (strcmp(key, "build_product") == 0)    copy_prop(g_build_product, val);
        else if (strcmp(key, "cpu_abilist") == 0)      copy_prop(g_cpu_abilist, val);
        else if (strcmp(key, "cpu_abilist32") == 0)    copy_prop(g_cpu_abilist32, val);
        else if (strcmp(key, "cpu_abilist64") == 0)    copy_prop(g_cpu_abilist64, val);
        else if (strcmp(key, "serialno") == 0)         copy_prop(g_serialno, val);
        else if (strcmp(key, "boot_serialno") == 0)    copy_prop(g_boot_serialno, val);
        else if (strcmp(key, "arch") == 0)             copy_prop(g_arch, val);
        else if (strcmp(key, "fingerprint") == 0)      copy_string(g_fingerprint, sizeof(g_fingerprint), val);
        else if (strcmp(key, "build_id") == 0)         copy_prop(g_build_id, val);
        else if (strcmp(key, "android_version") == 0)  copy_prop(g_android_release, val);
        else if (strcmp(key, "sdk_int") == 0)          copy_prop(g_sdk_int, val);
        else if (strcmp(key, "foldable_screen_support") == 0) copy_prop(g_foldable_screen_support, val);
        else if (strncmp(key, "prop.", 5) == 0 && key[5] != '\0') {
            g_custom_props.emplace_back(key + 5, val);
        }
        else if (strcmp(key, "targets") == 0) {
            g_targets_configured = true;
            g_targets.clear();
            char buf[512]; strncpy(buf, val, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
            char* tok = strtok(buf, ",");
            while (tok) { trim(tok); if (strlen(tok) > 0) g_targets.push_back(tok); tok = strtok(nullptr, ","); }
        }
    }
    fclose(fp);
    if (app_switch_seen) {
        g_targets_configured = true;
        g_targets = app_switch_targets;
    }
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

static void set_int_field(JNIEnv* env, jclass cls, const char* name, jint value) {
    jfieldID fid = env->GetStaticFieldID(cls, name, "I");
    if (!fid) { env->ExceptionClear(); return; }
    env->SetStaticIntField(cls, fid, value);
}

static void spoof_build(JNIEnv* env) {
    jclass cls = env->FindClass("android/os/Build");
    if (!cls) { env->ExceptionClear(); return; }
    set_field(env, cls, "MANUFACTURER", g_manufacturer);
    set_field(env, cls, "BRAND", g_brand);
    set_field(env, cls, "MODEL", g_model);
    set_field(env, cls, "DEVICE", g_device);
    set_field(env, cls, "PRODUCT", g_product);
    set_field(env, cls, "BOARD", g_board);
    set_field(env, cls, "HARDWARE", g_hardware);
    set_field(env, cls, "FINGERPRINT", g_fingerprint);
    set_field(env, cls, "ID", g_build_id);
    env->DeleteLocalRef(cls);

    jclass version_cls = env->FindClass("android/os/Build$VERSION");
    if (!version_cls) { env->ExceptionClear(); return; }
    set_field(env, version_cls, "RELEASE", g_android_release);
    set_int_field(env, version_cls, "SDK_INT", static_cast<jint>(atoi(g_sdk_int)));
    env->DeleteLocalRef(version_cls);
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
        LOGI("IAmPad v11 onLoad pid=%d", getpid());
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        if (!args || !args->nice_name) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        const char* process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!process) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        std::string process_name(process);
        env->ReleaseStringUTFChars(args->nice_name, process);

        int fd = api->getModuleDir();
        if (fd >= 0) {
            char dir[256];
            snprintf(dir, sizeof(dir), "/proc/self/fd/%d", fd);
            load_config(dir);
            close(fd);
        } else {
            LOGE("getModuleDir failed");
        }

        bool target = is_target(process_name.c_str());
        if (!target) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        LOGI("TARGET: %s uid=%d", process_name.c_str(), args->uid);
        build_prop_map();
        LOGI("spoofing %zu properties", g_prop_map.size());
        spoof_build(env);
        hook_system_properties_jni(api, env);
        orig_system_property_get = nullptr;
        api->pltHookRegister(0, 0, "__system_property_get",
                             (void*)iampad_system_property_get,
                             (void**)&orig_system_property_get);
        bool ok = api->pltHookCommit();
        LOGI("pltHookCommit(__system_property_get) = %s", ok ? "OK" : "FAIL");
        if (ok) {
            char test[PROP_VALUE_MAX];
            iampad_system_property_get("ro.os_foldable_screen_support", test);
            LOGI("VERIFY foldable_screen_support = %s", test);
        }
        LOGI("done for %s", process_name.c_str());
    }

private:
    Api* api = nullptr;
    JNIEnv* env = nullptr;
    bool is_target(const char* process) {
        if (!process) return false;
        const auto& t = g_targets_configured ? g_targets : defaults();
        for (const auto& pkg : t) {
            size_t len = pkg.length();
            if (strncmp(process, pkg.c_str(), len) == 0 &&
                (process[len] == '\0' || process[len] == ':')) {
                return true;
            }
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
