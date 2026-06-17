# IAmPad-Zygisk

开源的 Zygisk 平板模块，用于在手机上为微信、QQ、TIM、钉钉等应用启用平板/折叠屏模式。

参考开源 Xposed 模块 [I-Am-Pad](https://github.com/Houvven/I-Am-Pad) 的设备伪装思路，但使用 Zygisk 在系统层注入，不依赖 LSPosed 框架。

## 特点

- 纯本地运行，**不会发起任何网络请求**
- 代码透明，可自由审计
- 可通过 `config.conf` 配置伪装的设备型号和目标应用
- 为微信补充折叠屏属性信号，并提供“重启微信并清理缓存”操作

> 注：微信的平板登录判断不只读取 `Build` 和系统属性。Xposed 版 I-Am-Pad 还会 Hook 微信内部的 `checkLoginAsPad` / 折叠屏判断方法；本模块当前主要补强 Zygisk 可覆盖的系统属性链路。

## 安装要求

- Android 8.0+ 已 root 设备
- Magisk 24+ 并启用 Zygisk，或 KernelSU + ZygiskNext

## 安装方法

1. 下载 GitHub Actions 构建的 `IAmPad-Zygisk-*.zip`
2. 在 Magisk / KernelSU 中刷入
3. 重启手机
4. 如需自定义，修改 `/data/adb/modules/iampad/config.conf`
5. 如果微信仍沿用旧判断，在模块详情里点击“重启微信并清理缓存”，再重新打开微信

## 模拟原理

模块在 Zygote fork 出应用进程后执行以下操作：

1. **Hook 系统属性**
   - `__system_property_get("ro.product.manufacturer")` → Xiaomi
   - `__system_property_get("ro.product.brand")` → Xiaomi
   - `__system_property_get("ro.product.model")` → 23046RP50C
   - `__system_property_get("ro.build.characteristics")` → tablet

2. **修改 `android.os.Build` 静态字段**
   - `Build.MANUFACTURER`
   - `Build.BRAND`
   - `Build.MODEL`
   - `Build.DEVICE`
   - `Build.PRODUCT`

## 配置说明

默认配置会随模块安装到 `/data/adb/modules/iampad/config.conf`：

```conf
manufacturer=Xiaomi
brand=Xiaomi
model=23046RP50C
device=pipa
product=pipa
marketname=Xiaomi Pad 6 Pro
characteristics=tablet
board=pipa
hardware=qcom
locale=zh-CN
mod_device=pipa
build_product=pipa
fingerprint=Xiaomi/pipa/pipa:13/TKQ1.221114.001/V14.0.8.0.TMYCNXM:user/release-keys
build_id=TKQ1.221114.001
android_version=13
sdk_int=33
cpu_abilist=arm64-v8a,armeabi-v7a,armeabi
cpu_abilist32=armeabi-v7a,armeabi
cpu_abilist64=arm64-v8a
serialno=unknown
boot_serialno=unknown
arch=arm64
foldable_screen_support=true
targets=com.tencent.mm,com.tencent.mobileqq,com.tencent.tim,com.alibaba.android.rimet
```

`targets` 只匹配完整包名或该包名下的子进程（例如 `com.tencent.mm:tools`）。修改后重启目标应用生效。

也可以使用开关式配置：

```conf
wechat=1
qq=1
tim=1
dingtalk=1
```

如需额外伪装属性，可使用 `prop.` 前缀，例如：

```conf
prop.ro.example.key=value
```

## 编译

### 本地编译

需要安装 Android SDK + NDK（推荐 r25c 或更新）。

```bash
export ANDROID_HOME=$HOME/Library/Android/sdk
./build.sh "$ANDROID_HOME/ndk/26.3.11579264"
```

编译完成后会生成 `IAmPad-Zygisk.zip`。

### GitHub Actions 自动构建

推荐使用 GitHub Actions 构建，无需配置本地环境。

## 免责声明

本模块仅供学习和研究使用。使用可能导致账号封禁、数据丢失等风险，请在备用机/小号上测试。

## License

MIT
