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


## Vulkan 能做 MLP 推理吗？

可以。**Vulkan 本身是通用 GPU 计算 API（通过 Compute Pipeline）**，能实现矩阵乘、激活函数、归一化等算子，因此可以做 MLP 推理。

常见实现路线：
1. **纯 Vulkan Compute Shader**：自己写算子（MatMul/MLP），延迟最低、可深度定制；
2. **ncnn / MNN / TFLite(GPU delegate)**：把 MLP 交给移动端推理框架，再与 Vulkan 渲染共享数据；
3. **混合方案**：小 MLP（如 opacity/phi）用 compute shader，复杂网络交给推理框架。

### 本 Demo 当前状态

- 当前 fragment shader 里 `phi` 是固定值（`phi = 1.0`），属于“无 MLP 的近似版本”。
- 如果要对齐 Mobile-GS 思路，可把 `phi/opacity` 的 MLP 推理接入到：
  - 渲染前的 compute pass（生成每个高斯的 `phi/opacity` buffer），或
  - 每帧按视角更新的小批量推理缓存。

简言之：**Vulkan 具备 MLP 推理能力**，只是当前 demo 还没把这条链路接上。


## OpenGL 能做 MLP 推理吗？

可以，但通常不如 Vulkan/专用推理框架好用。

- **能做**：OpenGL/OpenGL ES 也能用 fragment/compute shader 实现矩阵乘和激活函数，所以理论上可跑 MLP。
- **限制**：
  1. 在移动端 OpenGL ES 上，通用计算能力和调试体验通常弱于 Vulkan Compute；
  2. 内存/同步/跨 pass 数据组织不如 Vulkan 灵活；
  3. 工程上更常见做法是用 ncnn/MNN/TFLite 处理推理，渲染层走 OpenGL/Vulkan。

结论：**OpenGL 不是“不能推理”，而是“能推但一般不是最优选”**。
