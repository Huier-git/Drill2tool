-- ==================================================
-- 钻机数据采集系统数据库 Schema v2.0
-- 设计理念：时间窗口对齐机制，支持多频率传感器数据
-- ==================================================

-- SQLite 性能优化配置
PRAGMA journal_mode = WAL;           -- 写优化
PRAGMA synchronous = NORMAL;         -- 平衡性能和安全
PRAGMA cache_size = -64000;          -- 64MB缓存
PRAGMA temp_store = MEMORY;          -- 临时文件在内存
PRAGMA mmap_size = 268435456;        -- 256MB内存映射

-- ==================================================
-- 1. 轮次表（rounds）
-- ==================================================
CREATE TABLE IF NOT EXISTS rounds (
    round_id          INTEGER PRIMARY KEY AUTOINCREMENT,
    start_ts_us       INTEGER NOT NULL,        -- 开始时间（微秒）
    end_ts_us         INTEGER,                 -- 结束时间（微秒）
    status            TEXT DEFAULT 'running',  -- running/completed/aborted
    operator_name     TEXT,                    -- 操作员
    note              TEXT,                    -- 备注
    created_at        DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_rounds_start ON rounds(start_ts_us);
CREATE INDEX IF NOT EXISTS idx_rounds_status ON rounds(status);

-- ==================================================
-- 2. 时间窗口表（time_windows）⭐ 核心创新
-- 固定1秒窗口，所有数据关联到窗口实现对齐
-- ==================================================
CREATE TABLE IF NOT EXISTS time_windows (
    window_id         INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id          INTEGER NOT NULL,        -- 关联轮次
    window_start_us   INTEGER NOT NULL,        -- 窗口起始时间（微秒）
    window_end_us     INTEGER NOT NULL,        -- 窗口结束时间（微秒）

    -- 数据标志（快速判断该窗口有哪些数据）
    has_vibration     INTEGER DEFAULT 0,       -- 是否有振动数据
    has_mdb           INTEGER DEFAULT 0,       -- 是否有MDB数据
    has_motor         INTEGER DEFAULT 0,       -- 是否有电机数据

    FOREIGN KEY (round_id) REFERENCES rounds(round_id) ON DELETE CASCADE
);

-- 关键索引：查询时间窗口
CREATE UNIQUE INDEX IF NOT EXISTS idx_tw_round_start ON time_windows(round_id, window_start_us);
CREATE INDEX IF NOT EXISTS idx_tw_round ON time_windows(round_id);

-- ==================================================
-- 3. 振动数据表（vibration_blocks）
-- 存储高频振动数据（5000Hz），BLOB格式
-- ==================================================
CREATE TABLE IF NOT EXISTS vibration_blocks (
    block_id          INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id          INTEGER NOT NULL,
    window_id         INTEGER NOT NULL,        -- 关联时间窗口
    channel_id        INTEGER NOT NULL,        -- 0=X, 1=Y, 2=Z
    start_ts_us       INTEGER NOT NULL,        -- 块起始时间
    sample_rate       REAL NOT NULL,           -- 采样频率
    n_samples         INTEGER NOT NULL,        -- 样本数量
    data_blob         BLOB NOT NULL,           -- float32数组

    -- 预计算统计（避免频繁解析BLOB）
    min_value         REAL,
    max_value         REAL,
    mean_value        REAL,
    rms_value         REAL,

    FOREIGN KEY (round_id) REFERENCES rounds(round_id) ON DELETE CASCADE,
    FOREIGN KEY (window_id) REFERENCES time_windows(window_id) ON DELETE CASCADE
);

-- 索引
CREATE INDEX IF NOT EXISTS idx_vib_round ON vibration_blocks(round_id);
CREATE INDEX IF NOT EXISTS idx_vib_window ON vibration_blocks(window_id);
CREATE INDEX IF NOT EXISTS idx_vib_channel ON vibration_blocks(channel_id);
CREATE INDEX IF NOT EXISTS idx_vib_round_channel ON vibration_blocks(round_id, channel_id);

-- ==================================================
-- 4. 标量数据表（scalar_samples）
-- 存储低中频标量数据（MDB 10Hz、电机 100Hz）
-- ==================================================
CREATE TABLE IF NOT EXISTS scalar_samples (
    sample_id         INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id          INTEGER NOT NULL,
    window_id         INTEGER NOT NULL,        -- 关联时间窗口
    sensor_type       INTEGER NOT NULL,        -- SensorType枚举
    channel_id        INTEGER NOT NULL,        -- 通道/电机号
    timestamp_us      INTEGER NOT NULL,        -- 采样时间戳
    value             REAL NOT NULL,           -- 采样值

    FOREIGN KEY (round_id) REFERENCES rounds(round_id) ON DELETE CASCADE,
    FOREIGN KEY (window_id) REFERENCES time_windows(window_id) ON DELETE CASCADE
);

-- 索引
CREATE INDEX IF NOT EXISTS idx_scalar_round ON scalar_samples(round_id);
CREATE INDEX IF NOT EXISTS idx_scalar_window ON scalar_samples(window_id);
CREATE INDEX IF NOT EXISTS idx_scalar_type ON scalar_samples(sensor_type);
CREATE INDEX IF NOT EXISTS idx_scalar_window_type ON scalar_samples(window_id, sensor_type);

-- ==================================================
-- 5. 事件标记表（events）
-- 记录钻进开始/结束、报警等关键事件
-- ==================================================
CREATE TABLE IF NOT EXISTS events (
    event_id          INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id          INTEGER NOT NULL,
    window_id         INTEGER,                 -- 可选：关联时间窗口
    event_type        TEXT NOT NULL,           -- drill_start/drill_end/alarm/manual_mark
    timestamp_us      INTEGER NOT NULL,        -- 事件时间
    description       TEXT,                    -- 事件描述

    FOREIGN KEY (round_id) REFERENCES rounds(round_id) ON DELETE CASCADE,
    FOREIGN KEY (window_id) REFERENCES time_windows(window_id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_event_round ON events(round_id);
CREATE INDEX IF NOT EXISTS idx_event_type ON events(event_type);

-- ==================================================
-- 6. 自动任务事件表（auto_task_events）
-- 记录自动任务执行的状态与关键传感器快照
-- ==================================================
CREATE TABLE IF NOT EXISTS auto_task_events (
    event_id            INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id            INTEGER NOT NULL,
    task_file           TEXT,
    step_index          INTEGER,
    state               TEXT NOT NULL,           -- started/step_started/step_completed/finished/failed
    reason              TEXT,
    depth_mm            REAL,
    torque_nm           REAL,
    pressure_n          REAL,
    velocity_mm_per_min REAL,
    force_upper_n       REAL,
    force_lower_n       REAL,
    timestamp_us        INTEGER NOT NULL,

    FOREIGN KEY (round_id) REFERENCES rounds(round_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_auto_task_round ON auto_task_events(round_id);
CREATE INDEX IF NOT EXISTS idx_auto_task_task ON auto_task_events(task_file);

-- ==================================================
-- 7. 频率变化日志表（frequency_log）
-- 保留原有功能
-- ==================================================
CREATE TABLE IF NOT EXISTS frequency_log (
    log_id            INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id          INTEGER,
    sensor_type       INTEGER NOT NULL,
    old_freq          REAL,
    new_freq          REAL NOT NULL,
    timestamp_us      INTEGER NOT NULL,
    comment           TEXT,

    FOREIGN KEY (round_id) REFERENCES rounds(round_id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_freq_round ON frequency_log(round_id);

-- ==================================================
-- 8. 系统配置表（system_config）
-- ==================================================
CREATE TABLE IF NOT EXISTS system_config (
    key               TEXT PRIMARY KEY,
    value             TEXT NOT NULL,
    description       TEXT,
    updated_at        DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 默认配置
INSERT OR IGNORE INTO system_config (key, value, description) VALUES
    ('db_version', '2.0', '数据库版本'),
    ('window_duration_us', '1000000', '时间窗口时长（微秒，固定1秒）'),
    ('default_vib_rate', '5000.0', '默认振动采样频率(Hz)'),
    ('default_mdb_rate', '10.0', '默认MDB采样频率(Hz)'),
    ('default_motor_rate', '100.0', '默认电机采样频率(Hz)');

-- ==================================================
-- Schema v2.0 创建完成
-- 核心特性：
-- 1. 时间窗口对齐（1秒窗口）
-- 2. 混合存储（BLOB + 预计算统计）
-- 3. 多频率数据支持
-- 4. 事件标记支持
-- 5. 自动任务执行记录支持
-- ==================================================
