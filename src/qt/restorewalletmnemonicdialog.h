// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_RESTOREWALLETMNEMONICDIALOG_H
#define BITCOIN_QT_RESTOREWALLETMNEMONICDIALOG_H

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

/** Collect wallet name, 24-word phrase, and optional encryption intent for restorefrommnemonic. */
class RestoreWalletMnemonicDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RestoreWalletMnemonicDialog(QWidget* parent = nullptr);

    QString walletName() const;
    QString mnemonic() const;
    bool encryptWalletChecked() const;

private:
    QLineEdit* m_name_edit{nullptr};
    QPlainTextEdit* m_mnemonic_edit{nullptr};
    QCheckBox* m_encrypt_checkbox{nullptr};
};

#endif // BITCOIN_QT_RESTOREWALLETMNEMONICDIALOG_H
