# Mobile-GS 论文贡献与代码实现对照（本仓库）

> 说明：本对照基于当前仓库代码（`/workspace/efficient-GS-viwer`）静态扫描，不代表作者全部工程（例如独立移动端 viewer 工程）。

## 0. Viewer 技术栈判定（Vulkan / WebGL / WebGPU）

### 结论
- 本仓库未包含可见的移动端 Viewer 工程（无 Android NDK/CMake + Vulkan、无 Web 前端项目）。
- 当前可见渲染后端是 **PyTorch + CUDA 扩展**（`CUDAExtension` + `.cu` 内核），不是 WebGL / WebGPU。

### 证据
- README 仅提供 `pretrain.py/train.py/render.py/metrics.py` 的训练评估流程。  
  见 `README.md`。
- 渲染核心模块 `diff_gaussian_rasterization_ms` 使用 `CUDAExtension`，源码由 `rasterizer_impl.cu / forward.cu / backward.cu / rasterize_points.cu` 组成。  
  见 `submodules/diff-gaussian-rasterization_ms/setup.py`。
- PyBind 接口导出 CUDA 栅格化函数。  
  见 `submodules/diff-gaussian-rasterization_ms/ext.cpp`。

---

## 1) 深度感知顺序无关渲染（Depth-aware OIR）

### 代码中可见实现点
- 渲染路径中引入 `phi/theta`，并与 `opacity` 一起送入 rasterizer。  
  见 `gaussian_renderer/__init__.py`。
- CUDA 侧 forward 接口显式接收 `theta/phi`，并输出累积统计量（如 `accum_weights_ptr` 等）。  
  见 `submodules/diff-gaussian-rasterization_ms/rasterize_points.cu`。

### 与“去排序”表述的关系
- 当前 CUDA 实现仍有显式 tile+depth key 构造、`SortPairs`（按深度排序）流程。  
  见 `submodules/diff-gaussian-rasterization_ms/cuda_rasterizer/rasterizer_impl.cu`。
- 因此，**本仓库可见训练/渲染代码**并不能直接证明“完全不做深度排序”的移动端实现细节；可能存在：
  1) 论文中的移动端 viewer 与本仓库训练端实现不同；
  2) 或该部分代码在其他仓库/尚未开源。

---

## 2) 神经视点依赖增强（Neural View-dependent Enhancement）

### 代码中可见实现点
- `OpaictyPhiNN` 网络输入包含 `SH + viewdir + scales + rotations`，输出 `phi` 与 `opacity`。  
  见 `scene/gaussian_model.py`。
- 在渲染函数中，调用 `opacity_phi_nn(...)` 得到 `phi, opacity` 并用于 rasterizer。  
  见 `gaussian_renderer/__init__.py`。

---

## 3) 一阶球谐函数蒸馏（First-order SH Distillation）

### 代码中可见实现点
- 训练加载 teacher 后调用 `gaussians.onedownSHdegree()` 进行 SH 降阶。  
  见 `train.py` 与 `scene/gaussian_model.py`。
- 训练 loss 同时包含图像蒸馏与深度蒸馏项（student 对 teacher）。  
  见 `train.py`。

---

## 4) 神经向量量化（Neural Vector Quantization）

### 代码中可见实现点
- `apply_svq()` 对 `scale/rotation/appearance` 分块做 KMeans 子向量量化。  
  见 `scene/gaussian_model.py`。
- `encode()` 将 codebook + index 持久化，index 使用 Huffman 编码；`decode()` 做对应重建。  
  见 `scene/gaussian_model.py`。

---

## 5) 基于贡献的剪枝（Contribution-based Pruning）

### 代码中可见实现点
- `train.py`：按低 opacity 与低 scale 的联合掩码进行周期性剪枝。  
  见 `train.py`。
- `pretrain.py`：依据 `accum_weights/area` 等重要性指标筛选并剪枝。  
  见 `pretrain.py`。

---

## 附：如何快速定位这五类实现

- 神经 opacity/phi：`scene/gaussian_model.py` 里 `OpaictyPhiNN` 与 `init_vnn`。
- 渲染调用入口：`gaussian_renderer/__init__.py`（`render` / `render_imp` / `render_teacher`）。
- OIR 相关 CUDA：`submodules/diff-gaussian-rasterization_ms/rasterize_points.cu` 与 `cuda_rasterizer/rasterizer_impl.cu`。
- 蒸馏与降 SH：`train.py` + `scene/gaussian_model.py`。
- SVQ 压缩：`scene/gaussian_model.py` 的 `apply_svq/encode/decode/kmeans`。
- 剪枝：`train.py` 与 `pretrain.py`。

---

## 面向业务落地的补充说明（Android / H5 / 京东 App 场景）

### Q1：Vulkan 可以在移动端 Android 上开发应用吗？

可以。Vulkan 是 Android 官方支持的原生图形 API（通常通过 NDK/C++ 使用），适合高性能 3D 渲染。

### Q2：Vulkan 可以直接用在浏览器（H5）里吗？

通常不可以。浏览器标准图形接口是 WebGL / WebGPU，不会把 Vulkan 作为前端 JS 直接可用 API 暴露。

### Q3：如果我要给京东 App 做这个功能，用 Vulkan 可以吗？

取决于你把功能放在哪一层：

1. **原生容器层（可行）**  
   如果京东 App 某个页面由原生模块承载（Native 页面、或 Native 插件容器），可以做 Vulkan 渲染，再把交互通过桥接层与业务系统打通。

2. **纯 H5 页面层（不直接可行）**  
   如果页面是纯 WebView/H5，前端只能走 WebGL/WebGPU，不能直接调用 Vulkan。

3. **混合方案（常见）**  
   - H5 负责业务 UI；
   - 重渲染区由原生组件承载（Vulkan）；
   - 两者通过 JSBridge/消息通道通信。

### Q4：京东 App 是 H5 吗？

大型电商 App 通常是“原生 + H5 + 小程序/容器化”的混合架构，不是“全 H5”。
因此是否能用 Vulkan，关键不在“是不是京东 App”，而在该功能最终落在哪个运行时：

- 运行时是浏览器/H5：优先 WebGL/WebGPU；
- 运行时是 Android 原生模块：可以 Vulkan。

### 结论建议（针对你当前目标）

- 如果目标是 **快速在 H5 上线**：优先 WebGL/WebGPU 路线（例如把点云/高斯渲染迁移到 WebGPU shader）。
- 如果目标是 **追求极致性能**：做 Native Vulkan 组件，再以混合架构接入业务 App。
- 本仓库当前公开代码偏训练与 CUDA 实验管线，**不等于**可直接产出 Android Vulkan viewer；移动端工程需要单独开发。


---

## 补充：`Cbg`（背景色）在 sort-free 前向颜色计算中的位置与数据流

> 你提到的 `Cbg` 在这份实现里对应变量名是 `bg_color`（CUDA 参数名），本质是每像素融合时的背景颜色向量（RGB）。

### 1) `Cbg/bg_color` 在哪里？

- 前向颜色 kernel `renderCUDA` 的入参包含 `const float* bg_color`。  
  见 `submodules/diff-gaussian-rasterization_ms/cuda_rasterizer/forward.cu`。
- 最终颜色写回公式里直接使用了它：  
  `out_color = C / w_fg * (1 - T) + T * bg_color`。  
  见 `submodules/diff-gaussian-rasterization_ms/cuda_rasterizer/forward.cu`。

### 2) 这个参数怎么来的（来源）？

1. **命令行/配置层**：`ModelParams` 定义了 `--white_background`（默认 `False`）。  
   见 `arguments/__init__.py`。
2. **训练/渲染入口层**：
   - 若 `dataset.white_background=True`，`bg_color=[1,1,1]`；否则 `[0,0,0]`；
   - 再构造成 CUDA tensor：`background = torch.tensor(bg_color, dtype=torch.float32, device="cuda")`。  
   见 `train.py` 与 `render.py`。
3. **Python 渲染包装层**：该 tensor 作为 `GaussianRasterizationSettings.bg` 保存。  
   见 `gaussian_renderer/__init__.py` 与 `submodules/diff-gaussian-rasterization_ms/diff_gaussian_rasterization_ms/__init__.py`。
4. **C++/CUDA 扩展层**：`raster_settings.bg` 被打包到 C++ 调用参数首位，并传入 CUDA kernel 的 `bg_color`。  
   见 `submodules/diff-gaussian-rasterization_ms/diff_gaussian_rasterization_ms/__init__.py`、`submodules/diff-gaussian-rasterization_ms/rasterize_points.cu`、`submodules/diff-gaussian-rasterization_ms/cuda_rasterizer/rasterizer_impl.cu`、`submodules/diff-gaussian-rasterization_ms/cuda_rasterizer/forward.cu`。

### 3) 这个参数如何变化（运行时行为）？

- **单次 train/render 进程内**：`background` 创建后不参与梯度更新，也没有在循环中被重写；通常是常量白或黑背景。  
  见 `train.py`、`render.py`。
- **跨任务/跨数据集**：通过 `--white_background` 配置切换为白/黑。  
  见 `arguments/__init__.py`。
- **额外相关点（仅 Blender/NeRF Synthetic 读图）**：
  数据集读取时，也会用同一个 `white_background` 将 RGBA 图像先做一次前景-背景合成：
  `arr = rgb * alpha + bg * (1-alpha)`。这影响 GT 图像本身。  
  见 `scene/dataset_readers.py`。

### 4) 从“数据流”角度的一句话总结

`--white_background` → Python 里构造 `background` CUDA tensor（白/黑）→ 写入 `raster_settings.bg` → 传到 CUDA `bg_color`（即你说的 `Cbg`）→ 在 sort-free 前向最终融合公式中参与每像素输出。


---

## 补充：sort-free 里 `C` 的计算实现（核心前向）

### 代码位置
- 主实现位于 `renderCUDA`：
  - 初始化：`C[ch]=0`, `w_fg_c[ch]=0`, `T=1`；
  - 每个高斯贡献：计算 `weight`、`alpha`，并把 `features * alpha * weight` 累加到 `C`；
  - 像素输出：`out_color = C / w_fg_c * (1-T) + T * bg_color`。

### 对应实现（按执行顺序）
1. **初始化累加量**
   - `float C[CHANNELS] = {0};`
   - `float w_fg_c[CHANNELS] = {0};`
   - `float T = 1.0f;`

2. **每个候选 Gaussian 的 sort-free 权重**
   - 深度与尺度参与构造 `weight`：
     `weight = exp(max_scale / depth) + phi / depth^2 + phi^2`

3. **Alpha 计算（空间核 + opacity）**
   - `alpha = min(0.99, opacity * exp(power))`
   - `power` 来自二维高斯椭圆核（conic）

4. **颜色累加（核心 C 计算）**
   - 对每个颜色通道 `ch`：
     - `C[ch] += feature[ch] * alpha * weight`
     - `w_fg_c[ch] += alpha * weight`

5. **透射率更新**
   - `T = T * (1 - alpha)`

6. **最终像素合成**
   - `out_color[ch] = C[ch] / w_fg_c[ch] * (1 - T) + T * bg_color[ch]`

### 等价伪代码
```text
C = 0, W = 0, T = 1
for g in gaussians_over_pixel:
    weight = f(depth_g, scale_g, phi_g)
    alpha  = opacity_g * gaussian2d(d)
    C += color_g * alpha * weight
    W += alpha * weight
    T *= (1 - alpha)
out = (C / W) * (1 - T) + T * Cbg
```

其中 `Cbg` 在代码里对应 `bg_color`。

### 一句话理解
这个实现不是“按深度顺序逐层 alpha-blend 直接累颜色”，而是先把前景颜色做 **加权累积（`C`）+ 归一化（`/w_fg_c`）**，再用 `T` 与背景色混合输出。


### FAQ：`T` 是累乘的，还算顺序无关吗？

短答案：**在这个公式里，`T_final = Π_i (1-α_i)` 对同一组高斯是顺序无关的（乘法可交换）**；
但实现层面仍可能有极小顺序差异（浮点非结合性）与工程性顺序处理。

#### 为什么这里“看起来累乘却仍可顺序无关”
- 该实现里：
  - `T` 更新是 `T = T * (1 - alpha)`；
  - 颜色累加是 `C += color * alpha * weight`（没有经典前向 alpha-blend 里的前缀透射 `T_prev` 逐项调制）；
  - 最终统一用 `(1 - T_final)` 乘到归一化前景上，再与 `T_final * Cbg` 融合。
- 因此，在忽略浮点舍入时：
  - `T_final` 是若干 `(1-α_i)` 的乘积，与遍历顺序无关；
  - `C` 和 `w_fg_c` 也是加法累计（同样在实数域与顺序无关）。

#### 为什么代码里仍能看到“排序/顺序”
- 本仓库 CUDA 管线仍有 tile+depth key 和排序步骤（工程调度与内存访问组织）。
- 另外，浮点加法/乘法在 GPU 上不是严格结合律，改变遍历顺序会带来数值层面的微小差异。

所以更准确说法是：**公式结构是“弱顺序依赖/近似顺序无关”的聚合式渲染，不是传统前缀透射驱动的严格有序 alpha blending。**


### FAQ：3DGS 里常见“前向累加到 1 就停”，这里没有 early-stop 会不会更慢？

这是个很专业的问题。对这份代码，结论分两层看：

1. **是的，当前实现里没有按 `T < eps` 直接 `break` 的早停分支**（在 `renderCUDA` 主循环中可见 `T` 更新，但没有对应 early termination 条件）。
2. **但也不是“所有高斯都完整计算”**，因为前面有多层筛选和跳过：
   - 只处理可见且覆盖到 tile 的高斯（视锥/半径/包围盒/tile overlap）；
   - 像素内也有快速 `continue`（`depth<0`、`power>0`、`alpha<1/255`）。

#### 这份实现里实际的“减负”机制
- 预处理阶段：半径为 0 或不触达像素块的高斯不会进入后续像素贡献。
- tile-binning：每个像素只遍历本 tile range 内的候选列表，不是全局 N 个高斯。
- 像素级过滤：
  - `depth < 0` 直接跳过；
  - 高斯核 `power > 0` 跳过；
  - `alpha < 1/255` 跳过。

#### 为什么还可能有性能优势（理论上）
- 传统有序 alpha blending 依赖更强的顺序结构，前缀透射率链式依赖会抑制并行组织。
- sort-free 聚合把前景合成改为“可并行累计的统计量（`C`、`w_fg`、`T_final`）”，更容易做并行化与统一 kernel 计算路径。

#### 但对“这份仓库代码”要实话实说
- 该仓库 CUDA 管线仍包含 tile+depth 的排序步骤；
- 且没有 `T`-based early-stop；
所以它并不等价于“完全无排序+严格早停替代”的最终移动端实现形态。你担心的极端密集场景下额外计算，确实是一个合理关注点。


### FAQ：渲染时“无效贡献”与训练时“低透明度剪枝”有什么区别？

不只是阈值不同，它们发生在**不同阶段**、作用对象和后果都不同：

1. **渲染时无效贡献过滤（runtime filtering）**
   - 发生时机：每一帧前向渲染时。
   - 作用对象：当前像素-高斯对（pair-level）。
   - 典型条件：`depth < 0`、`power > 0`、`alpha < 1/255` 时 `continue`。
   - 后果：只是“这一次像素计算里跳过该贡献”，高斯本体仍保留在模型中，下一帧/下个视角仍可参与。

2. **训练期剪枝（model pruning）**
   - 发生时机：训练循环中的周期步骤（`pruning_interval`）。
   - 作用对象：高斯点本体（point-level）。
   - 依据：统计低 opacity 与低 scale，并累计投票后生成 `prune_mask`，调用 `gaussians.prune_points` 直接删除参数。
   - 后果：被剪掉的高斯从模型中永久移除，后续所有帧都不再参与，也减少显存与计算。

#### 直观类比
- 无效贡献过滤：像“本帧临时静音某个声部”；
- 剪枝：像“把这个声部从乐谱里删掉”。

所以两者不是“仅仅数值不同”，而是 **临时跳过 vs 永久结构化删点** 的根本差别。


### FAQ：多个“分别训练”的高斯对象一起渲染，sort-free 会不会失真？

短答案：**会有风险，尤其是“分别训练后直接拼场景”时**。你说的“人物放大后无法 cover 住”是可能出现的。

#### 为什么会发生
- 这套聚合里每个高斯贡献由 `alpha * weight` 决定，`weight` 与 `depth/scale/phi` 相关。  
  当对象 A、B 来自不同训练过程时，它们的 `scale` 分布、`phi` 统计、opacity 标定并不一定在同一标尺上。
- 结果是：
  - 某对象在近景可能“权重过强/过弱”；
  - 或出现边缘穿插、前后关系不稳定、局部发灰/发亮；
  - 放大人物这类近景特写更容易暴露这个问题。

#### 关键点
- 当前方法最稳的使用方式仍是**同一场景联合优化**（对象之间在同一损失下共同校准）。
- “分别训练再直接合并”属于分布外组合，确实更容易出现你担心的失真。

#### 工程上常见缓解方案
1. **合并后联合微调（推荐）**：至少做短程 finetune，让 `scale/phi/opacity` 重新对齐。
2. **分层渲染+合成**：人物/背景分层渲染，结合深度图做后合成（必要时加边界 feather）。
3. **对象级标定**：给每个对象加可学习的全局系数（opacity/scale/颜色增益）做对齐。
4. **遮挡约束**：引入更强的几何/深度一致性约束，减少前后关系错误。
5. **LOD 与阈值策略**：近景提高对象局部高斯密度，远景做稀疏化，避免单一统计在全尺度失效。

#### 结合本仓库实现的现实判断
- 代码里的贡献权重直接使用 `max_scale/depth` 与 `phi` 组合；
- 没有看到专门针对“多独立对象合并”的校准模块。

所以如果你是“人 + 大场景”这种跨对象合成，建议默认把“合并后联合微调”当成必选步骤，而不是可选项。


### FAQ：合并后微调如果没有 ground truth，怎么校准？

可以做，但要从“监督学习”转成“约束优化/自监督优化”。常见做法是组合多个弱监督信号：

#### A. 几何与视图一致性（最常用）
1. **多视角重投影一致性**：
   - 在相邻视角之间约束颜色/特征一致性；
   - 对可见区域做 photometric consistency。
2. **深度一致性**：
   - 使用渲染深度图与外部深度先验（SfM/MVS/单目深度）做一致性约束；
   - 重点约束人物与背景交界处，降低穿插伪影。

#### B. 分层与遮挡监督（针对“人+大场景”很有效）
1. **前景掩码/人体分割先验**：
   - 用现成分割器得到人物 mask，约束人物高斯主要解释人物区域。
2. **层级合成损失**：
   - 人物层、背景层分别渲染，再按深度或 alpha 合成；
   - 对合成边缘加平滑/羽化正则，减少硬边和漏光。

#### C. 参数对齐（跨对象标尺校准）
1. **对象级可学习标量**：
   - 每个对象引入少量可学习参数（opacity scale、color gain、scale bias、phi bias）。
2. **分布对齐正则**：
   - 约束不同对象的 `scale/opacity/phi` 分布不要相差过大，避免某对象“统治权重”。

#### D. 无参考质量先验（没有真值也可用）
- 使用无参考图像质量损失（锐度、噪声、边缘一致性）与时序稳定性损失（视频场景时）。
- 可加入轻量感知先验（如 CLIP/LPIPS 风格的一致性约束）防止过拟合到伪影。

#### E. 工程建议（可直接落地）
1. **先冻结大部分参数**（位置/旋转先冻结），只调对象级校准参数；
2. 伪影明显后再逐步解冻局部高斯；
3. 优先优化交界区域（人物轮廓、接触面、遮挡边）；
4. 采用小学习率 + 早停，防止“无 GT 漂移”。

#### 结论
没有 GT 不是不能做，而是要把目标改成“多约束一致性最优化”。
对于你这个“人物并入大场景”的场景，**分层渲染 + 深度/掩码约束 + 对象级标尺校准**通常是最稳的无 GT 方案。


### FAQ：球谐蒸馏训练是怎么做的？一阶蒸馏编解码模型、loss、GT 各是什么？

下面按代码流程梳理（对应 `train.py` + `scene/gaussian_model.py` + `gaussian_renderer/__init__.py`）：

#### 1) 蒸馏训练整体流程
1. 从 checkpoint 读取参数；
2. 构建 teacher：`TeaGaussianModel(sh_degree=3)` 并 `restore`（teacher 侧保持高阶 SH 表达）；
3. student 使用 `GaussianModel` 恢复后进入蒸馏阶段，并初始化视角相关网络 `opacity_phi_nn`；
4. 训练时每个视角同时渲染：
   - student 输出 `image` 与 `render_depth`；
   - teacher 输出 `img_teacher` 与 `depth_teacher`；
5. 用 teacher 输出对 student 做蒸馏监督，同时保留对真实图像的重建项。

#### 2) “一阶蒸馏编解码模型”在代码里的样子
- 配置层默认 `sh_degree=1`（一阶 SH）。
- `construct_net()` 定义了一个轻量“编码-解码”结构：
  - `mlp_cont`：空间位置编码网络；
  - `mlp_view`：解码 view 相关 SH 系数（输出 `3 * max_sh_rest`）；
  - `mlp_dc`：解码 DC 分量（输出 3 通道）。
- 渲染时若 `net_enabled=True`，会用这些 MLP 重建 `dc + sh_rest` 后再进 rasterizer。
- 在压缩阶段，`encode()/decode()` 会存取并恢复这些 MLP 参数与量化码本。

#### 3) loss 如何计算
当前训练总损失由三部分组成：
1. **真实图像重建项**：`L1(image, gt_image)` 与 `SSIM(image, gt_image)`；
2. **蒸馏图像项**：`Ll1_distill = L1(image, img_teacher)`；
3. **蒸馏深度项**：`Ll1_depth = scale_invariant_loss(render_depth, depth_teacher)`。

代码中实际写法：
- `loss = recon + lambda_distill * Ll1_distill + lambda_depth * Ll1_depth`
- 后面又额外加了一次 `lambda_depth * Ll1_depth`（等于深度蒸馏被加权了两次）。

#### 4) 一阶蒸馏模型里的 GT 是怎么来的？
不是单一来源，而是两路 GT：

1. **真实 GT（数据集）**：
   - `gt_image = viewpoint_cam.original_image`。
2. **teacher GT（在线生成）**：
   - `img_teacher` 与 `depth_teacher` 由当前视角下 teacher 模型渲染得到。

也就是说，蒸馏项的“GT”是 teacher 当前前向输出，不是外部离线标注图。

#### 5) GT 里的参数是“训练出来的”还是“固定给定”的？
- **真实图像 GT**：来自数据集，不可学习。
- **teacher GT 的生成参数**：来自已训练 checkpoint（预训练阶段得到的 teacher 参数），在此蒸馏阶段作为教师参考；
  训练循环中并没有对 teacher 执行 optimizer step，默认等价于固定 teacher。

> 实务上可理解为：
> - 数据集图像提供“绝对外观监督”；
> - teacher 渲染提供“高阶表达向低阶表达迁移”的软监督。


### FAQ：`start_checkpoint` 从哪里来？数据集里没有啊

你理解正确：**checkpoint 不在数据集里**，而是训练脚本运行时在 `model_path` 下生成的。

- `pretrain.py` 默认会在指定迭代（默认包含 30000）保存：
  `model_path/chkpnt30000.pth`。
- `README` 的 fine-tune 命令正是把这个文件作为 `--start_checkpoint` 传给 `train.py`。

所以标准流程是：
1. 先跑 pretrain 产出 `chkpnt30000.pth`；
2. 再跑 train 用 `--start_checkpoint <model_path>/chkpnt30000.pth`。

如果你直接只拿数据集不跑 pretrain，自然不会有这个 checkpoint 文件。


### FAQ：做球谐蒸馏是不是必须先完整跑一遍 3DGS，太耗时怎么办？

你的担心是对的：按这份仓库当前训练脚本，蒸馏阶段基本假设你已经有 pretrain checkpoint。

#### 代码层面的现实情况
- `train.py` 中 teacher 的构建与 `restore` 放在 `if checkpoint:` 分支里；
- 但后续训练循环会无条件调用 `render_teacher(...)`。

这意味着在当前实现下，若没有 `--start_checkpoint`，流程并不完整（teacher 目标缺失）。

#### 是否一定要“完整重跑很久”
不一定。可以用下面策略减少时间：

1. **短程 teacher 预训练（推荐）**
   - 不必一上来跑满高迭代；先用较少迭代得到可用 teacher；
   - 再进入蒸馏阶段，通常比“从零高质量 teacher”快很多。

2. **蒸馏 warm-start**
   - 用已有相似场景 checkpoint 做初始化（同类数据、同相机分布）；
   - 再在目标场景做少量适配训练。

3. **分辨率/采样降级预热**
   - 先低分辨率、低视角采样训练 teacher；
   - 收敛到可用质量后再切回目标配置。

4. **阶段化训练**
   - 先只训练蒸馏关键分支（如网络参数与少量外观参数），
   - 再逐步解冻全量参数，减少前期计算负担。

5. **离线 teacher 缓存（工程优化）**
   - 对固定视角集预渲染 teacher 图像/深度作为软标签缓存，
   - 训练时减少 teacher 在线前向开销（代价是灵活性下降）。

#### 一句话建议
如果你追求工程效率，建议把流程改成“**快速 teacher（短程）→ 蒸馏细化**”，而不是“先跑很久 teacher 再开始蒸馏”。
