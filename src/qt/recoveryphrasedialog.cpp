// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/recoveryphrasedialog.h>

#include <qt/guiutil.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFont>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

RecoveryPhraseDialog::RecoveryPhraseDialog(const QString& mnemonic, QWidget* parent)
    : QDialog(parent, GUIUtil::dialog_flags)
{
    setWindowTitle(tr("Wallet recovery phrase"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* warning = new QLabel(this);
    warning->setWordWrap(true);
    warning->setTextFormat(Qt::RichText);
    warning->setText(tr(
        "<b>Write down your 24-word recovery phrase now.</b><br><br>"
        "This is <b>not</b> your wallet encryption passphrase. The recovery phrase restores your keys "
        "if you lose wallet.dat. The wallet passphrase (optional) only encrypts wallet.dat on this device — "
        "if you lose the passphrase you can still restore with this phrase into a new wallet.<br><br>"
        "Also back up <b>wallet.dat</b>. Either backup alone can recover funds; keeping both is best."));
    layout->addWidget(warning);

    m_phrase_view = new QTextEdit(this);
    m_phrase_view->setReadOnly(true);
    m_phrase_view->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    QFont mono = m_phrase_view->font();
    mono.setStyleHint(QFont::Monospace);
    m_phrase_view->setFont(mono);
    m_phrase_view->setPlainText(mnemonic);
    m_phrase_view->setMinimumHeight(120);
    layout->addWidget(m_phrase_view);

    m_ack_checkbox = new QCheckBox(tr("I have written down my recovery phrase in a safe place"), this);
    connect(m_ack_checkbox, &QCheckBox::stateChanged, this, &RecoveryPhraseDialog::onBackupAckChanged);
    layout->addWidget(m_ack_checkbox);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Continue"));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(m_buttons);

    GUIUtil::handleCloseWindowShortcut(this);
}

void RecoveryPhraseDialog::onBackupAckChanged(int state)
{
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(state == Qt::Checked);
}
