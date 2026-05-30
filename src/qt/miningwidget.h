// Copyright (c) 2025-present The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#ifndef BITCOIN_QT_MININGWIDGET_H
#define BITCOIN_QT_MININGWIDGET_H

#include <QWidget>
#include <QTimer>

class ClientModel;
class WalletModel;
enum class SyncType;
enum class SynchronizationState;

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
QT_END_NAMESPACE

/** Mining widget for CPU mining controls */
class MiningWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MiningWidget(QWidget *parent = nullptr);
    ~MiningWidget();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

private Q_SLOTS:
    void on_startMiningButton_clicked();
    void on_stopMiningButton_clicked();
    void updateMiningInfo();
    void updateIBDStatus(bool inIBD);

private:
    void updateUI();
    void setMiningActive(bool active);
    QString formatHashrate(double hashrate) const;

    ClientModel* m_clientModel{nullptr};
    WalletModel* m_walletModel{nullptr};
    
    QLabel* m_statusLabel{nullptr};
    QLabel* m_hashrateLabel{nullptr};
    QLabel* m_blocksFoundLabel{nullptr};
    QLabel* m_addressLabel{nullptr};
    QLineEdit* m_addressInput{nullptr};
    QPushButton* m_startButton{nullptr};
    QPushButton* m_stopButton{nullptr};
    
    QTimer* m_updateTimer{nullptr};
    bool m_miningActive{false};
    bool m_inIBD{false};
    double m_currentHashrate{0.0};
    uint64_t m_blocksFound{0};
    QString m_miningAddress;
};

#endif // BITCOIN_QT_MININGWIDGET_H

