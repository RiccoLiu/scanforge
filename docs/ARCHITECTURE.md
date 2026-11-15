# ScanForge 架构设计说明

> 面向「阶段 0–1 骨架」：说明依赖方向、线程边界、三个契约、状态机与编码纪律。
> 「每个模块参考哪个开源项目」见 [REFERENCE_MAP.md](REFERENCE_MAP.md)。

---

## 1. 设计目标与边界

ScanForge 是**一条可验证的设备链路**，不是一个点云查看器：

```
设备枚举 → 参数配置 → 采集 → 标定 → 三角化 → 滤波 → 显示 → 误差归因 → 报告/导出
```

判断某个功能该不该做，只看一个问题：**它是否让这条链路更完整、更可验证。**

刻意不做：网格重建、CAD 数模比对、几十种文件格式、内置脚本引擎、多语言、云端、AI 缺陷检测、QML 版本。

阶段 1 的另一条边界：**不引入任何第三方库**。PLY、体素滤波、平面拟合、三角化、测试框架全部手写（约 2k 行），目的是把原理打扎实，并让仓库在没有 Qt、没有网络、没有包管理器的机器上也能构建。

---

## 2. 编译期依赖方向（严格单向）

```
              ┌──────────────┐
              │    core/     │  纯 C++17，禁止 include 任何 Qt 头
              └──────┬───────┘
        ┌────────────┼─────────────┬──────────────┐
        ▼            ▼             ▼              ▼
   plugins/     pipeline/        cli/          tests/
   (Qt 契约)    (Qt 线程)     (无界面)      (单元测试)
        └────────────┼─────────────┘
                     ▼
                   app/      (QMainWindow + QOpenGLWidget)
```

- `core` 不依赖任何上层；`cli` 与 `tests` 只链接 `core` —— 这是解耦的**验收标准**而不是口号。
- `plugins` 与 `pipeline` 是 Qt 第一次出现的地方；`app` 只负责组装与展示。
- CMake 顶层在找不到 Qt6 时自动跳过 `plugins/pipeline/app`，`core + cli + tests` 照常构建。

---

## 3. 运行时三泳道

```
 GUI 线程               采集 QThread              处理 QThread
 ─────────             ──────────────            ──────────────
 MainWindow
  │ start()
  ▼
 PipelineController ──invokeMethod──▶ AcquisitionWorker
                                       │ nextFrame() 循环
                                       ├─ previewReady(QImage) ──────▶ 2D 预览
                                       └─ frameReady(Frame) ─────────▶ ProcessingWorker
                                                                        │ ReconstructionChain
                                                                        ▼
   PointCloudView ◀── cloudReady(PointCloudPtr, ms) ───────────────────┘
   (setCloud: 只换 pending_ 指针，update())
```

关键约束：

1. `moveToThread()` 的 QObject **不能有 parent**（`PipelineController` 里两个 worker 都是 `new` 出来的裸对象，析构里显式 `delete`）。
2. `PointCloudPtr = shared_ptr<const PointCloud>`，跨线程零拷贝且**发布后不可变**。
3. `Frame` / `PointCloudPtr` / `Error` / `DeviceState` 都经 `Qt::QueuedConnection` 传递，`main.cpp` 里 `qRegisterMetaType` 注册。
4. **任何 worker 线程都不得直接碰 GL context**（线程亲和）。`setCloud()` 只在 GUI 线程把指针放进 `pending_`，实际 GL 上传发生在 `paintGL()`。
5. **取消靠 `CancellationToken`（atomic）**，不靠 queued 的 stop 槽：`AcquisitionWorker::start()` 是长循环，不返回事件循环，queued 槽永远收不到。绝不 `QThread::terminate()`。
6. GUI 线程零阻塞：`stop()` 只翻转 token，线程退出由 `finished()` 信号触发 `QThread::quit()`；`wait()` 只在析构里做。

停止顺序：`cancel token → 循环自然退出 → finished() → quit() → 析构 wait()`。

---

## 4. 三个契约

| 契约 | 所在层 | 形态 | 为什么这样定 |
|---|---|---|---|
| `IDevice` | `plugins/`（Qt 插件） | `Q_DECLARE_INTERFACE` + `Q_PLUGIN_METADATA`；`parameters()` 返回 GenICam 风格「键值 + 类型 + 范围 + 单位」节点 | 换相机只换插件；不写死 `setExposure()`；加载器必须长命 |
| `IReconstructor` / `IPointCloudOperator` | `core/Contracts.hpp`（纯 C++） | `Result<T,E> process(...)`，可串成 `ReconstructionChain` | 同一条链能在 GUI、CLI、单测里跑；不抛异常跨线程 |
| `Frame` / `PointCloudPtr` | `core/Types.hpp`（纯 C++） | 帧带 `sequence` / `timestamp` / `timeBase`；点云带 `unit` / `frameId` | 排查「两次扫描对不上」必须能回溯时间戳基准与单位 |

`DeviceManager` 持有 `shared_ptr<QPluginLoader>` 与裸 `IDevice*`：卸载 DLL 会让指针悬空，这是 Qt 插件最常见的崩溃。

---

## 5. 六态状态机（UI 与运行时的唯一真源）

| 当前状态 | 允许的事件 | 迁移到 |
|---|---|---|
| Disconnected | Connect | Idle |
| Idle | StartAcquire / Fail / Disconnect | Acquiring / Error / Disconnected |
| Acquiring | EnterProcessing / StopAcquire / Fail / Disconnect | Processing / Idle / Error / Disconnected |
| Processing | FinishProcessing / StopAcquire / Fail | Idle / Idle / Error |
| Error | BeginRecover / Disconnect | Recovering / Disconnected |
| Recovering | RecoverDone / Fail / Disconnect | Idle / Error / Disconnected |

`canTransition()` 是 constexpr 自由函数；`MainWindow::updateActions()` 直接复用它，**不在每个控件里各写一套 enable/disable**。所以「采集中禁止改标定 / 禁止断开」是自动成立的。

---

## 6. 错误处理与取消

- 所有可能失败的调用返回 `Result<T, Error>` 或 `Status`；`Error` 带 `code / message / context`。
- **跨线程槽里绝不抛异常**，异常会穿过 queued connection 边界直接进入 Qt 事件循环。
- 单元测试里的 `mini_test.hpp` 用异常做断言，但那是测试进程内部，与生产路径无关。
- `CancellationToken` 用 `shared_ptr<atomic_bool>`，复制便宜、线程安全、可跨多个 worker 共享。

---

## 7. 单位与坐标系（工业软件最容易翻车的地方）

- `PointCloud::unit` 明确 `Millimeter` / `Meter`，`LengthUnit` 随数据走；
- `PointCloud::frameId` 记录坐标系（如 `camera` / `scanner`）；
- `Frame::timeBase` 记录时间基准（`steady_clock` / `ptp` / `device_ticks`）；
- `PlyIO` 把 `unit=` 与 `frame_id=` 写进 PLY 的 `comment` 行并在读取时恢复；
- 3D 视图 HUD **常显** unit / frame，避免 STL(mm) 与扫描仪导出(m) 混用。

---

## 8. 九条编码规约（已落实到骨架）

1. `core/` 禁止 include 任何 Qt 头；`QString` 不得出现在 core 的公开接口里。
2. 用**函数指针** `connect(&obj, &Class::signal, ...)`，禁用 `SIGNAL()/SLOT()` 字符串宏。
3. lambda 连接必须传 context 对象，避免对象销毁后回调悬空。
4. **QObject parent 树与 `std::unique_ptr` 二选一，绝不混用**（最常见的 Qt 崩溃源）；`moveToThread` 的对象干脆无 parent。
5. GUI 线程零阻塞；耗时操作放 worker，且必须可取消。
6. 用 `deleteLater()` 而非在槽里 `delete`；`ParameterDock` 重建表单即用此方式。
7. 退出按「停任务 → quit → wait」顺序，不 `terminate()`。
8. `paintGL()` 内不 allocate：VBO 一次申请、按 2 倍增长，后续 `write()`（`glBufferSubData`）。
9. 每个 `QDockWidget` / `QToolBar` 设唯一 `objectName`，否则 `saveState/restoreState` 静默失效。

补充两条工程约定：Windows/MSVC 必须 `/utf-8`；跨线程自定义类型必须 `Q_DECLARE_METATYPE` + `qRegisterMetaType`。

---

## 9. 如何扩展

**加一个真实相机插件**：新建 `plugins/<vendor>/`，实现 `QObject + IDevice`（照 `mock_camera`），`Q_PLUGIN_METADATA` 的 IID 保持 `org.scanforge.IDevice/1.0`，参数用节点树暴露；`DeviceManager::scan()` 会自动发现，UI 无需改动。参考 Aravis 的 GenICam 命名。

**加一个点云算子**：实现 `IPointCloudOperator::apply()`，`ReconstructionChain::addOperator()` 即可；务必写一条单元测试验证「点数/单位/坐标系」契约不丢。

**接 PCL / Open3D**：只替换 `Filters.cpp` 的实现，**不要改 `ErrorReport` 的指标定义**，否则前后数据不可比。建议先用 Open3D（MIT）的 KD 树替换暴力最近邻。

**接真实设备数据源**：实现 `IDataSource`（纯 C++）或 `IDevice`（Qt 插件），硬件层保留抽象接口，`cli` 与 `tests` 继续用 `SyntheticSource` 跑回归。

---

## 10. 已知限制（诚实清单）

| 限制 | 影响 | 计划 |
|---|---|---|
| 峰值检测取整像素、无畸变校正 | 精度受量化限制 | 阶段 5：亚像素高斯中心 + `undistortPoints` |
| 单帧重建不使用时间戳 | `--offset-ms` 只写元数据，不改变 RMSE | 阶段 5：运动补偿后才真正练时间同步归因 |
| Qt 信号队列**无界** | 高速采集可能堆积 | `FrameQueue.hpp` 已实现有界队列，阶段 4 接入形成背压 |
| 退出未确认处理队列排空 | 最后几帧可能丢失 | `PipelineController.cpp` 内已标 TODO |
| 暴力最近邻 O(N·M) | 点数上十万会变慢 | 换 KD 树，指标定义不变 |
| macOS 未适配 | Qt6 在 macOS 走 Metal | 阶段 5 用 Qt RHI 或 VTK 兜底 |
| GUI 阶段 1 用默认标定 | 插件注入的 `PlaneBiasMm` 不会反映在 GUI 重建里 | 阶段 5 从标定文件 / 插件节点加载 `CalibrationSet` |

---

## 11. 验收清单（怎么算「这一阶段做完了」）

- [x] `cmake -DSCANFORGE_BUILD_APP=OFF` 在无 Qt 机器上配置、构建、测试全绿（本机 14/14 通过）。
- [x] `scanforge-cli` 能生成 PLY 与 Markdown 误差报告，注入 2mm 标定偏置后 `plane_rmse` 从 `1.07e-5` 变为 `1.997`。
- [ ] 装 Qt 6.8 LTS 后完整 GUI 构建通过、三个 Dock 布局可保存恢复。
- [ ] 点 Connect 能加载 `mock_camera` 插件并自动生成参数面板。
- [ ] Start → 2D 预览与 3D 点云同时刷新；Stop 后线程干净退出、无崩溃。
- [ ] 100 万点 30fps+（阶段 4 补 LOD 后验收）。
