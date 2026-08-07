/***************************************************************************
 *   Copyright (c) 2017 Kustaa Nyholm  <kustaa.nyholm@sparetimelabs.com>   *
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
# include <algorithm>
# include <cfloat>
# ifdef FC_OS_WIN32
#  include <windows.h>
# endif
# ifdef FC_OS_MACOSX
#  include <OpenGL/gl.h>
# else
#  include <GL/gl.h>
# endif
# include <boost/math/constants/constants.hpp>
# include <Inventor/nodes/SoBaseColor.h>
# include <Inventor/nodes/SoCoordinate3.h>
# include <Inventor/nodes/SoDepthBuffer.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoIndexedFaceSet.h>
# include <Inventor/nodes/SoIndexedLineSet.h>
# include <Inventor/nodes/SoLightModel.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoOrthographicCamera.h>
# include <Inventor/nodes/SoRotation.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoShapeHints.h>
# include <Inventor/nodes/SoSwitch.h>
# include <Inventor/nodes/SoTexture2.h>
# include <Inventor/nodes/SoTextureCoordinate2.h>
# include <Inventor/nodes/SoTranslation.h>
# include <Inventor/events/SoEvent.h>
# include <Inventor/events/SoLocation2Event.h>
# include <Inventor/events/SoMouseButtonEvent.h>
# include <QApplication>
# include <QDialogButtonBox>
# include <QTimer>
# include <QSpinBox>
# include <QLineEdit>
# include <QCheckBox>
# include <QFontDialog>
# include <QFontInfo>
# include <QFontMetrics>
# include <QGridLayout>
# include <QCursor>
# include <QIcon>
# include <QImage>
# include <QMenu>
# include <QOpenGLTexture>
# include <QAction>
# include <QVBoxLayout>
# include <QPainterPath>
#endif


#include <App/Color.h>
#include <App/Document.h>
#include <Base/Tools.h>
#include <Base/UnitsApi.h>
#include <Eigen/Dense>

#include "NaviCube.h"

#include "Action.h"
#include "Application.h"
#include "Command.h"
#include "Action.h"
#include "MainWindow.h"
#include "SoTextImage.h"
#include "View3DInventorViewer.h"
#include "View3DInventor.h"
#include "Widgets.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Eigen;
using namespace std;
using namespace Gui;

class Face {
public:
	int m_FirstVertex;
	int m_VertexCount;
	GLuint m_TextureId;
	QColor m_Color;
	int m_PickId;
	int m_PickTexId;
	GLuint m_PickTextureId;
	int m_RenderPass;
	Face(
		 int firstVertex,
		 int vertexCount,
		 GLuint textureId,
		 int pickId,
		 int pickTexId,
		 GLuint pickTextureId,
		 const QColor& color,
		 int  renderPass
		)
	{
		m_FirstVertex = firstVertex;
		m_VertexCount = vertexCount;
		m_TextureId = textureId;
		m_PickId = pickId;
        m_PickTexId = pickTexId;
		m_PickTextureId = pickTextureId;
		m_Color = color;
		m_RenderPass = renderPass;
	}
};

enum { //
    TEX_FRONT = 1, // 0 is reserved for 'nothing picked'
    TEX_REAR,
    TEX_TOP,
    TEX_BOTTOM,
    TEX_LEFT,
    TEX_RIGHT,
    TEX_FRONT_FACE,
    TEX_CORNER_FACE,
    TEX_EDGE_FACE,
    TEX_FRONT_TOP,
    TEX_FRONT_BOTTOM,
    TEX_FRONT_LEFT,
    TEX_FRONT_RIGHT, 
    TEX_REAR_TOP,
    TEX_REAR_BOTTOM,
    TEX_REAR_LEFT,
    TEX_REAR_RIGHT,
    TEX_TOP_LEFT,
    TEX_TOP_RIGHT,
    TEX_BOTTOM_LEFT,
    TEX_BOTTOM_RIGHT,
    TEX_BOTTOM_RIGHT_REAR,
    TEX_BOTTOM_FRONT_RIGHT,
    TEX_BOTTOM_LEFT_FRONT,
    TEX_BOTTOM_REAR_LEFT,
    TEX_TOP_RIGHT_FRONT,
    TEX_TOP_FRONT_LEFT,
    TEX_TOP_LEFT_REAR,
    TEX_TOP_REAR_RIGHT,
    TEX_ARROW_NORTH,
    TEX_ARROW_NORTH_PICK,
    TEX_ARROW_SOUTH,
    TEX_ARROW_SOUTH_PICK,
    TEX_ARROW_EAST,
    TEX_ARROW_EAST_PICK,
    TEX_ARROW_WEST,
    TEX_ARROW_WEST_PICK,
    TEX_ARROW_RIGHT,
    TEX_ARROW_RIGHT_PICK,
    TEX_ARROW_LEFT,
    TEX_ARROW_LEFT_PICK,
    TEX_DOT_BACKSIDE,
    TEX_DOT_BACKSIDE_PICK,
    TEX_VIEW_MENU_ICON,
    TEX_VIEW_MENU_FACE
};
enum {
    DIR_UP,DIR_RIGHT,DIR_OUT
};
enum {
    SHAPE_SQUARE, SHAPE_EDGE, SHAPE_CORNER
};

class NaviCubeShared : public std::enable_shared_from_this<NaviCubeShared> {
public:
	NaviCubeShared()
        : m_labels {
            {"Front", "TextFront", "FRONT"},
            {"Rear", "TextRear", "REAR"},
            {"Top", "TextTop", "TOP"},
            {"Bottom", "TextBottom", "BOTTOM"},
            {"Right", "TextRight", "RIGHT"},
            {"Left", "TextLeft", "LEFT"},}
        , m_AxisLabels {
            {"X", "AxisLabelX", "X"},
            {"Y", "AxisLabelY", "Y"},
            {"Z", "AxisLabelZ", "Z"},}
        , m_colors {
            {"Text", "TextColor", Qt::black, m_TextColor},
            {"Highlight", "HiliteColor", QColor(170, 226, 255, 255), m_HiliteColor},
            {"Face", "FrontColor", QColor(226, 233, 239, 192), m_FrontFaceColor},
            {"Edge", "EdgeColor", QColor(226, 233, 239, 192).darker(140), m_EdgeFaceColor},
            {"Corner", "CornerColor", QColor(226, 233, 239, 192).darker(110), m_CornerFaceColor},
            {"Button", "ButtonColor", QColor(226, 233, 239, 128), m_ButtonColor},
            {"Border", "BorderColor", QColor(50, 50, 50, 255), m_BorderColor},
            {"Axis label", "AxisLabelColor", Qt::black, m_AxisLabelColor}}
    {
	    m_hGrp = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/NaviCube");
        getParams();
    }

	virtual ~ NaviCubeShared() {
        deinit();
    }

    static std::shared_ptr<NaviCubeShared> instance() {
        static std::weak_ptr<NaviCubeShared> _instance;
        auto res = _instance.lock();
        if (!res) {
            res = std::make_shared<NaviCubeShared>();
            _instance = res;
        }
        return res;
    }

	void handleMenu(QWidget *parent);
    void getParams();

	bool drawNaviCube(SoCamera *cam, int hiliteId, bool hit);

	/** Pick id under the cube-viewport coords (u, v), both 0..1 with v up.
	 *
	 * Geometric, no GL: the old implementation re-drew the cube into an FBO in
	 * fixed-function GL and read back a colour-coded pixel, which only works
	 * while the plain GL path owns the context -- with an external backend the
	 * cube is drawn from the Coin overlay graph and that pick pass silently
	 * produced nothing, so nothing ever highlighted and clicks were dead. These
	 * reproduce the same two passes analytically from the same data the drawing
	 * uses, so they also work in the WASM viewer.
	 */
	int pickButton(float u, float v) const;
	int pickCube(SoCamera *cam, float u, float v) const;

	bool initNaviCube();
	void addFace(const Vector3f&, const Vector3f&, int, int, int, bool flag=false);

    void setColors(QWidget *parent);
    void setLabels(QWidget *parent);
    QFont getLabelFont();
    void saveLabelFont(const QFont &);

    void setAxisLabels(QWidget *parent);
    QFont getAxisLabelFont();
    void saveAxisLabelFont(const QFont &);

	GLuint createCubeFaceTex(const char* text, int shape);
	GLuint createButtonTex(int button, bool stroke = true);
	GLuint createMenuTex(bool);

    void createAxisLabels();

    void deinit(QOpenGLContext *ctx = nullptr);

public:
	static int m_CubeWidgetSize;
    static long m_StepByTurn;
    static bool m_RotateToNearest;

    // With QPainter render hints, over sample is not really need. Resizing
    // texture just make it look worse.
	int m_OverSample = 1;

    QOpenGLContext *m_Context = nullptr;

	QColor m_TextColor;
	QColor m_HiliteColor;
	QColor m_ButtonColor;
	QColor m_FrontFaceColor;
	QColor m_EdgeFaceColor;
	QColor m_CornerFaceColor;
    QColor m_BorderColor;
	QColor m_AxisLabelColor;
    static bool m_ShowCS;
    static bool m_AutoHideCube;
    static bool m_AutoHideButton;
    static int m_AutoHideTimeout;

    QImage m_LabelX;
    QImage m_LabelY;
    QImage m_LabelZ;

	vector<GLubyte> m_IndexArray;
	vector<Vector2f> m_TextureCoordArray;
	vector<Vector3f> m_VertexArray;
	map<int, vector<Vector3f>> m_VertexArrays2;
	map<int,GLuint> m_Textures;
	vector<Face> m_Faces;
	vector<int> m_Buttons;
	vector<std::unique_ptr<QOpenGLTexture>> m_glTextures;
	// Coin overlay twins (raw-GL overlay Coin-ification, phase C): the
	// creators keep the rasterized images (bottom-up RGBA, same as the
	// GL upload) keyed by GL texture id so the per-viewer Coin graphs
	// can feed them as SoTexture2 images. m_TexGeneration bumps on
	// (re)init so the graphs rebuild after a deinit.
	map<GLuint, QImage> m_TexQImages;
	int m_TexGeneration = 0;

    ParameterGrp::handle m_hGrp;
    bool m_Saving = false;

    struct LabelInfo {
        const char *title;
        const char *name;
        const char *def;
    };
	vector<LabelInfo> m_labels;
	vector<LabelInfo> m_AxisLabels;

    struct ColorInfo {
        const char *title;
        const char *name;
        QColor def;
        QColor &color;
    };
	vector<ColorInfo> m_colors;

    QMenu m_Menu;
    QPointer<QDialog> m_DlgColors;
    QPointer<QDialog> m_DlgLabels;
    QPointer<QFontDialog> m_DlgFont;
    QPointer<QDialog> m_DlgAxisLabels;
    QPointer<QFontDialog> m_DlgAxisFont;
    static double m_BorderWidth;
    static double m_Chamfer;
};

int NaviCubeShared::m_CubeWidgetSize;
long NaviCubeShared::m_StepByTurn;
bool NaviCubeShared::m_RotateToNearest;
double NaviCubeShared::m_BorderWidth = 1.5;
double NaviCubeShared::m_Chamfer = 0.13;
bool NaviCubeShared::m_ShowCS;
bool NaviCubeShared::m_AutoHideCube;
bool NaviCubeShared::m_AutoHideButton;
int NaviCubeShared::m_AutoHideTimeout;

class NaviCubeImplementation : public ParameterGrp::ObserverType {
public:
	explicit NaviCubeImplementation(Gui::View3DInventorViewer*);
	~NaviCubeImplementation() override;
	void drawNaviCube();
	void drawCube();

	/// Observer message from the ParameterGrp
	void OnChange(ParameterGrp::SubjectType& rCaller, ParameterGrp::MessageType Reason) override;

	bool processSoEvent(const SoEvent* ev);

	// Coin overlay twins (raw-GL overlay Coin-ification, phase C).
	SoSeparator *getOverlayCubeGraph(Render::OverlayAnchor &anchor);
	SoSeparator *getOverlayButtonGraph(Render::OverlayAnchor &anchor);

private:
	void buildCoinCube();
	void buildCoinButtons();
	void fillCornerAnchor(Render::OverlayAnchor &anchor) const;

	bool mousePressed(short x, short y);
	bool mouseReleased(short x, short y);
	bool mouseMoved(short x, short y);
	int pickFace(short x, short y);
	bool inDragZone(short x, short y);

	void handleResize();

	void setHilite(int);

	SbRotation setView(float, float) const;
	SbRotation rotateView(SbRotation, int axis, float rotAngle, SbVec3f customAxis = SbVec3f(0, 0, 0)) const;
	void rotateView(const SbRotation&);
	void handleMenu();

public:
	Gui::View3DInventorViewer* m_View3DInventorViewer;
    std::shared_ptr<NaviCubeShared> m_Shared;
    ParameterGrp::handle m_hGrp;

	int m_CubeWidgetPosX = 0;
	int m_CubeWidgetPosY = 0;
	int m_CubeWidgetOffsetX = 0;
	int m_CubeWidgetOffsetY = 0;
	int m_PrevWidth = 0;
	int m_PrevHeight = 0;
	int m_HiliteId = 0;
	bool m_MouseDown = false;
	bool m_Dragging = false;
	bool m_MightDrag = false;
    bool m_Hit = false;
    NaviCube::Corner m_Corner = NaviCube::TopRightCorner;

	int &m_CubeWidgetSize = NaviCubeShared::m_CubeWidgetSize;
    QTimer timer;
    QTimer autoHideTimer;

	// Coin overlay twins: per-viewer graphs over the shared cube data
	// (textures/vertices live in NaviCubeShared; hover state and camera
	// sync are per viewer). Rebuilt when the shared data regenerates.
	struct CoinFace {
		int faceIndex;                    // into m_Shared->m_Faces
		SoMaterial *material;
		SoTextureCoordinate2 *texCoords;  // text faces only (flip)
		float uv = 1.0f;                  // last applied flip factor
	};
	CoinPtr<SoSeparator> m_CoinCubeRoot;
	std::vector<CoinFace> m_CoinFaces;
	CoinPtr<SoSeparator> m_CoinButtonRoot;
	std::vector<std::pair<int, SoMaterial*>> m_CoinButtons;
	CoinPtr<SoSwitch> m_CoinMenuHilite;
	int m_CoinGeneration = -1;
};

int NaviCube::getNaviCubeSize()
{
    return NaviCubeShared::m_CubeWidgetSize;
}

NaviCube::NaviCube(Gui::View3DInventorViewer* viewer) {
	m_NaviCubeImplementation = new NaviCubeImplementation(viewer);
}

NaviCube::~NaviCube() {
	delete m_NaviCubeImplementation;
}

void NaviCube::drawNaviCube() {
	m_NaviCubeImplementation->drawNaviCube();
}

bool NaviCube::processSoEvent(const SoEvent* ev) {
	return m_NaviCubeImplementation->processSoEvent(ev);
}

SoSeparator *NaviCube::getOverlayCubeGraph(Render::OverlayAnchor &anchor) {
	return m_NaviCubeImplementation->getOverlayCubeGraph(anchor);
}

SoSeparator *NaviCube::getOverlayButtonGraph(Render::OverlayAnchor &anchor) {
	return m_NaviCubeImplementation->getOverlayButtonGraph(anchor);
}

void NaviCube::setCorner(Corner c) {
    if (m_NaviCubeImplementation->m_Corner != c) {
        m_NaviCubeImplementation->m_Corner = c;
        m_NaviCubeImplementation->m_PrevWidth = 0;
        m_NaviCubeImplementation->m_PrevHeight = 0;
	    m_NaviCubeImplementation->m_View3DInventorViewer->getSoRenderManager()->scheduleRedraw();
    }
}

NaviCubeImplementation::NaviCubeImplementation(Gui::View3DInventorViewer* viewer)
	: m_View3DInventorViewer(viewer)
    , m_Shared(NaviCubeShared::instance())
    , m_hGrp(m_Shared->m_hGrp)
{
    m_hGrp->Attach(this);

    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [this](){
        m_Shared->getParams();
	    m_View3DInventorViewer->getSoRenderManager()->scheduleRedraw();
    });

    autoHideTimer.setSingleShot(true);
    QObject::connect(&autoHideTimer, &QTimer::timeout, [this](){
	    m_View3DInventorViewer->getSoRenderManager()->scheduleRedraw();
    });
}

NaviCubeImplementation::~NaviCubeImplementation() {
	m_hGrp->Detach(this);
    m_Shared->deinit(QOpenGLContext::currentContext());
}

void NaviCubeShared::getParams()
{
    for (auto &info : m_colors)
        info.color = QColor::fromRgba(m_hGrp->GetUnsigned(info.name, info.def.rgba()));
    m_CubeWidgetSize = m_hGrp->GetInt("CubeSize", 132);
    m_RotateToNearest = m_hGrp->GetBool("NaviRotateToNearest", true);
    m_StepByTurn = m_hGrp->GetInt("NaviStepByTurn", 8);
    m_ShowCS = m_hGrp->GetBool("ShowCS", true);
    m_BorderWidth = m_hGrp->GetFloat("BorderWidth", 1.5);
    m_Chamfer = m_hGrp->GetFloat("ChamferSize", 0.12);
    m_AutoHideCube = m_hGrp->GetBool("AutoHideCube", false);
    m_AutoHideButton = m_hGrp->GetBool("AutoHideButton", true);
    m_AutoHideTimeout = m_hGrp->GetInt("AutoHideTimeout", 300);
    deinit();
}

void NaviCubeShared::deinit(QOpenGLContext *ctx)
{
    // QOpenGLTexture insists on being destroyed only under the original
    // context that created itself, or else just refuse to delete even if the
    // context is being destroyed (which is kind of absurd IMO). So we'll have
    // to remember the original context, and deinit it here and let it be
    // recreated in another context.
    if (ctx && ctx != m_Context)
        return;

    m_Context = nullptr;

    m_glTextures.clear();
	m_IndexArray.clear();
	m_TextureCoordArray.clear();
	m_VertexArray.clear();
	m_VertexArrays2.clear();
	m_Textures.clear();
	m_Faces.clear();
	m_Buttons.clear();
	m_TexQImages.clear();
	// Invalidate the per-viewer Coin overlay graphs built on this data.
	++m_TexGeneration;
}

void NaviCubeImplementation::OnChange(ParameterGrp::SubjectType &, ParameterGrp::MessageType)
{
    if (!m_Shared->m_Saving)
        timer.start(200);
}

auto convertWeights = [](int weight) -> QFont::Weight {
    // Values above the legacy 0-99 range are already on the Qt6 100-900 scale
    if (weight > 99)
        return QFont::Weight(qBound(100, weight, 900));
    if (weight >= 87)
        return QFont::Black;
    if (weight >= 81)
        return QFont::ExtraBold;
    if (weight >= 75)
        return QFont::Bold;
    if (weight >= 63)
        return QFont::DemiBold;
    if (weight >= 57)
        return QFont::Medium;
    if (weight >= 50)
        return QFont::Normal;
    if (weight >= 25)
        return QFont::Light;
    if (weight >= 12)
        return QFont::ExtraLight;
    return QFont::Thin;
};

GLuint NaviCubeShared::createCubeFaceTex(const char* text, int shape) {
	int texSize = m_CubeWidgetSize * m_OverSample;
	float gapi = texSize * m_Chamfer;
	QImage image(texSize, texSize, QImage::Format_ARGB32);
	image.fill(qRgba(255, 255, 255, 0));
	QPainter paint;
	paint.begin(&image);
	paint.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

	if (text) {
		paint.setPen(Qt::white);
		paint.setFont(getLabelFont());
		paint.drawText(QRect(0, 0, texSize, texSize), Qt::AlignCenter,qApp->translate("Gui::NaviCube",text));
	}
	else if (shape == SHAPE_SQUARE) {
		QPainterPath pathSquare;
		auto rectSquare = QRectF(gapi, gapi, (qreal)texSize - 2.0 * gapi, (qreal)texSize - 2.0 * gapi);
		// Qt's coordinate system is x->left y->down, this must be taken into account on operations
		pathSquare.moveTo(rectSquare.left()         , rectSquare.bottom() - gapi);
		pathSquare.lineTo(rectSquare.left() + gapi  , rectSquare.bottom());
		pathSquare.lineTo(rectSquare.right() - gapi , rectSquare.bottom());
		pathSquare.lineTo(rectSquare.right()        , rectSquare.bottom() - gapi);
		pathSquare.lineTo(rectSquare.right()        , rectSquare.top() + gapi);
		pathSquare.lineTo(rectSquare.right() - gapi , rectSquare.top());
		pathSquare.lineTo(rectSquare.left() + gapi  , rectSquare.top());
		pathSquare.lineTo(rectSquare.left()         , rectSquare.top() + gapi);
		pathSquare.closeSubpath();
		paint.fillPath(pathSquare, Qt::white);
	}
	else if (shape == SHAPE_CORNER) {
		QPainterPath pathCorner;
		// the hexagon edges are of length sqrt(2) * gapi
		const auto hexWidth = 2 * sqrt(2) * gapi; // hexagon vertex to vertex distance
		const auto hexHeight = sqrt(3) * sqrt(2) * gapi; // edge to edge distance
		auto rectCorner = QRectF((texSize - hexWidth) / 2, (texSize - hexHeight) / 2, hexWidth, hexHeight);
		// Qt's coordinate system is x->left y->down, this must be taken into account on operations
		pathCorner.moveTo(rectCorner.left()                   , rectCorner.bottom() - hexHeight / 2); // left middle vertex
		pathCorner.lineTo(rectCorner.left() + hexWidth * 0.25 , rectCorner.bottom()); // left lower
		pathCorner.lineTo(rectCorner.left() + hexWidth * 0.75 , rectCorner.bottom()); // right lower
		pathCorner.lineTo(rectCorner.right()                  , rectCorner.bottom() - hexHeight / 2); // right middle
		pathCorner.lineTo(rectCorner.left() + hexWidth * 0.75 , rectCorner.top()); // right upper
		pathCorner.lineTo(rectCorner.left() + hexWidth * 0.25 , rectCorner.top()); // left upper
		pathCorner.closeSubpath();
		paint.fillPath(pathCorner, Qt::white);
	}
	else if (shape == SHAPE_EDGE) {
		QPainterPath pathEdge;
		// since the gap is 0.12, the rect must be geometriclly shifted up with a factor
		pathEdge.addRect(QRectF(2 * gapi, ((qreal)texSize - sqrt(2) * gapi) * 0.5, (qreal)texSize - 4.0 * gapi, sqrt(2) * gapi));
		paint.fillPath(pathEdge, Qt::white);
	}

	paint.end();
    auto texture = new QOpenGLTexture(image.mirrored());
    m_glTextures.emplace_back(texture);
    texture->setMaximumAnisotropy(4.0);
	texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
	texture->generateMipMaps();
    // Bottom-up RGBA copy for the Coin overlay twins (SoTexture2 images).
    m_TexQImages[texture->textureId()] =
        image.mirrored().convertToFormat(QImage::Format_RGBA8888);
    return texture->textureId();
}

void NaviCubeShared::createAxisLabels()
{
    QFont font = getAxisLabelFont();
    QFontMetrics fm(font);

    auto create = [&](const LabelInfo &info) {
        auto text = QString::fromUtf8(m_hGrp->GetASCII(info.name, info.def).c_str());
        if (text.isEmpty())
            return QImage();
        QSize size = fm.size(Qt::TextSingleLine, text);
        QPainter paint;
        QImage image(size, QImage::Format_Mono);
        image.fill(0);
        paint.begin(&image);
        paint.setPen(Qt::white);
        paint.setFont(font);
        paint.drawText(QRect(QPoint(), size), Qt::AlignCenter, text);
        paint.end();
        return image.mirrored();
    };

	m_LabelX = create(m_AxisLabels[0]);
	m_LabelY = create(m_AxisLabels[1]);
	m_LabelZ = create(m_AxisLabels[2]);
}

GLuint NaviCubeShared::createButtonTex(int button, bool stroke) {
	int texSize = m_CubeWidgetSize * m_OverSample;
	QImage image(texSize, texSize, QImage::Format_ARGB32);
	image.fill(qRgba(255, 255, 255, 0));
	QPainter painter;
	painter.begin(&image);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

	QTransform transform;
	transform.translate(texSize / 2, texSize / 2);
	transform.scale(texSize / 2, texSize / 2);
	painter.setTransform(transform);

	QPainterPath path;

	float as1 = 0.18f; // arrow size
	float as3 = as1 / 3;

	switch (button) {
	default:
		break;
	case TEX_ARROW_RIGHT:
	case TEX_ARROW_LEFT: {
		QRectF r(-1.00, -1.00, 2.00, 2.00);
		QRectF r0(r);
		r.adjust(as3, as3, -as3, -as3);
		QRectF r1(r);
		r.adjust(as3, as3, -as3, -as3);
		QRectF r2(r);
		r.adjust(as3, as3, -as3, -as3);
		QRectF r3(r);
		r.adjust(as3, as3, -as3, -as3);
		QRectF r4(r);

		float a0 = 72;
		float a1 = 45;
		float a2 = 32;

		if (TEX_ARROW_LEFT == button) {
			a0 = 180 - a0;
			a1 = 180 - a1;
			a2 = 180 - a2;
		}

		path.arcMoveTo(r0, a1);
		QPointF p0 = path.currentPosition();

		path.arcMoveTo(r2, a2);
		QPointF p1 = path.currentPosition();

		path.arcMoveTo(r4, a1);
		QPointF p2 = path.currentPosition();

		path.arcMoveTo(r1, a0);
		path.arcTo(r1, a0, -(a0 - a1));
		path.lineTo(p0);
		path.lineTo(p1);
		path.lineTo(p2);
		path.arcTo(r3, a1, +(a0 - a1));
		break;
	}
	case TEX_ARROW_EAST: {
		path.moveTo(1, 0);
		path.lineTo(1 - as1, +as1);
		path.lineTo(1 - as1, -as1);
		break;
	}
	case TEX_ARROW_WEST: {
		path.moveTo(-1, 0);
		path.lineTo(-1 + as1, -as1);
		path.lineTo(-1 + as1, +as1);
		break;
	}
	case TEX_ARROW_SOUTH: {
		path.moveTo(0, 1);
		path.lineTo(-as1, 1 - as1);
		path.lineTo(+as1, 1 - as1);
		break;
	}
	case TEX_ARROW_NORTH: {
		path.moveTo(0, -1);
		path.lineTo(+as1, -1 + as1);
		path.lineTo(-as1, -1 + as1);
		break;
	}
	case TEX_DOT_BACKSIDE: {
		path.addRoundedRect(QRectF(0.99 - as1, -0.99, as1, as1), as1*0.5, as1*0.5);
		break;
	}
	}

	painter.fillPath(path, Qt::white);
    if (stroke) {
        path.closeSubpath();
        painter.strokePath(path, QPen(Qt::black, 0));
    }

	painter.end();
	//image.save(str(enum2str(button))+str(".png"));

    auto texture = new QOpenGLTexture(image.mirrored());
    m_glTextures.emplace_back(texture);
    texture->setMaximumAnisotropy(4.0);
	texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
	texture->generateMipMaps();
    // Bottom-up RGBA copy for the Coin overlay twins (SoTexture2 images).
    m_TexQImages[texture->textureId()] =
        image.mirrored().convertToFormat(QImage::Format_RGBA8888);
    return texture->textureId();
}

GLuint NaviCubeShared::createMenuTex(bool forPicking) {
	int texSize = m_CubeWidgetSize * m_OverSample;
	QImage image(texSize, texSize, QImage::Format_ARGB32);
	image.fill(qRgba(0, 0, 0, 0));
	QPainter painter;
	painter.begin(&image);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

	QTransform transform;
	transform.translate(texSize * 12 / 16, texSize * 13 / 16);
	transform.scale(texSize / 200.0, texSize / 200.0); // 200 == size at which this was designed
	painter.setTransform(transform);

	QPainterPath path;

	if (forPicking) {
		path.addRoundedRect(-25, -8, 75, 45, 6, 6);
		painter.fillPath(path, Qt::white);
	}
	else {
		// top
		path.moveTo(0, 0);
		path.lineTo(15, 5);
		path.lineTo(0, 10);
		path.lineTo(-15, 5);

		painter.fillPath(path, QColor(240, 240, 240));

		// left
		QPainterPath path2;
		path2.lineTo(0, 10);
		path2.lineTo(-15, 5);
		path2.lineTo(-15, 25);
		path2.lineTo(0, 30);
		painter.fillPath(path2, QColor(190, 190, 190));

		// right
		QPainterPath path3;
		path3.lineTo(0, 10);
		path3.lineTo(15, 5);
		path3.lineTo(15, 25);
		path3.lineTo(0, 30);
		painter.fillPath(path3, QColor(220, 220, 220));

		// outline
		QPainterPath path4;
		path4.moveTo(0, 0);
		path4.lineTo(15, 5);
		path4.lineTo(15, 25);
		path4.lineTo(0, 30);
		path4.lineTo(-15, 25);
		path4.lineTo(-15, 5);
		path4.lineTo(0, 0);
		painter.strokePath(path4, QColor(128, 128, 128));

		// menu triangle
		QPainterPath path5;
		path5.moveTo(20, 10);
		path5.lineTo(40, 10);
		path5.lineTo(30, 20);
		path5.lineTo(20, 10);
		painter.fillPath(path5, QColor(64, 64, 64));
	}
	painter.end();
    auto texture = new QOpenGLTexture(image.mirrored());
    m_glTextures.emplace_back(texture);
    texture->setMaximumAnisotropy(4.0);
	texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
	texture->generateMipMaps();
    // Bottom-up RGBA copy for the Coin overlay twins (SoTexture2 images).
    m_TexQImages[texture->textureId()] =
        image.mirrored().convertToFormat(QImage::Format_RGBA8888);
    return texture->textureId();
}

void NaviCubeShared::addFace(const Vector3f& x, const Vector3f& z, int frontTex, int pickTex, int pickId, bool text) {
	Vector3f y = x.cross(-z);
	y = y / y.norm() * x.norm();

    // The exact chamfered-cube region polygon for this face: an octagon for
    // the 6 main faces, a rectangle for the 12 edge bevels, a hexagon for the
    // 8 corners. These points are the *actual* fill/pick geometry now (not a
    // full square masked by an alpha texture), so the drawn silhouette and the
    // pick region match the visible cube exactly. They are also reused to
    // stroke the borders (see drawNaviCube / buildCoinCube). Winding matches
    // the old square (CCW in local x/y) so GL_TRIANGLE_FAN + backface culling
    // stay correct.
    if (pickTex == TEX_FRONT_FACE) {
        auto x2 = x * (1 - m_Chamfer * 2);
        auto y2 = y * (1 - m_Chamfer * 2);
        auto x4 = x * (1 - m_Chamfer * 4);
        auto y4 = y * (1 - m_Chamfer * 4);
        m_VertexArrays2[pickId].reserve(8);
        m_VertexArrays2[pickId].emplace_back(z - x2 - y4);
        m_VertexArrays2[pickId].emplace_back(z - x4 - y2);
        m_VertexArrays2[pickId].emplace_back(z + x4 - y2);
        m_VertexArrays2[pickId].emplace_back(z + x2 - y4);

        m_VertexArrays2[pickId].emplace_back(z + x2 + y4);
        m_VertexArrays2[pickId].emplace_back(z + x4 + y2);
        m_VertexArrays2[pickId].emplace_back(z - x4 + y2);
        m_VertexArrays2[pickId].emplace_back(z - x2 + y4);
    }
    else if (pickTex == TEX_EDGE_FACE) {
        auto x4 = x * (1 - m_Chamfer * 4);
        auto y_sqrt2 = y * sqrt(2) * m_Chamfer;
        m_VertexArrays2[pickId].reserve(4);
        m_VertexArrays2[pickId].emplace_back(z - x4 - y_sqrt2);
        m_VertexArrays2[pickId].emplace_back(z + x4 - y_sqrt2);
        m_VertexArrays2[pickId].emplace_back(z + x4 + y_sqrt2);
        m_VertexArrays2[pickId].emplace_back(z - x4 + y_sqrt2);
    }
    else if (pickTex == TEX_CORNER_FACE) {
        auto x_sqrt2 = x * sqrt(2) * m_Chamfer;
        auto y_sqrt6 = y * sqrt(6) * m_Chamfer;
        m_VertexArrays2[pickId].reserve(6);
        m_VertexArrays2[pickId].emplace_back(z - 2 * x_sqrt2);
        m_VertexArrays2[pickId].emplace_back(z - x_sqrt2 - y_sqrt6);
        m_VertexArrays2[pickId].emplace_back(z + x_sqrt2 - y_sqrt6);
        m_VertexArrays2[pickId].emplace_back(z + 2 * x_sqrt2);
        m_VertexArrays2[pickId].emplace_back(z + x_sqrt2 + y_sqrt6);
        m_VertexArrays2[pickId].emplace_back(z - x_sqrt2 + y_sqrt6);
    }

    // Fill face: the exact region polygon, untextured flat color. Texture ids
    // are 0 (fill has no glyph) so the fill/label classifier
    // (m_TextureId == m_PickTextureId) reads true for fill and false for the
    // label square emitted below. Texcoords are computed from each point's
    // (x, y) projection so the parallel arrays stay well-formed for the Coin
    // twin, even though the untextured fill doesn't sample them.
    const auto &poly = m_VertexArrays2[pickId];
    const float xx = x.dot(x), yy = y.dot(y);
    int fillStart = int(m_VertexArray.size());
    for (const auto &p : poly) {
        m_VertexArray.push_back(p);
        Vector3f d = p - z;
        m_TextureCoordArray.emplace_back(0.5f + 0.5f * d.dot(x) / xx,
                                         0.5f + 0.5f * d.dot(y) / yy);
    }
    m_Faces.emplace_back(
        int(m_IndexArray.size()),
        int(poly.size()),
        0,
        pickId,
        pickTex,
        0,
        pickTex == TEX_EDGE_FACE ? m_EdgeFaceColor :
            (pickTex == TEX_CORNER_FACE ? m_CornerFaceColor : m_FrontFaceColor),
        1);
    for (size_t i = 0; i < poly.size(); i++)
        m_IndexArray.push_back(GLubyte(fillStart + i));

    // Label face: a separate centered 4-vert square carrying the glyph texture
    // (rendered upright in drawNaviCube's text pass, which relies on the 4
    // corners). Kept as its own square so the octagon fill doesn't distort the
    // text; picking skips it (untextured pick pass) so it never broadens the
    // pick region.
    if (text) {
        // Lift the label square a hair off the face plane along the face
        // normal. It is coplanar with its own octagon fill, so under the
        // LEQUAL depth test (and different triangulation) the two z-fight and
        // the plain fill intermittently hides the glyph at some view angles;
        // the small outward offset makes the label deterministically win.
        const Vector3f zt = z + z.normalized() * 0.01f;
        int textStart = int(m_VertexArray.size());
        m_VertexArray.emplace_back(zt - x - y);
        m_TextureCoordArray.emplace_back(0, 0);
        m_VertexArray.emplace_back(zt + x - y);
        m_TextureCoordArray.emplace_back(1, 0);
        m_VertexArray.emplace_back(zt + x + y);
        m_TextureCoordArray.emplace_back(1, 1);
        m_VertexArray.emplace_back(zt - x + y);
        m_TextureCoordArray.emplace_back(0, 1);
        m_Faces.emplace_back(
            int(m_IndexArray.size()),
            4,
            m_Textures[frontTex],
            pickId,
            pickTex,
            0,
            m_TextColor,
            2);
        for (int i = 0; i < 4; i++)
            m_IndexArray.push_back(GLubyte(textStart + i));
    }
}

bool NaviCubeShared::initNaviCube() {
    if (m_Context)
        return false;

    m_Context = QOpenGLContext::currentContext();
    if (!m_Context)
        return false;

    Vector3f x(1, 0, 0);
    Vector3f y(0, 1, 0);
    Vector3f z(0, 0, 1);

	float cs, sn;
	cs = cos(90 * M_PI / 180);
	sn = sin(90 * M_PI / 180);
	Matrix3f r90x;
	r90x << 1, 0, 0,
			0, cs, -sn,
			0, sn, cs;

	Matrix3f r90y;
	r90y << cs, 0, sn,
		     0, 1, 0,
		   -sn, 0, cs;

	Matrix3f r90z;
	r90z << cs, sn, 0,
			-sn, cs, 0,
			0, 0, 1;

	cs = cos(45 * M_PI / 180);
	sn = sin(45 * M_PI / 180);
	Matrix3f r45x;
	r45x << 1, 0, 0,
		    0, cs, -sn,
		    0, sn, cs;

	Matrix3f r45z;
	r45z << cs, sn, 0,
			-sn, cs, 0,
			0, 0, 1;

	// first create front and backside of faces
	m_Textures[TEX_FRONT_FACE] = createCubeFaceTex(nullptr, SHAPE_SQUARE);

    vector<string> labels;
	for (auto &info : m_labels)
		labels.push_back(m_hGrp->GetASCII(
                    info.name, QObject::tr(info.def).toUtf8().constData()));

	// create the main faces
	m_Textures[TEX_FRONT] = createCubeFaceTex(labels[0].c_str(), SHAPE_SQUARE);
	m_Textures[TEX_REAR] = createCubeFaceTex(labels[1].c_str(), SHAPE_SQUARE);
	m_Textures[TEX_TOP] = createCubeFaceTex(labels[2].c_str(), SHAPE_SQUARE);
	m_Textures[TEX_BOTTOM] = createCubeFaceTex(labels[3].c_str(), SHAPE_SQUARE);
	m_Textures[TEX_RIGHT] = createCubeFaceTex(labels[4].c_str(), SHAPE_SQUARE);
	m_Textures[TEX_LEFT] = createCubeFaceTex(labels[5].c_str(), SHAPE_SQUARE);

	// create the arrows
	m_Textures[TEX_ARROW_NORTH] = createButtonTex(TEX_ARROW_NORTH);
	m_Textures[TEX_ARROW_SOUTH] = createButtonTex(TEX_ARROW_SOUTH);
	m_Textures[TEX_ARROW_EAST] = createButtonTex(TEX_ARROW_EAST);
	m_Textures[TEX_ARROW_WEST] = createButtonTex(TEX_ARROW_WEST);
	m_Textures[TEX_ARROW_LEFT] = createButtonTex(TEX_ARROW_LEFT);
	m_Textures[TEX_ARROW_RIGHT] = createButtonTex(TEX_ARROW_RIGHT);
	m_Textures[TEX_DOT_BACKSIDE] = createButtonTex(TEX_DOT_BACKSIDE);
	m_Textures[TEX_ARROW_NORTH_PICK] = createButtonTex(TEX_ARROW_NORTH, false);
	m_Textures[TEX_ARROW_SOUTH_PICK] = createButtonTex(TEX_ARROW_SOUTH, false);
	m_Textures[TEX_ARROW_EAST_PICK] = createButtonTex(TEX_ARROW_EAST, false);
	m_Textures[TEX_ARROW_WEST_PICK] = createButtonTex(TEX_ARROW_WEST, false);
	m_Textures[TEX_ARROW_LEFT_PICK] = createButtonTex(TEX_ARROW_LEFT, false);
	m_Textures[TEX_ARROW_RIGHT_PICK] = createButtonTex(TEX_ARROW_RIGHT, false);
	m_Textures[TEX_DOT_BACKSIDE_PICK] = createButtonTex(TEX_DOT_BACKSIDE, false);

	m_Textures[TEX_VIEW_MENU_ICON] = createMenuTex(false);
	m_Textures[TEX_VIEW_MENU_FACE] = createMenuTex(true);

	// front,back,pick,pickid
	addFace(x, z, TEX_TOP, TEX_FRONT_FACE, TEX_TOP, true);
	x = r90x * x;
	z = r90x * z;
	addFace(x, z, TEX_FRONT, TEX_FRONT_FACE, TEX_FRONT, true);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_LEFT, TEX_FRONT_FACE, TEX_LEFT, true);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_REAR, TEX_FRONT_FACE, TEX_REAR, true);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_RIGHT, TEX_FRONT_FACE, TEX_RIGHT, true);
	x = r90x * r90z * x;
	z = r90x * r90z * z;
	addFace(x, z, TEX_BOTTOM, TEX_FRONT_FACE, TEX_BOTTOM, true);

	// add corner faces
	m_Textures[TEX_CORNER_FACE] = createCubeFaceTex(nullptr, SHAPE_CORNER);
	// we need to rotate to the edge, thus matrix for rotation angle of 54.7 deg
	cs = cos(atan(sqrt(2.0)));
	sn = sin(atan(sqrt(2.0)));
	Matrix3f r54x;
	r54x << 1, 0, 0,
		     0, cs, -sn,
		     0, sn, cs;

	z = r45z * r54x * z;
	x = r45z * r54x * x;
	z *= sqrt(3) * (1 - 2 * m_Chamfer); // corner face position along the cube diagonal

	addFace(x, z, TEX_CORNER_FACE, TEX_CORNER_FACE, TEX_BOTTOM_RIGHT_REAR);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_CORNER_FACE, TEX_CORNER_FACE, TEX_BOTTOM_FRONT_RIGHT);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_CORNER_FACE, TEX_CORNER_FACE, TEX_BOTTOM_LEFT_FRONT);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_CORNER_FACE, TEX_CORNER_FACE, TEX_BOTTOM_REAR_LEFT);
	x = r90x * r90x * r90z * x;
	z = r90x * r90x * r90z * z;
	addFace(x, z, TEX_CORNER_FACE, TEX_CORNER_FACE, TEX_TOP_RIGHT_FRONT);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_CORNER_FACE, TEX_CORNER_FACE, TEX_TOP_FRONT_LEFT);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_CORNER_FACE, TEX_CORNER_FACE, TEX_TOP_LEFT_REAR);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_CORNER_FACE, TEX_CORNER_FACE, TEX_TOP_REAR_RIGHT);

	// add edge faces
	m_Textures[TEX_EDGE_FACE] = createCubeFaceTex(nullptr, SHAPE_EDGE);
	// first back to top side
	x[0] = 1; x[1] = 0; x[2] = 0;
	z[0] = 0; z[1] = 0; z[2] = 1;
	// rotate 45 degrees up
	z = r45x * z;
	x = r45x * x;
	z *= sqrt(2) * (1 - m_Chamfer);
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_FRONT_TOP);
	x = r90x * x;
	z = r90x * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_FRONT_BOTTOM);
	x = r90x * x;
	z = r90x * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_REAR_BOTTOM);
	x = r90x * x;
	z = r90x * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_REAR_TOP);
	x = r90y * x;
	z = r90y * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_REAR_RIGHT);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_FRONT_RIGHT);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_FRONT_LEFT);
	x = r90z * x;
	z = r90z * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_REAR_LEFT);
	x = r90x * x;
	z = r90x * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_TOP_LEFT);
	x = r90y * x;
	z = r90y * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_TOP_RIGHT);
	x = r90y * x;
	z = r90y * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_BOTTOM_RIGHT);
	x = r90y * x;
	z = r90y * z;
	addFace(x, z, TEX_EDGE_FACE, TEX_EDGE_FACE, TEX_BOTTOM_LEFT);

	m_Buttons.push_back(TEX_ARROW_NORTH);
	m_Buttons.push_back(TEX_ARROW_SOUTH);
	m_Buttons.push_back(TEX_ARROW_EAST);
	m_Buttons.push_back(TEX_ARROW_WEST);
	m_Buttons.push_back(TEX_ARROW_LEFT);
	m_Buttons.push_back(TEX_ARROW_RIGHT);
	m_Buttons.push_back(TEX_DOT_BACKSIDE);

    createAxisLabels();

    return true;
}

void NaviCubeImplementation::drawNaviCube() {
	glViewport(m_CubeWidgetPosX - m_CubeWidgetSize / 2, m_CubeWidgetPosY - m_CubeWidgetSize / 2, m_CubeWidgetSize, m_CubeWidgetSize);
	drawCube();
}

void NaviCubeImplementation::handleResize() {
	SbVec2s view = m_View3DInventorViewer->getSoRenderManager()->getSize();
	if ((m_PrevWidth != view[0]) || (m_PrevHeight != view[1])) {
		if ((m_PrevWidth <= 0) || (m_PrevHeight <= 0)) {
		    // initial position
			m_CubeWidgetOffsetX = m_hGrp->GetInt("OffsetX", 0);
			m_CubeWidgetOffsetY = m_hGrp->GetInt("OffsetY", 0);
        }
        switch (m_Corner) {
        case NaviCube::TopLeftCorner:
            m_CubeWidgetPosX = m_CubeWidgetSize*1.1 / 2 + m_CubeWidgetOffsetX;
            m_CubeWidgetPosY = view[1] - m_CubeWidgetSize*1.1 / 2 - m_CubeWidgetOffsetY;
            break;
        case NaviCube::TopRightCorner:
            m_CubeWidgetPosX = view[0] - m_CubeWidgetSize*1.1 / 2 - m_CubeWidgetOffsetX;
            m_CubeWidgetPosY = view[1] - m_CubeWidgetSize*1.1 / 2 - m_CubeWidgetOffsetY;
            break;
        case NaviCube::BottomLeftCorner:
            m_CubeWidgetPosX = m_CubeWidgetSize*1.1 / 2 + m_CubeWidgetOffsetX;
            m_CubeWidgetPosY = m_CubeWidgetSize*1.1 / 2 + m_CubeWidgetOffsetY;
            break;
        case NaviCube::BottomRightCorner:
            m_CubeWidgetPosX = view[0] - m_CubeWidgetSize*1.1 / 2 - m_CubeWidgetOffsetX;
            m_CubeWidgetPosY = m_CubeWidgetSize*1.1 / 2 + m_CubeWidgetOffsetY;
            break;
        }
		m_PrevWidth = view[0];
		m_PrevHeight = view[1];

        if (m_CubeWidgetPosX < 0)
            m_CubeWidgetPosX = 0;
        else if (m_CubeWidgetPosX > m_PrevWidth)
            m_CubeWidgetPosX = m_PrevWidth;
        if (m_CubeWidgetPosY < 0)
            m_CubeWidgetPosY = 0;
        else if (m_CubeWidgetPosY > m_PrevHeight)
            m_CubeWidgetPosY = m_PrevHeight;

		m_View3DInventorViewer->getSoRenderManager()->scheduleRedraw();
	}
}

void NaviCubeImplementation::drawCube() {
	SoCamera* cam = m_View3DInventorViewer->getSoRenderManager()->getCamera();

	if (!cam)
		return;

	handleResize();
	if (m_Shared->drawNaviCube(cam, m_HiliteId, m_Hit))
		m_View3DInventorViewer->getSoRenderManager()->scheduleRedraw();
}

namespace {
// Guarded material writer for the overlay graphs: Coin notifies on every
// field write (= cache rebuild + backend re-feed), so only touch fields
// whose values actually changed.
void syncQColor(SoMaterial *mat, const QColor &c)
{
	SbColor col(float(c.redF()), float(c.greenF()), float(c.blueF()));
	if (mat->diffuseColor.getNum() != 1 || mat->diffuseColor[0] != col)
		mat->diffuseColor = col;
	float transp = 1.0f - float(c.alphaF());
	if (mat->transparency.getNum() != 1 || mat->transparency[0] != transp)
		mat->transparency = transp;
}

// Line-stroke letter shapes for the corner-axes labels, like the viewer's
// axis-cross overlay letters (custom axis-label texts fall back to X/Y/Z
// strokes here).
} // namespace

void NaviCubeImplementation::fillCornerAnchor(Render::OverlayAnchor &anchor) const
{
	switch (m_Corner) {
	case NaviCube::TopLeftCorner:
		anchor.corner = Render::OverlayAnchor::TopLeft; break;
	case NaviCube::TopRightCorner:
		anchor.corner = Render::OverlayAnchor::TopRight; break;
	case NaviCube::BottomLeftCorner:
		anchor.corner = Render::OverlayAnchor::BottomLeft; break;
	default:
		anchor.corner = Render::OverlayAnchor::BottomRight; break;
	}
	const SbViewportRegion vp =
		m_View3DInventorViewer->getSoRenderManager()->getViewportRegion();
	SbVec2s sz = vp.getViewportSizePixels();
	int minDim = std::min(sz[0], sz[1]);
	anchor.sizeFraction =
		minDim > 0 ? float(m_CubeWidgetSize) / float(minDim) : 0.25f;
	// Same placement as the GL viewport (handleResize): 5% of the cube
	// size plus the user offsets, inward from the anchoring corner.
	anchor.marginX = float(0.05 * m_CubeWidgetSize + m_CubeWidgetOffsetX);
	anchor.marginY = float(0.05 * m_CubeWidgetSize + m_CubeWidgetOffsetY);
	anchor.nearPlane = 0.1f;
	anchor.farPlane = 10.0f;
	anchor.cameraDistance = 5.0f;
}

void NaviCubeImplementation::buildCoinCube()
{
	auto shared = m_Shared.get();
	auto root = new SoSeparator;
	m_CoinCubeRoot = root;
	auto lightModel = new SoLightModel;
	lightModel->model = SoLightModel::BASE_COLOR;
	root->addChild(lightModel);

	// Corner axes with stroke labels (drawNaviCube's m_ShowCS block).
	if (NaviCubeShared::m_ShowCS) {
		auto cs = new SoSeparator;
		auto style = new SoDrawStyle;
		style->lineWidth = 2.0f;
		cs->addChild(style);
		auto coord = new SoCoordinate3;
		const SbVec3f axisPts[4] = {
			{-1.1f, -1.1f, -1.1f},
			{0.5f, -1.1f, -1.1f},
			{-1.1f, 0.5f, -1.1f},
			{-1.1f, -1.1f, 0.5f},
		};
		coord->point.setValues(0, 4, axisPts);
		cs->addChild(coord);
		const SbColor axisCols[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
		for (int i = 0; i < 3; ++i) {
			auto col = new SoBaseColor;
			col->rgb = axisCols[i];
			cs->addChild(col);
			auto lines = new SoIndexedLineSet;
			const int32_t idx[3] = {0, i + 1, -1};
			lines->coordIndex.setValues(0, 3, idx);
			cs->addChild(lines);
		}
		auto lblCol = new SoBaseColor;
		lblCol->rgb.setValue(float(shared->m_AxisLabelColor.redF()),
		                     float(shared->m_AxisLabelColor.greenF()),
		                     float(shared->m_AxisLabelColor.blueF()));
		cs->addChild(lblCol);
		// X/Y/Z labels as MODULATE-tinted glyph companions that billboard
		// backend-side to face the viewer (matching the overlay's mini
		// camera), so they stay upright under any orbit — including the WASM
		// viewer's own camera, where the old baked counter-rotation skewed.
		// Rasterised at the user's configured axis-label font (AxisFont /
		// AxisFontSize) so the ported glyph matches the raw-GL labels and the
		// preference controls the on-screen size (backend uses one px per glyph
		// px for overlay text).
		QFont axisFont = shared->getAxisLabelFont();
		int axisPx = QFontInfo(axisFont).pixelSize();
		if (axisPx <= 0)
			axisPx = QFontMetrics(axisFont).height();
		SbString axisFamily(axisFont.family().toUtf8().constData());
		constexpr float a = 1.1f;
		constexpr float b = -0.2f;
		const SbVec3f lblPos[3] = {
			{a + b, -a + b, -a}, {-a + b, a + b, -a}, {-a + b, -a + b, a + b}};
		static const char *const letters[3] = {"X", "Y", "Z"};
		for (int i = 0; i < 3; ++i) {
			auto sep = new SoSeparator;
			auto trans = new SoTranslation;
			trans->translation = lblPos[i];
			sep->addChild(trans);
			Gui::SoTextImage *img = nullptr;
			SoSeparator *companion = Gui::SoTextImage::createSubGraph(&img);
			img->string.setValue(letters[i]);
			img->fontName.setValue(axisFamily);
			img->fontSize = float(axisPx);
			img->justification = Gui::SoTextImage::CENTER;
			img->vcenter = TRUE;
			sep->addChild(companion);
			cs->addChild(sep);
		}
		root->addChild(cs);
	}

	// Cube faces: closed solid, backface-culled like the GL draw (the
	// backend keeps explicit culling for transparent overlay draws).
	auto hints = new SoShapeHints;
	hints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
	hints->shapeType = SoShapeHints::SOLID;
	root->addChild(hints);
	auto coords = new SoCoordinate3;
	{
		std::vector<SbVec3f> pts;
		pts.reserve(shared->m_VertexArray.size());
		for (const auto &v : shared->m_VertexArray)
			pts.emplace_back(v[0], v[1], v[2]);
		coords->point.setValues(0, int(pts.size()), pts.data());
	}
	root->addChild(coords);

	for (int pass = 0; pass < 3; ++pass) {
		for (size_t i = 0; i < shared->m_Faces.size(); ++i) {
			const Face &f = shared->m_Faces[i];
			if (f.m_RenderPass != pass)
				continue;
			auto sep = new SoSeparator;
			auto mat = new SoMaterial; // synced per frame
			sep->addChild(mat);
			auto tex = new SoTexture2;
			auto it = shared->m_TexQImages.find(f.m_TextureId);
			if (it != shared->m_TexQImages.end())
				tex->image.setValue(
					SbVec2s(short(it->second.width()),
					        short(it->second.height())),
					4, it->second.constBits());
			sep->addChild(tex);
			// Faces now have a variable vertex count: octagon (8) main-face
			// fills, hexagon (6) corners, rectangle (4) edges, and 4-vert
			// label squares.
			const int n = f.m_VertexCount;
			auto tc = new SoTextureCoordinate2;
			std::vector<SbVec2f> uvs(n);
			for (int k = 0; k < n; ++k) {
				const auto &t = shared->m_TextureCoordArray[
					shared->m_IndexArray[f.m_FirstVertex + k]];
				uvs[k].setValue(t[0], t[1]);
			}
			tc->point.setValues(0, n, uvs.data());
			sep->addChild(tc);
			auto quad = new SoIndexedFaceSet;
			std::vector<int32_t> ci(n + 1), ti(n + 1);
			for (int k = 0; k < n; ++k) {
				ci[k] = shared->m_IndexArray[f.m_FirstVertex + k];
				ti[k] = k;
			}
			ci[n] = ti[n] = -1;
			quad->coordIndex.setValues(0, n + 1, ci.data());
			quad->textureCoordIndex.setValues(0, n + 1, ti.data());
			sep->addChild(quad);
			root->addChild(sep);
			bool text = f.m_TextureId != f.m_PickTextureId;
			m_CoinFaces.push_back({int(i), mat, text ? tc : nullptr, 1.0f});
		}
	}

	// Face borders. The GL pass draws them depth-test-less as backface-
	// culled polygons in line mode; lines cannot cull, so keep the depth
	// test instead — the (depth-writing) front faces occlude the back
	// loops, and coplanar front loops pass on LEQUAL.
	if (NaviCubeShared::m_BorderWidth >= 1.0) {
		auto bsep = new SoSeparator;
		auto style = new SoDrawStyle;
		style->lineWidth = float(NaviCubeShared::m_BorderWidth);
		bsep->addChild(style);
		auto mat = new SoMaterial;
		syncQColor(mat, shared->m_BorderColor);
		bsep->addChild(mat);
		std::vector<SbVec3f> pts;
		std::vector<int32_t> idx;
		for (const auto &f : shared->m_Faces) {
			if (f.m_TextureId != f.m_PickTextureId)
				continue; // base pass only, like the GL border loop
			if (f.m_PickTexId != TEX_FRONT_FACE
			    && f.m_PickTexId != TEX_EDGE_FACE
			    && f.m_PickTexId != TEX_CORNER_FACE)
				continue;
			const auto &loop = shared->m_VertexArrays2[f.m_PickId];
			int start = int(pts.size());
			// Slightly off the cube surface so the loops win the depth
			// test against their own (coplanar, depth-writing) faces
			// from every view direction.
			for (const auto &v : loop)
				pts.emplace_back(v[0] * 1.005f, v[1] * 1.005f,
				                 v[2] * 1.005f);
			for (size_t k = 0; k < loop.size(); ++k)
				idx.push_back(start + int(k));
			idx.push_back(start);
			idx.push_back(-1);
		}
		auto bc = new SoCoordinate3;
		bc->point.setValues(0, int(pts.size()), pts.data());
		bsep->addChild(bc);
		auto ls = new SoIndexedLineSet;
		ls->coordIndex.setValues(0, int(idx.size()), idx.data());
		bsep->addChild(ls);
		root->addChild(bsep);
	}
}

void NaviCubeImplementation::buildCoinButtons()
{
	auto shared = m_Shared.get();
	auto root = new SoSeparator;
	m_CoinButtonRoot = root;
	auto lightModel = new SoLightModel;
	lightModel->model = SoLightModel::BASE_COLOR;
	root->addChild(lightModel);
	// One full-anchor quad per button texture: the GL draw covers the
	// whole cube viewport in a 0..1 y-down ortho; the anchor here is a
	// -1..1 y-up ortho (orthoHeight 2), same visual orientation.
	auto coords = new SoCoordinate3;
	const SbVec3f qpts[4] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
	coords->point.setValues(0, 4, qpts);
	root->addChild(coords);
	auto tcoords = new SoTextureCoordinate2;
	const SbVec2f uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	tcoords->point.setValues(0, 4, uvs);
	root->addChild(tcoords);

	auto addQuad = [&](SoGroup *parent, int texKey) -> SoMaterial * {
		auto sep = new SoSeparator;
		auto mat = new SoMaterial;
		sep->addChild(mat);
		auto tex = new SoTexture2;
		auto it = shared->m_TexQImages.find(shared->m_Textures[texKey]);
		if (it != shared->m_TexQImages.end())
			tex->image.setValue(
				SbVec2s(short(it->second.width()),
				        short(it->second.height())),
				4, it->second.constBits());
		sep->addChild(tex);
		auto quad = new SoIndexedFaceSet;
		static const int32_t qi[5] = {0, 1, 2, 3, -1};
		quad->coordIndex.setValues(0, 5, qi);
		quad->textureCoordIndex.setValues(0, 5, qi);
		sep->addChild(quad);
		parent->addChild(sep);
		return mat;
	};

	for (int b : shared->m_Buttons)
		m_CoinButtons.emplace_back(b, addQuad(root, b));
	// Menu icon: a hilite backdrop quad toggled on hover, then the icon
	// in the plain button color (matching the GL draw).
	m_CoinMenuHilite = new SoSwitch;
	auto hsep = new SoSeparator;
	syncQColor(addQuad(hsep, TEX_VIEW_MENU_FACE), shared->m_HiliteColor);
	m_CoinMenuHilite->addChild(hsep);
	m_CoinMenuHilite->whichChild = SO_SWITCH_NONE;
	root->addChild(m_CoinMenuHilite);
	m_CoinButtons.emplace_back(-1, addQuad(root, TEX_VIEW_MENU_ICON));
}

SoSeparator *NaviCubeImplementation::getOverlayCubeGraph(Render::OverlayAnchor &anchor)
{
	auto shared = m_Shared.get();
	shared->initNaviCube(); // no-op once ready; needs a current GL context
	if (!shared->m_Context)
		return nullptr;
	if (!m_Hit && NaviCubeShared::m_AutoHideCube)
		return nullptr;
	if (m_CoinGeneration != shared->m_TexGeneration) {
		m_CoinCubeRoot.reset();
		m_CoinButtonRoot.reset();
		m_CoinFaces.clear();
		m_CoinButtons.clear();
		m_CoinMenuHilite.reset();
		m_CoinGeneration = shared->m_TexGeneration;
	}
	if (!m_CoinCubeRoot)
		buildCoinCube();

	handleResize();

	SoCamera *cam = m_View3DInventorViewer->getSoRenderManager()->getCamera();
	SbRotation orient = cam ? cam->orientation.getValue()
	                        : SbRotation::identity();

	// Per-frame sync: hover highlight and the text-readability flip of
	// drawNaviCube()'s label pass.
	SbMatrix mx;
	mx = orient;
	mx = mx.inverse();
	mx[3][2] = -5.0f;
	for (auto &cf : m_CoinFaces) {
		const Face &f = shared->m_Faces[size_t(cf.faceIndex)];
		bool hilite = (m_HiliteId == f.m_PickId) && f.m_RenderPass < 2;
		syncQColor(cf.material, hilite ? shared->m_HiliteColor : f.m_Color);
		if (!cf.texCoords)
			continue;
		int idx = f.m_FirstVertex;
		const auto &mv1 = shared->m_VertexArray[shared->m_IndexArray[idx]];
		const auto &mv2 = shared->m_VertexArray[shared->m_IndexArray[idx + 1]];
		const auto &mv4 = shared->m_VertexArray[shared->m_IndexArray[idx + 3]];
		SbVec3f v1, v2, v4;
		mx.multVecMatrix(SbVec3f(mv1[0], mv1[1], mv1[2]), v1);
		mx.multVecMatrix(SbVec3f(mv2[0], mv2[1], mv2[2]), v2);
		mx.multVecMatrix(SbVec3f(mv4[0], mv4[1], mv4[2]), v4);
		float uv = (v1[0] - v2[0] > 0.001f && v1[1] - v4[1] > 0.001f)
			? -1.0f : 1.0f;
		if (uv != cf.uv) {
			cf.uv = uv;
			SbVec2f uvs[4];
			for (int k = 0; k < 4; ++k) {
				const auto &t = shared->m_TextureCoordArray[
					shared->m_IndexArray[idx + k]];
				uvs[k].setValue(uv * t[0], uv * t[1]);
			}
			cf.texCoords->point.setValues(0, 4, uvs);
		}
	}

	// Corner mini-perspective matching drawNaviCube()'s frustum: dim =
	// NEAR * tan(pi/8) * 1.2 at NEAR => half-angle atan(tan(22.5deg)*1.2).
	fillCornerAnchor(anchor);
	anchor.fovDeg =
		float(2.0 * atan(tan(M_PI / 8.0) * 1.2) * 180.0 / M_PI);
	anchor.orientFromScene = true;
	return m_CoinCubeRoot;
}

SoSeparator *NaviCubeImplementation::getOverlayButtonGraph(Render::OverlayAnchor &anchor)
{
	auto shared = m_Shared.get();
	if (!shared->m_Context)
		return nullptr;
	if (!m_Hit && (NaviCubeShared::m_AutoHideButton
	               || NaviCubeShared::m_AutoHideCube))
		return nullptr;
	if (!m_CoinButtonRoot)
		buildCoinButtons();

	for (auto &bp : m_CoinButtons) {
		bool hilite = bp.first >= 0 && m_HiliteId == bp.first;
		syncQColor(bp.second,
		           hilite ? shared->m_HiliteColor : shared->m_ButtonColor);
	}
	int which = m_HiliteId == TEX_VIEW_MENU_FACE
		? SO_SWITCH_ALL : SO_SWITCH_NONE;
	if (m_CoinMenuHilite->whichChild.getValue() != which)
		m_CoinMenuHilite->whichChild = which;

	fillCornerAnchor(anchor);
	anchor.fovDeg = 0.0f;
	anchor.orthoHeight = 2.0f;
	anchor.orientFromScene = false;
	return m_CoinButtonRoot;
}

bool NaviCubeShared::drawNaviCube(SoCamera *cam, int hiliteId, bool hit) {
    bool res = initNaviCube();

	// Store GL state.
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	GLfloat depthrange[2];
	glGetFloatv(GL_DEPTH_RANGE, depthrange);
	GLdouble projectionmatrix[16];
	glGetDoublev(GL_PROJECTION_MATRIX, projectionmatrix);

	glDepthMask(GL_TRUE);
	glDepthRange(0.0, 1.0);
	glClearDepth(1.0f);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	glDisable(GL_LIGHTING);
	//glDisable(GL_BLEND);

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//glTexEnvf(GL_TEXTURE_2D, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glDepthMask(GL_TRUE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glShadeModel(GL_SMOOTH);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	glAlphaFunc(GL_GREATER, 0.25);
	glEnable(GL_ALPHA_TEST);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	const float NEARVAL = 0.1f;
	const float FARVAL = 10.0f;
	const float dim = NEARVAL * float(tan(M_PI / 8.0)) * 1.2;
	glFrustum(-dim, dim, -dim, dim, NEARVAL, FARVAL);

	SbMatrix mx;
	mx = cam->orientation.getValue();

	mx = mx.inverse();
	mx[3][2] = -5.0;

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadMatrixf((float*)mx);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glClear(GL_DEPTH_BUFFER_BIT);

	{
		// Draw the axes
		if (m_ShowCS) {
			glDisable(GL_TEXTURE_2D);
			const float a=1.1f;
            const float b=-0.2f;

	        glLineWidth(2.0);

			glColor3f(1, 0, 0);
			glBegin(GL_LINES);
			glVertex3f(-1.1f, -1.1f, -1.1f);
			glVertex3f(+0.5f, -1.1f, -1.1f);
			glEnd();
			glRasterPos3d(a, -a, -a);

			glColor3f(0, 1, 0);
			glBegin(GL_LINES);
			glVertex3f(-1.1f, -1.1f, -1.1f);
			glVertex3f(-1.1f, +0.5f, -1.1f);
			glEnd();
			glRasterPos3d(-a, a, -a);

			glColor3f(0, 0, 1);
			glBegin(GL_LINES);
			glVertex3f(-1.1f, -1.1f, -1.1f);
			glVertex3f(-1.1f, -1.1f, +0.5f);
			glEnd();
			glRasterPos3d(-a, -a, a);

			glEnable(GL_TEXTURE_2D);

            // Render axis labels
            GLint unpack,rowlength;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowlength);

            glColor3fv(SbVec3f(m_AxisLabelColor.redF(),
                               m_AxisLabelColor.greenF(),
                               m_AxisLabelColor.blueF()).getValue());

            auto drawAxisLabel = [=](const QImage &img) {
                if (!img.isNull()) {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, img.bytesPerLine()*8);
                    glBitmap(img.width(), img.height(), 0, 0, 0, 0, img.constBits());
                }
            };
            glRasterPos3d(a + b, -a + b, -a);
            drawAxisLabel(m_LabelX);
            glRasterPos3d(-a + b, a + b, -a);
            drawAxisLabel(m_LabelY);
            glRasterPos3d(-a + b, -a + b, a + b);
            drawAxisLabel(m_LabelZ);

            glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, rowlength);
		}
	}

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, (void*) m_VertexArray.data());
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, m_TextureCoordArray.data());

	// Draw the cube faces
	if (hit || !m_AutoHideCube) {
        // Fill faces are untextured exact polygons now; disable texturing so
        // the flat face/edge/corner color is driver-independent (some drivers
        // still sample the bound texture 0).
        glDisable(GL_TEXTURE_2D);
		for (int pass = 0; pass < 3 ; pass++) {
            for (auto &f : m_Faces) {
                if (pass != f.m_RenderPass || f.m_TextureId != f.m_PickTextureId)
                    continue;

                QColor& c = (hiliteId == f.m_PickId) && (pass < 2) ? m_HiliteColor : f.m_Color;
                glColor4f(c.redF(), c.greenF(), c.blueF(),c.alphaF());

                glDrawElements(GL_TRIANGLE_FAN, f.m_VertexCount, GL_UNSIGNED_BYTE, (void*) &m_IndexArray[f.m_FirstVertex]);
            }
        }
        glEnable(GL_TEXTURE_2D);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);


	if (hit || !m_AutoHideCube) {
		for (int pass = 0; pass < 3 ; pass++) {
            for (auto &f : m_Faces) {
                if (pass != f.m_RenderPass || f.m_TextureId == f.m_PickTextureId)
                    continue;

                QColor& c = (hiliteId == f.m_PickId) && (pass < 2) ? m_HiliteColor : f.m_Color;
                glColor4f(c.redF(), c.greenF(), c.blueF(),c.alphaF());
                glBindTexture(GL_TEXTURE_2D, f.m_TextureId);

                // We are rendering a text label here. Checks the orientation
                // and flip the texture to make it more readable.
                //
                int idx = f.m_FirstVertex;
                const Vector3f &mv1 = m_VertexArray[m_IndexArray[idx]];
                const Vector3f &mv2 = m_VertexArray[m_IndexArray[idx+1]];
                const Vector3f &mv3 = m_VertexArray[m_IndexArray[idx+2]];
                const Vector3f &mv4 = m_VertexArray[m_IndexArray[idx+3]];

                SbVec3f v1, v2, v4;
                mx.multVecMatrix(SbVec3f(mv1[0], mv1[1], mv1[2]), v1);
                mx.multVecMatrix(SbVec3f(mv2[0], mv2[1], mv2[2]), v2);
                mx.multVecMatrix(SbVec3f(mv4[0], mv4[1], mv4[2]), v4);

                // The face vertex goes like this
                // v4------v3
                // |        |
                // |        |
                // v1------v2
                // We apply the model view matrix to the vertexes and flips
                // texture in u (aka x) axis if v1.x > v2.x, and v (aka y)
                // axis if v1.y > v4.y. Note that u and v must flip together or
                // else we'll have a mirror, which is impossible since we don't
                // do backface rendering
                float uv;
                if (v1[0] - v2[0] > 0.001f && v1[1] - v4[1] > 0.001f)
                    uv = -1.0f;
                else
                    uv = 1.0f;

                const Vector2f &t1 = m_TextureCoordArray[m_IndexArray[idx]];
                const Vector2f &t2 = m_TextureCoordArray[m_IndexArray[idx+1]];
                const Vector2f &t3 = m_TextureCoordArray[m_IndexArray[idx+2]];
                const Vector2f &t4 = m_TextureCoordArray[m_IndexArray[idx+3]];

                glBegin(GL_TRIANGLE_FAN);
                glTexCoord2f(uv*t1[0], uv*t1[1]); glVertex3f(mv1[0], mv1[1], mv1[2]);
                glTexCoord2f(uv*t2[0], uv*t2[1]); glVertex3f(mv2[0], mv2[1], mv2[2]);
                glTexCoord2f(uv*t3[0], uv*t3[1]); glVertex3f(mv3[0], mv3[1], mv3[2]);
                glTexCoord2f(uv*t4[0], uv*t4[1]); glVertex3f(mv4[0], mv4[1], mv4[2]);
                glEnd();
			}
        }

        if (m_BorderWidth >= 1.0f) {
	        glDisable(GL_DEPTH_TEST);
			glDisable(GL_TEXTURE_2D);
            const auto &c = m_BorderColor;
            glColor4f(c.redF(), c.greenF(), c.blueF(), c.alphaF());
            glLineWidth(m_BorderWidth);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            for (int pass = 0; pass < 3 ; pass++) {
                for (auto &f : m_Faces) {
                    if (pass != f.m_RenderPass || f.m_TextureId != f.m_PickTextureId)
                        continue;
                    if (f.m_PickTexId == TEX_FRONT_FACE || f.m_PickTexId == TEX_EDGE_FACE || f.m_PickTexId == TEX_CORNER_FACE) {
                        glBegin(GL_POLYGON);
                        for (const Vector3f& v : m_VertexArrays2[f.m_PickId]) {
                            glVertex3f(v[0], v[1], v[2]);
                        }
                        glEnd();
                    }
                }
            }
			glEnable(GL_TEXTURE_2D);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
	}

    if (hit || (!m_AutoHideButton && !m_AutoHideCube)) {
        // Draw the rotate buttons
        glEnable(GL_CULL_FACE);

        glDisable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, 1, 1, 0, 0, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        for (vector<int>::iterator b = m_Buttons.begin(); b != m_Buttons.end(); b++) {
            QColor& c = (hiliteId ==(*b)) ? m_HiliteColor : m_ButtonColor;
            glColor4f(c.redF(), c.greenF(), c.blueF(), c.alphaF());
            glBindTexture(GL_TEXTURE_2D, m_Textures[*b]);

            glBegin(GL_QUADS);
            glTexCoord2f(0, 0);
            glVertex3f(0.0f, 1.0f, 0.0f);
            glTexCoord2f(1, 0);
            glVertex3f(1.0f, 1.0f, 0.0f);
            glTexCoord2f(1, 1);
            glVertex3f(1.0f, 0.0f, 0.0f);
            glTexCoord2f(0, 1);
            glVertex3f(0.0f, 0.0f, 0.0f);
            glEnd();
        }

        // Draw the view menu icon
        if (hiliteId == TEX_VIEW_MENU_FACE) {
            QColor& hc = m_HiliteColor;
            glColor4f(hc.redF(), hc.greenF(), hc.blueF(), hc.alphaF());
            glBindTexture(GL_TEXTURE_2D, m_Textures[TEX_VIEW_MENU_FACE]);

            glBegin(GL_QUADS); // DO THIS WITH VERTEX ARRAYS
            glTexCoord2f(0, 0);
            glVertex3f(0.0f, 1.0f, 0.0f);
            glTexCoord2f(1, 0);
            glVertex3f(1.0f, 1.0f, 0.0f);
            glTexCoord2f(1, 1);
            glVertex3f(1.0f, 0.0f, 0.0f);
            glTexCoord2f(0, 1);
            glVertex3f(0.0f, 0.0f, 0.0f);
            glEnd();
        }

        {
            QColor& c = m_ButtonColor;
            glColor4f(c.redF(), c.greenF(), c.blueF(), c.alphaF());
            glBindTexture(GL_TEXTURE_2D, m_Textures[TEX_VIEW_MENU_ICON]);
        }

        glBegin(GL_QUADS); // FIXME do this with vertex arrays
        glTexCoord2f(0, 0);
        glVertex3f(0.0f, 1.0f, 0.0f);
        glTexCoord2f(1, 0);
        glVertex3f(1.0f, 1.0f, 0.0f);
        glTexCoord2f(1, 1);
        glVertex3f(1.0f, 0.0f, 0.0f);
        glTexCoord2f(0, 1);
        glVertex3f(0.0f, 0.0f, 0.0f);
        glEnd();
    }


	glPopMatrix();

	// Restore original state.

	glDepthRange(depthrange[0], depthrange[1]);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixd(projectionmatrix);

	glPopAttrib();

    return res;
}

namespace {
// Alpha-mask test against a button texture, matching the pick pass's
// glAlphaFunc(GL_GREATER, 0.25). The images in m_TexQImages are the bottom-up
// copies handed to the Coin twins, so t (and therefore v) runs with row 0 at
// the bottom -- the same sense as the event's y and the quad's texcoords.
bool maskHit(const QImage &img, float u, float v)
{
	if (img.isNull())
		return false;
	int px = int(u * (img.width() - 1) + 0.5f);
	int py = int(v * (img.height() - 1) + 0.5f);
	if (px < 0 || py < 0 || px >= img.width() || py >= img.height())
		return false;
	return qAlpha(img.pixel(px, py)) > 63;
}
} // namespace

int NaviCubeShared::pickButton(float u, float v) const
{
	// Reverse draw order: the buttons are drawn with the depth test off after
	// the cube, and the view-menu icon last of all, so later draws win.
	auto sample = [this, u, v](int texKey) {
		auto tex = m_Textures.find(texKey);
		if (tex == m_Textures.end())
			return false;
		auto img = m_TexQImages.find(tex->second);
		return img != m_TexQImages.end() && maskHit(img->second, u, v);
	};

	if (sample(TEX_VIEW_MENU_FACE))
		return TEX_VIEW_MENU_FACE;

	for (auto b = m_Buttons.rbegin(); b != m_Buttons.rend(); ++b) {
		// The pick variant (the unstroked mask) is the one the GL pass bound.
		if (sample(*b + 1))
			return *b;
	}
	return 0;
}

int NaviCubeShared::pickCube(SoCamera *cam, float u, float v) const
{
	// Same frustum and modelview drawNaviCube() sets up: a 1:1 viewport, the
	// camera's orientation, and the eye parked 5 units out along its axis.
	const float NEARVAL = 0.1f;
	const float dim = NEARVAL * float(tan(M_PI / 8.0)) * 1.2f;

	SbMatrix rot;
	rot.setRotate(cam->orientation.getValue());

	SbVec3f eye, dir;
	rot.multVecMatrix(SbVec3f(0, 0, 5), eye);
	rot.multDirMatrix(SbVec3f((2 * u - 1) * dim, (2 * v - 1) * dim, -NEARVAL), dir);
	if (dir.normalize() == 0.0f)
		return 0;

	// Nearest hit over the exact region polygons -- the octagons, edge
	// rectangles and corner hexagons that the fill/pick pass draws. They are
	// planar and convex, so a plane hit plus a same-side test is exact.
	int best = 0;
	float bestT = 0.0f;
	for (const auto &v2 : m_VertexArrays2) {
		const auto &poly = v2.second;
		if (poly.size() < 3)
			continue;
		auto pt = [&poly](int i) {
			return SbVec3f(poly[i][0], poly[i][1], poly[i][2]);
		};
		SbVec3f p0 = pt(0);
		SbVec3f n = (pt(1) - p0).cross(pt(2) - p0);
		float denom = n.dot(dir);
		if (fabs(denom) < 1e-9f)
			continue;
		float t = n.dot(p0 - eye) / denom;
		if (t <= 0.0f || (best && t >= bestT))
			continue;
		SbVec3f hit = eye + t * dir;
		bool inside = true;
		for (size_t i = 0, c = poly.size(); i < c; ++i) {
			SbVec3f a = pt(int(i));
			SbVec3f b = pt(int((i + 1) % c));
			if ((b - a).cross(hit - a).dot(n) < 0.0f) {
				inside = false;
				break;
			}
		}
		if (!inside)
			continue;
		best = v2.first;
		bestT = t;
	}
	return best;
}

int NaviCubeImplementation::pickFace(short x, short y) {
	auto shared = m_Shared.get();
	if (shared->m_VertexArrays2.empty())
		return 0; // not initialised yet

	SoCamera* cam = m_View3DInventorViewer->getSoRenderManager()->getCamera();
	if (!cam)
		return 0;

	handleResize();

	const float size = float(m_CubeWidgetSize);
	float u = (float(x) - (m_CubeWidgetPosX - m_CubeWidgetSize / 2)) / size;
	float v = (float(y) - (m_CubeWidgetPosY - m_CubeWidgetSize / 2)) / size;
	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
		return 0;

	if (int id = shared->pickButton(u, v))
		return id;
	return shared->pickCube(cam, u, v);
}

bool NaviCubeImplementation::mousePressed(short x, short y) {
	m_MouseDown = true;
	m_Dragging = false;
	m_MightDrag = inDragZone(x, y);
	int pick = pickFace(x, y);
	setHilite(pick);
	return pick != 0;
}

SbRotation NaviCubeImplementation::setView(float rotZ, float rotX) const {
	SbRotation rz, rx, t;
	rz.setValue(SbVec3f(0, 0, 1), rotZ * M_PI / 180);
	rx.setValue(SbVec3f(1, 0, 0), rotX * M_PI / 180);
	return rx * rz;
}

SbRotation NaviCubeImplementation::rotateView(SbRotation viewRot, int axis, float rotAngle, SbVec3f customAxis) const {
	SbVec3f up;
	viewRot.multVec(SbVec3f(0, 1, 0), up);

	SbVec3f out;
	viewRot.multVec(SbVec3f(0, 0, 1), out);

	SbVec3f right;
	viewRot.multVec(SbVec3f(1, 0, 0), right);

	SbVec3f direction;
	switch (axis) {
	default:
		return viewRot;
	case DIR_UP:
		direction = up;
		break;
	case DIR_OUT:
		direction = out;
		break;
	case DIR_RIGHT:
		direction = right;
		break;
	}

	if (customAxis != SbVec3f(0, 0, 0))
		direction = customAxis;

	SbRotation rot(direction, -rotAngle * M_PI / 180.0);
	SbRotation newViewRot = viewRot * rot;
	return newViewRot;
}

void NaviCubeImplementation::rotateView(const SbRotation& rot) {
	m_View3DInventorViewer->setCameraOrientation(rot);
}

bool NaviCubeImplementation::mouseReleased(short x, short y) {
	setHilite(0);
	m_MouseDown = false;
	if (m_Dragging) {
        switch (m_Corner) {
        case NaviCube::TopLeftCorner:
            m_CubeWidgetOffsetX = m_CubeWidgetPosX - m_CubeWidgetSize*1.1 / 2;
            m_CubeWidgetOffsetY = m_PrevWidth - m_CubeWidgetSize*1.1 / 2 - m_CubeWidgetPosY;
            break;
        case NaviCube::TopRightCorner:
            m_CubeWidgetOffsetX = m_PrevWidth - m_CubeWidgetSize*1.1 / 2 - m_CubeWidgetPosX;
            m_CubeWidgetOffsetY = m_PrevHeight - m_CubeWidgetSize*1.1 / 2 - m_CubeWidgetPosY;
            break;
        case NaviCube::BottomLeftCorner:
            m_CubeWidgetOffsetX = m_CubeWidgetPosX - m_CubeWidgetSize*1.1 / 2;
            m_CubeWidgetOffsetY = m_CubeWidgetPosY - m_CubeWidgetSize*1.1 / 2;
            break;
        case NaviCube::BottomRightCorner:
            m_CubeWidgetOffsetX = m_PrevWidth - m_CubeWidgetSize*1.1 / 2 - m_CubeWidgetPosX;
            m_CubeWidgetOffsetY = m_CubeWidgetPosY - m_CubeWidgetSize*1.1 / 2;
            break;
        }
        Base::StateLocker guard(m_Shared->m_Saving);
        m_hGrp->SetInt("OffsetX", m_CubeWidgetOffsetX);
        m_hGrp->SetInt("OffsetY", m_CubeWidgetOffsetY);
    } else {
        // get the current view
        SbMatrix ViewRotMatrix;
        SbRotation CurrentViewRot = m_View3DInventorViewer->getCameraOrientation();
        CurrentViewRot.getValue(ViewRotMatrix);

		float rot = 45;
		float tilt = 90 - Base::toDegrees(atan(sqrt(2.0)));
		int pick = pickFace(x, y);

		long step = Base::clamp(NaviCubeShared::m_StepByTurn, 4L, 36L);
		float rotStepAngle = 360.0f / step;
		bool toNearest = NaviCubeShared::m_RotateToNearest;
		bool applyRotation = true;

		SbRotation viewRot = CurrentViewRot;

		switch (pick) {
		default:
			return false;
			break;
		case TEX_FRONT:
			viewRot = setView(0, 90);
			// we don't want to dumb rotate to the same view since depending on from where the user clicked on FRONT
			// we have one of four suitable end positions.
			// we use here the same rotation logic used by other programs using OCC like "CAD Assistant"
			// when current matrix's 0,0 entry is larger than its |1,0| entry, we already have the final result
			// otherwise rotate around y
			if (toNearest) {
				if (ViewRotMatrix[0][0] < 0 && abs(ViewRotMatrix[0][0]) >= abs(ViewRotMatrix[1][0]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][0] > 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, 90);
				else if (ViewRotMatrix[1][0] < 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, -90);
			}
			break;
		case TEX_REAR:
			viewRot = setView(180, 90);
			if (toNearest) {
				if (ViewRotMatrix[0][0] > 0 && abs(ViewRotMatrix[0][0]) >= abs(ViewRotMatrix[1][0]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][0] > 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, -90);
				else if (ViewRotMatrix[1][0] < 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, 90);
			}
			break;
		case TEX_LEFT:
			viewRot = setView(270, 90);
			if (toNearest) {
				if (ViewRotMatrix[0][1] > 0 && abs(ViewRotMatrix[0][1]) >= abs(ViewRotMatrix[1][1]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][1] > 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, -90);
				else if (ViewRotMatrix[1][1] < 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, 90);
			}
			break;
		case TEX_RIGHT:
			viewRot = setView(90, 90);
			if (toNearest) {
				if (ViewRotMatrix[0][1] < 0 && abs(ViewRotMatrix[0][1]) >= abs(ViewRotMatrix[1][1]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][1] > 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, 90);
				else if (ViewRotMatrix[1][1] < 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, -90);
			}
			break;
		case TEX_TOP:
			viewRot = setView(0, 0);
			if (toNearest) {
				if (ViewRotMatrix[0][0] < 0 && abs(ViewRotMatrix[0][0]) >= abs(ViewRotMatrix[1][0]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][0] > 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, 90);
				else if (ViewRotMatrix[1][0] < 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, -90);
			}
			break;
		case TEX_BOTTOM:
			viewRot = setView(0, 180);
			if (toNearest) {
				if (ViewRotMatrix[0][0] < 0 && abs(ViewRotMatrix[0][0]) >= abs(ViewRotMatrix[1][0]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][0] > 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, 90);
				else if (ViewRotMatrix[1][0] < 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, -90);
			}
			break;
		case TEX_FRONT_TOP:
			// set to FRONT then rotate
			viewRot = setView(0, 90);
			viewRot = rotateView(viewRot, 1, 45);
			if (toNearest) {
				if (ViewRotMatrix[0][0] < 0 && abs(ViewRotMatrix[0][0]) >= abs(ViewRotMatrix[1][0]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][0] > 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, 90);
				else if (ViewRotMatrix[1][0] < 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, -90);
			}
			break;
		case TEX_FRONT_BOTTOM:
			// set to FRONT then rotate
			viewRot = setView(0, 90);
			viewRot = rotateView(viewRot, 1, -45);
			if (toNearest) {
				if (ViewRotMatrix[0][0] < 0 && abs(ViewRotMatrix[0][0]) >= abs(ViewRotMatrix[1][0]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][0] > 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, 90);
				else if (ViewRotMatrix[1][0] < 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, -90);
			}
			break;
		case TEX_REAR_BOTTOM:
			// set to REAR then rotate
			viewRot = setView(180, 90);
			viewRot = rotateView(viewRot, 1, -45);
			if (toNearest) {
				if (ViewRotMatrix[0][0] > 0 && abs(ViewRotMatrix[0][0]) >= abs(ViewRotMatrix[1][0]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][0] > 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, -90);
				else if (ViewRotMatrix[1][0] < 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, 90);
			}
			break;
		case TEX_REAR_TOP:
			// set to REAR then rotate
			viewRot = setView(180, 90);
			viewRot = rotateView(viewRot, 1, 45);
			if (toNearest) {
				if (ViewRotMatrix[0][0] > 0 && abs(ViewRotMatrix[0][0]) >= abs(ViewRotMatrix[1][0]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][0] > 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, -90);
				else if (ViewRotMatrix[1][0] < 0 && abs(ViewRotMatrix[1][0]) > abs(ViewRotMatrix[0][0]))
					viewRot = rotateView(viewRot, 2, 90);
			}
			break;
		case TEX_FRONT_LEFT:
			// set to FRONT then rotate
			viewRot = setView(0, 90);
			viewRot = rotateView(viewRot, 0, 45);
			if (toNearest) {
				if (ViewRotMatrix[1][2] < 0 && abs(ViewRotMatrix[1][2]) >= abs(ViewRotMatrix[0][2]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[0][2] > 0 && abs(ViewRotMatrix[0][2]) > abs(ViewRotMatrix[1][2]))
					viewRot = rotateView(viewRot, 2, -90);
				else if (ViewRotMatrix[0][2] < 0 && abs(ViewRotMatrix[0][2]) > abs(ViewRotMatrix[1][2]))
					viewRot = rotateView(viewRot, 2, 90);
			}
			break;
		case TEX_FRONT_RIGHT:
			// set to FRONT then rotate
			viewRot = setView(0, 90);
			viewRot = rotateView(viewRot, 0, -45);
			if (toNearest) {
				if (ViewRotMatrix[1][2] < 0 && abs(ViewRotMatrix[1][2]) >= abs(ViewRotMatrix[0][2]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[0][2] > 0 && abs(ViewRotMatrix[0][2]) > abs(ViewRotMatrix[1][2]))
					viewRot = rotateView(viewRot, 2, -90);
				else if (ViewRotMatrix[0][2] < 0 && abs(ViewRotMatrix[0][2]) > abs(ViewRotMatrix[1][2]))
					viewRot = rotateView(viewRot, 2, 90);
			}
			break;
		case TEX_REAR_RIGHT:
			// set to REAR then rotate
			viewRot = setView(180, 90);
			viewRot = rotateView(viewRot, 0, 45);
			if (toNearest) {
				if (ViewRotMatrix[1][2] < 0 && abs(ViewRotMatrix[1][2]) >= abs(ViewRotMatrix[0][2]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[0][2] > 0 && abs(ViewRotMatrix[0][2]) > abs(ViewRotMatrix[1][2]))
					viewRot = rotateView(viewRot, 2, -90);
				else if (ViewRotMatrix[0][2] < 0 && abs(ViewRotMatrix[0][2]) > abs(ViewRotMatrix[1][2]))
					viewRot = rotateView(viewRot, 2, 90);
			}
			break;
		case TEX_REAR_LEFT:
			// set to REAR then rotate
			viewRot = setView(180, 90);
			viewRot = rotateView(viewRot, 0, -45);
			if (ViewRotMatrix[1][2] < 0 && abs(ViewRotMatrix[1][2]) >= abs(ViewRotMatrix[0][2]))
				viewRot = rotateView(viewRot, 2, 180);
			else if (ViewRotMatrix[0][2] > 0 && abs(ViewRotMatrix[0][2]) > abs(ViewRotMatrix[1][2]))
				viewRot = rotateView(viewRot, 2, -90);
			else if (ViewRotMatrix[0][2] < 0 && abs(ViewRotMatrix[0][2]) > abs(ViewRotMatrix[1][2]))
				viewRot = rotateView(viewRot, 2, 90);
			break;
		case TEX_TOP_LEFT:
			// set to LEFT then rotate
			viewRot = setView(270, 90);
			viewRot = rotateView(viewRot, 1, 45);
			if (toNearest) {
				if (ViewRotMatrix[0][1] > 0 && abs(ViewRotMatrix[0][1]) >= abs(ViewRotMatrix[1][1]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][1] > 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, -90);
				else if (ViewRotMatrix[1][1] < 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, 90);
			}
			break;
		case TEX_TOP_RIGHT:
			// set to RIGHT then rotate
			viewRot = setView(90, 90);
			viewRot = rotateView(viewRot, 1, 45);
			if (toNearest) {
				if (ViewRotMatrix[0][1] < 0 && abs(ViewRotMatrix[0][1]) >= abs(ViewRotMatrix[1][1]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][1] > 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, 90);
				else if (ViewRotMatrix[1][1] < 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, -90);
			}
			break;
		case TEX_BOTTOM_RIGHT:
			// set to RIGHT then rotate
			viewRot = setView(90, 90);
			viewRot = rotateView(viewRot, 1, -45);
			if (toNearest) {
				if (ViewRotMatrix[0][1] < 0 && abs(ViewRotMatrix[0][1]) >= abs(ViewRotMatrix[1][1]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][1] > 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, 90);
				else if (ViewRotMatrix[1][1] < 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, -90);
			}
			break;
		case TEX_BOTTOM_LEFT:
			// set to LEFT then rotate
			viewRot = setView(270, 90);
			viewRot = rotateView(viewRot, 1, -45);
			if (toNearest) {
				if (ViewRotMatrix[0][1] > 0 && abs(ViewRotMatrix[0][1]) >= abs(ViewRotMatrix[1][1]))
					viewRot = rotateView(viewRot, 2, 180);
				else if (ViewRotMatrix[1][1] > 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, -90);
				else if (ViewRotMatrix[1][1] < 0 && abs(ViewRotMatrix[1][1]) > abs(ViewRotMatrix[0][1]))
					viewRot = rotateView(viewRot, 2, 90);
			}
			break;
		case TEX_BOTTOM_LEFT_FRONT:
			viewRot = setView(rot - 90, 90 + tilt);
			// we have 3 possible end states:
			// - z-axis is not rotated larger than 120 deg from (0, 1, 0) -> we are already there
			// - y-axis is not rotated larger than 120 deg from (0, 1, 0)
			// - x-axis is not rotated larger than 120 deg from (0, 1, 0)
			if (toNearest) {
				if (ViewRotMatrix[1][0] > 0.4823)
					viewRot = rotateView(viewRot, 0, -120, SbVec3f(1, 1, 1));
				else if (ViewRotMatrix[1][1] > 0.4823)
					viewRot = rotateView(viewRot, 0, 120, SbVec3f(1, 1, 1));
			}
			break;
		case TEX_BOTTOM_FRONT_RIGHT:
			viewRot = setView(90 + rot - 90, 90 + tilt);
			if (toNearest) {
				if (ViewRotMatrix[1][0] < -0.4823)
					viewRot = rotateView(viewRot, 0, 120, SbVec3f(-1, 1, 1));
				else if (ViewRotMatrix[1][1] > 0.4823)
					viewRot = rotateView(viewRot, 0, -120, SbVec3f(-1, 1, 1));
			}
			break;
		case TEX_BOTTOM_RIGHT_REAR:
			viewRot = setView(180 + rot - 90, 90 + tilt);
			if (toNearest) {
				if (ViewRotMatrix[1][0] < -0.4823)
					viewRot = rotateView(viewRot, 0, -120, SbVec3f(-1, -1, 1));
				else if (ViewRotMatrix[1][1] < -0.4823)
					viewRot = rotateView(viewRot, 0, 120, SbVec3f(-1, -1, 1));
			}
			break;
		case TEX_BOTTOM_REAR_LEFT:
			viewRot = setView(270 + rot - 90, 90 + tilt);
			if (toNearest) {
				if (ViewRotMatrix[1][0] > 0.4823)
					viewRot = rotateView(viewRot, 0, 120, SbVec3f(1, -1, 1));
				else if (ViewRotMatrix[1][1] < -0.4823)
					viewRot = rotateView(viewRot, 0, -120, SbVec3f(1, -1, 1));
			}
			break;
		case TEX_TOP_RIGHT_FRONT:
			viewRot = setView(rot, 90 - tilt);
			if (toNearest) {
				if (ViewRotMatrix[1][0] > 0.4823)
					viewRot = rotateView(viewRot, 0, -120, SbVec3f(-1, 1, -1));
				else if (ViewRotMatrix[1][1] < -0.4823)
					viewRot = rotateView(viewRot, 0, 120, SbVec3f(-1, 1, -1));
			}
			break;
		case TEX_TOP_FRONT_LEFT:
			viewRot = setView(rot - 90, 90 - tilt);
			if (toNearest) {
				if (ViewRotMatrix[1][0] < -0.4823)
					viewRot = rotateView(viewRot, 0, 120, SbVec3f(1, 1, -1));
				else if (ViewRotMatrix[1][1] < -0.4823)
					viewRot = rotateView(viewRot, 0, -120, SbVec3f(1, 1, -1));
			}
			break;
		case TEX_TOP_LEFT_REAR:
			viewRot = setView(rot - 180, 90 - tilt);
			if (toNearest) {
				if (ViewRotMatrix[1][0] < -0.4823)
					viewRot = rotateView(viewRot, 0, -120, SbVec3f(1, -1, -1));
				else if (ViewRotMatrix[1][1] > 0.4823)
					viewRot = rotateView(viewRot, 0, 120, SbVec3f(1, -1, -1));
			}
			break;
		case TEX_TOP_REAR_RIGHT:
			viewRot = setView(rot - 270, 90 - tilt);
			if (toNearest) {
				if (ViewRotMatrix[1][0] > 0.4823)
					viewRot = rotateView(viewRot, 0, 120, SbVec3f(-1, -1, -1));
				else if (ViewRotMatrix[1][1] > 0.4823)
					viewRot = rotateView(viewRot, 0, -120, SbVec3f(-1, -1, -1));
			}
			break;
		case TEX_ARROW_LEFT:
			viewRot = rotateView(viewRot, DIR_OUT, rotStepAngle);
			break;
		case TEX_ARROW_RIGHT:
			viewRot = rotateView(viewRot, DIR_OUT, -rotStepAngle);
			break;
		case TEX_ARROW_WEST:
			viewRot = rotateView(viewRot, DIR_UP, -rotStepAngle);
			break;
		case TEX_ARROW_EAST:
			viewRot = rotateView(viewRot, DIR_UP, rotStepAngle);
			break;
		case TEX_ARROW_NORTH:
			viewRot = rotateView(viewRot, DIR_RIGHT, -rotStepAngle);
			break;
		case TEX_ARROW_SOUTH:
			viewRot = rotateView(viewRot, DIR_RIGHT, rotStepAngle);
			break;
		case TEX_DOT_BACKSIDE:
			viewRot = rotateView(viewRot, DIR_UP, 180);
			break;
		case TEX_VIEW_MENU_FACE:
			m_Hit = true;
			m_Shared->handleMenu(m_View3DInventorViewer->parentWidget());
			applyRotation = false;
			break;
		}

		if (applyRotation)
			rotateView(viewRot);
	}
	return true;
}


void NaviCubeImplementation::setHilite(int hilite) {
	if (hilite != m_HiliteId) {
		m_HiliteId = hilite;
		//cerr << "m_HiliteFace " << m_HiliteId << endl;
		m_View3DInventorViewer->getSoRenderManager()->scheduleRedraw();
	}
}

bool NaviCubeImplementation::inDragZone(short x, short y) {
	int dx = x - m_CubeWidgetPosX;
	int dy = y - m_CubeWidgetPosY;
	int limit = m_CubeWidgetSize / 4;
	return abs(dx) < limit && abs(dy) < limit;
}

bool NaviCubeImplementation::mouseMoved(short x, short y) {
    bool redraw = false;
    bool res = false;
	setHilite(pickFace(x, y));
    
	if (m_MouseDown) {
		if (m_MightDrag && !m_Dragging && !inDragZone(x, y))
			m_Dragging = true;
		if (m_Dragging) {
			setHilite(0);
			SbVec2s view = m_View3DInventorViewer->getSoRenderManager()->getSize();
			int width = view[0];
			int height = view[1];
			int len = m_CubeWidgetSize / 2;
			m_CubeWidgetPosX = std::min(std::max(static_cast<int>(x), len), width - len);
			m_CubeWidgetPosY = std::min(std::max(static_cast<int>(y), len), height - len);
            redraw = true;
            res = true;
		}
	}
    else
        m_Dragging = false;

    bool hit = m_HiliteId || m_Dragging;
    if (!hit) {
        int dx = x - m_CubeWidgetPosX;
        int dy = y - m_CubeWidgetPosY;
	    hit = abs(dx)<m_CubeWidgetSize/2 && abs(dy)<m_CubeWidgetSize/2;
    }
    if (m_Hit != hit) {
        m_Hit = hit;
        if (!redraw)
            autoHideTimer.start(NaviCubeShared::m_AutoHideTimeout);
    }

    if (redraw)
        this->m_View3DInventorViewer->getSoRenderManager()->scheduleRedraw();
    return res;
}

bool NaviCubeImplementation::processSoEvent(const SoEvent* ev) {
	short x, y;
	ev->getPosition().getValue(x, y);
	// FIXME find out why do we need to hack the cursor position to get
	// 2019-02-17
	// The above comment is truncated; don't know what it's about
	// The two hacked lines changing the cursor position are responsible for
	// parts of the navigational cluster not being active.
	// Commented them out and everything seems to be working
//    y += 4;
//    x -= 2;
	if (ev->getTypeId().isDerivedFrom(SoMouseButtonEvent::getClassTypeId())) {
		const auto mbev = static_cast<const SoMouseButtonEvent*>(ev);
		if (mbev->isButtonPressEvent(mbev, SoMouseButtonEvent::BUTTON1))
			return mousePressed(x, y);
		if (mbev->isButtonReleaseEvent(mbev, SoMouseButtonEvent::BUTTON1))
			return mouseReleased(x, y);
	}
	if (ev->getTypeId().isDerivedFrom(SoLocation2Event::getClassTypeId()))
		return mouseMoved(x, y);
	return false;
}


void NaviCubeShared::handleMenu(QWidget *parent) {
    if (!m_Menu.actions().isEmpty()) {
        m_Menu.popup(QCursor::pos());
        return;
    }

    m_Menu.setToolTipsVisible(true);
    CommandManager &rcCmdMgr = Application::Instance->commandManager();
    static std::vector<const char *> commands = {
        "Std_OrthographicCamera",
        "Std_PerspectiveCamera",
        0,
        "Std_ViewIsometric",
        "Std_ViewDimetric",
        "Std_ViewTrimetric",
        0,
        "Std_ViewFitAll",
    };
    for (auto command : commands) {
        if (!command) {
            m_Menu.addSeparator();
        }
        else {
            Command* cmd = rcCmdMgr.getCommandByName(command);
            if (cmd)
                cmd->addTo(&m_Menu);
        }
    }
    m_Menu.addSeparator();

    QCheckBox *checkboxRotate;
    auto action = Gui::Action::addCheckBox(
                                &m_Menu,
                                QObject::tr("Rotate to nearest"),
                                QObject::tr("Rotates to nearest possible state when clicking a cube face"),
                                QIcon(),
                                m_RotateToNearest,
                                &checkboxRotate);
    QObject::connect(action, &QAction::toggled, [this](bool checked) {
        m_hGrp->SetBool("NaviRotateToNearest", checked);
    });

    auto subMenu = m_Menu.addMenu(QObject::tr("Auto hide"));

    QCheckBox *checkboxAutoHideButton;
    action = Gui::Action::addCheckBox(
                                subMenu,
                                QObject::tr("Buttons"),
                                QObject::tr("Auto hide navigation buttons on mouse leave"),
                                QIcon(),
                                m_AutoHideButton,
                                &checkboxAutoHideButton);
    QObject::connect(action, &QAction::toggled, [this](bool checked) {
        m_hGrp->SetBool("AutoHideButton", checked);
    });

    QCheckBox *checkboxAutoHideCube;
    action = Gui::Action::addCheckBox(
                                subMenu,
                                QObject::tr("Navigation cube"),
                                QObject::tr("Auto hide navigation cube on mouse leave"),
                                QIcon(),
                                m_AutoHideCube,
                                &checkboxAutoHideCube);
    QObject::connect(action, &QAction::toggled, [this](bool checked) {
        m_hGrp->SetBool("AutoHideCube", checked);
    });

    auto spinBoxAutoHide = new QSpinBox;
    spinBoxAutoHide->setMinimum(0);
    spinBoxAutoHide->setMaximum(9999);
    spinBoxAutoHide->setSingleStep(1);
    spinBoxAutoHide->setValue(m_AutoHideTimeout);
    Gui::Action::addWidget(&m_Menu, QObject::tr("Auto hide timeout"),
                           QString(), spinBoxAutoHide);
    QObject::connect(spinBoxAutoHide, QOverload<int>::of(&QSpinBox::valueChanged), [this](int value) {
        m_hGrp->SetInt("AutoHideTimeout", value);
    });

    QCheckBox *checkboxShowCS;
    action = Gui::Action::addCheckBox(
                                &m_Menu,
                                QObject::tr("Show coordinate system"),
                                QString(),
                                QIcon(),
                                m_ShowCS,
                                &checkboxShowCS);
    QObject::connect(action, &QAction::toggled, [this](bool checked) {
        m_hGrp->SetBool("ShowCS", checked);
    });

    auto spinBoxSize = new QSpinBox;
    spinBoxSize->setMinimum(10);
    spinBoxSize->setMaximum(1024);
    spinBoxSize->setSingleStep(10);
    spinBoxSize->setValue(m_CubeWidgetSize);
    Gui::Action::addWidget(&m_Menu, QObject::tr("Cube size"),
            QObject::tr("Size of the navigation cube"), spinBoxSize);
    QObject::connect(spinBoxSize, QOverload<int>::of(&QSpinBox::valueChanged), [this](int value) {
        m_hGrp->SetInt("CubeSize", value);
    });

    auto spinBoxSteps = new QSpinBox;
    spinBoxSteps->setMinimum(4);
    spinBoxSteps->setMaximum(36);
    spinBoxSteps->setValue(m_StepByTurn);
    Gui::Action::addWidget(&m_Menu, QObject::tr("Steps by turn"),
            QObject::tr("Number of steps by turn when using arrows (default = 8 : step angle = 360/8 = 45 deg)"),
            spinBoxSteps);
    QObject::connect(spinBoxSteps, QOverload<int>::of(&QSpinBox::valueChanged), [this](int value) {

        m_hGrp->SetInt("NaviStepByTurn", value);
    });

    auto spinBoxWidth = new QDoubleSpinBox;
    spinBoxWidth->setMinimum(0.);
    spinBoxWidth->setMaximum(10.);
    spinBoxWidth->setSingleStep(0.5);
    spinBoxWidth->setValue(m_BorderWidth);
    Gui::Action::addWidget(&m_Menu, QObject::tr("Border width"),
            QObject::tr("Cube face border line width"), spinBoxWidth);
    QObject::connect(spinBoxWidth, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    [this](double value) {
        m_hGrp->SetFloat("BorderWidth", value);
    });

    auto spinBoxChamfer = new QDoubleSpinBox;
    spinBoxChamfer->setMinimum(0.);
    spinBoxChamfer->setMaximum(1.);
    spinBoxChamfer->setSingleStep(0.01);
    spinBoxChamfer->setValue(m_Chamfer);
    Gui::Action::addWidget(&m_Menu, QObject::tr("Corner size"),
            QObject::tr("Cube face corner chamfer size factor"), spinBoxChamfer);
    QObject::connect(spinBoxChamfer, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    [this](double value) {
        m_hGrp->SetFloat("ChamferSize", value);
    });

    QObject::connect(&m_Menu, &QMenu::aboutToShow,
    [=](){
        { QSignalBlocker blocker(checkboxRotate); checkboxRotate->setChecked(m_RotateToNearest); }
        { QSignalBlocker blocker(checkboxAutoHideCube); checkboxAutoHideCube->setChecked(m_AutoHideCube); }
        { QSignalBlocker blocker(checkboxAutoHideButton); checkboxAutoHideButton->setChecked(m_AutoHideButton); }
        { QSignalBlocker blocker(spinBoxAutoHide); spinBoxAutoHide->setValue(m_AutoHideTimeout); }
        { QSignalBlocker blocker(checkboxShowCS); checkboxShowCS->setChecked(m_ShowCS); }
        { QSignalBlocker blocker(spinBoxSize); spinBoxSize->setValue(m_CubeWidgetSize); }
        { QSignalBlocker blocker(spinBoxSteps); spinBoxSteps->setValue(m_StepByTurn); }
        { QSignalBlocker blocker(spinBoxWidth); spinBoxWidth->setValue(m_BorderWidth); }
    });

    action = m_Menu.addAction(QObject::tr("Colors..."));
    action->setToolTip(QObject::tr("Change navigation cube face colors"));
    QObject::connect(action, &QAction::triggered, [parent]() {
        NaviCube::setColors(parent);
    });

    action = m_Menu.addAction(QObject::tr("Labels..."));
    action->setToolTip(QObject::tr("Change navigation cube labels"));
    QObject::connect(action, &QAction::triggered, [parent]() {
        NaviCube::setLabels(parent);
    });

    action = m_Menu.addAction(QObject::tr("Axis labels..."));
    action->setToolTip(QObject::tr("Change coordinate system axis labels"));
    QObject::connect(action, &QAction::triggered, [parent]() {
        NaviCubeShared::instance()->setAxisLabels(parent);
    });

    m_Menu.popup(QCursor::pos());
}

void NaviCube::setLabels(QWidget *parent)
{
    NaviCubeShared::instance()->setLabels(parent);
}

void NaviCubeShared::setLabels(QWidget *parent)
{
    if (m_DlgLabels) {
        m_DlgLabels->setParent(parent);
        m_DlgLabels->show();
        return;
    }

    auto layout = new QVBoxLayout;
    layout->setSizeConstraint(QLayout::SetFixedSize);
    m_DlgLabels = new QDialog(parent);
    QDialog &dlg = *m_DlgLabels;
    dlg.setAttribute(Qt::WA_DeleteOnClose);
    dlg.setLayout(layout);
    dlg.setWindowTitle(QObject::tr("Navigation Cube Labels"));
    auto grid = new QGridLayout;
    layout->addLayout(grid);
    auto buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok|QDialogButtonBox::Cancel, Qt::Horizontal);
    QObject::connect(buttons, SIGNAL(accepted()), &dlg, SLOT(accept()));
    QObject::connect(buttons, SIGNAL(rejected()), &dlg, SLOT(reject()));
    layout->addWidget(buttons);
    int row = 0;
    std::vector<std::string> labels;
    for (auto &info : m_labels) {
        grid->addWidget(new QLabel(QObject::tr(info.title)), row, 0);
        labels.push_back(m_hGrp->GetASCII(info.name));
        auto edit = new QLineEdit;
        if (labels.back().empty())
            edit->setText(QObject::tr(info.def));
        else
            edit->setText(QString::fromUtf8(labels.back().c_str()));
        auto timer = new QTimer(edit);
        timer->setSingleShot(true);
        QObject::connect(edit, &QLineEdit::textEdited, [timer]() {
            timer->start(500);
        });
        QObject::connect(timer, &QTimer::timeout, [this, edit, info]() {
            QString t = edit->text();
            if (t.isEmpty())
                m_hGrp->RemoveASCII(info.name);
            else
                m_hGrp->SetASCII(info.name, t.toUtf8().constData());
        });
        grid->addWidget(edit, row++, 1);
    }

    QFont font = getLabelFont();
    auto fontButton = new QPushButton(QObject::tr("Label font"));
    fontButton->setFont(font);
    grid->addWidget(fontButton, row++, 0, 1, 2);
    QObject::connect(fontButton, &QPushButton::clicked, [this, &dlg, fontButton]() {
        if (this->m_DlgFont) {
            this->m_DlgFont->show();
            return;
        }
        QFont curFont(getLabelFont());
        auto fontDlg = new QFontDialog(&dlg);
        fontDlg->setAttribute(Qt::WA_DeleteOnClose);
        // fontDlg->setOption(QFontDialog::DontUseNativeDialog);
        fontDlg->setCurrentFont(curFont);
        this->m_DlgFont = fontDlg;
        QObject::connect(fontDlg, &QFontDialog::currentFontChanged, [this, fontButton](const QFont &f) {
            saveLabelFont(f);
            fontButton->setFont(getLabelFont());
        });
        QObject::connect(fontDlg, &QFontDialog::finished, [this, curFont, fontButton](int result) {
            if (result == QDialog::Rejected) {
                saveLabelFont(curFont);
                fontButton->setFont(getLabelFont());
            }
        });
        fontDlg->show();
    });

    auto checkbox = new QCheckBox(QObject::tr("Auto scale"));
    checkbox->setToolTip(QObject::tr("Auto scale font pixel size based on navigation cube size.\n"
                                     "If disabled, then use the selected font point size.\n"));
    bool autoSize = m_hGrp->GetBool("FontAutoSize", true);
    checkbox->setChecked(autoSize);
    grid->addWidget(checkbox, row, 0);
    QObject::connect(checkbox, &QCheckBox::toggled, [this, fontButton](bool checked) {
        m_hGrp->SetBool("FontAutoSize", checked);
        fontButton->setFont(getLabelFont());
    });

    auto spinBoxScale = new QDoubleSpinBox;
    grid->addWidget(spinBoxScale, row++, 1);
    spinBoxScale->setValue(m_hGrp->GetFloat("FontScale", 0.22));
    spinBoxScale->setMinimum(0.1);
    spinBoxScale->setSingleStep(0.01);
    QObject::connect(spinBoxScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        [this, fontButton](double value) {
            m_hGrp->SetFloat("FontScale", value);
            fontButton->setFont(getLabelFont());
        });

    grid->addWidget(new QLabel(QObject::tr("Font stretch")), row, 0);
    auto spinBoxStretch = new QSpinBox;
    grid->addWidget(spinBoxStretch, row++, 1);
    spinBoxStretch->setValue(font.stretch());
    QObject::connect(spinBoxStretch, QOverload<int>::of(&QSpinBox::valueChanged),
        [this, fontButton](int value) {
            m_hGrp->SetInt("FontStretch", value);
            fontButton->setFont(getLabelFont());
        });

    auto self = this->shared_from_this();
    QObject::connect(&dlg, &QDialog::finished, [self, labels, font, autoSize](int result) {
        if (result == QDialog::Rejected) {
            int i=0;
            for (auto &info : self->m_labels) {
                if (labels[i].empty())
                    self->m_hGrp->RemoveASCII(info.name);
                else
                    self->m_hGrp->SetASCII(info.name, labels[i].c_str());
                ++i;
            }
            self->saveLabelFont(font);
            self->m_hGrp->SetBool("FontAutoSize", autoSize);
        }
    });

    m_DlgLabels->show();
}

void NaviCubeShared::setAxisLabels(QWidget *parent)
{
    if (m_DlgAxisLabels) {
        m_DlgAxisLabels->setParent(parent);
        m_DlgAxisLabels->show();
        return;
    }

    auto layout = new QVBoxLayout;
    layout->setSizeConstraint(QLayout::SetFixedSize);
    m_DlgAxisLabels = new QDialog(parent);
    QDialog &dlg = *m_DlgAxisLabels;
    dlg.setAttribute(Qt::WA_DeleteOnClose);
    dlg.setLayout(layout);
    dlg.setWindowTitle(QObject::tr("Navigation Cube Axis Labels"));
    auto grid = new QGridLayout;
    layout->addLayout(grid);
    auto buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok|QDialogButtonBox::Cancel, Qt::Horizontal);
    QObject::connect(buttons, SIGNAL(accepted()), &dlg, SLOT(accept()));
    QObject::connect(buttons, SIGNAL(rejected()), &dlg, SLOT(reject()));
    layout->addWidget(buttons);
    int row = 0;
    std::vector<std::string> labels;
    for (auto &info : m_AxisLabels) {
        grid->addWidget(new QLabel(QObject::tr(info.title)), row, 0);
        labels.push_back(m_hGrp->GetASCII(info.name, info.def));
        auto edit = new QLineEdit;
        edit->setText(QString::fromUtf8(labels.back().c_str()));
        auto timer = new QTimer(edit);
        timer->setSingleShot(true);
        QObject::connect(edit, &QLineEdit::textEdited, [timer]() {
            timer->start(500);
        });
        QObject::connect(timer, &QTimer::timeout, [this, edit, info]() {
            QString t = edit->text();
            m_hGrp->SetASCII(info.name, t.toUtf8().constData());
        });
        grid->addWidget(edit, row++, 1);
    }

    QFont font = getAxisLabelFont();
    auto fontButton = new QPushButton(QObject::tr("Label font"));
    fontButton->setFont(font);
    grid->addWidget(fontButton, row++, 0, 1, 2);
    QObject::connect(fontButton, &QPushButton::clicked, [this, &dlg, fontButton]() {
        if (this->m_DlgAxisFont) {
            this->m_DlgAxisFont->show();
            return;
        }
        QFont curFont(getAxisLabelFont());
        auto fontDlg = new QFontDialog(&dlg);
        fontDlg->setAttribute(Qt::WA_DeleteOnClose);
        fontDlg->setCurrentFont(curFont);
        this->m_DlgAxisFont = fontDlg;
        QObject::connect(fontDlg, &QFontDialog::currentFontChanged, [this, fontButton](const QFont &f) {
            saveAxisLabelFont(f);
            fontButton->setFont(getAxisLabelFont());
        });
        QObject::connect(fontDlg, &QFontDialog::finished, [this, curFont, fontButton](int result) {
            if (result == QDialog::Rejected) {
                saveAxisLabelFont(curFont);
                fontButton->setFont(getAxisLabelFont());
            }
        });
        fontDlg->show();
    });

    auto self = this->shared_from_this();
    QObject::connect(&dlg, &QDialog::finished, [self, labels, font](int result) {
        if (result == QDialog::Rejected) {
            int i=0;
            for (auto &info : self->m_AxisLabels) {
                if (labels[i] == info.def)
                    self->m_hGrp->RemoveASCII(info.name);
                else
                    self->m_hGrp->SetASCII(info.name, labels[i].c_str());
                ++i;
            }
            self->saveAxisLabelFont(font);
        }
    });

    m_DlgAxisLabels->show();
}

void NaviCube::setColors(QWidget *parent)
{
    NaviCubeShared::instance()->setColors(parent);
}

void NaviCubeShared::setColors(QWidget *parent)
{
    if (m_DlgColors) {
        m_DlgColors->setParent(parent);
        m_DlgColors->show();
        return;
    }

    m_DlgColors = new QDialog(parent);
    QDialog &dlg = *m_DlgColors;
    dlg.setAttribute(Qt::WA_DeleteOnClose);
    auto layout = new QVBoxLayout;
    layout->setSizeConstraint(QLayout::SetFixedSize);
    dlg.setLayout(layout);
    dlg.setWindowTitle(QObject::tr("Navigation Cube Colors"));
    auto grid = new QGridLayout;
    layout->addLayout(grid);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, Qt::Horizontal);
    QObject::connect(buttons, SIGNAL(accepted()), &dlg, SLOT(accept()));
    QObject::connect(buttons, SIGNAL(rejected()), &dlg, SLOT(reject()));
    layout->addWidget(buttons);
    int row = 0;
    std::vector<QColor> colors;
    for (auto &info : m_colors) {
        colors.push_back(info.color);
        grid->addWidget(new QLabel(QObject::tr(info.title)), row, 0);
        ColorButton *button = new ColorButton(nullptr);
        button->setAllowTransparency(true);
        button->setAutoChangeColor(true);
        button->setColor(info.color);
        QObject::connect(button, &ColorButton::changed, [this, button, info]() {
            m_hGrp->SetUnsigned(info.name, button->color().rgba());
        });
        grid->addWidget(button, row, 1);
        ++row;
    }
    auto self = this->shared_from_this();
    QObject::connect(&dlg, &QDialog::finished, [self, colors](int result) {
        if (result == QDialog::Rejected) {
            int i=0;
            for (auto &info : self->m_colors)
                self->m_hGrp->SetUnsigned(info.name, colors[i++].rgba());
        }
    });
    m_DlgColors->show();
}

QFont NaviCubeShared::getLabelFont()
{
    QString fontString = QString::fromUtf8((m_hGrp->GetASCII("FontString", "Helvetica")).c_str());
    int fontSize = m_hGrp->GetInt("FontSize");
    QFont sansFont(fontString);
    if (fontSize <= 0 || m_hGrp->GetBool("FontAutoSize", true)) {
	    int texSize = m_CubeWidgetSize * m_OverSample;
        sansFont.setPixelSize(m_hGrp->GetFloat("FontScale", 0.22) * texSize);
    } else if (fontSize > 0)
        sansFont.setPointSize(fontSize);
    sansFont.setItalic(m_hGrp->GetBool("FontItalic", false));
    int weight = m_hGrp->GetInt("FontWeight", 87);
    if (weight > 0)
        sansFont.setWeight(convertWeights(weight));
    int stretch = m_hGrp->GetInt("FontStretch", 62);
    if (stretch > 0)
        sansFont.setStretch(stretch);
    return sansFont;
}

void NaviCubeShared::saveLabelFont(const QFont &font)
{
    if (font.family().isEmpty())
        m_hGrp->RemoveASCII("FontString");
    else
        m_hGrp->SetASCII("FontString", font.family().toUtf8().constData());
    m_hGrp->SetInt("FontSize", font.pointSize());
    m_hGrp->SetInt("FontWeight", font.weight());
    m_hGrp->SetBool("FontItalic", font.italic());
}

QFont NaviCubeShared::getAxisLabelFont()
{
    QString fontString = QString::fromUtf8((m_hGrp->GetASCII("AxisFont", "Monospace")).c_str());
    int fontSize = m_hGrp->GetInt("AxisFontSize", 8);
    QFont font(fontString);
    font.setPointSize(fontSize);
    font.setItalic(m_hGrp->GetBool("AxisFontItalic", false));
    int weight = m_hGrp->GetInt("AxisFontWeight", 50);
    if (weight > 0)
        font.setWeight(convertWeights(weight));
    return font;
}

void NaviCubeShared::saveAxisLabelFont(const QFont &font)
{
    if (font.family().isEmpty())
        m_hGrp->RemoveASCII("AxisFont");
    else
        m_hGrp->SetASCII("AxisFont", font.family().toUtf8().constData());
    m_hGrp->SetInt("AxisFontSize", font.pointSize());
    m_hGrp->SetInt("AxisFontWeight", font.weight());
    m_hGrp->SetBool("AxisFontItalic", font.italic());
}
