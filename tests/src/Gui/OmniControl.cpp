// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ParamRegistry.h>
#include <App/PropertyStandard.h>

#include "Gui/OmniControl.h"
#include "Gui/SceneControl.h"
#include <src/App/InitApplication.h>

using App::ParamInfo;
using App::ParamRegistry;

// The omni search ops of the control channel (docs/OmniSearch.md sec 6),
// driven through handleSceneControlRequest() as the server would drive
// them, without a socket and without a Gui::Application -- so the
// command ops are only checked to answer NoGui; the parameter catalog,
// its versioned delta, the parameter actions, the object listing and
// the resolve are exercised in full.
class testOmniControl: public QObject
{
    Q_OBJECT

public:
    testOmniControl()
    {
        tests::initApplication();
    }

private Q_SLOTS:

    void init()
    {
        doc = App::GetApplication().newDocument("OmniControlTest");
        auto a = doc->addObject("App::DocumentObjectGroup", "GroupA");
        a->Label.setValue("First group");
        auto b = doc->addObject("App::DocumentObjectGroup", "GroupB");
        b->Label.setValue("Second group");
        auto part = doc->addObject("App::Part", "Part");
        static_cast<App::PropertyLinkList*>(part->getPropertyByName("Group"))
            ->setValues({b});
        doc->recompute();
    }

    void cleanup()
    {
        App::GetApplication().closeDocument(doc->getName());
        doc = nullptr;
    }

    QJsonObject ask(const QJsonObject &req, bool viewOnly = false)
    {
        std::string json = QJsonDocument(req).toJson(QJsonDocument::Compact).toStdString();
        std::string answer = Gui::handleSceneControlRequest(json, doc->getName(), viewOnly);
        lastBytes = answer.size();
        return QJsonDocument::fromJson(QByteArray::fromStdString(answer)).object();
    }

    QJsonObject op(const char *name, std::initializer_list<std::pair<QString, QJsonValue>> fields = {})
    {
        QJsonObject req;
        req[QLatin1String("id")] = ++nextId;
        req[QLatin1String("op")] = QString::fromLatin1(name);
        for (const auto &f : fields)
            req[f.first] = f.second;
        return req;
    }

    void test_paramCatalog()  // NOLINT
    {
        auto reply = ask(op("omni.catalog", {{"list", "params"}}));
        QVERIFY(reply.value("ok").toBool());
        QCOMPARE(reply.value("id").toInt(), nextId);
        QCOMPARE(reply.value("list").toString(), QStringLiteral("params"));
        QVERIFY(reply.value("full").toBool());
        const QString session = reply.value("session").toString();
        QVERIFY(!session.isEmpty());
        QCOMPARE(session.toStdString(), Gui::OmniControl::sessionId());
        const double version = reply.value("version").toDouble();
        QVERIFY(version >= 1);
        const QJsonArray add = reply.value("add").toArray();
        QCOMPARE(size_t(add.size()), ParamRegistry::instance().entries().size());
        QVERIFY(reply.value("remove").toArray().isEmpty());
        qDebug("params catalog: %d rows, %zu bytes", add.size(), lastBytes);

        // A row carries what the desktop row shows and what its editor needs
        bool found = false;
        for (const auto &v : add) {
            auto row = v.toObject();
            if (row.value("name").toString() != QLatin1String("App::DocumentParams::CheckExtension"))
                continue;
            found = true;
            QCOMPARE(row.value("key").toString(),
                     QStringLiteral("User parameter:BaseApp/Preferences/Document/CheckExtension"));
            QCOMPARE(row.value("path").toString(), QStringLiteral("Preferences/Document/CheckExtension"));
            QCOMPARE(row.value("entry").toString(), QStringLiteral("CheckExtension"));
            QCOMPARE(row.value("type").toString(), QStringLiteral("Bool"));
            QVERIFY(!row.value("default").toString().isEmpty());
        }
        QVERIFY(found);

        // Current: nothing to add
        reply = ask(op("omni.catalog", {{"list", "params"}, {"session", session}, {"version", version}}));
        QVERIFY(!reply.value("full").toBool());
        QCOMPARE(reply.value("version").toDouble(), version);
        QVERIFY(reply.value("add").toArray().isEmpty());
        QVERIFY(reply.value("remove").toArray().isEmpty());
        QVERIFY(lastBytes < 200);

        // Another run's version means nothing: the whole list again
        reply = ask(op("omni.catalog", {{"list", "params"}, {"session", "elsewhere"}, {"version", version}}));
        QVERIFY(reply.value("full").toBool());
        QCOMPARE(size_t(reply.value("add").toArray().size()), ParamRegistry::instance().entries().size());

        // A library registering more parameters is a new version, and a
        // viewer on the old one gets exactly the new rows
        std::vector<ParamInfo> infos;
        infos.emplace_back("Test", "OmniControlParams", "User parameter:BaseApp/Preferences/OmniControlTest",
                           "Extra", "Extra", ParamInfo::Int, 7);
        infos.back().setTitle("Extra parameter").setProxy("SpinBox").setRange(0, 10, 1);
        ParamRegistry::instance().add(std::move(infos));
        reply = ask(op("omni.catalog", {{"list", "params"}, {"session", session}, {"version", version}}));
        QVERIFY(!reply.value("full").toBool());
        QCOMPARE(reply.value("version").toDouble(), version + 1);
        QCOMPARE(reply.value("add").toArray().size(), 1);
        auto row = reply.value("add").toArray().first().toObject();
        QCOMPARE(row.value("key").toString(),
                 QStringLiteral("User parameter:BaseApp/Preferences/OmniControlTest/Extra"));
        QCOMPARE(row.value("proxy").toString(), QStringLiteral("SpinBox"));
        QCOMPARE(row.value("min").toDouble(), 0.0);
        QCOMPARE(row.value("max").toDouble(), 10.0);
        QCOMPARE(row.value("default").toString(), QStringLiteral("7"));
        QVERIFY(reply.value("remove").toArray().isEmpty());

        QCOMPARE(double(Gui::OmniControl::catalogVersion(QStringLiteral("params"))), version + 1);
        reply = ask(op("omni.catalog", {{"list", "nothing"}}));
        QVERIFY(!reply.value("ok").toBool());
        QCOMPARE(reply.value("code").toString(), QStringLiteral("UnknownList"));
    }

    void test_paramActions()  // NOLINT
    {
        const QString key = QStringLiteral("User parameter:BaseApp/Preferences/Document/CheckExtension");
        auto info = ParamRegistry::instance().find("User parameter:BaseApp/Preferences/Document",
                                                   "CheckExtension");
        QVERIFY(info);
        ParamRegistry::instance().reset(*info);
        const std::string original = ParamRegistry::instance().getValue(*info);

        auto reply = ask(op("param.get", {{"key", key}}));
        QVERIFY(reply.value("ok").toBool());
        QCOMPARE(reply.value("value").toString().toStdString(), original);
        QVERIFY(!reply.value("set").toBool());

        const QString flipped = original == "true" ? QStringLiteral("false") : QStringLiteral("true");
        reply = ask(op("param.set", {{"key", key}, {"value", flipped}}));
        QVERIFY(reply.value("ok").toBool());
        QCOMPARE(reply.value("value").toString(), flipped);
        QVERIFY(reply.value("set").toBool());
        QCOMPARE(ParamRegistry::instance().getValue(*info), flipped.toStdString());

        // A JSON bool is accepted for a Bool
        reply = ask(op("param.set", {{"key", key}, {"value", original == "true"}}));
        QCOMPARE(reply.value("value").toString().toStdString(), original);

        // The volatile row detail
        QJsonArray keys{key, QStringLiteral("no/such")};
        reply = ask(op("omni.rows", {{"list", "params"}, {"keys", keys}}));
        QVERIFY(reply.value("ok").toBool());
        auto rows = reply.value("rows").toObject();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.value(key).toObject().value("value").toString().toStdString(), original);
        QVERIFY(rows.value(key).toObject().value("set").toBool());

        reply = ask(op("param.reset", {{"key", key}}));
        QVERIFY(reply.value("ok").toBool());
        QVERIFY(!reply.value("set").toBool());
        QVERIFY(!ParamRegistry::instance().isSet(*info));

        reply = ask(op("param.set", {{"key", key}, {"value", "maybe"}}));
        QCOMPARE(reply.value("code").toString(), QStringLiteral("BadValue"));
        reply = ask(op("param.get", {{"key", "no/such"}}));
        QCOMPARE(reply.value("code").toString(), QStringLiteral("UnknownParam"));

        // Writes are refused on a view-only connection, reads answered
        reply = ask(op("param.set", {{"key", key}, {"value", flipped}}), true);
        QCOMPARE(reply.value("code").toString(), QStringLiteral("ViewOnly"));
        reply = ask(op("param.reset", {{"key", key}}), true);
        QCOMPARE(reply.value("code").toString(), QStringLiteral("ViewOnly"));
        reply = ask(op("param.get", {{"key", key}}), true);
        QVERIFY(reply.value("ok").toBool());
        reply = ask(op("omni.catalog", {{"list", "params"}}), true);
        QVERIFY(reply.value("ok").toBool());
    }

    void test_objects()  // NOLINT
    {
        auto reply = ask(op("omni.objects"));
        QVERIFY(reply.value("ok").toBool());
        QCOMPARE(reply.value("doc").toString(), QString::fromUtf8(doc->getName()));
        auto objects = reply.value("objects").toArray();
        // GroupA, GroupB, Part and the origin the part brought along
        QCOMPARE(objects.size(), int(doc->getObjects().size()));
        auto byName = [&](const char *name) {
            for (const auto &v : objects) {
                if (v.toObject().value("name").toString() == QLatin1String(name))
                    return v.toObject();
            }
            return QJsonObject();
        };
        auto a = byName("GroupA");
        QCOMPARE(a.value("name").toString(), QStringLiteral("GroupA"));
        QCOMPARE(a.value("label").toString(), QStringLiteral("First group"));
        QCOMPARE(a.value("type").toString(), QStringLiteral("App::DocumentObjectGroup"));
        QVERIFY(!a.contains("children"));
        auto part = byName("Part");
        QCOMPARE(part.value("name").toString(), QStringLiteral("Part"));
        QVERIFY(!part.contains("label"));   // the label is the name
        QVERIFY(part.value("children").toArray().contains(QStringLiteral("GroupB")));
        // No Gui::Application: no views
        QVERIFY(reply.value("views").toArray().isEmpty());

        reply = ask(op("omni.objects", {{"doc", "Nope"}}));
        QCOMPARE(reply.value("code").toString(), QStringLiteral("UnknownDocument"));
    }

    void test_resolve()  // NOLINT
    {
        auto reply = ask(op("omni.resolve", {{"query", "GroupA"}}));
        QVERIFY(reply.value("ok").toBool());
        QCOMPARE(reply.value("kind").toString(), QStringLiteral("object"));
        QCOMPARE(reply.value("obj").toString(), QStringLiteral("GroupA"));
        QCOMPARE(reply.value("label").toString(), QStringLiteral("First group"));
        QCOMPARE(reply.value("sub").toString(), QString());

        reply = ask(op("omni.resolve", {{"query", "<<Second group>>"}}));
        QCOMPARE(reply.value("kind").toString(), QStringLiteral("object"));
        QCOMPARE(reply.value("obj").toString(), QStringLiteral("GroupB"));

        // A sub-object path names the sub-object, with the path kept
        reply = ask(op("omni.resolve", {{"query", "Part.GroupB"}}));
        QCOMPARE(reply.value("kind").toString(), QStringLiteral("object"));
        QCOMPARE(reply.value("obj").toString(), QStringLiteral("GroupB"));
        QCOMPARE(reply.value("top").toString(), QStringLiteral("Part"));
        QCOMPARE(reply.value("sub").toString(), QStringLiteral("GroupB."));

        reply = ask(op("omni.resolve", {{"query", "GroupA.Label"}}));
        QCOMPARE(reply.value("kind").toString(), QStringLiteral("property"));
        QCOMPARE(reply.value("scope").toString(), QStringLiteral("object"));
        QCOMPARE(reply.value("obj").toString(), QStringLiteral("GroupA"));
        auto prop = reply.value("prop").toObject();
        QCOMPARE(prop.value("name").toString(), QStringLiteral("Label"));
        QCOMPARE(prop.value("type").toString(), QStringLiteral("String"));
        QCOMPARE(prop.value("value").toString(), QStringLiteral("First group"));

        // A '#' before the name is this document; "#." its own members
        reply = ask(op("omni.resolve", {{"query", "#GroupB"}}));
        QCOMPARE(reply.value("obj").toString(), QStringLiteral("GroupB"));
        reply = ask(op("omni.resolve", {{"query", "#.Comment"}}));
        QCOMPARE(reply.value("kind").toString(), QStringLiteral("property"));
        QCOMPARE(reply.value("scope").toString(), QStringLiteral("document"));
        QCOMPARE(reply.value("doc").toString(), QString::fromUtf8(doc->getName()));
        QCOMPARE(reply.value("prop").toObject().value("name").toString(), QStringLiteral("Comment"));
        reply = ask(op("omni.resolve", {{"query", QString::fromUtf8(doc->getName()) + "#.Comment"}}));
        QCOMPARE(reply.value("scope").toString(), QStringLiteral("document"));

        // No view without a Gui::Application
        reply = ask(op("omni.resolve", {{"query", "#.ActiveView.DrawStyle"}}));
        QCOMPARE(reply.value("code").toString(), QStringLiteral("NoMatch"));
        reply = ask(op("omni.resolve", {{"query", "#.View1.DrawStyle"}}));
        QCOMPARE(reply.value("code").toString(), QStringLiteral("NoMatch"));

        reply = ask(op("omni.resolve", {{"query", "Nothing"}}));
        QCOMPARE(reply.value("code").toString(), QStringLiteral("NoMatch"));
        reply = ask(op("omni.resolve", {{"query", ""}}));
        QCOMPARE(reply.value("code").toString(), QStringLiteral("NoMatch"));

        // The "#." members resolve on an empty document too
        auto empty = App::GetApplication().newDocument("OmniControlEmpty");
        QJsonObject req = op("omni.resolve", {{"query", "#.Label"}, {"doc", QString::fromUtf8(empty->getName())}});
        reply = ask(req);
        QCOMPARE(reply.value("kind").toString(), QStringLiteral("property"));
        QCOMPARE(reply.value("doc").toString(), QString::fromUtf8(empty->getName()));
        App::GetApplication().closeDocument(empty->getName());
    }

    void test_commandsWithoutGui()  // NOLINT
    {
        auto reply = ask(op("omni.catalog", {{"list", "commands"}}));
        QVERIFY(reply.value("ok").toBool());
        QCOMPARE(reply.value("version").toDouble(), 0.0);
        QVERIFY(reply.value("add").toArray().isEmpty());
        reply = ask(op("command.run", {{"name", "Std_New"}}));
        QCOMPARE(reply.value("code").toString(), QStringLiteral("NoGui"));
        reply = ask(op("command.children", {{"name", "Std_DrawStyle"}}));
        QCOMPARE(reply.value("code").toString(), QStringLiteral("NoGui"));
        reply = ask(op("command.run", {{"name", "Std_New"}}), true);
        QCOMPARE(reply.value("code").toString(), QStringLiteral("ViewOnly"));
        reply = ask(op("omni.rows", {{"list", "commands"}, {"keys", QJsonArray{QStringLiteral("Std_New")}}}));
        QVERIFY(reply.value("ok").toBool());
        QVERIFY(reply.value("rows").toObject().isEmpty());
    }

private:
    App::Document *doc = nullptr;
    int nextId = 0;
    size_t lastBytes = 0;
};

QTEST_MAIN(testOmniControl)
#include "OmniControl.moc"
