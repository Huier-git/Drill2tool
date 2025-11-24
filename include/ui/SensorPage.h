#ifndef SENSORPAGE_H
#define SENSORPAGE_H

#include <QWidget>
#include "control/AcquisitionManager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class SensorPage; }
QT_END_NAMESPACE

/**
 * @brief 传感器采集控制页面
 * 
 * 功能：
 * 1. 配置并连接VK701震动传感器
 * 2. 配置并连接Modbus TCP传感器
 * 3. 配置并连接ZMotion电机控制器
 * 4. 设置采样频率
 * 5. 启动/停止采集
 */
class SensorPage : public QWidget
{
    Q_OBJECT

public:
    explicit SensorPage(QWidget *parent = nullptr);
    ~SensorPage();
    
    void setAcquisitionManager(AcquisitionManager *manager);

private slots:
    // VK701连接
    void onVK701ConnectClicked();
    void onVK701DisconnectClicked();
    void onVK701FrequencyChanged();
    
    // Modbus连接
    void onMdbConnectClicked();
    void onMdbDisconnectClicked();
    void onMdbFrequencyChanged();
    
    // ZMotion连接
    void onMotorConnectClicked();
    void onMotorDisconnectClicked();
    void onMotorFrequencyChanged();
    
    // 采集控制
    void onStartAll();
    void onStopAll();
    void onStartNewRound();
    void onEndRound();
    
    // 状态更新
    void onAcquisitionStateChanged(bool isRunning);
    void onRoundChanged(int roundId);
    void onErrorOccurred(const QString &workerName, const QString &error);
    void onStatisticsUpdated(const QString &info);

private:
    void setupConnections();
    void updateUIState();

private:
    Ui::SensorPage *ui;
    AcquisitionManager *m_acquisitionManager;
    
    bool m_vk701Connected;
    bool m_mdbConnected;
    bool m_motorConnected;
};

#endif // SENSORPAGE_H
