# Android Vulkan Sort-Free Demo (0阶 SH)

> 这是一个 **Android NDK + Vulkan** 可运行 Demo，包含：
> - PLY(ASCII) 读取；
> - 实时渲染主循环；
> - 触控交互（拖动旋转视角）；
> - point-sprite 高斯 splat 的 sort-free 加权累积（单通道近似版）。

## 当前实现状态

- ✅ NativeActivity + Vulkan 实时循环（swapchain/renderpass/cmd buffer/present）
- ✅ 从 `assets/scene.ply` 读取点云（x/y/z/r/g/b/scale/opacity）
- ✅ 触控 yaw/pitch 交互
- ✅ 使用 Vulkan Graphics Pipeline 绘制 point sprite Gaussian splat
- ✅ 0阶 SH（直接 RGB 颜色）
- ⚠️ 当前是“单 pass 加权累积近似版”，尚未做完整双附件 `accum+reveal` + fullscreen composite

## 目录

- `app/src/main/cpp/main.cpp`：Vulkan 初始化、PLY上屏、交互、绘制
- `app/src/main/cpp/ply_loader.*`：PLY 解析
- `app/src/main/cpp/shaders/*`：备用 shader 草案（完整双 pass 的参考）
- `app/src/main/assets/scene.ply`：示例点云

## 依赖与构建

1. Android Studio 打开 `android_vulkan_sortfree_demo`。
2. 需要 Android SDK + NDK（建议 r26+）。
3. 真机需支持 Vulkan（arm64-v8a）。
4. 直接 Run。

> 说明：
> Native 代码里用到了 `shaderc` 做运行时 GLSL->SPIR-V 编译；
> 如果你的 NDK/环境不带 `shaderc`，代码会自动回退为 clear-only（仍可运行）。

## 下一步（可继续增强）

- 完整实现双 pass weighted-accum + composite（`accum/reveal` 离屏附件）
- 加入 Pinch 缩放 / 双指平移
- 支持二进制 PLY 与更大规模流式上传


## 打包 ZIP

在仓库根目录执行：

```bash
bash scripts/package_android_demo.sh
```

默认输出：`artifacts/android_vulkan_sortfree_demo.zip`
