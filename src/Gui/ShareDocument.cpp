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
#include <QRegularExpression>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
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
 * One remembered client, kept in user.cfg under
 * Preferences/SceneShare/Clients so a backend restart does not forget
 * who anybody is.
 *
 * The link's token is remembered with them (shareParams "Token"),
 * which is the point of the whole thing: sharing again reuses it, so
 * the URL already in someone's browser still opens. Nothing here
 * starts sharing by itself — persistence is memory, not autostart.
 */
struct ClientRecord
{
    QString name;
    QString address;
    bool viewOnly = false;
    /// Refused on sight: every connection matching this record is
    /// disconnected as it arrives. A door policy, not a lock — anyone
    /// holding the token can still knock, and will be shown out each
    /// time. The lock is a new token.
    bool banned = false;
};

/// The parameter subgroup name for a record. Readable in user.cfg, so
/// a name and address can be found by eye; the sanitizing is only what
/// the parameter tree cannot hold. Wildcards survive it — they are
/// what a rule is made of.
QString recordKey(const QString &name, const QString &address)
{
    QString key = name.isEmpty() ? address : name + QLatin1Char('@') + address;
    QString out;
    for (QChar c : key) {
        out += (c.isLetterOrNumber() || c == QLatin1Char('.')
                || c == QLatin1Char(':') || c == QLatin1Char('-')
                || c == QLatin1Char('@') || c == QLatin1Char('_')
                || c == QLatin1Char('*') || c == QLatin1Char('?'))
            ? c : QLatin1Char('_');
    }
    return out.left(80);
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

/// Whether \a value satisfies \a pattern, where the pattern may use
/// shell wildcards — `*` alone is everyone, `lei-*` is a family of
/// names, `192.168.1.*` a subnet.
bool patternMatch(const QString &pattern, const QString &value)
{
    if (!pattern.contains(QLatin1Char('*'))
            && !pattern.contains(QLatin1Char('?')))
        return pattern == value;
    if (pattern == QLatin1String("*"))
        return true;
    QRegularExpression re(
        QRegularExpression::wildcardToRegularExpression(pattern),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(value).hasMatch();
}

/// How specific a pattern is, for choosing between rules that both
/// match: a literal beats a partial wildcard beats `*`.
int specificity(const QString &pattern)
{
    if (pattern == QLatin1String("*"))
        return 0;
    int literal = 0;
    for (QChar c : pattern) {
        if (c != QLatin1Char('*') && c != QLatin1Char('?'))
            ++literal;
    }
    return (pattern.contains(QLatin1Char('*'))
            || pattern.contains(QLatin1Char('?')))
        ? 1 + literal : 4096 + literal;
}

std::vector<ClientRecord> loadRecords()
{
    std::vector<ClientRecord> out;
    auto clients = shareParams()->GetGroup("Clients");
    for (const auto &sub : clients->GetGroups()) {
        ClientRecord rec;
        rec.name = QString::fromUtf8(sub->GetASCII("Name", "").c_str());
        rec.address = QString::fromUtf8(sub->GetASCII("Address", "").c_str());
        rec.viewOnly = sub->GetBool("ViewOnly", false);
        rec.banned = sub->GetBool("Banned", false);
        if (!rec.name.isEmpty() || !rec.address.isEmpty())
            out.push_back(rec);
    }
    return out;
}

void saveRecord(const ClientRecord &rec)
{
    auto sub = shareParams()->GetGroup("Clients")->GetGroup(
        recordKey(rec.name, rec.address).toUtf8().constData());
    sub->SetASCII("Name", rec.name.toUtf8().constData());
    sub->SetASCII("Address", rec.address.toUtf8().constData());
    sub->SetBool("ViewOnly", rec.viewOnly);
    sub->SetBool("Banned", rec.banned);
}

void forgetRecord(const ClientRecord &rec)
{
    shareParams()->GetGroup("Clients")->RemoveGrp(
        recordKey(rec.name, rec.address).toUtf8().constData());
}

/*!
 * The record governing a client, or none.
 *
 * Every record is a pattern pair, so one entry can be a person
 * (`lei-phone` @ `203.0.113.7`), a family (`lei-*` @ `*`), or the
 * house rule (`*` @ `*` — anyone from anywhere). The most specific
 * match wins, which is what lets a blanket rule coexist with the
 * exceptions to it: ban `*`, then allow the two names you invited.
 *
 * Name and address are both patterns because neither is dependable
 * alone: an unnamed viewer has only its address, and a named one moves
 * between networks — a phone leaving wifi arrives from somewhere else
 * entirely, while the name is the part a person chose.
 */
const ClientRecord *matchRecord(const std::vector<ClientRecord> &records,
                                const QString &name, const QString &address)
{
    const ClientRecord *best = nullptr;
    int bestScore = -1;
    const QString addr = addressKey(address);
    for (const auto &rec : records) {
        if (!patternMatch(rec.name, name)
                || !patternMatch(rec.address, addr))
            continue;
        // The name outranks the address: it is what a person chose,
        // and the address is where they happen to be today.
        const int score = specificity(rec.name) * 8192
            + specificity(rec.address);
        if (score > bestScore) {
            bestScore = score;
            best = &rec;
        }
    }
    return best;
}

/// What a rule is, said once — the tooltip every widget that offers or
/// shows one carries, because the matching order is the part that is
/// not guessable from the fields.
QString ruleHelp()
{
    return QCoreApplication::translate("Gui::SharePanel",
        "<b>Rule</b> — a name and address pattern, with the access to "
        "give everyone matching both.<br><br>"
        "<code>*</code> matches anything and <code>?</code> one "
        "character, so <code>*</code> @ <code>*</code> is anyone from "
        "anywhere, <code>lei-*</code> @ <code>*</code> is a family of "
        "names, and <code>guest</code> @ <code>192.168.1.*</code> is one "
        "name on one network. Addresses are matched without the port.<br><br>"
        "The <b>most specific match wins</b> — a literal beats a partial "
        "wildcard beats <code>*</code>, and the name outranks the address. "
        "So a blanket rule and its exceptions live together: ban "
        "<code>*</code> and add the names you invited, or make "
        "<code>*</code> view-only and give named people editing.<br><br>"
        "A client a rule covers is not remembered separately, so the rule "
        "stays the one place that decides; changing a connected client's "
        "access records that client alone and leaves the rule alone.");
}

/// Whether this record is a rule rather than a remembered visitor —
/// it names a set, so no single client is "it".
bool isRule(const ClientRecord &rec)
{
    return rec.name.contains(QLatin1Char('*'))
        || rec.name.contains(QLatin1Char('?'))
        || rec.address.contains(QLatin1Char('*'))
        || rec.address.contains(QLatin1Char('?'));
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
            "Clients seen while sharing are remembered in user.cfg with "
            "their access, and greyed out here while they are away. A "
            "returning client is restored to the access it had."));
        tree->setRootIsDecorated(false);
        tree->setSelectionMode(QAbstractItemView::NoSelection);
        tree->header()->setStretchLastSection(false);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        layout->addWidget(tree, 1);

        auto *bottom = new QHBoxLayout;
        // A rule cannot be created by anyone connecting, so it needs a
        // way in of its own.
        auto *ruleBtn = new QPushButton(tr("Add rule…"), this);
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
                 const std::vector<ClientRecord> &records)
    {
        urlEdit->setText(url);
        // Rebuild only when membership or a mode changed — a rebuild
        // every roster tick would yank the combo out from under the
        // pointer. Durations update in place.
        std::vector<std::pair<uint64_t, bool>> sig;
        sig.reserve(clients.size() + records.size());
        for (const auto &c : clients)
            sig.emplace_back(c.id, c.viewOnly);
        for (const auto &rec : records) {
            sig.emplace_back(qHash(rec.name + rec.address) | (1ull << 32),
                             rec.viewOnly);
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
            const uint64_t id = c.id;
            // The mode is both applied and remembered: what the host
            // decides about someone should still be true when they come
            // back tomorrow.
            ClientRecord rec;
            rec.name = QString::fromUtf8(c.client.c_str());
            rec.address = addressKey(QString::fromUtf8(c.address.c_str()));
            if (const ClientRecord *known =
                    matchRecord(records, rec.name, rec.address)) {
                // Editing from a connected row writes a record for
                // *this* client, never back into a rule the whole room
                // shares — a rule is edited on its own row.
                if (!isRule(*known))
                    rec = *known;
                else
                    rec.viewOnly = known->viewOnly;
            }
            const bool isBanned = rec.banned;
            connect(mode, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [id, saved = rec](int index) mutable {
                        Render::SceneStreamServer::instance()
                            .setClientViewOnly(id, index == 1);
                        saved.viewOnly = index == 1;
                        saveRecord(saved);
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
            kick->setToolTip(tr("Disconnect now. The link still works, so "
                                "they can come back."));
            connect(kick, &QPushButton::clicked, this, [id]() {
                Render::SceneStreamServer::instance().kickClient(id);
            });
            auto *ban = new QPushButton(tr("Ban"), actions);
            ban->setToolTip(tr("Disconnect and refuse them from now on. "
                               "Lift it here; a new token is what locks "
                               "everyone else out."));
            connect(ban, &QPushButton::clicked, this,
                    [this, id, saved = rec]() mutable {
                saved.banned = true;
                saveRecord(saved);
                Render::SceneStreamServer::instance().kickClient(id);
                lastSig.clear();
                if (onChanged)
                    onChanged();
            });
            row->addWidget(kick);
            row->addWidget(ban);
            tree->setItemWidget(item, 5, actions);
            if (isBanned)
                item->setText(3, tr("banned"));
        }

        // Everyone remembered but not here, and the rules: greyed,
        // still editable — setting someone to view-only, or banning
        // them, before they arrive is the sensible way to hand out a
        // link, and a rule is only ever edited this way.
        for (const auto &rec : records) {
            bool here = false;
            for (const auto &c : clients) {
                if (!isRule(rec)
                        && QString::fromUtf8(c.client.c_str()) == rec.name
                        && addressKey(QString::fromUtf8(c.address.c_str()))
                               == rec.address)
                    here = true;
            }
            if (here)
                continue;

            auto *item = new QTreeWidgetItem(tree);
            item->setText(0, rec.name.isEmpty() ? tr("(unnamed)") : rec.name);
            item->setText(1, rec.address);
            item->setText(3, rec.banned ? tr("banned")
                              : (isRule(rec) ? tr("rule") : tr("away")));
            if (isRule(rec)) {
                // The row is the only place a rule is ever read, so it
                // carries the whole explanation.
                item->setToolTip(0, ruleHelp());
                item->setToolTip(1, ruleHelp());
            }
            for (int col = 0; col < 4; ++col)
                item->setForeground(col, QBrush(Qt::gray));

            auto *mode = new QComboBox(tree);
            mode->addItem(tr("Can edit"));
            mode->addItem(tr("View only"));
            mode->setCurrentIndex(rec.viewOnly ? 1 : 0);
            connect(mode, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [saved = rec](int index) mutable {
                        saved.viewOnly = index == 1;
                        saveRecord(saved);
                    });
            tree->setItemWidget(item, 4, mode);

            auto *actions = new QWidget(tree);
            auto *row = new QHBoxLayout(actions);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(4);
            auto *ban = new QPushButton(
                rec.banned ? tr("Unban") : tr("Ban"), actions);
            connect(ban, &QPushButton::clicked, this,
                    [this, saved = rec]() mutable {
                saved.banned = !saved.banned;
                saveRecord(saved);
                lastSig.clear();
                if (onChanged)
                    onChanged();
            });
            // Forgetting is not a ban: it drops what we know, so the
            // next visit is a stranger's — with whatever access a
            // stranger gets. Ban is the one that keeps someone out.
            auto *forget = new QPushButton(tr("Forget"), actions);
            forget->setToolTip(tr("Drop this record. They are neither "
                                  "banned nor restricted afterwards."));
            connect(forget, &QPushButton::clicked, this, [this, rec]() {
                forgetRecord(rec);
                lastSig.clear();   // force a rebuild on the next tick
                if (onChanged)
                    onChanged();
            });
            row->addWidget(ban);
            row->addWidget(forget);
            tree->setItemWidget(item, 5, actions);
        }
    }

    /// Something in the panel changed what is stored; the manager
    /// re-reads and redraws.
    std::function<void()> onChanged;

private:
    /// Ask for a rule and store it. Defaults to `*` @ `*` — the
    /// house rule — because that is the one worth reaching for: set
    /// everyone to view-only, or ban everyone and allow by name.
    void addRule()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Add rule"));
        auto *form = new QFormLayout(&dlg);
        auto *nameEdit = new QLineEdit(QStringLiteral("*"), &dlg);
        nameEdit->setToolTip(ruleHelp());
        auto *addrEdit = new QLineEdit(QStringLiteral("*"), &dlg);
        addrEdit->setToolTip(ruleHelp());
        auto *modeBox = new QComboBox(&dlg);
        modeBox->addItem(tr("Can edit"));
        modeBox->addItem(tr("View only"));
        modeBox->addItem(tr("Banned"));
        modeBox->setToolTip(ruleHelp());
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

        ClientRecord rec;
        rec.name = nameEdit->text().trimmed();
        rec.address = addrEdit->text().trimmed();
        if (rec.name.isEmpty())
            rec.name = QStringLiteral("*");
        if (rec.address.isEmpty())
            rec.address = QStringLiteral("*");
        rec.viewOnly = modeBox->currentIndex() == 1;
        rec.banned = modeBox->currentIndex() == 2;
        saveRecord(rec);
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
    /// Connections whose remembered access has already been applied,
    /// so the host flipping someone back is not undone on the next
    /// roster tick. Ids are never reused within a run.
    std::set<uint64_t> restored;
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

    auto &server = Render::SceneStreamServer::instance();
    server.setTrustProxy(proxyBox->isChecked());
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

    // Apply what is remembered about each connection, once — a ban
    // ends it, an access mode is restored. Once per connection id, so
    // the host overruling either afterwards is not undone on the next
    // tick, and the record only changes when the host changes it.
    const std::vector<ClientRecord> records = loadRecords();
    for (const auto &c : clients) {
        if (!pimpl->restored.insert(c.id).second)
            continue;
        const QString name = QString::fromUtf8(c.client.c_str());
        const QString address = QString::fromUtf8(c.address.c_str());
        const ClientRecord *known = matchRecord(records, name, address);
        if (!known) {
            // First sight, and no rule covers them: remember them, so
            // they can be given an access (or banned) while away. The
            // address is stored without its source port, which differs
            // on every visit.
            ClientRecord rec;
            rec.name = name;
            rec.address = addressKey(address);
            rec.viewOnly = c.viewOnly;
            saveRecord(rec);
            continue;
        }
        if (known->banned) {
            server.kickClient(c.id);
            continue;
        }
        if (known->viewOnly != c.viewOnly)
            server.setClientViewOnly(c.id, known->viewOnly);
    }
    // Ids of connections that have gone: keep the applied set from
    // growing for the life of the process.
    for (auto it = pimpl->restored.begin(); it != pimpl->restored.end();) {
        bool live = false;
        for (const auto &c : clients)
            live = live || c.id == *it;
        it = live ? std::next(it) : pimpl->restored.erase(it);
    }

    if (pimpl->indicator) {
        pimpl->indicator->setText(
            QObject::tr("Sharing · %1").arg(clients.size()));
        pimpl->indicator->setToolTip(pimpl->shareUrl());
        pimpl->indicator->show();
    }
    if (pimpl->panel && pimpl->panel->isVisible())
        pimpl->panel->refresh(pimpl->shareUrl(), clients, records);
}
