# MAX35 AURORA Factory4G NoCamera - Web Upload v1.4.0

这是给 **SpotPear / 斑梨 MAX35 4G + ML307** 做的“GitHub 网页直接上传版”。

## 这版为什么文件少

仓库根目录不再展开一千多个原厂源码文件。斑梨原厂源码（删除了 `managed_components`、文档和 releases 等可恢复内容）被放进一个压缩包：

`factory-base/xiaozhi-esp32-factory-source-minimal.tar.gz`

GitHub Actions 编译时会自动：

1. 解开原厂源码；
2. 通过 ESP-IDF Component Manager 下载原厂 `dependencies.lock` 锁定的组件；
3. 只应用 AURORA UI、固定 OTA、跳过摄像头启动这三个改动；
4. 使用斑梨原厂 `sp-esp32-s3-lcd-3.5-cam-ml307` 目标和 ESP-IDF 5.5 编译；
5. 输出可从 `0x0` 烧录的 merged BIN。

解压本项目后，实际需要上传到 GitHub 的文件总共只有 11 个，适合网页上传；最大的单个文件也不到 10 MB。

## 固定后台

固件固定使用：

`http://124.221.112.55:8002/xiaozhi/ota/`

`Ota::GetCheckVersionUrl()` 直接返回编译地址，不读取旧 NVS 的 `wifi/ota_url`，因此 4G 版本无需配网页面选择后台。

## 摄像头

当前版本为了先验证 LCD、UI、ML307、语音和后台，**不初始化摄像头**：

- 不调用 `InitializeCamera()`；
- 不安装厂家“摄像头识别失败自动重启”的日志钩子；
- `GetCamera()` 返回 `nullptr`。

这不会改动 ML307、音频、触摸、LCD 初始化、电源管理和协议逻辑。

## AURORA UI

- 待机显示 AURORA 首页；
- 进入 Listening / Speaking 自动切到独立 DIALOG 页面；
- 用户识别文本显示在上方；
- 小智多段 TTS 文本累计显示；
- 长回复自动换行；
- 超过可视区域后先停留约 1.6 秒，再以约 50 px/s 缓慢向下阅读；
- 回复还没滚完时不会提前回首页；
- 回到 Idle 且文字读完后再返回 AURORA 首页。

## GitHub 网页上传

1. 解压这个 ZIP。
2. 新建一个空 GitHub 仓库。
3. 点 **Add file -> Upload files**。
4. 把解压后的全部内容上传到仓库根目录，**`.github` 文件夹也要上传**。
5. 提交后进入 **Actions**。
6. 选择 **Build MAX35 AURORA 4G - Web Upload**。
7. 点 **Run workflow**。

本工作流只有手动触发，不会因为上传文件自动多跑一次。

## 编译成功后刷哪个

下载 Artifact：

`MAX35-AURORA-Factory4G-NoCamera-v1.4.0`

刷：

`MAX35_AURORA_FACTORY4G_NOCAM_v1.4.0_merged-binary.bin`

烧录地址：

`0x0`

第一次测试建议先整片擦除 Flash，再烧 merged BIN。

Artifact 里还会包含原厂恢复固件：

`FACTORY_RECOVERY_ESP32S3-MAX35-4G-LCD-BCamera-Case.bin`

## 重要说明

为了把仓库压缩到网页可上传的规模，`managed_components` 没有打进 `factory-base`。Actions 会按原厂 `main/idf_component.yml` + `dependencies.lock` 从 Espressif Component Registry 下载对应版本。这是唯一和“把 600MB 原厂目录全部提交进 GitHub”不同的地方。
