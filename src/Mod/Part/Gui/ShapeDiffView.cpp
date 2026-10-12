/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Builder.hxx>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTransform.h>
#include <Message_ProgressIndicator.hxx>
#include <QCoreApplication>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <list>
#include <thread>
#include <vector>
#endif

#include <App/Application.h>
#include <App/Color.h>
#include <App/GeoFeature.h>
#include <Base/Parameter.h>
#include <Base/Placement.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Gui/WaitCursor.h>
#include <Mod/Part/App/ShapeDiff.h>
#include <Mod/Part/App/TopoShape.h>

#include "PartParams.h"
#include "ShapeDiffView.h"
#include "ViewProvider.h"

using namespace PartGui;

namespace
{

/// How much of a kind is seen at the side the position is at, and at the other
constexpr float Full = 0.7f;
constexpr float Ghost = 0.2f;
/// What did not change: thin, wherever the position is
constexpr float Thin = 0.15f;

/** The fuse of two shapes on a thread of its own (sec 32.16).
 *
 * What it is given are copies, made before it starts: nothing it reads is
 * anything the document, a recompute or the view has. What it found is read
 * once it has said, in the main thread, that it is done.
 */
struct Work
{
    Part::TopoShape a;
    Part::TopoShape b;
    std::atomic<bool> stop {false};
    bool ok = false;
    Part::ShapeDiff::Material material;
    std::thread thread;
};

/// What the fuse asks whether to go on
class Stopped: public Message_ProgressIndicator
{
public:
    explicit Stopped(const std::atomic<bool>& stop)
        : _stop(stop)
    {}
    Standard_Boolean UserBreak() override
    {
        return _stop.load() ? Standard_True : Standard_False;
    }

protected:
    void Show(const Message_ProgressScope&, const Standard_Boolean) override
    {}

private:
    const std::atomic<bool>& _stop;
};

/// Two shapes taken apart by their material, and what they were known by
struct Apart
{
    int id = 0;
    std::string refA;
    std::string refB;
    TopoDS_Shape a;
    TopoDS_Shape b;
    bool ok = false;
    Part::ShapeDiff::Material material;
    std::shared_ptr<Work> work; ///< while it is being worked out
};

/// The last few: a row gone back to is not fused again
constexpr size_t ApartKept = 8;

/// All of it is the main thread's
struct Aparts
{
    std::list<Apart> kept;
    /// Who waits for which, and what each does when it is there
    std::map<const void*, std::pair<int, std::function<void()>>> waiting;
    /// Told to stop, and not done with it yet
    std::vector<std::shared_ptr<Work>> stopping;
    int ids = 0;
    bool quitting = false;
    bool watched = false;
};

Aparts& aparts()
{
    static Aparts all;
    return all;
}

/// The application goes: nothing is left running, or held
void quit()
{
    auto& all = aparts();
    all.quitting = true;
    std::vector<std::shared_ptr<Work>> works = all.stopping;
    for (auto& entry : all.kept) {
        if (entry.work)
            works.push_back(entry.work);
    }
    for (auto& work : works)
        work->stop = true;
    for (auto& work : works) {
        if (work->thread.joinable())
            work->thread.join();
    }
    all.kept.clear();
    all.waiting.clear();
    all.stopping.clear();
}

/// A fuse is done, or stopped: said by its thread, here in the main one
void arrived(const std::shared_ptr<Work>& work, int id)
{
    if (work->thread.joinable())
        work->thread.join();
    auto& all = aparts();
    all.stopping.erase(std::remove(all.stopping.begin(), all.stopping.end(), work),
                       all.stopping.end());
    bool found = false;
    for (auto& entry : all.kept) {
        if (entry.id == id && entry.work == work) {
            entry.ok = work->ok;
            entry.material = work->material;
            entry.work.reset();
            found = true;
        }
    }
    if (!found)
        return;
    // One at a time, each looked up when its turn is: what one does may be
    // the end of another
    for (;;) {
        auto it = std::find_if(all.waiting.begin(), all.waiting.end(), [id](const auto& item) {
            return item.second.first == id;
        });
        if (it == all.waiting.end())
            break;
        auto then = std::move(it->second.second);
        all.waiting.erase(it);
        then();
    }
}

/// \a who waits for nothing any more. What nobody waits for is stopped.
void leave(const void* who)
{
    auto& all = aparts();
    auto it = all.waiting.find(who);
    if (it == all.waiting.end())
        return;
    const int id = it->second.first;
    all.waiting.erase(it);
    for (const auto& item : all.waiting) {
        if (item.second.first == id)
            return;
    }
    for (auto entry = all.kept.begin(); entry != all.kept.end(); ++entry) {
        if (entry->id == id && entry->work) {
            entry->work->stop = true;
            all.stopping.push_back(entry->work);
            all.kept.erase(entry);
            break;
        }
    }
}

enum class Got
{
    Yes,   ///< taken apart
    No,    ///< not two solids, or the fuse failed
    Later, ///< being worked out: said when it is there
};

/** The material of \a a and \a b, from what is kept where it is there.
 *
 * A value of the log is another shape each time it is read, so it is known
 * again by its ref; a shape with no ref -- what an object has now -- by
 * being that shape, where it was. \a was says whether it was there.
 *
 * The fuse of two heavy solids takes seconds, and tens of them (sec 32.16):
 * of more faces than ShapeDiffInlineFaces it is worked out on a thread of
 * its own, and \a then is called when it is there -- unless \a who has
 * left by then.
 */
Got apart(const Part::TopoShape& a, const Part::TopoShape& b, const std::string& refA,
          const std::string& refB, const Part::ShapeDiff::Material*& material, bool& was,
          const void* who, std::function<void()> then)
{
    auto& all = aparts();
    const TopoDS_Shape& sa = a.getShape();
    const TopoDS_Shape& sb = b.getShape();
    auto is = [](const std::string& ref, const std::string& was, const TopoDS_Shape& shape,
                 const TopoDS_Shape& held) {
        return ref == was && (!ref.empty() || shape.IsEqual(held));
    };
    auto got = [&]() {
        Apart& first = all.kept.front();
        if (first.work) {
            all.waiting[who] = {first.id, std::move(then)};
            return Got::Later;
        }
        material = &first.material;
        return first.ok ? Got::Yes : Got::No;
    };
    for (auto it = all.kept.begin(); it != all.kept.end(); ++it) {
        if (!is(refA, it->refA, sa, it->a) || !is(refB, it->refB, sb, it->b))
            continue;
        all.kept.splice(all.kept.begin(), all.kept, it);
        was = true;
        return got();
    }
    was = false;
    Apart made;
    made.id = ++all.ids;
    made.refA = refA;
    made.refB = refB;
    // Held only where it is what the shape is known by
    if (refA.empty())
        made.a = sa;
    if (refB.empty())
        made.b = sb;
    auto solid = [](const TopoDS_Shape& shape) {
        return TopExp_Explorer(shape, TopAbs_SOLID).More();
    };
    const long faces = static_cast<long>(a.countSubShapes(TopAbs_FACE))
                       + static_cast<long>(b.countSubShapes(TopAbs_FACE));
    const long inlined = App::GetApplication()
                             .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Part")
                             ->GetInt("ShapeDiffInlineFaces", 300);
    if (all.quitting || !solid(sa) || !solid(sb) || sa.IsEqual(sb) || faces <= inlined) {
        Gui::WaitCursor wait;
        made.ok = Part::ShapeDiff::material(a, b, made.material);
    }
    else {
        auto work = std::make_shared<Work>();
        work->a = Part::TopoShape(BRepBuilderAPI_Copy(sa).Shape());
        work->b = Part::TopoShape(BRepBuilderAPI_Copy(sb).Shape());
        made.work = work;
        if (!all.watched) {
            all.watched = true;
            QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, []() { quit(); });
        }
        const int id = made.id;
        work->thread = std::thread([work, id]() {
            try {
                Handle(Stopped) stopped = new Stopped(work->stop);
                work->ok = Part::ShapeDiff::material(work->a, work->b, work->material, 0.,
                                                     stopped->Start());
            }
            catch (...) {
                work->ok = false;
            }
            QMetaObject::invokeMethod(
                qApp, [work, id]() { arrived(work, id); }, Qt::QueuedConnection);
        });
    }
    all.kept.push_front(std::move(made));
    // What is being worked out is not let go of for being old
    for (auto it = all.kept.end(); all.kept.size() > ApartKept && it != all.kept.begin();) {
        --it;
        if (!it->work && it != all.kept.begin())
            it = all.kept.erase(it);
    }
    return got();
}

}  // namespace

const char* ShapeDiffView::name(Kind kind)
{
    switch (kind) {
        case Same:
            return "same";
        case ChangedA:
            return "changedA";
        case ChangedB:
            return "changedB";
        case Removed:
            return "removed";
        case Added:
            return "added";
        default:
            return "";
    }
}

ShapeDiffView::ShapeDiffView(ViewProviderPartExt& owner)
    : _owner(owner)
{
    _root = new Gui::SoFCPathAnnotation;
    _root->priority = -2;
    _root->setName("ShapeDiff");
    _transform = new SoTransform;
    _root->addChild(_transform);
    for (auto& sw : _switch) {
        sw = new SoSwitch;
        sw->whichChild = SO_SWITCH_NONE;
        _root->addChild(sw);
    }
    // Beside what the object shows of itself, so that it is there whether
    // the object is shown or not, and moves with what the object is in.
    _owner.getRoot()->addChild(_root);
}

ShapeDiffView::~ShapeDiffView()
{
    leave(this);
    int index = _owner.getRoot()->findChild(_root);
    if (index >= 0)
        _owner.getRoot()->removeChild(index);
    for (auto& view : _view) {
        if (view)
            view->beforeDelete();
    }
}

bool ShapeDiffView::show(const Part::TopoShape& a, const Part::TopoShape& b, Mode mode,
                         const std::string& refA, const std::string& refB)
{
    _a = a;
    _b = b;
    _refA = refA;
    _refB = refB;
    _mode = mode;
    return build();
}

void ShapeDiffView::setMode(Mode mode)
{
    if (mode == _mode)
        return;
    _mode = mode;
    build();
}

bool ShapeDiffView::build()
{
    TopoDS_Shape shapes[Kinds];
    int counts[Kinds] = {};
    std::map<std::string, double> summary;

    // Whatever was waited for
    leave(this);
    const Part::ShapeDiff::Material* material = nullptr;
    if (_mode == Mode::Material) {
        bool was = false;
        Got got = Got::No;
        if (!_a.isNull() && !_b.isNull()) {
            got = apart(_a, _b, _refA, _refB, material, was, this, [this]() {
                _arrived = true;
                build();
                _arrived = false;
            });
        }
        if (got != Got::Yes)
            material = nullptr;
        // Until it is there, the faces
        if (got == Got::Later)
            summary["material.pending"] = 1;
        else if (got == Got::No)
            summary["material.none"] = 1;
        else
            summary["material.kept"] = was && !_arrived ? 1 : 0;
    }
    if (material) {
        auto add = [&](Kind kind, const TopoDS_Shape& shape, double volume) {
            summary[std::string("volume.") + name(kind)] = volume;
            if (shape.IsNull())
                return;
            shapes[kind] = shape;
            ++counts[kind];
        };
        add(Same, material->common, material->commonVolume);
        add(Removed, material->removed, material->removedVolume);
        add(Added, material->added, material->addedVolume);
        // "same" is what the faces are counted by
        summary["volume.common"] = summary["volume.same"];
        summary.erase("volume.same");
    }
    else {
        Part::ShapeDiff diff(_a, _b);
        const auto& faces = diff.elements(TopAbs_FACE);
        if (faces.empty())
            return false;
        BRep_Builder builder;
        TopoDS_Compound kinds[Kinds];
        for (auto& kind : kinds)
            builder.MakeCompound(kind);
        auto add = [&](Kind kind, const Part::TopoShape& from, int index) {
            builder.Add(kinds[kind], from.getSubShape(TopAbs_FACE, index));
            ++counts[kind];
        };
        for (const auto& face : faces) {
            ++summary[Part::ShapeDiff::name(face.what)];
            if (face.by != Part::ShapeDiff::By::None)
                ++summary[std::string("by.") + Part::ShapeDiff::name(face.by)];
            switch (face.what) {
                case Part::ShapeDiff::What::Same:
                    add(Same, _b, face.b);
                    break;
                case Part::ShapeDiff::What::Moved:
                case Part::ShapeDiff::What::Changed:
                    add(ChangedA, _a, face.a);
                    add(ChangedB, _b, face.b);
                    break;
                case Part::ShapeDiff::What::Removed:
                    add(Removed, _a, face.a);
                    break;
                case Part::ShapeDiff::What::Added:
                    add(Added, _b, face.b);
                    break;
            }
        }
        for (int kind = 0; kind < Kinds; ++kind)
            shapes[kind] = kinds[kind];
    }
    summary["material"] = material ? 1 : 0;
    _summary = std::move(summary);

    const App::Color grey(0.6f, 0.6f, 0.6f);
    const App::Color changed(static_cast<uint32_t>(PartParams::getPreviewDressColor()));
    const App::Color removed(static_cast<uint32_t>(PartParams::getPreviewSubColor()));
    const App::Color added(static_cast<uint32_t>(PartParams::getPreviewAddColor()));
    const App::Color colors[Kinds] = {grey, changed, changed, removed, added};

    for (int kind = 0; kind < Kinds; ++kind) {
        _switch[kind]->removeAllChildren();
        if (_view[kind]) {
            _view[kind]->beforeDelete();
            _view[kind].reset();
        }
        if (!counts[kind])
            continue;
        // As PartDesign's preview of what a feature adds is set up
        // (ViewProviderAddSub): a view of the object that is no view
        // provider of the document, with looks of its own.
        //
        // The shape first, before the view has an object: a view of the
        // object's own shape has the object's looks, and every one set below
        // would be written to the object -- a change of the document.
        auto view = std::make_unique<ViewProviderPart>();
        view->setGivenShape(Part::TopoShape(shapes[kind]));
        view->forceUpdate();
        view->MapFaceColor.setValue(false);
        view->MapLineColor.setValue(false);
        view->MapPointColor.setValue(false);
        view->MapTransparency.setValue(false);
        view->ForceMapColors.setValue(false);
        view->Selectable.setValue(false);
        view->Lighting.setValue(1);
        view->enableFullSelectionHighlight(false, false, false);
        view->setStatus(Gui::SecondaryView, true);
        view->attach(_owner.getObject());
        view->setDefaultMode(0);
        view->show();
        App::Color color = colors[kind];
        color.a = 0.f;
        view->LineColor.setValue(color);
        auto material = view->PointMaterial.getValue();
        material.diffuseColor = color;
        material.transparency = 1.0f;
        view->PointMaterial.setValue(material);
        view->ShapeColor.setValue(color);
        view->updateVisual();
        _switch[kind]->addChild(view->getRoot());
        _view[kind] = std::move(view);
    }
    updatePlacement();
    apply();
    return true;
}

void ShapeDiffView::setOptions(float position, bool differencesOnly)
{
    _position = std::min(1.f, std::max(0.f, position));
    _differencesOnly = differencesOnly;
    apply();
}

void ShapeDiffView::updatePlacement()
{
    // The faces drawn are where each state had them, and this is drawn under
    // the object's own placement: taken out again.
    Base::Matrix4D matrix;
    if (auto geo = Base::freecad_dynamic_cast<App::GeoFeature>(_owner.getObject()))
        matrix = geo->Placement.getValue().inverse().toMatrix();
    _transform->setMatrix(Gui::ViewProvider::convert(matrix));
}

void ShapeDiffView::apply()
{
    auto seen = [](float weight) { return Ghost + (Full - Ghost) * weight; };
    const float opacity[Kinds] = {Thin, seen(1.f - _position), seen(_position),
                                  seen(1.f - _position), seen(_position)};
    _summary["position"] = static_cast<int>(std::lround(_position * 100.f));
    _summary["differencesOnly"] = _differencesOnly ? 1 : 0;
    for (int kind = 0; kind < Kinds; ++kind) {
        const bool shown = _view[kind] && !(kind == Same && _differencesOnly);
        _switch[kind]->whichChild = shown ? SO_SWITCH_ALL : SO_SWITCH_NONE;
        const int transparency = static_cast<int>(std::lround((1.f - opacity[kind]) * 100.f));
        if (_view[kind])
            _view[kind]->Transparency.setValue(transparency);
        _summary[std::string("shown.") + name(static_cast<Kind>(kind))] = shown ? 1 : 0;
        _summary[std::string("transparency.") + name(static_cast<Kind>(kind))] = transparency;
    }
}
