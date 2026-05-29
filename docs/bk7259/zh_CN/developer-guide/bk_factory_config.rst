Factory Config 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

Factory Config 是一个工厂配置管理模块，提供了配置项的持久化存储和管理功能。该模块支持配置项的读写、同步到 Flash、恢复出厂设置等操作，并提供了 SRAM 缓存机制以提高读取性能。配置项存储在 Flash 中，支持掉电保存，同时提供 SRAM 缓存以加速频繁访问的配置项。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **持久化存储**: 配置项存储在 Flash 中，支持掉电保存
- **SRAM 缓存**: 支持配置项 SRAM 缓存，提高读取性能
- **自动同步**: 支持配置项自动同步到 Flash
- **恢复出厂设置**: 支持一键恢复出厂默认配置
- **灵活扩展**: 支持用户自定义配置项
- **线程安全**: 使用互斥锁保护配置操作

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Factory Config 模块架构如下：

::

    应用层 (Application)
            ↓
    Factory Config API (bk_factory_config.h)
            ↓
    SRAM 缓存 (Factory Cache)
            ↓
    Easy Flash (Flash 存储)
            ↓
    Flash 分区

工作流程
---------------------------------

初始化流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``bk_factory_init()`` 详细流程：

::

    1. 检查初始化标志
        - 读取 "sys_initialized" 配置项
        ↓
    2. 首次初始化
        - 如果未初始化，调用 bk_factory_reset()
        - 写入所有默认配置到 Flash
        ↓
    3. 已初始化
        - 如果已初始化，调用 bk_factory_cache_init()
        - 从 Flash 加载配置到 SRAM 缓存
        ↓
    4. 注册重启回调
        - bk_reboot_callback_register(bk_reboot_sync_config)
        - 确保重启前同步配置
        ↓
    5. 注册 CLI 命令
        - factory_read, factory_write, factory_sync, factory_reset
        ↓
    初始化完成

配置读取流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``bk_config_read()`` 详细流程：

::

    1. 查找缓存索引
        - find_cache_index(key)
        - 在 SRAM 缓存中查找配置项
        ↓
    2. 从缓存读取
        - memcpy(value, cache_ptr, value_len)
        ↓
    3. 返回读取长度
        - 返回 valid_len
        ↓
    读取完成

配置写入流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``bk_config_write()`` 详细流程：

::

    1. 查找缓存索引
        - find_cache_index(key)
        ↓
    2. 验证长度
        - 检查 value_len 是否小于等于配置项最大长度
        ↓
    3. 写入缓存
        - memcpy(cache_ptr, value, value_len)
        - 更新 valid_len
        ↓
    4. 返回结果
        - 0: 成功
        - -1: 失败
        ↓
    写入完成（仅写入缓存，需调用 sync 同步到 Flash）

配置同步流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``bk_config_sync_flash()`` 详细流程：

::

    1. 检查缓存有效性
        - 如果缓存未初始化，直接返回
        ↓
    2. 遍历所有配置项
        - 平台配置项
        - 用户注册配置项
        ↓
    3. 检查是否需要更新
        - 比较 Flash 值和缓存值
        - is_config_update()
        ↓
    4. 写入 Flash
        - 如果值已改变，调用 bk_set_env_enhance()
        - 写入到 Easy Flash
        ↓
    5. 同步完成
        ↓
    同步完成

恢复出厂设置流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``bk_factory_reset()`` 详细流程：

::

    1. 遍历所有配置项
        - 平台配置项
        - 用户注册配置项
        ↓
    2. 写入默认值
        - 使用配置项的默认值
        - bk_set_env_enhance()
        ↓
    3. 初始化缓存
        - bk_factory_cache_init()
        - 从 Flash 加载到缓存
        ↓
    恢复完成

重要接口
---------------------------------

初始化接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 初始化工厂配置模块
     * 
     * 检查是否首次初始化，如果是则恢复出厂设置，否则加载缓存
     */
    void bk_factory_init(void);

    /**
     * @brief 恢复出厂设置
     * 
     * 将所有配置项重置为默认值
     */
    void bk_factory_reset(void);

配置操作接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 读取配置项
     * 
     * @param key 配置项键名
     * @param value 值缓冲区指针
     * @param value_len 值缓冲区长度
     * @return int 
     *         - > 0: 实际读取的长度
     *         - 0: 未找到或失败
     */
    int bk_config_read(const char *key, void *value, int value_len);

    /**
     * @brief 写入配置项
     * 
     * @param key 配置项键名
     * @param value 值指针
     * @param value_len 值长度
     * @return int 
     *         - 0: 成功
     *         - -1: 失败
     * 
     * @note 写入仅更新缓存，需调用 bk_config_sync_flash() 同步到 Flash
     */
    int bk_config_write(const char *key, const void *value, int value_len);

同步接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 同步配置到 Flash
     * 
     * 将所有已修改的配置项从缓存同步到 Flash
     */
    void bk_config_sync_flash(void);

    /**
     * @brief 安全同步配置到 Flash
     * 
     * 线程安全版本的同步函数
     * 
     * @return bk_err_t 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    bk_err_t bk_config_sync_flash_safely(void);

用户配置注册接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 注册用户自定义配置项
     * 
     * @param config 配置项数组指针
     * @param config_len 配置项数量
     * 
     * @note 配置结构必须在 Flash 中定义（使用 const）
     */
    void bk_regist_factory_user_config(const struct factory_config_t *config, 
                                       uint16_t config_len);

配置结构定义
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    struct factory_config_t {
        char *key;              // 配置项键名
        void *value;            // 默认值指针
        uint8_t defval_size;    // 默认值大小
        uint8_t need_sram_cache; // 是否需要 SRAM 缓存
        uint16_t value_max_size; // 值最大大小
    };

主要宏定义
---------------------------------

配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 启用 Factory Config 模块
    CONFIG_BK_FACTORY_CONFIG=y

    // 依赖配置
    CONFIG_EASY_FLASH=y         // Easy Flash 支持

平台配置项
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

默认平台配置项：

.. code:: c

    // 系统初始化标志
    {"sys_initialized", "1", 1, BK_FALSE, 1}

    // 音量配置（SRAM 缓存）
    {"volume", &s_factory_volume, 4, BK_TRUE, 4}

    // Agent 信息（SRAM 缓存）
    {"d_agent_info", "\0", 1, BK_TRUE, 588}

使用示例
---------------------------------

基本使用
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #include "bk_factory_config.h"

    void example_usage(void)
    {
        // 初始化
        bk_factory_init();
        
        // 读取配置
        uint32_t volume = 0;
        int ret = bk_config_read("volume", &volume, sizeof(volume));
        if (ret > 0) {
            // 使用配置值
        }
        
        // 写入配置
        volume = 10;
        bk_config_write("volume", &volume, sizeof(volume));
        
        // 同步到 Flash
        bk_config_sync_flash();
    }

注册用户配置
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 定义用户配置（必须在 Flash 中）
    static const uint32_t s_default_timeout = 5000;
    static const struct factory_config_t s_user_config[] = {
        {
            .key = "timeout",
            .value = (void *)&s_default_timeout,
            .defval_size = 4,
            .need_sram_cache = BK_TRUE,
            .value_max_size = 4,
        },
    };

    void register_user_config(void)
    {
        // 注册用户配置
        bk_regist_factory_user_config(s_user_config, 
                                      sizeof(s_user_config) / sizeof(s_user_config[0]));
        
        // 初始化工厂配置
        bk_factory_init();
    }

CLI 命令
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

模块提供以下 CLI 命令：

- ``factory_read``: 读取配置项（示例：读取 volume）
- ``factory_write``: 写入配置项（示例：写入 volume）
- ``factory_sync``: 同步配置到 Flash
- ``factory_reset``: 恢复出厂设置

注意事项
---------------------------------

1. **同步时机**: 写入配置后需调用 ``bk_config_sync_flash()`` 才能持久化到 Flash

2. **线程安全**: 配置读写操作不是线程安全的，多线程访问需加锁

3. **配置键名**: 配置键名必须唯一，不能与平台配置项冲突

