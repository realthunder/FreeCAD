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
#include <QCheckBox>
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
#include <QCoreApplication>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <map>
#include <set>
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

/*!
 * One grant, as stored in user.cfg under Preferences/SceneShare/Grants
 * (docs/ShareAccess.md §2): an invitation, and who may use it. This is
 * the **persistent** list — every grant ever issued, each with an
 * enabled flag; the **live** list the door actually checks is the
 * server's (SceneStreamServer::setGrants), seeded from the enabled
 * entries here when sharing starts and free to grow live-only rename
 * easings meanwhile. Nothing here starts sharing by itself —
 * persistence is memory, not autostart.
 */
struct ShareGrant
{
    QString token;     ///< invitation secret, exact; empty = none required
    QString identity;  ///< pattern on the verified identity (may be empty = `*`)
    QString name;      ///< pattern on the self-declared name
    QString address;   ///< pattern on the address, matched portless
    int access = 0;    ///< 0 = edit, 1 = view-only, 2 = banned
    /// A disabled grant admits nobody but is kept — the "ban = drop
    /// from live, keep the entry disabled" move, re-enabled later
    /// without reissuing a link.
    bool enabled = true;
    /// Server id when this row mirrors a live-only easing minted at
    /// runtime (never stored); 0 for persistent grants.
    uint64_t liveId = 0;
};

/// The same invitation, for finding a row's grant in the stored list —
/// what it grants (access, enabled) may be the very thing being edited.
bool sameGrant(const ShareGrant &a, const ShareGrant &b)
{
    return a.token == b.token && a.identity == b.identity
        && a.name == b.name && a.address == b.address;
}

/*!
 * The address a record is keyed by: the client's address without the
 * ephemeral source port.
 *
 * A direct connection reports `ip:port`, and the port is different on
 * every visit — recording it would mean recording something that can
 * never match again. A proxied client already reports a bare address
 * (the forwarded one), so this leaves it alone.
 */
QString addressKey(const QString &address)
{
    int colon = address.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0)
        return address;
    const QString tail = address.mid(colon + 1);
    bool digits = !tail.isEmpty();
    for (QChar c : tail)
        digits = digits && c.isDigit();
    // Only a v4 address carries its port this way; an IPv6 address is
    // all colons and must be left whole.
    if (!digits || address.count(QLatin1Char(':')) != 1)
        return address;
    return address.left(colon);
}

/// The stored grants, in order. Migrates the pre-grant model once
/// (docs/ShareAccess.md §1): the single shared token becomes the
/// house invitation (`*` @ `*`, can edit — exactly what that token
/// meant), and every remembered client/rule record becomes a grant on
/// that token with the access it had. The old `Clients` group is left
/// in place but never read again.
std::vector<ShareGrant> loadGrants()
{
    auto hGrp = shareParams();
    auto grants = hGrp->GetGroup("Grants");
    std::vector<ShareGrant> out;
    for (const auto &sub : grants->GetGroups()) {
        ShareGrant g;
        g.token = QString::fromUtf8(sub->GetASCII("Token", "").c_str());
        g.identity = QString::fromUtf8(sub->GetASCII("Identity", "").c_str());
        g.name = QString::fromUtf8(sub->GetASCII("Name", "*").c_str());
        g.address = QString::fromUtf8(sub->GetASCII("Address", "*").c_str());
        g.access = int(sub->GetInt("Access", 0));
        g.enabled = sub->GetBool("Enabled", true);
        out.push_back(g);
    }
    if (!out.empty() || hGrp->GetBool("GrantsMigrated", false))
        return out;

    const QString token = QString::fromUtf8(
        hGrp->GetASCII("Token", "").c_str());
    ShareGrant house;
    house.token = token;
    house.name = QStringLiteral("*");
    house.address = QStringLiteral("*");
    out.push_back(house);
    auto clients = hGrp->GetGroup("Clients");
    for (const auto &sub : clients->GetGroups()) {
        ShareGrant g;
        g.token = token;
        g.name = QString::fromUtf8(sub->GetASCII("Name", "").c_str());
        g.address = QString::fromUtf8(sub->GetASCII("Address", "").c_str());
        if (g.name.isEmpty() && g.address.isEmpty())
            continue;
        if (g.name.isEmpty())
            g.name = QStringLiteral("*");
        if (g.address.isEmpty())
            g.address = QStringLiteral("*");
        g.access = sub->GetBool("Banned", false) ? 2
            : sub->GetBool("ViewOnly", false) ? 1 : 0;
        out.push_back(g);
    }
    hGrp->SetBool("GrantsMigrated", true);
    return out;
}

/// Rewrite the stored grant list. Live-only easings (liveId) are the
/// server's, not ours — "Keep" copies one into a stored grant first.
void saveGrants(const std::vector<ShareGrant> &list)
{
    auto hGrp = shareParams();
    hGrp->RemoveGrp("Grants");
    auto grants = hGrp->GetGroup("Grants");
    int n = 0;
    for (const auto &g : list) {
        if (g.liveId)
            continue;
        auto sub = grants->GetGroup(
            QStringLiteral("G%1").arg(n++).toUtf8().constData());
        sub->SetASCII("Token", g.token.toUtf8().constData());
        sub->SetASCII("Identity", g.identity.toUtf8().constData());
        sub->SetASCII("Name", g.name.toUtf8().constData());
        sub->SetASCII("Address", g.address.toUtf8().constData());
        sub->SetInt("Access", g.access);
        sub->SetBool("Enabled", g.enabled);
    }
}

Render::SceneGrant toSceneGrant(const ShareGrant &g)
{
    Render::SceneGrant out;
    out.token = g.token.toUtf8().constData();
    out.identity = g.identity.toUtf8().constData();
    out.client = g.name.toUtf8().constData();
    out.address = g.address.toUtf8().constData();
    out.access = g.access;
    out.id = g.liveId;
    out.liveOnly = g.liveId != 0;
    return out;
}

/// Rebuild the server's live list — the door — from the enabled stored
/// grants, keeping whatever live-only easings the server minted since
/// sharing started. The server re-judges every connection against the
/// result, which is where a ban or downgrade actually takes effect.
void pushGrants(const std::vector<ShareGrant> &stored)
{
    auto &server = Render::SceneStreamServer::instance();
    std::vector<Render::SceneGrant> live;
    for (const auto &g : stored) {
        if (g.enabled && !g.liveId)
            live.push_back(toSceneGrant(g));
    }
    for (const auto &g : server.grants()) {
        if (g.liveOnly)
            live.push_back(g);
    }
    server.setGrants(live);
}

/// What a rule is, said once — the tooltip every widget that offers or
/// shows one carries, because the matching order is the part that is
/// not guessable from the fields.
QString ruleHelp()
{
    return QCoreApplication::translate("Gui::SharePanel",
        "<b>Grant</b> — an invitation, and who may use it. A connection "
        "must match one to get in at all: its token exactly (an empty "
        "token in the grant means none is required), and its verified "
        "identity, name and address against the patterns.<br><br>"
        "<code>*</code> matches anything and <code>?</code> one "
        "character, so <code>*</code> @ <code>*</code> is anyone from "
        "anywhere, <code>lei-*</code> a family of names, "
        "<code>*@example.com</code> everyone the sign-in door verified "
        "under that domain. Addresses are matched without the port.<br><br>"
        "The <b>most specific match wins</b> — a literal beats a partial "
        "wildcard beats <code>*</code>; identity outranks name outranks "
        "address. So a blanket grant and its exceptions live together: "
        "make <code>*</code> view-only and give named people editing, or "
        "ban one name while the house invitation stands. No match at all "
        "is refused at the door, before any scene bytes.<br><br>"
        "A <b>disabled</b> grant admits nobody but is kept, so it can be "
        "re-enabled without reissuing a link. A grant marked <i>this "
        "session</i> was minted live for a renamed client and dies with "
        "the process — Keep it to write it down.");
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
        tree->setToolTip(tr(
            "Connected clients, then the grants — the invitations the "
            "door checks. A connection must match a grant to get in; "
            "the most specific match decides its access."));
        tree->setRootIsDecorated(false);
        tree->setSelectionMode(QAbstractItemView::NoSelection);
        tree->header()->setStretchLastSection(false);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        layout->addWidget(tree, 1);

        auto *bottom = new QHBoxLayout;
        // A grant cannot be created by anyone connecting, so it needs
        // a way in of its own.
        auto *ruleBtn = new QPushButton(tr("Add grant…"), this);
        ruleBtn->setToolTip(ruleHelp());
        connect(ruleBtn, &QPushButton::clicked, this, [this]() { addRule(); });
        auto *stopBtn = new QPushButton(tr("Stop sharing"), this);
        connect(stopBtn, &QPushButton::clicked, this, [this]() {
            if (onStop)
                onStop();
        });
        auto *closeBtn = new QPushButton(tr("Close"), this);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::hide);
        bottom->addWidget(stopBtn);
        bottom->addWidget(ruleBtn);
        bottom->addStretch(1);
        bottom->addWidget(closeBtn);
        layout->addLayout(bottom);
        resize(560, 300);
    }

    void refresh(const QString &url,
                 const std::vector<Render::SceneClientInfo> &clients,
                 const std::vector<ShareGrant> &grants)
    {
        urlEdit->setText(url);
        // Rebuild only when membership, a name, an access or the grant
        // list changed — a rebuild every roster tick would yank the
        // combo out from under the pointer. Durations update in place.
        std::vector<std::pair<uint64_t, bool>> sig;
        sig.reserve(clients.size() + grants.size());
        for (const auto &c : clients) {
            // The name is part of the signature: a hello lands after
            // the row was first built, and the row must follow it.
            sig.emplace_back(c.id
                ^ (uint64_t(qHash(QString::fromUtf8(c.client.c_str())))
                   << 20), c.viewOnly);
        }
        for (const auto &g : grants) {
            sig.emplace_back((uint64_t(qHash(g.token + g.identity + g.name
                                             + g.address)) | (1ull << 32))
                                 + uint64_t(g.access) + (g.liveId << 33),
                             g.enabled);
        }
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
            // What the front door verified outranks what the client
            // typed (docs/ShareAccess.md §4): the identity is who this
            // is, the label just what they call themselves.
            const QString identity = QString::fromUtf8(c.identity.c_str());
            if (!identity.isEmpty()) {
                if (c.client.empty() || identity == name)
                    name = identity;
                else
                    name = identity + QStringLiteral(" (") + name
                         + QLatin1Char(')');
                item->setToolTip(0, tr("Signed in through the sharing "
                                       "front door"));
            }
            item->setText(0, name);
            item->setText(1, QString::fromUtf8(c.address.c_str()));
            // A proxied row shows where the client is; the tooltip says
            // what it came through, so a surprising address can still
            // be traced back to a connection.
            if (c.proxied)
                item->setToolTip(1, tr("via %1")
                    .arg(QString::fromUtf8(c.peer.c_str())));
            item->setText(2, QString::fromUtf8(c.doc.c_str()));
            item->setText(3, formatDuration(c.connectedMs));

            auto *mode = new QComboBox(tree);
            mode->addItem(tr("Can edit"));
            mode->addItem(tr("View only"));
            mode->setCurrentIndex(c.viewOnly ? 1 : 0);
            mode->setToolTip(tr(
                "This connection alone, for this session. A durable "
                "decision is a grant — the rows below."));
            const uint64_t id = c.id;
            connect(mode, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [id](int index) {
                        Render::SceneStreamServer::instance()
                            .setClientViewOnly(id, index == 1);
                    });
            tree->setItemWidget(item, 4, mode);

            // Two ways to end a session, because they mean different
            // things: showing someone out of this one, and not letting
            // them back in.
            auto *actions = new QWidget(tree);
            auto *row = new QHBoxLayout(actions);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(4);
            auto *kick = new QPushButton(tr("Kick"), actions);
            kick->setToolTip(tr("Disconnect now. The invitation still "
                                "works, so they can come back."));
            connect(kick, &QPushButton::clicked, this, [id]() {
                Render::SceneStreamServer::instance().kickClient(id);
            });
            auto *ban = new QPushButton(tr("Ban"), actions);
            ban->setToolTip(tr("Add a banned grant for them — refused at "
                               "the door from now on, whatever invitation "
                               "they hold. Lift it on the grant's row."));
            const QString banIdentity =
                QString::fromUtf8(c.identity.c_str());
            const QString banName = QString::fromUtf8(c.client.c_str());
            const QString banAddr =
                addressKey(QString::fromUtf8(c.address.c_str()));
            connect(ban, &QPushButton::clicked, this,
                    [this, banIdentity, banName, banAddr]() {
                // Ban by what is most theirs: the verified identity
                // when the front door asserted one (it survives any
                // rename or move), else name and address together.
                ShareGrant g;
                g.access = 2;
                if (!banIdentity.isEmpty()) {
                    g.identity = banIdentity;
                    g.name = g.address = QStringLiteral("*");
                }
                else {
                    g.name = banName.isEmpty() ? QStringLiteral("*")
                                               : banName;
                    g.address = banAddr.isEmpty() ? QStringLiteral("*")
                                                  : banAddr;
                }
                auto list = loadGrants();
                list.push_back(g);
                saveGrants(list);
                pushGrants(list);   // the door re-judges; they are out
                lastSig.clear();
                if (onChanged)
                    onChanged();
            });
            row->addWidget(kick);
            row->addWidget(ban);
            tree->setItemWidget(item, 5, actions);
        }

        // The grants: every invitation written down (greyed while it
        // is only paperwork), plus the live-only easings the server
        // minted this session. Editing here is editing the door.
        for (const auto &g : grants) {
            auto *item = new QTreeWidgetItem(tree);
            QString who = g.name.isEmpty() ? QStringLiteral("*") : g.name;
            if (!g.identity.isEmpty()
                    && g.identity != QLatin1String("*")) {
                who = (g.name.isEmpty() || g.name == QLatin1String("*"))
                    ? g.identity
                    : g.identity + QStringLiteral(" (") + g.name
                        + QLatin1Char(')');
            }
            item->setText(0, who);
            item->setText(1, g.address.isEmpty() ? QStringLiteral("*")
                                                 : g.address);
            item->setText(3, g.liveId ? tr("this session")
                              : g.enabled ? tr("grant") : tr("off"));
            item->setToolTip(0, ruleHelp());
            item->setToolTip(1, ruleHelp());
            item->setToolTip(3, g.token.isEmpty()
                ? tr("No token required — matched on identity, name "
                     "and address alone.")
                : tr("Invite token: %1").arg(g.token));
            for (int col = 0; col < 4; ++col)
                item->setForeground(col, QBrush(g.enabled ? Qt::gray
                                                          : Qt::darkGray));

            auto *actions = new QWidget(tree);
            auto *row = new QHBoxLayout(actions);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(4);
            if (g.liveId) {
                // An easing is the server's: keep it (write it down)
                // or drop it — not edited in place.
                item->setText(4, g.access == 1 ? tr("View only")
                                               : tr("Can edit"));
                auto *keep = new QPushButton(tr("Keep"), actions);
                keep->setToolTip(tr(
                    "Write this session-only easing into the stored "
                    "grants, so it survives a restart."));
                connect(keep, &QPushButton::clicked, this,
                        [this, saved = g]() {
                    auto list = loadGrants();
                    ShareGrant copy = saved;
                    copy.liveId = 0;
                    list.push_back(copy);
                    saveGrants(list);
                    // Stored copy first, then retire the easing — in
                    // this order, so the client it covers is never
                    // between grants when the door re-judges.
                    pushGrants(list);
                    Render::SceneStreamServer::instance().removeGrant(
                        saved.liveId);
                    lastSig.clear();
                    if (onChanged)
                        onChanged();
                });
                auto *drop = new QPushButton(tr("Drop"), actions);
                drop->setToolTip(tr(
                    "Delete the easing now. The renamed client is "
                    "re-judged and may be refused."));
                connect(drop, &QPushButton::clicked, this,
                        [this, liveId = g.liveId]() {
                    Render::SceneStreamServer::instance().removeGrant(
                        liveId);
                    lastSig.clear();
                    if (onChanged)
                        onChanged();
                });
                row->addWidget(keep);
                row->addWidget(drop);
            }
            else {
                auto *mode = new QComboBox(tree);
                mode->addItem(tr("Can edit"));
                mode->addItem(tr("View only"));
                mode->addItem(tr("Banned"));
                mode->setCurrentIndex(g.access);
                mode->setToolTip(ruleHelp());
                connect(mode,
                        qOverload<int>(&QComboBox::currentIndexChanged),
                        this, [this, saved = g](int index) {
                    auto list = loadGrants();
                    for (auto &e : list) {
                        if (sameGrant(e, saved)) {
                            e.access = index;
                            break;
                        }
                    }
                    saveGrants(list);
                    pushGrants(list);
                    lastSig.clear();
                    if (onChanged)
                        onChanged();
                });
                tree->setItemWidget(item, 4, mode);

                auto *toggle = new QPushButton(
                    g.enabled ? tr("Disable") : tr("Enable"), actions);
                toggle->setToolTip(tr(
                    "A disabled grant admits nobody but is kept — "
                    "re-enable it later without reissuing a link."));
                connect(toggle, &QPushButton::clicked, this,
                        [this, saved = g]() {
                    auto list = loadGrants();
                    for (auto &e : list) {
                        if (sameGrant(e, saved)) {
                            e.enabled = !e.enabled;
                            break;
                        }
                    }
                    saveGrants(list);
                    pushGrants(list);
                    lastSig.clear();
                    if (onChanged)
                        onChanged();
                });
                // Forgetting is not disabling: it drops the grant for
                // good. Disable is the reversible one.
                auto *forget = new QPushButton(tr("Forget"), actions);
                forget->setToolTip(tr(
                    "Drop this grant entirely. Disable is the "
                    "reversible one."));
                connect(forget, &QPushButton::clicked, this,
                        [this, saved = g]() {
                    auto list = loadGrants();
                    list.erase(std::remove_if(list.begin(), list.end(),
                                              [&saved](const ShareGrant &e) {
                                                  return sameGrant(e, saved);
                                              }),
                               list.end());
                    saveGrants(list);
                    pushGrants(list);
                    lastSig.clear();
                    if (onChanged)
                        onChanged();
                });
                row->addWidget(toggle);
                row->addWidget(forget);
            }
            tree->setItemWidget(item, 5, actions);
        }
    }

    /// Something in the panel changed what is stored; the manager
    /// re-reads and redraws.
    std::function<void()> onChanged;

private:
    /// Ask for a grant and store it. Defaults to the house shape —
    /// today's token, anyone from anywhere — because that is the one
    /// worth narrowing: set everyone to view-only, ban a name, or key
    /// an invitation on a verified identity.
    void addRule()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Add grant"));
        auto *form = new QFormLayout(&dlg);
        auto *tokenEdit = new QLineEdit(QString::fromUtf8(
            shareParams()->GetASCII("Token", "").c_str()), &dlg);
        tokenEdit->setToolTip(tr(
            "The exact token this grant is for — the invitation itself. "
            "Defaults to the share's current token; clear it to match "
            "any (an identity-keyed grant behind a sign-in front door "
            "needs no token)."));
        auto *idEdit = new QLineEdit(QStringLiteral("*"), &dlg);
        idEdit->setToolTip(ruleHelp());
        auto *nameEdit = new QLineEdit(QStringLiteral("*"), &dlg);
        nameEdit->setToolTip(ruleHelp());
        auto *addrEdit = new QLineEdit(QStringLiteral("*"), &dlg);
        addrEdit->setToolTip(ruleHelp());
        auto *modeBox = new QComboBox(&dlg);
        modeBox->addItem(tr("Can edit"));
        modeBox->addItem(tr("View only"));
        modeBox->addItem(tr("Banned"));
        modeBox->setToolTip(ruleHelp());
        form->addRow(tr("Token:"), tokenEdit);
        form->addRow(tr("Identity:"), idEdit);
        form->addRow(tr("Name:"), nameEdit);
        form->addRow(tr("Address:"), addrEdit);
        form->addRow(tr("Access:"), modeBox);
        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(buttons);
        if (dlg.exec() != QDialog::Accepted)
            return;

        ShareGrant g;
        g.token = tokenEdit->text().trimmed();
        g.identity = idEdit->text().trimmed();
        g.name = nameEdit->text().trimmed();
        g.address = addrEdit->text().trimmed();
        if (g.identity.isEmpty())
            g.identity = QStringLiteral("*");
        if (g.name.isEmpty())
            g.name = QStringLiteral("*");
        if (g.address.isEmpty())
            g.address = QStringLiteral("*");
        g.access = modeBox->currentIndex();
        auto list = loadGrants();
        list.push_back(g);
        saveGrants(list);
        pushGrants(list);
        lastSig.clear();
        if (onChanged)
            onChanged();
    }

public:

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
        // The address may carry its own port — behind a reverse proxy
        // the public port is the proxy's, not the one we bind — and
        // its own scheme, for a proxy that terminates TLS. Only a bare
        // host gets the serving port appended.
        QString scene = host;
        if (!scene.contains(QLatin1String("://")))
            scene = QStringLiteral("http://") + scene;
        QString hostPart = scene.section(QLatin1String("://"), 1);
        if (!hostPart.contains(QLatin1Char(':')))
            scene += QStringLiteral(":%1").arg(port);
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
        "reverse proxy this differs from the bind address — give it a "
        "port (host:port) or a scheme (https://host) and that is used "
        "verbatim, otherwise the serving port is appended."));
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
    // The token this machine last shared with, so the links already in
    // people's browsers still open after a restart. New mints a fresh
    // one, which is also how everyone currently holding a link is shut
    // out.
    QString savedToken = QString::fromUtf8(
        hGrp->GetASCII("Token", "").c_str());
    tokenEdit->setText(savedToken.isEmpty() ? randomToken() : savedToken);
    tokenEdit->setToolTip(QObject::tr(
        "Viewers need this token to connect; it rides in the link. It is "
        "remembered, so sharing again keeps existing links working — "
        "press New to invalidate them. Clear it to share without one."));
    auto *tokenBtn = new QPushButton(QObject::tr("New"), &dlg);
    QObject::connect(tokenBtn, &QPushButton::clicked, tokenEdit,
                     [tokenEdit]() { tokenEdit->setText(randomToken()); });
    tokenRow->addWidget(tokenEdit, 1);
    tokenRow->addWidget(tokenBtn);
    form->addRow(QObject::tr("Token:"), tokenRow);

    auto *proxyBox = new QCheckBox(
        QObject::tr("Behind a reverse proxy"), &dlg);
    proxyBox->setChecked(hGrp->GetBool("TrustProxy", false));
    proxyBox->setToolTip(QObject::tr(
        "Take the client address from the proxy's X-Forwarded-For "
        "header. Only believed for connections arriving from this "
        "machine, which is what a local proxy or tunnel looks like — "
        "a plain ssh tunnel cannot carry the address by itself."));
    form->addRow(QString(), proxyBox);

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
    hGrp->SetBool("TrustProxy", proxyBox->isChecked());
    // Remembered so the next share reuses it and the links people
    // already hold keep working across a restart. Sharing is never
    // started by this — only the token survives, not the session.
    hGrp->SetASCII("Token", token.toUtf8().constData());

    // The stored grants are the door (docs/ShareAccess.md §2), and the
    // dialog's token is the house invitation — the `*` @ `*` grant a
    // plain link mints from — so point that grant at today's token
    // rather than grow a second one. Editing the token here is what
    // "New" is for: the old links stop matching anything.
    std::vector<ShareGrant> grants = loadGrants();
    bool house = false;
    for (auto &g : grants) {
        const bool anyName = g.name.isEmpty()
            || g.name == QLatin1String("*");
        const bool anyAddr = g.address.isEmpty()
            || g.address == QLatin1String("*");
        const bool anyId = g.identity.isEmpty()
            || g.identity == QLatin1String("*");
        if (anyName && anyAddr && anyId && g.access != 2) {
            g.token = token;
            house = true;
            break;
        }
    }
    if (!house) {
        ShareGrant g;
        g.token = token;
        g.name = g.address = QStringLiteral("*");
        grants.insert(grants.begin(), g);
    }
    saveGrants(grants);

    auto &server = Render::SceneStreamServer::instance();
    server.setTrustProxy(proxyBox->isChecked());
    // The door first: it must gate the very first request the
    // listener answers, not arrive after it is up. The token is still
    // set for the link preview and as what the hello vocabulary
    // presents; the grants are what judge it.
    server.setToken(token.toUtf8().constData());
    pushGrants(grants);
    SceneServeSource *source = SceneServeSource::serve(guiDoc, port);
    if (!source) {
        server.setToken(std::string());
        server.setGrants({});
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
        server.setGrants({});
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
        pimpl->panel->onChanged = []() {
            ShareDocumentManager::instance().refreshUi();
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
    // The live list dies with the share — easings and all; the next
    // start seeds a fresh one from what is written down.
    server.setGrants({});
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
    auto &server = Render::SceneStreamServer::instance();
    std::vector<Render::SceneClientInfo> clients;
    server.clients(clients);
    // No enforcement here: the door judges (docs/ShareAccess.md §2).
    // What used to be a roster-poll eviction — a banned client
    // connecting, registering, and being shown out a tick later — is
    // now a refusal before any scene bytes, in the server.

    if (pimpl->indicator) {
        pimpl->indicator->setText(
            QObject::tr("Sharing · %1").arg(clients.size()));
        pimpl->indicator->setToolTip(pimpl->shareUrl());
        pimpl->indicator->show();
    }
    if (pimpl->panel && pimpl->panel->isVisible()) {
        // The panel shows the stored grants plus whatever live-only
        // easings the server minted this session, marked apart.
        std::vector<ShareGrant> grants = loadGrants();
        for (const auto &g : server.grants()) {
            if (!g.liveOnly)
                continue;
            ShareGrant e;
            e.token = QString::fromUtf8(g.token.c_str());
            e.identity = QString::fromUtf8(g.identity.c_str());
            e.name = QString::fromUtf8(g.client.c_str());
            e.address = QString::fromUtf8(g.address.c_str());
            e.access = g.access;
            e.liveId = g.id;
            grants.push_back(e);
        }
        pimpl->panel->refresh(pimpl->shareUrl(), clients, grants);
    }
}
