-- ==================================================
-- 钻机数据采集系统数据库Schema
-- ==================================================

-- 1. 轮次表（rounds）
-- 记录每一轮实验/钻进的基本信息
CREATE TABLE IF NOT EXISTS rounds (
    round_id        INTEGER PRIMARY KEY AUTOINCREMENT,
    start_ts_us     INTEGER NOT NULL,       -- 开始时间（微秒级Unix时间戳）
    end_ts_us       INTEGER,                -- 结束时间（微秒级Unix时间戳）
    operator_name   TEXT,                   -- 操作员
    note            TEXT,                   -- 备注信息
    created_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 创建索引
CREATE INDEX IF NOT EXISTS idx_rounds_start ON rounds(start_ts_us);
CREATE INDEX IF NOT EXISTS idx_rounds_end ON rounds(end_ts_us);

-- 2. 标量数据表（scalar_samples）
-- 存储低频（10Hz）和中频数据：MDB传感器、电机参数等
CREATE TABLE IF NOT EXISTS scalar_samples (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id        INTEGER NOT NULL,       -- 关联到rounds表
    sensor_type     INTEGER NOT NULL,       -- 传感器类型（SensorType枚举值）
    channel_id      INTEGER NOT NULL,       -- 通道ID/电机号
    timestamp_us    INTEGER NOT NULL,       -- 采样时间戳（微秒）
    value           REAL NOT NULL,          -- 采样值
    seq_in_round    INTEGER,                -- 在本轮次中的序号（可选）
    FOREIGN KEY (round_id) REFERENCES rounds(round_id) ON DELETE CASCADE
);

-- 创建索引加速查询
CREATE INDEX IF NOT EXISTS idx_scalar_round ON scalar_samples(round_id);
CREATE INDEX IF NOT EXISTS idx_scalar_type ON scalar_samples(sensor_type);
CREATE INDEX IF NOT EXISTS idx_scalar_time ON scalar_samples(timestamp_us);
CREATE INDEX IF NOT EXISTS idx_scalar_round_type ON scalar_samples(round_id, sensor_type);

-- 3. 振动数据表（vibration_blocks）
-- 存储高频（5000Hz）振动数据，按块存储
CREATE TABLE IF NOT EXISTS vibration_blocks (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id        INTEGER NOT NULL,       -- 关联到rounds表
    channel_id      INTEGER NOT NULL,       -- 振动通道(0=X, 1=Y, 2=Z)
    start_ts_us     INTEGER NOT NULL,       -- 本块第一个样本的时间戳（微秒）
    sample_rate     REAL NOT NULL,          -- 采样频率(Hz)
    n_samples       INTEGER NOT NULL,       -- 本块样本数量
    data_blob       BLOB NOT NULL,          -- 原始数据（二进制）
    seq_in_round    INTEGER,                -- 块序号
    FOREIGN KEY (round_id) REFERENCES rounds(round_id) ON DELETE CASCADE
);

-- 创建索引
CREATE INDEX IF NOT EXISTS idx_vib_round ON vibration_blocks(round_id);
CREATE INDEX IF NOT EXISTS idx_vib_channel ON vibration_blocks(channel_id);
CREATE INDEX IF NOT EXISTS idx_vib_time ON vibration_blocks(start_ts_us);

-- 4. 频率变化日志表（frequency_log）
-- 记录采样频率的变化历史
CREATE TABLE IF NOT EXISTS frequency_log (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id        INTEGER,                -- 关联到rounds表（可为NULL表示全局设置）
    sensor_type     INTEGER NOT NULL,       -- 传感器类型
    old_freq        REAL,                   -- 旧频率(Hz)
    new_freq        REAL NOT NULL,          -- 新频率(Hz)
    timestamp_us    INTEGER NOT NULL,       -- 变化时间戳（微秒）
    comment         TEXT,                   -- 备注
    FOREIGN KEY (round_id) REFERENCES rounds(round_id) ON DELETE SET NULL
);

-- 创建索引
CREATE INDEX IF NOT EXISTS idx_freq_round ON frequency_log(round_id);
CREATE INDEX IF NOT EXISTS idx_freq_type ON frequency_log(sensor_type);
CREATE INDEX IF NOT EXISTS idx_freq_time ON frequency_log(timestamp_us);

-- 5. 系统配置表（system_config）
-- 存储系统配置参数
CREATE TABLE IF NOT EXISTS system_config (
    key             TEXT PRIMARY KEY,       -- 配置键
    value           TEXT NOT NULL,          -- 配置值
    description     TEXT,                   -- 描述
    updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 插入默认配置
INSERT OR IGNORE INTO system_config (key, value, description) VALUES
    ('default_vib_rate', '5000.0', '默认振动采样频率(Hz)'),
    ('default_mdb_rate', '10.0', '默认MDB采样频率(Hz)'),
    ('default_motor_rate', '100.0', '默认电机采样频率(Hz)'),
    ('db_version', '1.0', '数据库版本');

-- 6. 自动任务事件表（auto_task_events）
-- 记录自动钻进的开始、步骤切换、完成、失败等事件与传感器快照
CREATE TABLE IF NOT EXISTS auto_task_events (
    event_id            INTEGER PRIMARY KEY AUTOINCREMENT,
    round_id            INTEGER NOT NULL,
    task_file           TEXT,
    step_index          INTEGER,
    state               TEXT NOT NULL,
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
