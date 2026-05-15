// Copyright (c) 2025-present The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include <qt/miningwidget.h>

#include <qt/bitcoinaddressvalidator.h>
#include <qt/clientmodel.h>
#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <interfaces/node.h>
#include <univalue.h>
#include <validation.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

MiningWidget::MiningWidget(QWidget *parent)
    : QWidget(parent)
{
    // Create layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // Title
    QLabel* titleLabel = new QLabel(tr("CPU Mining"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);
    
    // Status frame
    QFrame* statusFrame = new QFrame(this);
    statusFrame->setFrameShape(QFrame::StyledPanel);
    statusFrame->setFrameShadow(QFrame::Raised);
    QVBoxLayout* statusLayout = new QVBoxLayout(statusFrame);
    
    // Status label
    m_statusLabel = new QLabel(tr("Status: Inactive"), this);
    statusLayout->addWidget(m_statusLabel);
    
    // Hashrate label
    m_hashrateLabel = new QLabel(tr("Hashrate: 0 H/s"), this);
    statusLayout->addWidget(m_hashrateLabel);
    
    // Blocks found label
    m_blocksFoundLabel = new QLabel(tr("Blocks found: 0"), this);
    statusLayout->addWidget(m_blocksFoundLabel);
    
    mainLayout->addWidget(statusFrame);
    
    // Address input
    QLabel* addressLabel = new QLabel(tr("Mining Address:"), this);
    mainLayout->addWidget(addressLabel);
    
    m_addressInput = new QLineEdit(this);
    BitcoinAddressEntryValidator* validator = new BitcoinAddressEntryValidator(this);
    m_addressInput->setValidator(validator);
    m_addressInput->setPlaceholderText(tr("Enter address to receive mining rewards"));
    connect(m_addressInput, &QLineEdit::textChanged, this, &MiningWidget::updateUI);
    mainLayout->addWidget(m_addressInput);
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    m_startButton = new QPushButton(tr("Start Mining"), this);
    m_startButton->setEnabled(false);
    connect(m_startButton, &QPushButton::clicked, this, &MiningWidget::on_startMiningButton_clicked);
    buttonLayout->addWidget(m_startButton);
    
    m_stopButton = new QPushButton(tr("Stop Mining"), this);
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &MiningWidget::on_stopMiningButton_clicked);
    buttonLayout->addWidget(m_stopButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Update timer (poll getmininginfo every 3 seconds)
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &MiningWidget::updateMiningInfo);
    m_updateTimer->start(3000); // 3 seconds
    
    // Initial update
    updateMiningInfo();
}

MiningWidget::~MiningWidget()
{
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
}

void MiningWidget::setClientModel(ClientModel *clientModel)
{
    m_clientModel = clientModel;
    if (clientModel) {
        // Connect to IBD status updates
        connect(clientModel, &ClientModel::numBlocksChanged, this, [this](int count, const QDateTime& blockDate, double nVerificationProgress, SyncType synctype, SynchronizationState sync_state) {
            // Check if we're in IBD (sync_state != POST_INIT means we're still syncing)
            bool inIBD = (sync_state != SynchronizationState::POST_INIT);
            updateIBDStatus(inIBD);
        });
        updateMiningInfo();
    }
}

void MiningWidget::setWalletModel(WalletModel *walletModel)
{
    m_walletModel = walletModel;
    if (walletModel) {
        // Pre-fill address input with a receive address if available
        // This is optional - user can still change it
    }
}

void MiningWidget::on_startMiningButton_clicked()
{
    if (!m_clientModel) return;
    
    QString address = m_addressInput->text().trimmed();
    if (address.isEmpty()) {
        QMessageBox::warning(this, tr("Mining"), tr("Please enter a mining address."));
        return;
    }
    
    // Validate address format
    if (m_addressInput->hasAcceptableInput() == false) {
        QMessageBox::warning(this, tr("Mining"), tr("Invalid address format."));
        return;
    }
    
    // Call startmining RPC
    try {
        UniValue params(UniValue::VARR);
        params.push_back(address.toStdString());
        
        UniValue result = m_clientModel->node().executeRpc("startmining", params, "");
        
        if (result.isObject() && result["success"].get_bool()) {
            m_miningAddress = address;
            setMiningActive(true);
            QMessageBox::information(this, tr("Mining"), tr("Mining started successfully."));
        } else {
            QMessageBox::warning(this, tr("Mining"), tr("Failed to start mining. It may already be active."));
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Mining"), tr("Error starting mining: %1").arg(QString::fromStdString(e.what())));
    }
    
    // Update immediately
    updateMiningInfo();
}

void MiningWidget::on_stopMiningButton_clicked()
{
    if (!m_clientModel) return;
    
    // Call stopmining RPC
    try {
        UniValue params(UniValue::VARR);
        
        UniValue result = m_clientModel->node().executeRpc("stopmining", params, "");
        
        if (result.isObject() && result["success"].get_bool()) {
            setMiningActive(false);
            QMessageBox::information(this, tr("Mining"), tr("Mining stopped successfully."));
        } else {
            QMessageBox::warning(this, tr("Mining"), tr("Failed to stop mining."));
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Mining"), tr("Error stopping mining: %1").arg(QString::fromStdString(e.what())));
    }
    
    // Update immediately
    updateMiningInfo();
}

void MiningWidget::updateMiningInfo()
{
    if (!m_clientModel) return;
    
    try {
        UniValue params(UniValue::VARR);
        
        UniValue result = m_clientModel->node().executeRpc("getminingstatus", params, "");
        if (!result.isObject()) {
            result = m_clientModel->node().executeRpc("getmininginfo", params, "");
        }

        if (result.isObject()) {
            // Update mining status (getminingstatus uses "active"; getmininginfo uses "mining_active")
            bool active = result.exists("active") ? result["active"].get_bool() : result["mining_active"].get_bool();
            setMiningActive(active);
            
            if (active) {
                // Update hashrate
                if (result.exists("hashrate")) {
                    m_currentHashrate = result["hashrate"].get_real();
                    m_hashrateLabel->setText(tr("Hashrate: %1").arg(formatHashrate(m_currentHashrate)));
                }
                
                // Update blocks found
                if (result.exists("blocks_found")) {
                    m_blocksFound = result["blocks_found"].getInt<int64_t>();
                    m_blocksFoundLabel->setText(tr("Blocks found: %1").arg(m_blocksFound));
                }
                
                // Update address
                if (result.exists("address")) {
                    m_miningAddress = QString::fromStdString(result["address"].get_str());
                } else if (result.exists("mining_address")) {
                    m_miningAddress = QString::fromStdString(result["mining_address"].get_str());
                    m_addressInput->setText(m_miningAddress);
                }
            } else {
                m_hashrateLabel->setText(tr("Hashrate: 0 H/s"));
                m_currentHashrate = 0.0;
            }
        }
    } catch (const std::exception& e) {
        // Silently fail - RPC might not be available yet
        // Don't spam error messages on every poll
    }
}

void MiningWidget::updateIBDStatus(bool inIBD)
{
    m_inIBD = inIBD;
    updateUI();
}

void MiningWidget::updateUI()
{
    // Allow mining during IBD if we're at height 0 (genesis)
    // Otherwise, disable mining controls during IBD
    bool canMine = !m_inIBD;
    if (m_inIBD && m_clientModel) {
        int blockHeight = m_clientModel->getNumBlocks();
        if (blockHeight == 0) {
            canMine = true;  // Allow mining at genesis height even during IBD
        }
    }
    
    m_startButton->setEnabled(canMine && !m_miningActive && !m_addressInput->text().trimmed().isEmpty());
    m_stopButton->setEnabled(canMine && m_miningActive);
    m_addressInput->setEnabled(canMine && !m_miningActive);
    
    if (m_inIBD) {
        int blockHeight = m_clientModel ? m_clientModel->getNumBlocks() : -1;
        if (blockHeight == 0) {
            m_statusLabel->setText(tr("Status: Mining allowed at genesis (height 0)"));
        } else {
            m_statusLabel->setText(tr("Status: Waiting for sync (mining disabled during IBD)"));
        }
    } else if (m_miningActive) {
        m_statusLabel->setText(tr("Status: Active"));
    } else {
        m_statusLabel->setText(tr("Status: Inactive"));
    }
}

void MiningWidget::setMiningActive(bool active)
{
    m_miningActive = active;
    updateUI();
}

QString MiningWidget::formatHashrate(double hashrate) const
{
    if (hashrate >= 1e9) {
        return QString("%1 GH/s").arg(hashrate / 1e9, 0, 'f', 2);
    } else if (hashrate >= 1e6) {
        return QString("%1 MH/s").arg(hashrate / 1e6, 0, 'f', 2);
    } else if (hashrate >= 1e3) {
        return QString("%1 kH/s").arg(hashrate / 1e3, 0, 'f', 2);
    } else {
        return QString("%1 H/s").arg(hashrate, 0, 'f', 2);
    }
}

