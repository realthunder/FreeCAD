/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <vector>
#endif

#include <App/Application.h>
#include <App/Document.h>

#include "ShareDocument.h"

#include "Application.h"
#include "Document.h"
#include "MainWindow.h"
#include "Renderer/SceneServer.h"
#include "SceneServeSource.h"

using namespace Gui;

namespace
{

/// The first address another machine could actually reach — the
/// default the dialog offers for the URL host. Tunnels and reverse
/// proxies make this a suggestion, never an answer, which is why the
/// field is editable and remembered.
QString detectHost()
{
    for (const QHostAddress &a : QNetworkInterface::allAddresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
            return a.toString();
    }
    return QStringLiteral("localhost");
}

QString randomToken()
{
    // 64 bits of hex: short enough to live in a pasted URL, long
    // enough that guessing it is not how anyone gets in.
    return QStringLiteral("%1").arg(
        QRandomGenerator::global()->generate64(), 16, 16,
        QLatin1Char('0'));
}

QString formatDuration(qulonglong ms)
{
    qulonglong s = ms / 1000;
    if (s < 60)
        return QStringLiteral("%1s").arg(s);
    if (s < 3600)
        return QStringLiteral("%1:%2").arg(s / 60).arg(
            s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(
        (s % 3600) / 60, 2, 10, QLatin1Char('0')).arg(
        s % 60, 2, 10, QLatin1Char('0'));
}

ParameterGrp::handle shareParams()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/SceneShare");
}

/// The small always-on-top pill in the corner of the 3D area while
/// sharing is up: a red dot, "Sharing · N", clickable.
class ShareIndicator: public QWidget
{
public:
    std::function<void()> onClick;

    explicit ShareIndicator(QWidget *parent)
        : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);
        if (parent)
            parent->installEventFilter(this);
        setText(QString());
    }

    void setText(const QString &text)
    {
        label = text.isEmpty() ? tr("Sharing") : text;
        QFontMetrics fm(font());
        resize(fm.horizontalAdvance(label) + 38, fm.height() + 12);
        reposition();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(24, 24, 24, 200));
        p.drawRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
        p.setBrush(QColor(232, 64, 64));
        p.drawEllipse(QRectF(11.0, r.height() / 2.0 - 3.5, 8.0, 8.0));
        p.setPen(Qt::white);
        p.drawText(rect().adjusted(26, 0, -12, 0),
                   Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    void mousePressEvent(QMouseEvent *ev) override
    {
        if (ev->button() == Qt::LeftButton && onClick)
            onClick();
    }

    bool eventFilter(QObject *watched, QEvent *ev) override
    {
        if (watched == parentWidget() && ev->type() == QEvent::Resize)
            reposition();
        return QWidget::eventFilter(watched, ev);
    }

private:
    void reposition()
    {
        if (QWidget *parent = parentWidget()) {
            move(parent->width() - width() - 12, 10);
            raise();
        }
    }

    QString label;
};

/// The client panel: the share URL, the roster with per-client mode
/// and kick, and the stop-sharing button.
class SharePanel: public QDialog
{
public:
    std::function<void()> onStop;

    explicit SharePanel(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(tr("Sharing"));
        setAttribute(Qt::WA_DeleteOnClose, false);
        auto *layout = new QVBoxLayout(this);

        auto *urlRow = new QHBoxLayout;
        urlEdit = new QLineEdit(this);
        urlEdit->setReadOnly(true);
        auto *copyBtn = new QPushButton(tr("Copy"), this);
        connect(copyBtn, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(urlEdit->text());
        });
        urlRow->addWidget(new QLabel(tr("Link:"), this));
        urlRow->addWidget(urlEdit, 1);
        urlRow->addWidget(copyBtn);
        layout->addLayout(urlRow);

        tree = new QTreeWidget(this);
        tree->setColumnCount(6);
        tree->setHeaderLabels({tr("Client"), tr("Address"), tr("Document"),
                               tr("Connected"), tr("Access"), QString()});
        tree->setRootIsDecorated(false);
        tree->setSelectionMode(QAbstractItemView::NoSelection);
        tree->header()->setStretchLastSection(false);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        layout->addWidget(tree, 1);

        auto *bottom = new QHBoxLayout;
        auto *stopBtn = new QPushButton(tr("Stop sharing"), this);
        connect(stopBtn, &QPushButton::clicked, this, [this]() {
            if (onStop)
                onStop();
        });
        auto *closeBtn = new QPushButton(tr("Close"), this);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::hide);
        bottom->addWidget(stopBtn);
        bottom->addStretch(1);
        bottom->addWidget(closeBtn);
        layout->addLayout(bottom);
        resize(560, 300);
    }

    void refresh(const QString &url,
                 const std::vector<Render::SceneClientInfo> &clients)
    {
        urlEdit->setText(url);
        // Rebuild only when membership or a mode changed — a rebuild
        // every roster tick would yank the combo out from under the
        // pointer. Durations update in place.
        std::vector<std::pair<uint64_t, bool>> sig;
        sig.reserve(clients.size());
        for (const auto &c : clients)
            sig.emplace_back(c.id, c.viewOnly);
        if (sig == lastSig) {
            for (int i = 0; i < tree->topLevelItemCount()
                     && i < int(clients.size()); ++i)
                tree->topLevelItem(i)->setText(
                    3, formatDuration(clients[size_t(i)].connectedMs));
            return;
        }
        lastSig = std::move(sig);

        tree->clear();
        for (const auto &c : clients) {
            auto *item = new QTreeWidgetItem(tree);
            QString name = QString::fromUtf8(c.client.c_str());
            if (name.isEmpty())
                name = c.viewer ? tr("(unnamed)") : tr("(connection)");
            item->setText(0, name);
            item->setText(1, QString::fromUtf8(c.address.c_str()));
            item->setText(2, QString::fromUtf8(c.doc.c_str()));
            item->setText(3, formatDuration(c.connectedMs));

            auto *mode = new QComboBox(tree);
            mode->addItem(tr("Can edit"));
            mode->addItem(tr("View only"));
            mode->setCurrentIndex(c.viewOnly ? 1 : 0);
            const uint64_t id = c.id;
            connect(mode, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [id](int index) {
                        Render::SceneStreamServer::instance()
                            .setClientViewOnly(id, index == 1);
                    });
            tree->setItemWidget(item, 4, mode);

            auto *kick = new QPushButton(tr("Kick"), tree);
            connect(kick, &QPushButton::clicked, this, [id]() {
                Render::SceneStreamServer::instance().kickClient(id);
            });
            tree->setItemWidget(item, 5, kick);
        }
    }

private:
    QLineEdit *urlEdit = nullptr;
    QTreeWidget *tree = nullptr;
    std::vector<std::pair<uint64_t, bool>> lastSig;
};

}  // namespace

class ShareDocumentManager::Private
{
public:
    /// The documents this manager started serving; pruned as they
    /// close (the source unserves itself on document teardown).
    std::vector<App::Document *> docs;
    int port = 0;
    QString host;
    QString viewerPage;
    QString token;
    QPointer<ShareIndicator> indicator;
    QPointer<SharePanel> panel;
    QTimer timer;
    bool notifierInstalled = false;

    void prune()
    {
        docs.erase(std::remove_if(docs.begin(), docs.end(),
                                  [](App::Document *doc) {
                                      return !SceneServeSource::serving(doc);
                                  }),
                   docs.end());
    }

    /// The link a viewer opens. The viewer page is wherever
    /// fcviewer.html is hosted (the backend serves scenes, not pages);
    /// with none configured the query tail is shown alone, ready to
    /// paste after one.
    QString shareUrl() const
    {
        QString scene = QStringLiteral("http://%1:%2").arg(host).arg(port);
        QString query = QStringLiteral("?scene=%1")
            .arg(QString::fromUtf8(QUrl::toPercentEncoding(scene)));
        if (!docs.empty())
            query += QStringLiteral("&doc=%1").arg(
                QString::fromUtf8(QUrl::toPercentEncoding(
                    QString::fromUtf8(docs.front()->getName()))));
        if (!token.isEmpty())
            query += QStringLiteral("&token=%1").arg(token);
        return viewerPage.isEmpty() ? query : viewerPage + query;
    }
};

ShareDocumentManager &ShareDocumentManager::instance()
{
    static ShareDocumentManager manager;
    return manager;
}

ShareDocumentManager::ShareDocumentManager()
    : pimpl(new Private)
{
    pimpl->timer.setInterval(2000);
    QObject::connect(&pimpl->timer, &QTimer::timeout, &pimpl->timer,
                     [this]() { refreshUi(); });
}

ShareDocumentManager::~ShareDocumentManager()
{
    delete pimpl;
}

bool ShareDocumentManager::active() const
{
    pimpl->prune();
    return !pimpl->docs.empty()
        && Render::SceneStreamServer::instance().running();
}

void ShareDocumentManager::openShareDialog()
{
    if (active()) {
        showPanel();
        return;
    }

    Gui::Document *guiDoc = Application::Instance->activeDocument();
    if (!guiDoc || !guiDoc->getDocument()) {
        QMessageBox::warning(getMainWindow(), QObject::tr("Share document"),
                             QObject::tr("There is no active document to "
                                         "share."));
        return;
    }

    auto hGrp = shareParams();
    QDialog dlg(getMainWindow());
    dlg.setWindowTitle(QObject::tr("Share document"));
    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    auto *docLabel = new QLabel(
        QString::fromUtf8(guiDoc->getDocument()->Label.getValue()), &dlg);
    form->addRow(QObject::tr("Document:"), docLabel);

    auto *portSpin = new QSpinBox(&dlg);
    portSpin->setRange(1, 65535);
    portSpin->setValue(int(hGrp->GetInt("Port", 8210)));
    form->addRow(QObject::tr("Port:"), portSpin);

    auto *hostEdit = new QLineEdit(&dlg);
    QString detected = detectHost();
    QString savedHost = QString::fromUtf8(
        hGrp->GetASCII("ExternalHost", "").c_str());
    hostEdit->setText(savedHost.isEmpty() ? detected : savedHost);
    hostEdit->setToolTip(QObject::tr(
        "The address viewers reach this machine at. With a tunnel or "
        "reverse proxy this differs from the bind address."));
    form->addRow(QObject::tr("External address:"), hostEdit);

    auto *pageEdit = new QLineEdit(&dlg);
    pageEdit->setText(QString::fromUtf8(
        hGrp->GetASCII("ViewerPage", "").c_str()));
    pageEdit->setPlaceholderText(
        QStringLiteral("http://host/fcviewer.html"));
    pageEdit->setToolTip(QObject::tr(
        "Where the viewer page is hosted. Left empty, the link shows "
        "only the query to paste after one."));
    form->addRow(QObject::tr("Viewer page:"), pageEdit);

    auto *tokenRow = new QHBoxLayout;
    auto *tokenEdit = new QLineEdit(&dlg);
    tokenEdit->setText(randomToken());
    tokenEdit->setToolTip(QObject::tr(
        "Viewers need this token to connect; it rides in the link. "
        "Clear it to share without one."));
    auto *tokenBtn = new QPushButton(QObject::tr("New"), &dlg);
    QObject::connect(tokenBtn, &QPushButton::clicked, tokenEdit,
                     [tokenEdit]() { tokenEdit->setText(randomToken()); });
    tokenRow->addWidget(tokenEdit, 1);
    tokenRow->addWidget(tokenBtn);
    form->addRow(QObject::tr("Token:"), tokenRow);

    auto *urlPreview = new QLineEdit(&dlg);
    urlPreview->setReadOnly(true);
    form->addRow(QObject::tr("Link:"), urlPreview);

    auto updatePreview = [&]() {
        Private preview;
        preview.docs.push_back(guiDoc->getDocument());
        preview.port = portSpin->value();
        preview.host = hostEdit->text().trimmed();
        preview.viewerPage = pageEdit->text().trimmed();
        preview.token = tokenEdit->text().trimmed();
        urlPreview->setText(preview.shareUrl());
        // Not pruned on destruction: preview never served anything.
        preview.docs.clear();
    };
    QObject::connect(portSpin, qOverload<int>(&QSpinBox::valueChanged),
                     &dlg, updatePreview);
    QObject::connect(hostEdit, &QLineEdit::textChanged, &dlg, updatePreview);
    QObject::connect(pageEdit, &QLineEdit::textChanged, &dlg, updatePreview);
    QObject::connect(tokenEdit, &QLineEdit::textChanged, &dlg, updatePreview);
    updatePreview();

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(
        QObject::tr("Start sharing"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg,
                     &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const int port = portSpin->value();
    const QString host = hostEdit->text().trimmed();
    const QString viewerPage = pageEdit->text().trimmed();
    const QString token = tokenEdit->text().trimmed();
    hGrp->SetInt("Port", port);
    hGrp->SetASCII("ExternalHost", host.toUtf8().constData());
    hGrp->SetASCII("ViewerPage", viewerPage.toUtf8().constData());

    auto &server = Render::SceneStreamServer::instance();
    // The door first: the token must gate the very first request the
    // listener answers, not arrive after it is up.
    server.setToken(token.toUtf8().constData());
    SceneServeSource *source = SceneServeSource::serve(guiDoc, port);
    if (!source) {
        server.setToken(std::string());
        QMessageBox::critical(getMainWindow(), QObject::tr("Share document"),
                              QObject::tr("The document could not be served. "
                                          "Sharing needs a render engine "
                                          "that can publish scenes (see the "
                                          "report view for details)."));
        return;
    }
    if (!server.running()) {
        SceneServeSource::unserve(guiDoc);
        server.setToken(std::string());
        QMessageBox::critical(getMainWindow(), QObject::tr("Share document"),
                              QObject::tr("The scene server could not "
                                          "listen on port %1.").arg(port));
        return;
    }

    pimpl->docs.push_back(guiDoc->getDocument());
    pimpl->port = port;
    pimpl->host = host;
    pimpl->viewerPage = viewerPage;
    pimpl->token = token;

    if (!pimpl->notifierInstalled) {
        pimpl->notifierInstalled = true;
        // Server threads announce roster changes; repaint on the GUI
        // thread. The singleton outlives every connection.
        server.setClientsChangedNotifier([]() {
            QMetaObject::invokeMethod(qApp, []() {
                ShareDocumentManager::instance().refreshUi();
            }, Qt::QueuedConnection);
        });
    }

    if (!pimpl->indicator) {
        QWidget *parent = getMainWindow()
            ? static_cast<QWidget *>(getMainWindow()->getMdiArea())
            : nullptr;
        if (!parent)
            parent = getMainWindow();
        pimpl->indicator = new ShareIndicator(parent);
        pimpl->indicator->onClick = []() {
            ShareDocumentManager::instance().showPanel();
        };
    }
    pimpl->indicator->show();
    pimpl->timer.start();
    refreshUi();
}

void ShareDocumentManager::showPanel()
{
    if (!active())
        return;
    if (!pimpl->panel) {
        pimpl->panel = new SharePanel(getMainWindow());
        pimpl->panel->onStop = []() {
            ShareDocumentManager::instance().stopSharing();
        };
    }
    refreshUi();
    pimpl->panel->show();
    pimpl->panel->raise();
    pimpl->panel->activateWindow();
}

void ShareDocumentManager::stopSharing()
{
    auto &server = Render::SceneStreamServer::instance();
    for (App::Document *doc : pimpl->docs) {
        if (SceneServeSource *source = SceneServeSource::sourceFor(doc))
            SceneServeSource::unserve(source->document());
    }
    pimpl->docs.clear();
    server.stop();
    server.setToken(std::string());
    pimpl->timer.stop();
    if (pimpl->indicator)
        pimpl->indicator->hide();
    if (pimpl->panel)
        pimpl->panel->hide();
}

void ShareDocumentManager::refreshUi()
{
    if (pimpl->docs.empty())
        return;
    if (!active()) {
        // The last shared document closed underneath us (its source
        // unserves itself): fold the whole share down.
        stopSharing();
        return;
    }
    std::vector<Render::SceneClientInfo> clients;
    Render::SceneStreamServer::instance().clients(clients);
    if (pimpl->indicator) {
        pimpl->indicator->setText(
            QObject::tr("Sharing · %1").arg(clients.size()));
        pimpl->indicator->setToolTip(pimpl->shareUrl());
        pimpl->indicator->show();
    }
    if (pimpl->panel && pimpl->panel->isVisible())
        pimpl->panel->refresh(pimpl->shareUrl(), clients);
}
