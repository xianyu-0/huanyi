# 幻衣 - AI 虚拟试穿系统

> 一个基于 **Qt + OpenCV + ComfyUI + CatVTON** 的桌面端 AI 虚拟试穿系统。

---

## ✨ 功能特性

| 功能 | 说明 |
|------|------|
| 🎨 **AI 生图** | 输入中文描述，自动生成服装图 |
| 👕 **虚拟试穿** | 基于 CatVTON 试穿 |

---

## 📸 效果截图

### 主界面

![主界面](docs/screenshot_main.png)

### AI 生图

![AI 生图](docs/screenshot_generate.png)

---

## 🛠 技术栈

| 模块 | 工具 |
|------|------|
| **界面** | Qt 6.8.3 |
| **图像处理** | OpenCV 4.11 |

---

## 开始

### 1. 克隆项目

```bash
git clone https://github.com/xianyu-0/huanyi.git
cd huanyi
```

### 2. 配置依赖

**OpenCV**：下载 OpenCV MinGW 版，解压到指定目录。

**ComfyUI**：安装秋叶整合包。

### 3. 编译运行

用 Qt Creator 打开 `CMakeLists.txt`，选择 `Desktop Qt 6.8.3 MinGW 64-bit`。

---

## 📁 项目结构

```text
huanyi/
├── CMakeLists.txt
├── main.cpp
├── mainwindow.cpp
└── README.md
```

---

## 🔧 核心代码

### AI 生图

```cpp
translateToEnglish(prompt);
startGenerate(translatedPrompt);
```

### 虚拟试穿

```cpp
forceCopy(bodyPath, comfyInput + "body.jpg");
manager->post(tryonRequest, json);
```

---

## 📌 开发环境

- **Qt Creator** 14.0+
- **编译器** MinGW 13.1.0
- **构建系统** CMake

---

## 📄 License

本项目仅供学习交流使用。

---

## 🙏 致谢

- [ComfyUI](https://github.com/comfyanonymous/ComfyUI)
- [CatVTON](https://github.com/Zheng-Chong/CatVTON)