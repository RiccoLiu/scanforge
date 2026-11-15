# ScanForge 模块 ↔ 开源参考映射

> 这份文档回答的是：**软件的每一个部分，应该去看哪个开源仓库的哪些文件，学什么，以及能不能把代码拿过来用。**
>
> 阅读方法比读什么更重要：每个项目只看 `CMakeLists.txt` + 接口头文件 + 一个最小实现，**不要一头扎进渲染器或算法内核**，否则两周就陷进去了。

---

## 0. 先看这张总表

| # | ScanForge 模块（本仓库文件） | 参考开源项目 | 具体看哪些文件 / 目录 | 学到什么 | 代码可用性 |
|---|---|---|---|---|---|
| 1 | `CMakeLists.txt`、`cmake/`、`目录分层`、`.github/workflows/` | **neuvision** | 顶层 `CMakeLists.txt`、`cmake/`、`apps/ lib/ cmake/ tests/ 3rdparty/`、vcpkg 配置、CI 脚本 | 结构光扫描工程的真实骨架、库与应用分离、多平台构建 | 思路可复用；许可以其仓库 LICENSE 为准 |
| 2 | `core/include/scanforge/Types.hpp` | Open3D / PCL / PDAL | `cpp/open3d/geometry/PointCloud.h`、`common/include/pcl/PCLPointCloud2.h`、`pdal/PointView.hpp`、`pdal/Dimension.hpp` | 点云值类型设计、字段元数据、**单位与坐标系必须随数据走** | MIT / BSD-3 / BSD，可安全复用思路 |
| 3 | `core/include/scanforge/Result.hpp` | LLVM / C++23 / tl::expected | `llvm/include/llvm/Support/Error.h`、`std::expected` 提案、 `tl/expected.hpp` | 显式错误通道、`andThen` 链式传播、**跨 queued 连接不抛异常** | Apache-2.0 / 标准 / CC0 |
| 4 | `core/include/scanforge/Contracts.hpp`、`ReconstructionChain` | PDAL / 3DUNDERWORLD-SLS | `pdal/Stage.hpp`、`pdal/Filter.hpp`、`pdal/PipelineReader.cpp`；`ImageProcessor → Buckets → Reconstructor` | 可串联的处理阶段、中间数据格式、库与应用分离 | BSD / LGPL-3.0 |
| 5 | `core/Calibration.hpp`、`src/Calibration.cpp` | OpenCV / Kalibr | `modules/calib3d/src/calibration.cpp`、`calib3d/src/fisheye.cpp`；Kalibr 的 `calibrate_cameras` 与 PDF 报告格式 | 针孔模型、畸变、射线-平面求交、重投影误差报告长什么样 | OpenCV 4.5+ Apache-2.0；Kalibr BSD 系 |
| 6 | `core/Synthetic.hpp`、`src/Synthetic.cpp` | Kalibr / EuRoC MAV / 3DUNDERWORLD-SLS | Kalibr 的仿真标定数据集；EuRoC MAV（11 组双目 20Hz + IMU 200Hz、自带真值）；`example_data/` | **已知真值 + 缺陷注入**：噪声、丢帧、时间偏移、标定偏差 | 数据集按其官方条款使用；代码为原创 |
| 7 | `core/PlyIO.hpp`、`src/PlyIO.cpp` | PCL / PDAL / Open3D | `io/src/ply_io.cpp`、`io/PlyReader.cpp`、`cpp/open3d/io/PointCloudIO.cpp` | PLY header 解析、属性索引、把单位/坐标系写进 comment | BSD-3 / BSD / MIT |
| 8 | `core/Filters.hpp`、`src/Filters.cpp` | PCL / Open3D / CloudCompare | `filters/src/voxel_grid.cpp`、`cpp/open3d/geometry/PointCloud.cpp`（VoxelDownSample）、CloudCompare 标量场与距离计算 | 体素降采样、最小二乘平面、误差统计与归因文本 | BSD-3 / MIT；**CloudCompare 是 GPL，只读思路** |
| 9 | `core/FrameQueue.hpp` | Qt 官方 / folly | Qt 文档 `QThread` + queued connection 生产者消费者示例；`folly/concurrency/MPMCQueue.h`（阶段 4 再看） | 背压、close/drain、为什么先写 mutex 版再谈无锁 | Qt 文档 / Apache-2.0 |
| 10 | `core/DeviceStateMachine.hpp` | Qt 官方 / CloudCompare | `QStateMachine` 官方示例；`mainwindow.cpp` 里按钮 enable/disable 逻辑 | 六态迁移表作为 UI 与运行时的**唯一真源** | Qt 文档；GPL 只读概念 |
| 11 | `plugins/include/scanforge/IDevice.hpp`、`DeviceManager.*` | **CloudCompare**（重点）/ Aravis / neuvision | `ccPluginInterface.h`、`ccMainAppInterface.h`、`ccPluginManager.cpp`、`FileIOFilter`；`src/arvgc*.c`（GenICam 节点树）；neuvision 相机插件 | 插件契约、`Q_DECLARE_INTERFACE` + `Q_PLUGIN_METADATA` + `qobject_cast`、加载器生命周期、参数节点树 | **CloudCompare / neuvision 只读架构**；Aravis LGPL-2.1+ 可参考 |
| 12 | `plugins/mock_camera/` | Aravis | `src/arv-fake-camera.c`、`tools/arv-viewer`、`src/arv-camera.c` | 无硬件也能验证「枚举 → 打开 → 取流 → 参数 → 关闭」全链路 | LGPL-2.1-or-later |
| 13 | `pipeline/AcquisitionWorker.*`、`ProcessingWorker.*`、`PipelineController.*` | Qt 官方 / PDAL / neuvision | Qt「QThread / Worker Object」文档与示例；`pdal/Stage::run()`；neuvision `apps/` 连线 | `moveToThread`、函数指针 connect、取消令牌、优雅退出、跨线程零拷贝 | Qt 文档 / BSD / 只读架构 |
| 14 | `app/MainWindow.*`、`app/Docks.*` | CloudCompare / SLMaster / Qt 官方 | `mainwindow.cpp`、`ccPluginManager` UI；SLMaster 的「标定 → 重建 → 后处理」产品形态；`QMainWindow`/`QDockWidget` 文档 | IDE 式多 Dock 布局、`objectName` 与布局持久化、参数面板由节点树生成、状态机驱动按钮 | GPL 只读；SLMaster 许可需自行核实 |
| 15 | `app/PointCloudView.*`、`app/src/shaders/` | CloudCompare / Potree / Open3D / Qt 官方 | `ccGLWindowInterface`、`qCC_glWindow`；Potree 八叉树多分辨率 LOD；`cpp/open3d/visualization/`；`QOpenGLWidget` 文档 | 点云渲染、点精灵、VAO/VBO 复用、GPU 缓冲、LOD 思路、HUD | GPL 只读 / BSD / MIT / 官方文档 |
| 16 | `cli/src/main.cpp`、`docs/` 里的报告 | PDAL / Open3D | `apps/pdal.cpp` 的 `pdal pipeline`、Open3D `examples/` | 用命令行驱动同一条核心管线，逼着 core 真正与 UI 解耦 | BSD / MIT |
| 17 | `tests/` | Catch2 / Qt 官方 | Catch2 v3 `examples/`；`QtTest`、`QSignalSpy` 文档 | 零依赖测试兜底、后续信号与状态机的 QtTest 验证 | BSL-1.0 / Qt 文档 |

---

## 1. 按「层次」细看：每一层到底借鉴了什么

### 1.1 工程骨架（第 1 行）

**neuvision** 是最贴近 ScanForge 的参考：它同样是 C++/Qt/CMake 的结构光扫描框架，`apps/ lib/ cmake/ tests/ 3rdparty/` 的分层、插件式相机（OpenCV / Basler / AVT）、标定向导、vcpkg 依赖管理与多平台 CI，几乎就是本仓库 `core / plugins / pipeline / app` 划分的现实版本。

看它的顺序：顶层 `CMakeLists.txt` → `cmake/` 下的 Find/Config 脚本 → 一个最小相机插件 → `apps/` 里怎么把插件装进主窗口。**不要看它的重建算法**。注意它基于 Qt 5.15、OpenCV 4.2+/PCL 1.10+，部分工业相机插件年久未测，所以只学结构，不要照抄依赖版本；许可以其仓库 LICENSE 为准。

Qt6 自身的构建系统变化（qmake 仅兼容、官方推荐 CMake + Ninja）以 Qt 官方文档为准，本仓库用 `CMAKE_AUTOMOC/AUTORCC`，且只在找到 Qt6 时才开启。

### 1.2 纯 C++ core（第 2–10 行）

这一层刻意**一个 Qt 头文件都没有**。参考来自三个方向：

- **数据模型**：Open3D 的 `PointCloud`（MIT）与 PCL 的点类型（BSD-3）告诉你点里应该有什么；PDAL 的 `PointView`（BSD）告诉你字段元数据怎么组织。`Types.hpp` 里 `PointCloud::unit` / `frameId` 是借鉴 PDAL 与工业软件教训加的——CloudCompare 用户最常踩的坑就是 STL 用 mm、扫描仪导出用 m，`PlyIO.cpp` 因此把 `unit=` 与 `frame_id=` 写进 PLY 注释并在读取时恢复。
- **错误处理**：LLVM `Error.h`（Apache-2.0）与 C++23 `std::expected` 给出 `Result<T,E>` 的形态。规则是**跨线程槽里绝不抛异常**，一律返回 `Result`/`Status`。
- **管线形态**：PDAL 的 `Stage` 链与 3DUNDERWORLD-SLS（LGPL-3.0）的 `ImageProcessor → Buckets → Reconstructor` 分层。后者尤其值得看：它**采集层不实现、只留接口**，CPU/CUDA 双实现由 CMake 自动检测——正好印证「硬件层留抽象接口」的决定。
- **几何**：OpenCV `calib3d`（4.5.0 起 Apache-2.0）与 Kalibr 报告格式。`Calibration.cpp` 现在只做「射线 ∩ 光平面」和重投影 RMS，畸变校正是明确的 TODO。
- **合成真值**：这是最被低估的一步。Kalibr 的仿真数据、EuRoC MAV 自带内外参与 1mm 级真值、3DUNDERWORLD-SLS 的 `example_data/`，共同给出「真值已知 + 故意注入偏差」的方法。真实设备上做不到这件事。

`FrameQueue.hpp` 现在刻意是 **mutex 版**：能讲清背压、close/drain、虚假唤醒，比一上来写无锁环形缓冲值钱得多。folly 的 `MPMCQueue` 留到阶段 4 有 benchmark 之后再看。

### 1.3 插件层（第 11–12 行）

**CloudCompare 是插件架构的教科书，但许可证是 GPL，所以只能读架构、不能抄代码。** 只读三个文件即可：

- `ccPluginInterface.h` — 插件契约怎么写；
- `ccMainAppInterface.h` — 插件如何回调主程序；
- `ccPluginManager.cpp` — 加载器如何发现、实例化、保持生命周期。

（另可看 `FileIOFilter` 作为「文件格式也是插件」的范例，`ccHObject` 作为对象树。）`DeviceManager.cpp` 里「**必须把 QPluginLoader 实例活到设备指针之后**」这条，正是这一类项目里最经典的崩溃来源。许可证有个细节：CloudCompare 的 README 写 GPLv3、`license.txt` 写 GPL-2.0-or-later，**以你实际 checkout 的 tag 里的 license.txt 为准**。

参数模型不参考 CloudCompare，而参考 **Aravis**（LGPL-2.1-or-later）：`src/arvgc*.c` 把曝光、增益、触发、像素格式统一成 GenICam 节点树。所以 `IDevice::parameters()` 返回「键值 + 类型 + 范围 + 单位」，而不是写死 `setExposure()`。`mock_camera` 的无硬件思路也来自 Aravis 的 `arv-fake-camera` + `arv-viewer`。

> Aravis 是 C/glib + Meson，Windows 编译成本偏高，且不含 Camera Link；这里只借接口思想，不引入它作为依赖。

### 1.4 流水线与界面（第 13–15 行）

线程模型以 **Qt 官方「QThread / Worker Object」文档**为准，不要从 GPL 项目里抄线程代码。三条硬规则都在 `pipeline/` 里有对应实现：

1. `moveToThread` 的 QObject **不能有 parent**；
2. 长循环里取消靠 `CancellationToken`（atomic），**不能靠 queued 的 stop 槽**（循环不返回事件循环，槽永远收不到）；
3. `Frame` / `PointCloudPtr` 跨线程用 `Qt::QueuedConnection`，并在 `main.cpp` 里 `qRegisterMetaType`；点云用 `shared_ptr<const>` 零拷贝。

界面布局借鉴 **CloudCompare `mainwindow.cpp`**（IDE 式多 Dock）与 **SLMaster**（产品形态最像扫描仪上位机：标定 → 条纹编码 → 3D 重建 → 后处理；许可证需自行核实，QML 部分不必照搬）。Qt 文档里的两条硬约束已写进代码：`QMainWindow` 不能直接 `setLayout()`；每个 Dock 必须有唯一 `objectName` 才能 `saveState/restoreState`。

**OpenGL 只占 15–20%**：`PointCloudView` 参考 Qt `QOpenGLWidget` 文档 + CloudCompare 的 `ccGLWindowInterface`（只读思路）+ Potree 的八叉树 LOD（BSD）+ Open3D 可视化层（MIT）。刻意不手写渲染管线、不做 PBR/高级 shader——那些样板代码恰好是 AI 生成最熟的区域，也偏离你的感知链路优势。

### 1.5 时序与硬件触发（补充参考）

`SLStudio`（GPLv3）虽然不能复用代码，但它的 `trigger_circuit/`、标定板 PDF 与 `example_data/` 是理解「**没有硬触发时，帧相对时刻只是猜测**」最好的材料。注意它 2021-08 起改 GPLv3，且明确**内部分发也需公开**；qmake/Qt5、无 release。阶段 5 做时间同步误差归因时再去读。

---

## 2. 建议阅读顺序

1. **neuvision** —— 先建立「一个结构光上位机长什么样」的整体印象（只看 CMake 分层 + 一个插件）。
2. **CloudCompare 的三个接口文件** —— 插件契约与加载器（只读，别打开渲染器）。
3. **3DUNDERWORLD-SLS** —— 库/应用分离、中间格式、采集层留接口。
4. **Aravis** —— GenICam 节点树与相机模拟器。
5. **PDAL / Open3D** —— 管线式数据流、配准与滤波算法（阶段 5 再深入）。
6. **SLStudio** —— 硬件触发与时序（阶段 5 再读）。
7. **Potree** —— 点云卡顿时的八叉树 LOD（按需查，不必通读）。

---

## 3. 许可证分界线（务必记住）

| 类别 | 项目 | 可以做什么 |
|---|---|---|
| **GPL（只读架构）** | CloudCompare、SLStudio | 看设计、看接口、学组织方式。**不复制任何代码**；若要商用/闭源，连「照着改写」都要避免 |
| **LGPL（可链接，注意义务）** | Aravis（2.1+）、3DUNDERWORLD-SLS（3.0）、Qt6 | **动态链接**、保持库可替换、署名；修改库自身文件通常需同源公开；Qt 静态链接会触发 relink 义务 |
| **宽松许可（可复用）** | Open3D（MIT）、PCL/PDAL/Potree（BSD）、OpenCV 4.5+（Apache-2.0）、Catch2（BSL-1.0）、LLVM Error.h（Apache-2.0） | 可复用，保留版权声明即可 |
| **自有代码** | 本仓库全部源码 | MIT；第三方依赖**不 vendored**，由构建系统按需引入 |

> 本仓库目前**不引入任何第三方库源码**，所有实现均为原创；上表是「下一步要接 PCL / Open3D / Aravis 时」的决策依据。

---

## 4. 刻意不参考 / 不照搬的东西

- **不复刻 CloudCompare**：它是通用点云/网格处理平台，ScanForge 是设备链路；功能面越宽，主线越薄。
- **不照搬 SLStudio 的 qmake/Qt5 构建**，也不引入它的 GPL 代码。
- **不引入 PCL/VTK 全家桶**：Windows 上版本冲突最容易把人劝退，阶段 1 先用手写版本把原理打扎实。
- **不一上来 QML**：工业桌面上位机的主流仍是 Widgets；SLMaster 的 QML 部分不是本项目的主线。
- **不做网格重建 / CAD 比对 / 几十种格式 / 脚本引擎 / 云端 / AI 缺陷检测**：那是 MeshLab / CloudCompare 的战场。
- **不看渲染器内核**：`ccGLWindowInterface` 之外的渲染代码，投入产出比极低。

---

## 5. 把参考变成作品，而不是变成阅读量

每读完一个项目，只做一件事并在本仓库落地：

| 读完 | 在 ScanForge 里落地的动作 |
|---|---|
| neuvision | 补一个 `FileSource` 插件（复现 `apps/ lib/` 的分层） |
| CloudCompare 插件三件套 | 给 `IDevice` 增加 `QPluginLoader` 元数据校验与插件版本协商 |
| 3DUNDERWORLD-SLS | 把 `Frame` 与 `PointCloud` 之间加一层明确的中间格式 |
| Aravis | 给 `MockCamera` 增加枚举参数与只读节点，并写 `arv-viewer` 式的参数面板测试 |
| PDAL / Open3D | 把 `evaluateAgainstCloud` 的暴力最近邻换成 KD 树，保持指标定义不变 |
| SLStudio | 给 `Frame` 增加硬触发标志，补运动补偿，让 `--offset-ms` 真正影响 RMSE |
| Potree | 给 `PointCloudView` 加体素 LOD，验收 100 万点 30fps+ |

能讲清「为什么这样设计」的代码，才是面试里最值钱的部分。
