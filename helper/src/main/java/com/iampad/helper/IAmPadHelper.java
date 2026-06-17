package com.iampad.helper;

import android.app.Application;
import android.content.Context;
import android.os.Build;
import android.util.Log;

import org.luckypray.dexkit.DexKitBridge;
import org.luckypray.dexkit.query.FindMethod;
import org.luckypray.dexkit.query.matchers.MethodMatcher;
import org.luckypray.dexkit.result.MethodData;

import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

import kotlin.jvm.functions.Function0;
import top.canyie.pine.Pine;
import top.canyie.pine.PineConfig;
import top.canyie.pine.callback.MethodHook;
import top.canyie.pine.callback.MethodReplacement;

/**
 * Java helper loaded by the Zygisk module at app process startup.
 * Uses Pine for ART method hooking and DexKit for locating obfuscated methods.
 */
public class IAmPadHelper {
    private static final String TAG = "IAmPadHelper";
    private static final String QQ_TARGET_MODEL = "23046RP50C";

    private static String sModulePath;
    private static boolean sPineReady = false;

    public static void init(String packageName, String modulePath) {
        Log.i(TAG, "init package=" + packageName + " path=" + modulePath);
        sModulePath = modulePath;
        try {
            setupPine();
        } catch (Throwable t) {
            Log.e(TAG, "setupPine failed", t);
            return;
        }
        hookApplication(packageName);
    }

    private static String getAbi() {
        String[] abis = Build.SUPPORTED_ABIS;
        if (abis != null && abis.length > 0) {
            String abi = abis[0];
            if (abi.equals("arm64-v8a") || abi.equals("armeabi-v7a") || abi.equals("x86") || abi.equals("x86_64")) {
                return abi;
            }
        }
        if (Build.CPU_ABI != null) return Build.CPU_ABI;
        return "arm64-v8a";
    }

    private static void setupPine() {
        if (sPineReady) return;
        final String libDir = sModulePath + "/helper/libs/" + getAbi();
        PineConfig.libLoader = new Pine.LibLoader() {
            @Override
            public void loadLib() {
                System.load(libDir + "/libpine.so");
            }
        };
        PineConfig.disableHiddenApiPolicy = true;
        PineConfig.disableHiddenApiPolicyForPlatformDomain = true;
        Pine.ensureInitialized();
        sPineReady = true;
        Log.i(TAG, "Pine initialized");
    }

    private static void hookApplication(final String packageName) {
        try {
            Method onCreate = Application.class.getDeclaredMethod("onCreate");
            Pine.hook(onCreate, new MethodHook() {
                @Override
                public void beforeCall(Pine.CallFrame callFrame) throws Throwable {
                    Application app = (Application) callFrame.thisObject;
                    Context ctx = app.getApplicationContext();
                    ClassLoader cl = ctx.getClassLoader();
                    Log.i(TAG, "Application.onCreate fired for " + packageName);
                    try {
                        if ("com.tencent.mm".equals(packageName)) {
                            hookWeChat(cl);
                        } else if ("com.tencent.mobileqq".equals(packageName) || "com.tencent.tim".equals(packageName)) {
                            hookQQ(ctx, cl, packageName);
                        } else if ("com.alibaba.android.rimet".equals(packageName)) {
                            hookDingTalk(cl);
                        }
                    } catch (Throwable t) {
                        Log.e(TAG, "app-specific hook error", t);
                    }
                }
            });
        } catch (Throwable t) {
            Log.e(TAG, "hookApplication failed", t);
        }
    }

    private static DexKitBridge createDexKit(ClassLoader cl) {
        System.loadLibrary("dexkit");
        return DexKitBridge.create(cl, true);
    }

    private static void hookWeChat(ClassLoader cl) {
        DexKitBridge bridge = createDexKit(cl);
        try {
            MethodData md = bridge.findMethod(FindMethod.create()
                    .searchPackages("com.tencent.mm.ui")
                    .matcher(MethodMatcher.create()
                            .modifiers(Modifier.PUBLIC | Modifier.STATIC)
                            .paramCount(0)
                            .usingStrings("royole", "tecno", "ro.os_foldable_screen_support")
                            .returnType("boolean")
                    )
            ).firstOrThrow(new Function0<Throwable>() {
                @Override
                public Throwable invoke() {
                    return new RuntimeException("isFoldableDevice not found");
                }
            });
            Method m = md.getMethodInstance(cl);
            Pine.hook(m, MethodReplacement.returnConstant(Boolean.TRUE));
            Log.i(TAG, "hooked isFoldableDevice: " + m);
        } catch (Throwable t) {
            Log.e(TAG, "hookWeChat isFoldableDevice", t);
        }

        try {
            MethodData md = bridge.findMethod(FindMethod.create()
                    .searchPackages("com.tencent.mm")
                    .matcher(MethodMatcher.create()
                            .modifiers(Modifier.PUBLIC | Modifier.FINAL)
                            .paramCount(3)
                            .usingStrings("MicroMsg.CgiCheckLoginAsPad", "/cgi-bin/micromsg-bin/checkloginaspad")
                    )
            ).firstOrThrow(new Function0<Throwable>() {
                @Override
                public Throwable invoke() {
                    return new RuntimeException("checkLoginAsPad not found");
                }
            });
            Method m = md.getMethodInstance(cl);
            Pine.hook(m, MethodReplacement.returnConstant(Boolean.TRUE));
            Log.i(TAG, "hooked checkLoginAsPad: " + m);
        } catch (Throwable t) {
            Log.e(TAG, "hookWeChat checkLoginAsPad", t);
        }
        bridge.close();
    }

    private static void hookQQ(Context context, ClassLoader cl, String packageName) {
        simulateTabletModel("Xiaomi", QQ_TARGET_MODEL, "Xiaomi");
        resetQQModelCacheIfNeeded(context);
        Log.i(TAG, "QQ/TIM tablet hooks applied");
    }

    private static void hookDingTalk(ClassLoader cl) {
        DexKitBridge bridge = createDexKit(cl);
        try {
            MethodData md = bridge.findMethod(FindMethod.create()
                    .searchPackages("com.alibaba.android.dingtalkbase.foldable")
                    .matcher(MethodMatcher.create()
                            .modifiers(Modifier.STATIC)
                            .paramCount(1)
                            .paramTypes("android.app.Activity")
                            .returnType("boolean")
                            .usingStrings("isMultiLoginFoldableDevice")
                    )
            ).firstOrThrow(new Function0<Throwable>() {
                @Override
                public Throwable invoke() {
                    return new RuntimeException("isMultiLoginFoldableDevice not found");
                }
            });
            Method m = md.getMethodInstance(cl);
            Pine.hook(m, MethodReplacement.returnConstant(Boolean.TRUE));
            Log.i(TAG, "hooked DingTalk isMultiLoginFoldableDevice: " + m);
        } catch (Throwable t) {
            Log.e(TAG, "hookDingTalk", t);
        }
        bridge.close();
    }

    private static void simulateTabletModel(String brand, String model, String manufacturer) {
        try {
            setStaticField(Build.class, "MANUFACTURER", manufacturer);
            setStaticField(Build.class, "BRAND", brand);
            setStaticField(Build.class, "MODEL", model);
            setStaticField(Build.class, "DEVICE", "pipa");
            setStaticField(Build.class, "PRODUCT", "pipa");
        } catch (Throwable t) {
            Log.e(TAG, "simulateTabletModel", t);
        }
    }

    private static void setStaticField(Class<?> cls, String name, String value) {
        try {
            Field f = cls.getDeclaredField(name);
            f.setAccessible(true);
            f.set(null, value);
        } catch (NoSuchFieldException e) {
            Log.w(TAG, "missing field " + name);
        } catch (IllegalAccessException e) {
            Log.w(TAG, "cannot set " + name);
        }
    }

    private static void resetQQModelCacheIfNeeded(Context context) {
        try {
            android.content.SharedPreferences prefs = context.getSharedPreferences("BUGLY_COMMON_VALUES", Context.MODE_PRIVATE);
            String stored = prefs.getString("model", null);
            if (stored != null && !stored.equals(QQ_TARGET_MODEL)) {
                Log.i(TAG, "QQ stored model mismatch: " + stored + ", clearing cache");
                String dataDir = context.getApplicationInfo().dataDir;
                deleteRecursive(new File(dataDir, "files/mmkv/Pandora"));
                deleteRecursive(new File(dataDir, "files/mmkv/Pandora.crc"));
                android.os.Process.killProcess(android.os.Process.myPid());
            }
        } catch (Throwable t) {
            Log.e(TAG, "resetQQModelCache", t);
        }
    }

    private static void deleteRecursive(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        file.delete();
    }
}
