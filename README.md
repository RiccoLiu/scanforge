# ScanForge — 工业三维扫描上位机参考实现

一个**从零编写、可构建、可测试**的开源骨架，用来把 Qt6 + 现代 C++ 的全部工程知识点挂在一条真实链路上：

> 可插拔数据源 → 采集流水线 → 标定与三角化 → OpenGL 点云显示 → 误差归因与报告

它不是点云查看器，也不是 CloudCompare 的简化版。**判断一个功能该不该做，只看它是否让「采集 → 标定 → 重建 → 判定 → 交付」这条链路更完整、更可验证。**

> 合规红线：本仓库不含任何公司代码、标定参数、客户数据或真实设备序列；硬件层只留接口，用合成数据与公开数据集驱动。License: MIT（自有代码）。

---

## 1. 目录结构

```
scanforge/
├── CMakeLists.txt              # 顶层：特性开关 + Qt6 自动探测
├── cmake/CompilerWarnings.cmake
├── core/                       # 纯 C++17，禁止 include 任何 Qt 头文件
│   ├── include/scanforge/      # Types / Result / Contracts / Calibration /
│   │                           # PlyIO / Synthetic / Filters /
│   │                           # FrameQueue / DeviceStateMachine
│   └── src/                    # 三角化、PLY、合成真值、滤波与误差评估
├── plugins/                    # Qt 插件契约 + QPluginLoader 加载器
│   ├── include/scanforge/      # IDevice.hpp（GenICam 风格参数节点树）
│   └── mock_camera/            # 可注入噪声/丢帧/时间偏移的模拟相机插件
├── pipeline/                   # 三泳道线程模型（Qt QObject 适配层）
│   ├── AcquisitionWorker / ProcessingWorker / PipelineController
├── app/                        # QMainWindow + 3 Dock + QOpenGLWidget 显示层
│   └── src/shaders/            # GLSL 330 core，走 qrc
├── cli/                        # 无界面批处理，只链接 core
├── tests/                      # 14 个单元测试，零外部依赖（可切 Catch2）
├── docs/
│   ├── ARCHITECTURE.md         # 依赖方向、线程边界、契约、状态机、编码规约
│   └── REFERENCE_MAP.md        # ★ 每个模块该参考哪个开源项目、看哪些文件
└── .github/workflows/ci.yml
```

编译期依赖严格单向：`core ← plugins ← pipeline ← app`，`core ← cli / tests`。
运行时数据则跨三线程回流：GUI 线程 | 采集 QThread | 处理 QThread。

---

## 2. 快速开始（不需要 Qt，2 分钟验证）

```bash
cmake -S . -B build -DSCANFORGE_BUILD_APP=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# Debug 也必须能跑（15/15 已验证）
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

# 无界面端到端跑一遍
./build/cli/Release/scanforge-cli --frames 6 --noise 0.8 \
    --output scan.ply --report scan-report.md
```

**Windows / MSVC 三个坑（本仓库已处理或已注明）**：
1. `/utf-8` 必须加，否则 MSVC 按 GBK(936) 解析 UTF-8 源文件，中文字符串常量会报 `C4819 / C2001`。已在 `cmake/CompilerWarnings.cmake` 配置。
2. 若环境变量同时存在 `HTTPS_PROXY` 与 `https_proxy`，MSBuild 会因大小写不敏感的环境字典报 `MSB6001`。构建前 `unset HTTPS_PROXY https_proxy` 即可（与代码无关）。
3. **Debug 构建下 `std::normal_distribution` 的 `stddev` 必须 > 0。** 标准 `[rand.dist.norm.normal]` 明确要求这一点，MSVC Debug 在 `<random>:3489` 用 `_STL_ASSERT(0.0 < _Sigma0, "invalid sigma argument for normal_distribution")` 直接 `abort()`；附加调试器时会弹出「Debug Assertion Failed」并停住，**表现像程序死机**，按「全部中断」看到的栈顶就是 `_Init`。此前 `generateFrames()` 把 `noiseSigmaPx = 0` 钳成 0 后构造分布，Release 下断言被编译掉所以能跑（属于 UB）。现已改为 `sigma <= 0` 或 NaN 时**不构造分布**、噪声直接置 0，并加了回归用例。`tests/mini_test.hpp` 同时改为逐行 `std::unitbuf` 刷新，测试中止时最后一行 `[ RUN ]` 会指出是哪个用例。

Qt 层在没装 Qt6 时会被 CMake 自动跳过，**core / cli / tests 仍可完整构建运行**——这正是 `core/` 解耦的验收标准。

### 构建完整 GUI（需要 Qt 6.8 LTS 或更新 LTS）

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt6.8>/lib/cmake \
      -DSCANFORGE_BUILD_APP=ON
cmake --build build --config Release
# 插件输出到 build/plugins/，程序启动后点 Connect 自动扫描该目录
```

可选开关：`SCANFORGE_USE_CATCH2=ON`（有 Catch2 v3 时改用 `TEST_CASE/REQUIRE`，测试源码不用改）。

---

## 3. 双指标误差归因（骨架里最有教学价值的部分）

`cli` 用**已知真值**的合成数据跑完整链路，并同时输出两个指标：

| 运行 | 点到真值平面 RMSE | 点云最近邻 RMSE |
|---|---|---|
| `--bias-mm 0 --noise 0.8` | `0.0000107 mm` | `0.333 mm` |
| `--bias-mm 2 --noise 0.8` | `1.99661 mm` | `2.02712 mm` |

原因是几何事实：**三角化交点是「射线 ∩ 标定平面」，所以无论像素噪声多大，重建点都严格落在标定平面上**。因此：

- `evaluateAgainstPlane()` 抓的是**标定偏差**（陈旧光平面 d / 外参 / 温漂）；
- `evaluateAgainstCloud()` 抓的是**噪声、峰值检测、触发抖动、运动误差**。

两类指标分开，才能做「曝光 → 触发与同步 → 标定 → 配准 → 机械振动」的分层归因。真实设备永远给不了真值，合成数据永远能——这是这台「软件标定环境」最大的价值。

---

## 4. 模块 ↔ 开源参考（速查）

完整版见 **[docs/REFERENCE_MAP.md](docs/REFERENCE_MAP.md)**（逐文件列出仓库路径、看什么、许可证边界）。

| ScanForge 模块 | 主要参考 | 学什么 |
|---|---|---|
| 顶层 CMake / 分层 / CI | **neuvision** | 插件式相机工程骨架、vcpkg、多平台 CI |
| `core/Types.hpp` | Open3D / PCL / PDAL | 点云值类型、单位与坐标系元数据 |
| `core/Result.hpp` | LLVM `Error.h` / `std::expected` | 显式错误通道，不跨线程抛异常 |
| `core/Contracts.hpp` | PDAL `Stage.hpp` / 3DUNDERWORLD-SLS | 链式算子、库与应用分离、中间格式 |
| `core/Calibration.*` | OpenCV calib3d / Kalibr | 相机模型、光平面、重投影误差报告 |
| `core/Synthetic.*` | Kalibr 仿真 / EuRoC MAV | 已知真值 + 注入偏差做误差归因 |
| `core/PlyIO.*` | PCL / PDAL / Open3D | 格式解析与单位注释 |
| `core/Filters.*` | PCL VoxelGrid / Open3D | 体素降采样、最小二乘平面、误差报告 |
| `plugins/IDevice.hpp` | **CloudCompare 插件接口**（GPL，只读）、Aravis | 插件契约、GenICam 节点树 |
| `plugins/mock_camera` | Aravis `arv-fake-camera` | 无硬件验证整条采集链路 |
| `pipeline/` | Qt QThread Worker 官方示例 / PDAL | 三线程、取消令牌、优雅退出 |
| `app/`（界面） | CloudCompare `mainwindow.cpp`（GPL，只读）、SLMaster | IDE 式多 Dock 工业布局 |
| `app/PointCloudView` | CloudCompare `ccGLWindowInterface` / Potree / Qt 文档 | 点云渲染、八叉树 LOD、HUD |
| `cli/` | PDAL `pdal pipeline` | 命令行驱动同一条核心管线 |

**许可证红线**：GPL 项目（CloudCompare、SLStudio）**只读架构、不抄代码**；MIT / BSD / LGPL（Open3D、PCL、PDAL、Potree、Aravis、3DUNDERWORLD-SLS）可复用但要分清动态链接与同源修改义务；Qt 开源版用动态链接。

---

## 5. 两条纪律（比选什么版本更重要）

1. **`core/` 禁止 include 任何 Qt 头文件。** 否则会写成满屏 `QString/QVector` 的「Qt 方言」，看着会 Qt，换个框架就不会写 C++ 了。附带好处是 core 能被 `cli` 与单元测试直接复用。
2. **仓库从零写。** 不搬公司一行代码、一个标定参数、一份客户数据。硬件层只留接口，用模拟源 + 合成点云 + 公开数据集驱动——既是合规红线，也顺手把插件化练了。

---

## 6. 与 26 周学习路线的对应

| 阶段 | 周次 | 本仓库对应内容 |
|---|---|---|
| 0 环境与 CI | 1–2 | 已完成：顶层 CMake、警告配置、CI、目录分层 |
| 1 纯 C++ core | 3–6 | 已完成：类型体系、Result、PLY、合成真值、误差评估、14 个测试 |
| 2 Qt 界面 | 7–10 | 已完成骨架：MainWindow / 三个 Dock / QSettings 布局持久化 |
| 3 插件与三线程流水线 | 11–14 | 已完成骨架：**分水岭**，状态机、取消令牌、优雅退出 |
| 4 OpenGL 显示 | 15–18 | 已完成骨架：QOpenGLWidget、VAO/VBO 复用、HUD；LOD 待补 |
| 5 标定/配准/报告 | 19–22 | 部分：三角化 + 双指标报告；ICP 配准、畸变校正待补 |
| 6 开源化交付 | 23–26 | README / ARCHITECTURE / REFERENCE_MAP / LICENSE / CI 已就位 |

---

## 7. 已知 TODO（按优先级，别一次全做）

1. `triangulateLaserLine()` 目前是「列内最亮像素」，需换亚像素高斯中心 + 畸变校正；
2. 运动补偿：当前单帧重建不使用时间戳，所以 `--offset-ms` 只写进元数据、不影响 RMSE，补上后才能真正练「时间同步误差归因」；
3. `BoundedQueue` 已实现但尚未接入 Qt 信号队列（当前是**无界**的），阶段 4 接入以形成背压；
4. 退出前确认处理队列排空（见 `PipelineController.cpp` 的 TODO）；
5. ICP 配准、统计离群滤除、八叉树 LOD、Qt RHI/VTK 兜底、Catch2 与 QtTest。

---

## 8. 简历可用的三条成果

- 设计并实现纯 C++17 core 与 Qt6 UI 的**单向依赖隔离**，同一条重建管线可在 GUI、CLI、单元测试三种环境下运行，CI 无 GUI 回归。
- 实现**已知真值 + 缺陷注入**的合成扫描数据源，建立平面距离 / 点云最近邻双指标误差归因，可定量复现 2mm 标定偏置（`plane RMSE 0.00001 → 1.997 mm`）。
- 实现三线程采集流水线与六态设备状态机，用取消令牌而非 `terminate()` 实现随时中止与优雅退出，跨线程以 `shared_ptr<const PointCloud>` 零拷贝传递。
