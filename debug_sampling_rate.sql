-- 诊断采样率问题的SQL查询

-- 1. 检查time_windows表（应该每秒一个窗口）
SELECT
    'Time Windows' as table_name,
    COUNT(*) as window_count,
    MIN(window_start_us) as first_window_us,
    MAX(window_start_us) as last_window_us,
    (MAX(window_start_us) - MIN(window_start_us)) / 1000000.0 as duration_seconds
FROM time_windows
WHERE round_id = (SELECT MAX(round_id) FROM rounds);

-- 2. 检查MDB标量样本（应该每秒10个样本）
SELECT
    'MDB Scalar Samples' as table_name,
    COUNT(*) as total_samples,
    COUNT(DISTINCT window_id) as window_count,
    CAST(COUNT(*) AS FLOAT) / COUNT(DISTINCT window_id) as avg_samples_per_window,
    sensor_type
FROM scalar_samples
WHERE round_id = (SELECT MAX(round_id) FROM rounds)
  AND sensor_type >= 100 AND sensor_type < 200
GROUP BY sensor_type
ORDER BY sensor_type;

-- 3. 检查特定时间窗口的MDB样本详情（第一个窗口）
SELECT
    'First Window MDB Detail' as info,
    window_id,
    sensor_type,
    channel_id,
    timestamp_us,
    value
FROM scalar_samples
WHERE window_id = (
    SELECT window_id FROM time_windows
    WHERE round_id = (SELECT MAX(round_id) FROM rounds)
    ORDER BY window_start_us LIMIT 1
)
AND sensor_type >= 100 AND sensor_type < 200
ORDER BY timestamp_us;

-- 4. 检查振动数据块（应该每秒有数据，但n_samples可能不是5000）
SELECT
    'Vibration Blocks' as table_name,
    channel_id,
    COUNT(*) as block_count,
    SUM(n_samples) as total_samples,
    AVG(n_samples) as avg_samples_per_block,
    COUNT(DISTINCT window_id) as window_count,
    CAST(SUM(n_samples) AS FLOAT) / COUNT(DISTINCT window_id) as avg_samples_per_window
FROM vibration_blocks
WHERE round_id = (SELECT MAX(round_id) FROM rounds)
GROUP BY channel_id
ORDER BY channel_id;

-- 5. 检查第一个时间窗口的振动块详情
SELECT
    'First Window Vibration Detail' as info,
    channel_id,
    n_samples,
    start_ts_us,
    min_value,
    max_value,
    mean_value,
    rms_value
FROM vibration_blocks
WHERE window_id = (
    SELECT window_id FROM time_windows
    WHERE round_id = (SELECT MAX(round_id) FROM rounds)
    ORDER BY window_start_us LIMIT 1
)
ORDER BY channel_id;
