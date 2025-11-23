# ESP32 OTA 固件管理服务（宝塔部署说明）

本目录提供一个独立的 OTA 固件管理小网站，配合 `xn_ota_manger` 工程中的 HTTP OTA 功能使用。

- 网站语言：PHP（无需数据库）
- 主要功能：上传固件、查看固件列表、删除固件、编辑版本配置（生成 `version.json`）
- 返回的 `version.json` 与 `http_ota_module.h` 中的说明完全兼容：

```json
{"version":"1.0.1","url":"http://xxx/firmware.bin","description":"修复bug","force":false}
```

---

## 1. 目录结构

将 `ota_server` 目录整体部署到网站根目录后，结构如下：

- `index.php`
  - OTA 固件管理网页（前端界面 + 与 API 交互）。
- `FirmwareAPI.php`
  - 后端 API：
    - `action=save_config`：保存 OTA 配置到 `firmware/version.json`；
    - `action=upload`：上传 `.bin` 固件到 `firmware/` 目录；
    - `action=delete`：从 `firmware/` 删除指定固件文件。
- `firmware/`
  - 固件文件和 `version.json` 存放目录（首次访问时由程序自动创建）。

> 说明：你只需要把整个 `ota_server` 目录上传到服务器，不需要自行创建 `firmware` 目录。

---

## 2. 在宝塔中新建站点

1. 登录宝塔面板。
2. 进入 **网站 → 添加站点**：
   - **域名**：填写你的域名或留空仅用 IP+端口；
   - **根目录**：例如 `/www/wwwroot/esp32_ota`；
   - **数据库**：可选择“纯静态”，本项目不需要数据库；
   - **运行环境**：选择带 PHP 的环境（如 `Nginx + PHP 7.4+`）。
3. 创建完成后，进入该站点的 **设置**：
   - **网站目录 → 运行目录**：保持为网站根目录，入口文件会自动使用 `index.php`；
   - **PHP 版本**：建议 7.4 及以上；
   - **伪静态 / 安全**：本项目不依赖伪静态规则，按默认即可。

---

## 3. 部署 ota_server 文件

1. 在宝塔中进入该站点的 **网站目录**（例如 `/www/wwwroot/esp32_ota`）。
2. 将本工程中的 `ota_server` 目录里的所有文件上传到网站根目录下：
   - `index.php`
   - `FirmwareAPI.php`
   - （`firmware` 目录可不必提前上传，运行时会自动创建）。
3. 部署完成后，你的网站根目录大致如下：

```text
/www/wwwroot/esp32_ota/
├─ index.php
├─ FirmwareAPI.php
└─ firmware/        # 首次使用时自动创建
```

4. 在浏览器访问：

```text
http://你的域名/
```

即可看到 OTA 固件管理页面。

---

## 4. 配置上传大小与权限

1. 在宝塔站点设置中，进入 **PHP 设置 / 上传限制**：
   - 将单文件上传大小设置为 ≥ `20MB`（根据你的固件大小适当调大）。
2. 确保网站根目录及其子目录具有写权限（Linux 下一般为 `755`，由宝塔默认创建即可）。
3. 若使用 Nginx：
   - 进入 **站点设置 → Nginx → 配置文件**，确认未对 `/firmware/` 做额外限制；
   - 一般默认配置即可正常访问 `http://域名/firmware/xxx.bin`。

---

## 5. 使用 OTA 管理页面

1. 在浏览器打开：

```text
http://你的域名/
```

2. 页面上方“当前 OTA 配置”区域会显示：
   - `配置文件路径`：固定为 `/firmware/version.json`；
   - `设备访问地址`：例如 `http://your-domain.com/firmware/version.json`。

3. 使用步骤：

- **步骤 1：上传固件**
  - 在“上传固件”模块中点击或拖拽 `.bin` 文件；
  - 上传完成后，下方“已上传的固件”列表会出现新条目；
  - 每个固件条目右侧有“复制URL”按钮，可复制下载地址，例如：
    - `http://your-domain.com/firmware/app_v1.0.1.bin`。

- **步骤 2：配置版本信息**
  - 在“当前 OTA 配置”中填写：
    - **固件版本号**：如 `1.0.1`；
    - **固件下载URL**：粘贴上一步复制的固件下载地址；
    - **更新说明**：填入本次版本的更新内容；
    - **强制更新**：勾选后，设备侧可根据 `force` 字段做强制升级策略。
  - 点击“保存配置”，会在 `firmware/` 目录下生成或更新 `version.json`。

- **步骤 3：设备侧使用 version_url**
  - 设备端 `xn_ota_manger` 工程中，在 `main/main.c` 里有一行类似：

    ```c
    snprintf(cfg.version_url,
             sizeof(cfg.version_url),
             "http://win.xingnian.vip:16623/firmware/version.json");
    ```

  - 将其中的 URL 修改为你在本网站上看到的“设备访问地址”，例如：

    ```c
    "http://your-domain.com/firmware/version.json"
    ```

  - 重新烧录固件后，设备在联网并初始化 OTA 管理模块时，就会从该地址获取版本信息并决定是否升级。

---

## 6. 安全性建议

默认情况下，本 OTA 管理页面未做登录鉴权，建议配合宝塔站点安全策略使用：

1. 在宝塔站点设置中启用：
   - **访问限制 / 密码访问**（可设置简单的 HTTP Basic Auth 保护本页面）；
   - 或将站点绑定到仅内网可访问的地址。
2. 若你需要更完善的用户系统，可在此基础上自行增加登录页，或接入现有的后台管理系统。

---

## 7. 常见问题

- **Q: 上传固件时报“文件太大”？**
  - A: 请在宝塔中调大 PHP 上传限制和 Nginx 的 `client_max_body_size`，并确保不小于固件文件大小。

- **Q: 设备访问 `version_url` 报 404？**
  - A: 检查：
    - 浏览器是否能直接访问 `http://your-domain.com/firmware/version.json`；
    - `firmware` 目录下是否已经生成 `version.json`；
    - 站点根目录是否正确指向包含 `index.php` 与 `FirmwareAPI.php` 的目录。

- **Q: 想把固件放到其他路径？**
  - A: 可以修改 `FirmwareAPI.php` 与 `index.php` 中关于 `firmware` 路径的部分，并相应调整设备侧的下载 URL 与 version_url。
