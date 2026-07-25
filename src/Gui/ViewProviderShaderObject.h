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

#ifndef GUI_ViewProviderShaderObject_H
#define GUI_ViewProviderShaderObject_H

#include "InventorBase.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderPythonFeature.h"

class SoShaderProgram;
class SoVertexShader;
class SoFragmentShader;
class SoSeparator;

namespace Gui {

/** View provider of App::ShaderProgram (docs/RenderDebug.md §6.5).
 *
 * Owns the Coin SoShaderProgram node built from the object's properties.
 * The node instance is shared by every consumer (App::Shader demo preview,
 * Appearance bindings), so a property edit updates the node fields and Coin
 * notification invalidates all enclosing render caches — no cross-view
 * notification needed. Displays nothing itself.
 */
class GuiExport ViewProviderShaderProgram : public ViewProviderDocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderShaderProgram);

public:
    ViewProviderShaderProgram();
    ~ViewProviderShaderProgram() override;

    void attach(App::DocumentObject *obj) override;
    void updateData(const App::Property *prop) override;
    bool isShow() const override {return true;}

    /// The shared shader program node consumers insert into their graphs
    SoShaderProgram *getShaderNode() const;

private:
    void updateShaderNode();

    CoinPtr<SoShaderProgram> pcShaderProgram;
    CoinPtr<SoVertexShader> pcVertexShader;
    CoinPtr<SoFragmentShader> pcFragmentShader;
};

using ViewProviderShaderProgramPython = ViewProviderPythonFeatureT<ViewProviderShaderProgram>;


/** View provider of App::Shader: the demo preview.
 *
 * Displays the effect on a built-in Coin primitive (Demo property) with the
 * linked programs' shared SoShaderProgram nodes inserted ahead of the shape
 * (the SoFCRenderMaterial placement rules). Scene-level ("post") programs
 * are not applied by the demo — activating those is the Appearance object's
 * job.
 */
class GuiExport ViewProviderShader : public ViewProviderDocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderShader);

public:
    ViewProviderShader();
    ~ViewProviderShader() override;

    void attach(App::DocumentObject *obj) override;
    void updateData(const App::Property *prop) override;
    std::vector<std::string> getDisplayModes() const override;
    void setDisplayMode(const char *ModeName) override;
    bool isShow() const override {return true;}

    /// Rebuild the demo subtree (also poked by program view providers on attach)
    void updateDemo();

private:
    CoinPtr<SoSeparator> pcDemoRoot;
};

using ViewProviderShaderPython = ViewProviderPythonFeatureT<ViewProviderShader>;


/** View provider of App::Appearance. Placeholder: binding activation
 * (path-keyed shader override capture) is implemented in the next slice.
 */
class GuiExport ViewProviderAppearance : public ViewProviderDocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderAppearance);

public:
    ViewProviderAppearance();
    ~ViewProviderAppearance() override;

    bool isShow() const override {return true;}
};

using ViewProviderAppearancePython = ViewProviderPythonFeatureT<ViewProviderAppearance>;

} // namespace Gui

#endif // GUI_ViewProviderShaderObject_H
