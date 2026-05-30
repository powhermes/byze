// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_RECOVERYPHRASEDIALOG_H
#define BITCOIN_QT_RECOVERYPHRASEDIALOG_H

#include <QDialog>

class QLabel;
class QCheckBox;
class QTextEdit;
class QDialogButtonBox;

/** One-time display of the 24-word BIP39 recovery phrase after wallet creation. */
class RecoveryPhraseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RecoveryPhraseDialog(const QString& mnemonic, QWidget* parent = nullptr);

private Q_SLOTS:
    void onBackupAckChanged(int state);

private:
    QTextEdit* m_phrase_view{nullptr};
    QCheckBox* m_ack_checkbox{nullptr};
    QDialogButtonBox* m_buttons{nullptr};
};

#endif // BITCOIN_QT_RECOVERYPHRASEDIALOG_H
