#ifndef DRILLCONTROLPAGE_H
#define DRILLCONTROLPAGE_H

#include <QWidget>
#include <QTimer>
#include <QMap>
#include "control/MechanismDefs.h"

// 前向声明
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class IMotionDriver;
class ZMotionDriver;
class FeedController;
class RotationController;
class PercussionController;
class ClampController;
class StorageController;
class DockingController;
class ArmExtensionController;
class ArmGripController;
class ArmRotationController;
struct MechanismParams;

namespace Ui {
class DrillControlPage;
}

/**
 * @brief 钻机高级控制页面
 *
 * 功能：
 * 1. 9个机构的独立手动控制
 * 2. 实时状态显示
 * 3. 从JSON配置加载参数
 * 4. 统一的初始化和停止操作
 */
class DrillControlPage : public QWidget
{
    Q_OBJECT

public:
    explicit DrillControlPage(QWidget *parent = nullptr);
    ~DrillControlPage();

    /**
     * @brief 设置运动控制驱动
     */
    void setDriver(IMotionDriver* driver);

    /**
     * @brief 获取控制器实例（供AutoTaskPage共享使用）
     */
    FeedController* feedController() const { return m_feedController; }
    RotationController* rotationController() const { return m_rotationController; }
    PercussionController* percussionController() const { return m_percussionController; }

signals:
    void logMessage(const QString& message);
    void controllersReady();  // 控制器创建完成信号

private slots:
    // 系统控制
    void onConnectClicked();
    void onInitAllClicked();
    void onStopAllClicked();
    void onReloadConfigClicked();

    // Fz - 进给
    void onFzInitClicked();
    void onFzMoveClicked();
    void onFzSafeClicked();
    void onFzStopClicked();

    // Pr - 回转
    void onPrInitClicked();
    void onPrStartClicked();
    void onPrStopClicked();

    // Pi - 冲击
    void onPiUnlockClicked();
    void onPiStartClicked();
    void onPiStopClicked();

    // Cb - 夹紧
    void onCbInitClicked();
    void onCbOpenClicked();
    void onCbCloseClicked();

    // Sr - 料仓
    void onSrInitClicked();
    void onSrPrevClicked();
    void onSrNextClicked();
    void onSrGotoClicked();

    // Dh - 对接
    void onDhConnectClicked();
    void onDhExtendClicked();
    void onDhRetractClicked();

    // Me - 机械手伸缩
    void onMeInitClicked();
    void onMeMoveClicked();
    void onMeExtendClicked();
    void onMeRetractClicked();

    // Mg - 机械手夹紧
    void onMgInitClicked();
    void onMgOpenClicked();
    void onMgCloseClicked();

    // Mr - 机械手回转
    void onMrInitClicked();
    void onMrMoveClicked();
    void onMrDrillClicked();
    void onMrStorageClicked();

    // 状态更新
    void updateStatus();

    // 控制器信号处理
    void onControllerStateChanged(int state, const QString& message);
    void onControllerError(const QString& error);

    // 配置编辑
    void onMechanismSelectionChanged(int index);
    void onApplyConfigClicked();
    void onSaveConfigClicked();
    void onResetConfigClicked();

    // 配置热更新
    void onMechanismConfigChanged(Mechanism::Code code);

private:
    void setupUI();
    void setupConnections();
    void setupConfigTab();
    void createControllers();
    void destroyControllers();
    void loadConfig();

    // 配置编辑辅助函数
    void loadMechanismToUI(Mechanism::Code code);
    void updateConfigGroupVisibility(Mechanism::Code code);
    void loadKeyPositionsToTable(Mechanism::Code code, const MechanismParams& params);
    void saveKeyPositionsFromTable(MechanismParams& params);

    void updateFzStatus();
    void updatePrStatus();
    void updatePiStatus();
    void updateCbStatus();
    void updateSrStatus();
    void updateDhStatus();
    void updateMeStatus();
    void updateMgStatus();
    void updateMrStatus();

    void setStatusLabel(QLabel* label, const QString& text, const QString& color);
    void appendLog(const QString& message);

private:
    Ui::DrillControlPage *ui;

    // 运动控制驱动
    IMotionDriver* m_driver;
    bool m_ownsDriver;  // 是否拥有驱动实例

    // 机构控制器
    FeedController* m_feedController;
    RotationController* m_rotationController;
    PercussionController* m_percussionController;
    ClampController* m_clampController;
    StorageController* m_storageController;
    DockingController* m_dockingController;
    ArmExtensionController* m_armExtController;
    ArmGripController* m_armGripController;
    ArmRotationController* m_armRotController;

    // 状态更新定时器
    QTimer* m_statusTimer;

    // 连接状态
    bool m_isConnected;

    // 配置编辑当前选中机构
    Mechanism::Code m_currentConfigMechanism;
};

#endif // DRILLCONTROLPAGE_H
