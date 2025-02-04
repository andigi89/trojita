/* Copyright (C) 2006 - 2014 Jan Kundrát <jkt@flaska.net>
   Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>

   This file is part of the Trojita Qt IMAP e-mail client,
   http://trojita.flaska.net/

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License or (at your option) version 3 or any later version
   accepted by the membership of KDE e.V. (or its successor approved
   by the membership of KDE e.V.), which shall act as a proxy
   defined in Section 14 of version 3 of the license.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "SMTP.h"
#include "UiUtils/Formatting.h"

namespace MSA
{

SMTP::SMTP(QObject *parent, const QString &host, quint16 port, bool encryptedConnect, bool TLS, bool auth,
           const QString &user):
    AbstractMSA(parent), host(host), port(port),
    encryptedConnect(encryptedConnect), TLS(TLS), auth(auth),
    user(user), failed(false), isWaitingForPassword(false), sendingMode(MODE_SMTP_INVALID)
{
    qwwSmtp = new QwwSmtpClient(this);
    // FIXME: handle SSL errors in the same way as we handle IMAP TLS errors, with key pinning, etc.
    connect(qwwSmtp, &QwwSmtpClient::sslErrors, this, &SMTP::handleSslErrors);
    connect(qwwSmtp, &QwwSmtpClient::connected, this, &AbstractMSA::sending);
    connect(qwwSmtp, &QwwSmtpClient::done, this, &SMTP::handleDone);
    connect(qwwSmtp, &QwwSmtpClient::socketError, this, &SMTP::handleError);
    connect(qwwSmtp, &QwwSmtpClient::logReceived, this, [this](const QByteArray& data) {
        emit logged(Common::LogKind::LOG_IO_READ, QStringLiteral("SMTP"), QString::fromUtf8(data));
    });
    connect(qwwSmtp, &QwwSmtpClient::logSent, this, [this](const QByteArray& data) {
        emit logged(Common::LogKind::LOG_IO_WRITTEN, QStringLiteral("SMTP"), QString::fromUtf8(data));
    });
}

void SMTP::cancel()
{
    qwwSmtp->disconnectFromHost();
    if (!failed) {
        failed = true;
        emit error(tr("Sending of the message was cancelled"));
    }
}

void SMTP::handleDone(bool ok)
{
    if (failed) {
        // This is a duplicate notification. The QwwSmtpClient is known to send contradicting results, see e.g. bug 321272.
        return;
    }
    if (ok) {
        emit sent();
    } else {
        failed = true;
        if (qwwSmtp->errorString().isEmpty())
            emit error(tr("Sending of the message failed."));
        else
            emit error(tr("Sending of the message failed with the following error: %1").arg(qwwSmtp->errorString()));
    }
}

void SMTP::handleError(QAbstractSocket::SocketError err, const QString &msg)
{
    Q_UNUSED(err);
    failed = true;
    emit error(msg);
}

void SMTP::handleSslErrors(const QList<QSslError>& errors)
{
    auto msg = UiUtils::Formatting::sslErrorsToHtml(errors);
    emit error(tr("<p>Cannot send message due to an SSL/TLS error</p>\n%1").arg(msg));
}

void SMTP::setPassword(const QString &password)
{
    pass = password;
    if (isWaitingForPassword)
        sendContinueGotPassword();
}

void SMTP::sendMail(const QByteArray &from, const QList<QByteArray> &to, const QByteArray &data)
{
    this->from = from;
    this->to = to;
    this->data = data;
    this->sendingMode = MODE_SMTP_DATA;
    this->isWaitingForPassword = true;
    emit progressMax(data.size());
    emit progress(0);
    emit connecting();
    if (!auth || !pass.isEmpty()) {
        sendContinueGotPassword();
        return;
    }
    emit passwordRequested(user, host);
}

void SMTP::sendContinueGotPassword()
{
    isWaitingForPassword = false;
    if (encryptedConnect)
        qwwSmtp->connectToHostEncrypted(host, port);
    else
        qwwSmtp->connectToHost(host, port);
    if (Tls)
        qwwSmtp->TLS();
    if (auth)
        qwwSmtp->authenticate(user, pass, QwwSmtpClient::AuthAny);
    emit sending(); // FIXME: later
    switch (sendingMode) {
    case MODE_SMTP_DATA:
        {
            //RFC5321 specifies to prepend a period to lines starting with a period in section 4.5.2
            if (data.startsWith('.'))
                data.prepend('.');
            data.replace("\n.", "\n..");
            qwwSmtp->sendMail(from, to, data);
        }
        break;
    case MODE_SMTP_BURL:
        qwwSmtp->sendMailBurl(from, to, data);
        break;
    default:
        failed = true;
        emit error(tr("Unknown SMTP mode"));
        break;
    }
    qwwSmtp->disconnectFromHost();
}

bool SMTP::supportsBurl() const
{
    return true;
}

void SMTP::sendBurl(const QByteArray &from, const QList<QByteArray> &to, const QByteArray &imapUrl)
{
    this->from = from;
    this->to = to;
    this->data = imapUrl;
    this->sendingMode = MODE_SMTP_BURL;
    this->isWaitingForPassword = true;
    emit progressMax(1);
    emit progress(0);
    emit connecting();
    if (!auth || !pass.isEmpty()) {
        sendContinueGotPassword();
        return;
    }
    emit passwordRequested(user, host);
}

SMTPFactory::SMTPFactory(const QString &host, quint16 port, bool encryptedConnect, bool TLS,
                         bool auth, const QString &user):
    m_host(host), m_port(port), m_encryptedConnect(encryptedConnect), m_TLS(TLS),
    m_auth(auth), m_user(user)
{
}

SMTPFactory::~SMTPFactory()
{
}

AbstractMSA *SMTPFactory::create(QObject *parent) const
{
    return new SMTP(parent, m_host, m_port, m_encryptedConnect, m_TLS, m_auth, m_user);
}

}
