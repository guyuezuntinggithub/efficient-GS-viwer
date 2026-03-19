# WebGL2 顶点着色器 MLP 推理（Opacity）Demo

这个 demo 演示：

- 在 **WebGL2 顶点着色器** 中实现一个小 MLP（2层）
- 直接预测每个点的 `opacity`
- 用 point sprite 高斯核在片元阶段完成 splat
- 鼠标拖动旋转，滚轮缩放

## 运行

直接用静态服务器打开目录即可，例如：

```bash
cd webgl_mlp_opacity_demo
python -m http.server 8080
# 浏览器打开 http://localhost:8080
```

## 说明

- 这是最小可运行示例，MLP 权重使用随机初始化（演示推理路径）。
- 你可以把训练好的权重替换到 `main.js` 里的 `uW* / uB*` uniforms。
- 真正工程里建议把权重打包为纹理或 UBO，以减少 uniform 传输成本。
