// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ParamRegistry.h>
#include <App/PropertyStandard.h>

#include "Gui/OmniSearch.h"
#include "Gui/PrefWidgets.h"
#include <src/App/InitApplication.h>

using namespace Gui::OmniSearch;
using App::ParamInfo;
using App::ParamRegistry;

// The search layer without the box: the grammar, object resolution, the
// parameter search and the editor factory. Commands need a Gui::Application
// and are exercised by hand through the box (docs/OmniSearch.md).
class testOmniSearch: public QObject
{
    Q_OBJECT

public:
    testOmniSearch()
    {
        tests::initApplication();
    }

private Q_SLOTS:

    void init()
    {
        doc = App::GetApplication().newDocument("OmniSearchTest");
        auto a = doc->addObject("App::DocumentObjectGroup", "GroupA");
        a->Label.setValue("First group");
        auto b = doc->addObject("App::DocumentObjectGroup", "GroupB");
        b->Label.setValue("Second group");
        doc->recompute();
    }

    void cleanup()
    {
        App::GetApplication().closeDocument(doc->getName());
        doc = nullptr;
    }

    void test_parseInput()  // NOLINT
    {
        Input in = parseInput(QStringLiteral("/"));
        QCOMPARE(in.mode, Mode::Chooser);
        QCOMPARE(in.offset, 0);

        in = parseInput(QStringLiteral("/cm"));
        QCOMPARE(in.mode, Mode::Chooser);

        in = parseInput(QStringLiteral("/ Box"));
        QCOMPARE(in.mode, Mode::Object);
        QCOMPARE(in.query, QStringLiteral("Box"));
        QCOMPARE(in.offset, 2);

        in = parseInput(QStringLiteral("/cmd draw style"));
        QCOMPARE(in.mode, Mode::Command);
        QCOMPARE(in.query, QStringLiteral("draw style"));
        QCOMPARE(in.offset, 5);

        in = parseInput(QStringLiteral("/param "));
        QCOMPARE(in.mode, Mode::Param);
        QCOMPARE(in.query, QString());
        QCOMPARE(in.offset, 7);

        // no prefix: an object query as typed
        in = parseInput(QStringLiteral("Box.Length"));
        QCOMPARE(in.mode, Mode::Object);
        QCOMPARE(in.query, QStringLiteral("Box.Length"));
        QCOMPARE(in.offset, 0);

        QCOMPARE(QString::fromLatin1(modePrefix(Mode::Command)), QStringLiteral("/cmd "));
    }

    void test_resolveObject()  // NOLINT
    {
        auto owner = doc->getObject("GroupA");
        QVERIFY(owner);
        ObjectMatch m;

        QVERIFY(resolveObject(QStringLiteral("GroupB"), owner, m));
        QVERIFY(m.obj.getObject() == doc->getObject("GroupB"));
        QVERIFY(m.prop == nullptr);

        // by label
        QVERIFY(resolveObject(QStringLiteral("<<Second group>>"), owner, m));
        QVERIFY(m.obj.getObject() == doc->getObject("GroupB"));

        // a property
        QVERIFY(resolveObject(QStringLiteral("GroupB.Label"), owner, m));
        QVERIFY(m.obj.getObject() == doc->getObject("GroupB"));
        QVERIFY(m.prop == &doc->getObject("GroupB")->Label);

        QVERIFY(!resolveObject(QStringLiteral("NoSuchObject"), owner, m));
        QVERIFY(!resolveObject(QStringLiteral("GroupB.NoSuchProperty"), owner, m));
        QVERIFY(!resolveObject(QStringLiteral(""), owner, m));
        QVERIFY(!resolveObject(QStringLiteral("GroupB"), nullptr, m));
    }

    void test_resolveLocalProperty()  // NOLINT
    {
        auto a = doc->getObject("GroupA");
        auto b = doc->getObject("GroupB");
        ObjectMatch m;

        // no locals: '.' is the owner, as in an expression
        QVERIFY(resolveObject(QStringLiteral(".Label"), a, m));
        QVERIFY(m.prop == &a->Label);
        QCOMPARE(m.props.size(), size_t(1));

        // locals given but empty: '.' names nothing
        std::vector<App::DocumentObject*> none;
        QVERIFY(!resolveObject(QStringLiteral(".Label"), a, m, &none));
        // ... while a full path still resolves
        QVERIFY(resolveObject(QStringLiteral("GroupB.Label"), a, m, &none));
        QCOMPARE(m.props.size(), size_t(1));

        // two locals: the property of both, owner first
        std::vector<App::DocumentObject*> both{a, b};
        QVERIFY(resolveObject(QStringLiteral(".Label"), a, m, &both));
        QVERIFY(m.prop == &a->Label);
        QCOMPARE(m.props.size(), size_t(2));
        QVERIFY(m.props[0] == &a->Label);
        QVERIFY(m.props[1] == &b->Label);

        // a property only the owner has is edited alone
        auto prop = a->addDynamicProperty("App::PropertyInteger", "OnlyA");
        QVERIFY(prop);
        QVERIFY(resolveObject(QStringLiteral(".OnlyA"), a, m, &both));
        QCOMPARE(m.props.size(), size_t(1));
    }

    void test_resolveDocumentMember()  // NOLINT
    {
        auto a = doc->getObject("GroupA");
        ObjectMatch m;

        // '#.' is the owner's document
        QVERIFY(resolveObject(QStringLiteral("#.Comment"), a, m));
        QVERIFY(m.doc == doc);
        QVERIFY(m.prop == &doc->Comment);
        QCOMPARE(m.props.size(), size_t(1));
        QVERIFY(m.obj.getObject() == nullptr);

        // by name and by label
        doc->Label.setValue("Omni test");
        QVERIFY(resolveObject(QStringLiteral("OmniSearchTest#.Comment"), a, m));
        QVERIFY(m.prop == &doc->Comment);
        QVERIFY(resolveObject(QStringLiteral("<<Omni test>>#.Comment"), a, m));
        QVERIFY(m.prop == &doc->Comment);

        // another document, while the owner stays in this one
        auto other = App::GetApplication().newDocument("OmniSearchOther");
        QVERIFY(resolveObject(QStringLiteral("OmniSearchOther#.Comment"), a, m));
        QVERIFY(m.doc == other);
        QVERIFY(m.prop == &other->Comment);
        App::GetApplication().closeDocument(other->getName());

        // '#Obj' is an object of the owner's document
        QVERIFY(resolveObject(QStringLiteral("#GroupB"), a, m));
        QVERIFY(m.obj.getObject() == doc->getObject("GroupB"));
        QVERIFY(m.prop == nullptr);
        QVERIFY(resolveObject(QStringLiteral("#GroupB.Label"), a, m));
        QVERIFY(m.prop == &doc->getObject("GroupB")->Label);

        // misses: no such document, property or view (no Gui here)
        QVERIFY(!resolveObject(QStringLiteral("NoSuchDoc#.Comment"), a, m));
        QVERIFY(!resolveObject(QStringLiteral("#.NoSuchProperty"), a, m));
        QVERIFY(!resolveObject(QStringLiteral("#.ActiveView.DrawStyle"), a, m));
        QVERIFY(!resolveObject(QStringLiteral("#.View1.DrawStyle"), a, m));
        QVERIFY(!resolveObject(QStringLiteral("#"), a, m));
        QVERIFY(!resolveObject(QStringLiteral("#."), a, m));

        // a pseudo property is not a property: "_self" names the object, as
        // in the tree search; ViewObject needs a view provider
        QVERIFY(resolveObject(QStringLiteral("GroupB._self"), a, m));
        QVERIFY(m.prop == nullptr);
        QVERIFY(m.obj.getObject() == doc->getObject("GroupB"));
        QVERIFY(!resolveObject(QStringLiteral("GroupB.ViewObject"), a, m));
        QVERIFY(!resolveObject(QStringLiteral("GroupB.ViewObject.Visibility"), a, m));
    }

    void test_documentMembers()  // NOLINT
    {
        auto a = doc->getObject("GroupA");
        auto members = documentMembers(QStringLiteral("#."), a);
        QVERIFY(!members.empty());
        bool comment = false;
        for (auto &m : members) {
            QVERIFY(!m.name.endsWith(QLatin1Char('.')));  // no view without a Gui
            if (m.name == QStringLiteral("Comment"))
                comment = true;
        }
        QVERIFY(comment);
        QVERIFY(documentMembers(QStringLiteral("OmniSearchTest#."), a).size() == members.size());
        QVERIFY(documentMembers(QStringLiteral("NoSuchDoc#."), a).empty());
        QVERIFY(documentMembers(QStringLiteral("#.ActiveView."), a).empty());
        QVERIFY(documentMembers(QStringLiteral("#.View1."), a).empty());
        QVERIFY(documentMembers(QStringLiteral("#.Comment"), a).empty());
        QVERIFY(documentMembers(QStringLiteral("GroupB."), a).empty());

        QString head, tail;
        QVERIFY(splitMemberQuery(QStringLiteral("#.Com"), head, tail));
        QCOMPARE(head, QStringLiteral("#."));
        QCOMPARE(tail, QStringLiteral("Com"));
        QVERIFY(splitMemberQuery(QStringLiteral("<<A#B>>#.ActiveView.Draw"), head, tail));
        QCOMPARE(head, QStringLiteral("<<A#B>>#.ActiveView."));
        QCOMPARE(tail, QStringLiteral("Draw"));
        QVERIFY(splitMemberQuery(QStringLiteral("#.View2."), head, tail));
        QCOMPARE(head, QStringLiteral("#.View2."));
        QCOMPARE(tail, QString());
        QVERIFY(splitMemberQuery(QStringLiteral("Doc#."), head, tail));
        QCOMPARE(tail, QString());
        QVERIFY(!splitMemberQuery(QStringLiteral("Doc#Box"), head, tail));
        QVERIFY(!splitMemberQuery(QStringLiteral("#.View2.Placement.Base"), head, tail));
        QVERIFY(!splitMemberQuery(QStringLiteral("Box.Length"), head, tail));
    }

    void test_searchParams()  // NOLINT
    {
        auto hits = searchParams(QStringLiteral("preferences/document checkextension"));
        QCOMPARE(hits.size(), size_t(1));
        QCOMPARE(hits[0].info->name, "CheckExtension");
        QVERIFY(hits[0].value == ParamRegistry::instance().getValue(*hits[0].info));

        QVERIFY(searchParams(QStringLiteral("nothing-matches-this-anywhere")).empty());
        QVERIFY(searchParams(QString()).size() == ParamRegistry::instance().entries().size());
    }

    void test_paramListModelAndFilter()  // NOLINT
    {
        Gui::ParamListModel model;
        QCOMPARE(model.rowCount(), int(ParamRegistry::instance().entries().size()));

        Gui::KeywordFilterModel filter;
        filter.setSourceModel(&model);
        filter.setKeywords(QStringLiteral("Preferences/Document CheckExtension"));
        QCOMPARE(filter.rowCount(), 1);
        auto index = filter.index(0, 0);
        auto info = Gui::ParamListModel::infoOf(index);
        QVERIFY(info);
        QCOMPARE(info->name, "CheckExtension");
        QCOMPARE(index.data(Qt::DisplayRole).toString(), QStringLiteral("Preferences/Document/CheckExtension"));
        QCOMPARE(index.data(ParamPathRole).toString(),
                 QStringLiteral("User parameter:BaseApp/Preferences/Document/CheckExtension"));
        QVERIFY(!index.data(ParamValueRole).toString().isEmpty());

        filter.setKeywords(QString());
        QCOMPARE(filter.rowCount(), model.rowCount());
    }

    void test_createParamEditorByProxy()  // NOLINT
    {
        const char *path = "User parameter:BaseApp/Preferences/OmniSearchTest";
        QWidget parent;

        ParamInfo combo("T", "C", path, "Combo", "Combo", ParamInfo::Int, 1);
        combo.setProxy("ComboBox").setItems({{"Zero", "", nullptr}, {"One", "", nullptr}}, false, false);
        auto w = createParamEditor(combo, &parent);
        auto cb = qobject_cast<Gui::PrefComboBox*>(w);
        QVERIFY(cb);
        QCOMPARE(cb->count(), 2);
        QCOMPARE(cb->entryName(), QByteArray("Combo"));

        ParamInfo spin("T", "C", path, "Spin", "Spin", ParamInfo::Int, 5);
        spin.setProxy("SpinBox").setRange(0, 10, 2, 0);
        w = createParamEditor(spin, &parent);
        auto sb = qobject_cast<Gui::PrefSpinBox*>(w);
        QVERIFY(sb);
        QCOMPARE(sb->maximum(), 10);
        QCOMPARE(sb->singleStep(), 2);

        ParamInfo fspin("T", "C", path, "FSpin", "FSpin", ParamInfo::Float, 0.5);
        fspin.setProxy("SpinBox").setRange(0, 1, 0.1, 2);
        w = createParamEditor(fspin, &parent);
        auto dsb = qobject_cast<Gui::PrefDoubleSpinBox*>(w);
        QVERIFY(dsb);
        QCOMPARE(dsb->decimals(), 2);

        ParamInfo color("T", "C", path, "Color", "Color", ParamInfo::Hex, 0xFF0000FFu);
        color.setProxy("Color").setTransparency(true);
        QVERIFY(qobject_cast<Gui::PrefColorButton*>(createParamEditor(color, &parent)));

        ParamInfo file("T", "C", path, "File", "File", ParamInfo::String, "");
        file.setProxy("File");
        QVERIFY(qobject_cast<Gui::PrefFileChooser*>(createParamEditor(file, &parent)));

        ParamInfo accel("T", "C", path, "Accel", "Accel", ParamInfo::String, "");
        accel.setProxy("ShortcutEdit");
        QVERIFY(qobject_cast<Gui::PrefAccelLineEdit*>(createParamEditor(accel, &parent)));

        ParamInfo pattern("T", "C", path, "Pattern", "Pattern", ParamInfo::Int, 0);
        pattern.setProxy("LinePattern");
        QVERIFY(qobject_cast<Gui::PrefLinePattern*>(createParamEditor(pattern, &parent)));
    }

    void test_createParamEditorByType()  // NOLINT
    {
        const char *path = "User parameter:BaseApp/Preferences/OmniSearchTest";
        QWidget parent;
        auto &reg = ParamRegistry::instance();

        ParamInfo b("T", "C", path, "Flag", "Flag", ParamInfo::Bool, true);
        reg.reset(b);
        auto cb = qobject_cast<Gui::PrefCheckBox*>(createParamEditor(b, &parent));
        QVERIFY(cb);
        QVERIFY(cb->isChecked());
        reg.setValue(b, "false");
        cb = qobject_cast<Gui::PrefCheckBox*>(createParamEditor(b, &parent));
        QVERIFY(cb);
        QVERIFY(!cb->isChecked());
        reg.reset(b);

        ParamInfo i("T", "C", path, "Count", "Count", ParamInfo::Int, -3);
        reg.reset(i);
        auto sb = qobject_cast<Gui::PrefSpinBox*>(createParamEditor(i, &parent));
        QVERIFY(sb);
        QCOMPARE(sb->value(), -3);

        ParamInfo u("T", "C", path, "UCount", "UCount", ParamInfo::UInt, 7u);
        reg.reset(u);
        sb = qobject_cast<Gui::PrefSpinBox*>(createParamEditor(u, &parent));
        QVERIFY(sb);
        QCOMPARE(sb->minimum(), 0);

        // a custom proxy the registry cannot describe falls back to the type
        ParamInfo custom("T", "C", path, "Custom", "Custom", ParamInfo::Float, 1.25);
        custom.setProxy("AnimationCurve");
        reg.reset(custom);
        auto dsb = qobject_cast<Gui::PrefDoubleSpinBox*>(createParamEditor(custom, &parent));
        QVERIFY(dsb);
        QCOMPARE(dsb->value(), 1.25);

        ParamInfo s("T", "C", path, "Name", "Name", ParamInfo::String, "abc");
        reg.reset(s);
        auto le = qobject_cast<Gui::PrefLineEdit*>(createParamEditor(s, &parent));
        QVERIFY(le);
        QCOMPARE(le->text(), QStringLiteral("abc"));

        // an editor saves through the same group the registry reads
        le->setText(QStringLiteral("xyz"));
        le->onSave();
        QVERIFY(reg.getValue(s) == "xyz");
        reg.reset(s);
    }

private:
    App::Document *doc = nullptr;
};

QTEST_MAIN(testOmniSearch)

#include "OmniSearch.moc"
