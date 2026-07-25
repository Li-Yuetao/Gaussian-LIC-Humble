# TensorRT 8.6.1 段错误根因诊断与解决

## 问题症状

```
Finished parsing network model. Parse time: ...
段错误（核心已转储）
```

### 关键特征
- ✅ ONNX 导出成功
- ✅ TensorRT 能解析 ONNX
- ❌ parse 后进入 build engine 阶段直接崩溃

---

## 排查过程（排除法）

| 假设 | 验证结果 | 结论 |
|------|--------|------|
| FP16 tactic 触发 bug | FP32 也崩溃 | ❌ 不是 FP16 专属 |
| 512×640 输入尺寸太大 | 480×640 也崩溃 | ❌ 不是单纯尺寸问题 |
| Builder 优化等级过高 | `--builderOptimizationLevel=0` 仍崩 | ❌ 不是 tactic 搜索问题 |
| INT64 权重不兼容 | 转换后仍崩溃 | ⚠️ 隐患但非根本原因 |

---

## 🎯 真正的根因

### 原始 ONNX 的问题结构
- **体积**：~900 MB（dangerously large）
- **包含大量动态 shape 相关节点**（Cast, Gather, Shape, ReduceProd）

### TensorRT 8.6.1 崩溃点
在 build engine 阶段处理这些动态 shape 节点组合时的优化器错误

---

## ✅ 解决方案

### 关键步骤

#### 1. INT64 → INT32 转换
```python
# 转换结果：44个 INT64 常量 → INT32
Converted 44 INT64 tensors/constants
```

#### 2. onnxsim 静态化简化 ⭐ 最关键
```bash
python -m onnxsim \
  spnet_512_640_int32.onnx \
  spnet_512_640_int32_sim.onnx \
  --overwrite-input-shape rgb:1,3,512,640 depth:1,1,512,640 mask:1,1,512,640
```

**简化效果**：
```
Cast: 44 → 0
Gather: 44 → 0
Shape: 44 → 0
ReduceProd: 44 → 0
ReduceMean: 132 → 88
```

#### 3. 关键坑⚠️
onnxsim 后**不能继续使用 `--optShapes`**

原因：onnxsim 时用了 `--overwrite-input-shape` 将 ONNX 变成静态输入模型，后续再加 `--optShapes` 会报错：
```
Static model does not take explicit shapes since the shape of inference tensors will be determined by the model itself
```

---

## 最终成功命令

### 512×640 FP16（推荐用于实际运行）
```bash
/home/lyt/cpp_lib/TensorRT-8.6.1.6/bin/trtexec \
  --onnx=spnet_512_640_int32_sim.onnx \
  --saveEngine=spnet_512_640.engine \
  --fp16 \
  --builderOptimizationLevel=0 \
  --memPoolSize=workspace:4096
```

### 480×640 FP16
```bash
/home/lyt/cpp_lib/TensorRT-8.6.1.6/bin/trtexec \
  --onnx=spnet_480_640_int32_sim.onnx \
  --saveEngine=spnet_480_640.engine \
  --fp16 \
  --builderOptimizationLevel=0 \
  --memPoolSize=workspace:4096
```

---

## 性能对比

| 精度 | 吞吐量 | 延迟 | GPU计算时间 | 结论 |
|------|------|------|-----------|------|
| **FP32** | 3.08 qps | 324.57 ms | 323.33 ms | 仅验证用 |
| **FP16** | 96.20 qps | 11.58 ms | 10.36 ms | **必须用于实际运行** |

**加速比**：~31 倍 ✨

---

## 一句话总结

不是环境、不是 FP16、不是 workspace；核心是 900MB 大型 ONNX 的动态 shape 图结构触发 TensorRT 8.6.1 build 阶段的优化器崩溃。通过 **INT64→INT32 + onnxsim 静态化简化 + 去掉 --optShapes** 完美解决。

---

## 验证状态

- ✅ spnet_480_640.engine 成功生成
- ✅ spnet_512_640.engine 成功生成
- ✅ Gaussian-LIC 完整跑通（585k Gaussians）
- ✅ PSNR 25.79（训练视图），PSNR 25.41（新视点）
