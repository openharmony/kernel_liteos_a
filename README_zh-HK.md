# LiteOS-A內核<a name="ZH-CN_TOPIC_0000001096612501"></a>

- [簡介](#section11660541593)
- [目錄](#section161941989596)
- [約束](#section119744591305)
- [使用說明](#section741617511812)
- [準備](#section1579912573329)
- [獲取源碼](#section11443189655)
- [編譯構建](#section2081013992812)
- [貢獻](#section1371123476304)
- [相關倉](#section1371113476307)

## 簡介<a name="section11660541593"></a>

OpenHarmony LiteOS-A內核是基於Huawei LiteOS內核演進發展的新一代內核，Huawei LiteOS是面向IoT領域構建的輕量級物聯網操作系統。在IoT產業高速發展的潮流中，OpenHarmony LiteOS-A內核能夠帶給用戶小體積、低功耗、高性能的體驗以及統一開放的生態系統能力，新增了豐富的內核機制、更加全面的POSIX標準接口以及統一驅動框架**HDF**（OpenHarmony Driver Foundation）等，為設備廠商提供了更統一的接入方式，為OpenHarmony的應用開發者提供了更友好的開發體驗。圖1為OpenHarmony LiteOS-A內核架構圖：

**圖 1** OpenHarmony LiteOS-A內核架構圖<a name="fig27311582210"></a>

![](figures/OpenHarmony-LiteOS-A内核架构图.png "OpenHarmony-LiteOS-A內核架構圖")

## 目錄<a name="section161941989596"></a>

```
/kernel/liteos_a
├── apps # 用戶態的init和shell應用程序
├── arch # 體系架構的目錄，如arm等
│ └── arm # arm架構代碼
├── bsd # freebsd相關的驅動和適配層模塊代碼引入，例如USB等
├── compat # 內核接口兼容性目錄
│ └── posix # posix相關接口
├── drivers # 內核驅動
│ └── char # 字符設備
│ ├── mem # 訪問物理IO設備驅動
│ ├── quickstart # 系統快速啟動接口目錄
│ ├── random # 隨機數設備驅動
│ └── video # framebuffer驅動框架
├── fs # 文件系統模塊，主要來源於NuttX開源項目
│ ├── fat # fat文件系統
│ ├── jffs2 # jffs2文件系統
│ ├── include # 對外暴露頭文件存放目錄
│ ├── nfs # nfs文件系統
│ ├── proc # proc文件系統
│ ├── ramfs # ramfs文件系統
│ └── vfs # vfs層
├── kernel # 進程、內存、IPC等模塊
│ ├── base # 基礎內核，包括調度、內存等模塊
│ ├── common # 內核通用組件
│ ├── extended # 擴展內核，包括動態加載、vdso、liteipc等模塊
│ ├── include # 對外暴露頭文件存放目錄
│ └── user # 加載init進程
├── lib # 內核的lib庫
├── net # 網絡模塊，主要來源於lwip開源項目
├── platform # 支持不同的芯片平台代碼，如Hi3516DV300等
│ ├── hw # 時鐘與中斷相關邏輯代碼
│ ├── include # 對外暴露頭文件存放目錄
│ └── uart # 串口相關邏輯代碼
├── security # 安全特性相關的代碼，包括進程權限管理和虛擬id映射管理
├── syscall # 系統調用
└── tools # 構建工具及相關配置和代碼
```

## 約束<a name="section119744591305"></a>

- 開發語言：C/C++；
- 適用於Hi3516DV300單板；
- Hi3516DV300默認使用FAT文件系統。

## 使用說明<a name="section741617511812"></a>

OpenHarmony LiteOS-A內核支持[Hi3516DV300](https://gitee.com/openharmony/docs/blob/master/zh-cn/device-dev/quick-start/quickstart-appendix-hi3516.md)單板，開發者可基於此單板開發運行自己的應用程序。

### 準備<a name="section1579912573329"></a>

開發者需要在Ubuntu上搭建編譯環境：

- [編譯環境凖備](https://gitee.com/openharmony/docs/blob/master/zh-cn/device-dev/quick-start/Readme-CN.md);

### 獲取源碼<a name="section11443189655"></a>

在Ubuntu服務器上下載並解壓一套源代碼，源碼獲取方式參考[源碼獲取](https://gitee.com/openharmony/docs/blob/master/zh-cn/device-dev/get-code/sourcecode-acquire.md)。

### 編譯構建<a name="section2081013992812"></a>

開發者開發第一個應用程序可參考：[helloworld for Hi3516DV300](https://gitee.com/openharmony/docs/blob/master/zh-cn/device-dev/quick-start/quickstart-pkg-3516-helloworld.md)。

編譯可以參考：[編譯指導](https://gitee.com/openharmony/docs/blob/master/zh-cn/device-dev/quick-start/quickstart-pkg-3516-build.md)

## 貢獻<a name="section1371123476304"></a>

[如何貢獻](https://gitee.com/openharmony/docs/blob/HEAD/zh-cn/contribute/%E5%8F%82%E4%B8%8E%E8%B4%A1%E7%8C%AE.md)

[Commit message規範](https://gitee.com/openharmony/kernel_liteos_a/wikis/Commit%20message%20%E8%A7%84%E8%8C%83)

[OpenHarmony C&C++ 安全編程指南](https://gitee.com/openharmony/docs/blob/master/zh-cn/contribute/OpenHarmony-c-cpp-secure-coding-guide.md)

[LiteOS-A 內核編碼規範指南](https://gitee.com/openharmony/kernel_liteos_a/wikis/LiteOS-A%20%E5%86%85%E6%A0%B8%E7%BC%96%E7%A0%81%E8%A7%84%E8%8C%83%E6%8C%87%E5%8D%97)

## 相關倉<a name="section1371113476307"></a>

[內核子系統](https://gitee.com/openharmony/docs/blob/master/zh-cn/readme/%E5%86%85%E6%A0%B8%E5%AD%90%E7%B3%BB%E7%BB%9F.md)

[drivers\_liteos](https://gitee.com/openharmony/drivers_liteos/blob/master/README_zh.md)

[kernel\_liteos\_a](https://gitee.com/openharmony/kernel_liteos_a/blob/master/README_zh-HK.md) 
