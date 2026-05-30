// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/restorewalletmnemonicdialog.h>

#include <qt/guiutil.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

RestoreWalletMnemonicDialog::RestoreWalletMnemonicDialog(QWidget* parent)
    : QDialog(parent, GUIUtil::dialog_flags)
{
    setWindowTitle(tr("Restore wallet from recovery phrase"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(this);
    intro->setWordWrap(true);
    intro->setText(tr(
        "Enter your 24-word recovery phrase. This restores taproot descriptors and quantum keys the same way "
        "as backing up wallet.dat.\n\n"
        "The optional wallet encryption passphrase is only for encrypting wallet.dat on this computer — "
        "it is not your recovery phrase."));
    layout->addWidget(intro);

    auto* name_label = new QLabel(tr("Wallet name:"), this);
    layout->addWidget(name_label);
    m_name_edit = new QLineEdit(this);
    layout->addWidget(m_name_edit);

    auto* phrase_label = new QLabel(tr("Recovery phrase (24 words):"), this);
    layout->addWidget(phrase_label);
    m_mnemonic_edit = new QPlainTextEdit(this);
    m_mnemonic_edit->setPlaceholderText(tr("word1 word2 … word24"));
    m_mnemonic_edit->setMinimumHeight(120);
    layout->addWidget(m_mnemonic_edit);

    m_encrypt_checkbox = new QCheckBox(tr("Encrypt restored wallet (wallet.dat passphrase)"), this);
    layout->addWidget(m_encrypt_checkbox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Restore"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_name_edit, &QLineEdit::textChanged, this, [buttons, this](const QString& text) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    GUIUtil::handleCloseWindowShortcut(this);
}

QString RestoreWalletMnemonicDialog::walletName() const
{
    return m_name_edit->text().trimmed();
}

QString RestoreWalletMnemonicDialog::mnemonic() const
{
    return m_mnemonic_edit->toPlainText().simplified();
}

bool RestoreWalletMnemonicDialog::encryptWalletChecked() const
{
    return m_encrypt_checkbox->isChecked();
}
