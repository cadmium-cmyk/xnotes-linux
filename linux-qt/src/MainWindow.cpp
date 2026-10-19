#include "MainWindow.h"

#include "CanvasWidget.h"
#include "Icons.h"
#include "NoteFinder.h"
#include "PageStylePopup.h"
#include "PagesPanel.h"
#include "PenStylePopup.h"
#include "PdfExport.h"
#include "SelectionBar.h"

#include <QApplication>
#include <QCloseEvent>
#include <QClipboard>
#include <QColorDialog>
#include <QDateTime>
#include <QDockWidget>
#include <QGridLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QPushButton>
#include <QScrollBar>
#include <QImageReader>
#include <QMimeData>
#include <QUuid>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QMenu>
#include <QToolButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QStatusBar>
#include <QStyleHints>
#include <QTimer>
#include <QToolBar>

namespace {

struct ToolEntry {
    const char* id;
    const char* label;
    const char* key;
    const char* icon;
};

const ToolEntry kTools[] = {
    {"pan", "Pan", "1", "pan"},
    {"pen", "Pen", "2", "ic_stroke_regular"},
    {"highlighter", "Highlighter", "3", "ic_stroke_highlighter"},
    {"eraser", "Eraser", "4", "eraser"},
    {"lasso", "Lasso", "5", "lasso"},
    {"select", "Select", "6", "select"},
    {"shape", "Shape", "7", "shape"},
    {"text_box", "Text", "8", "textBox"},
};

/* Kinds of pen (the Android stroke tools), drawn by the pen button. */
const ToolEntry kPens[] = {
    {"pen", "Pen", "", "ic_stroke_regular"},
    {"dashed", "Dashed", "", "ic_stroke_dashed"},
    {"calligraphy", "Calligraphy", "", "ic_stroke_calligraphy"},
    {"speed", "Speed", "", "ic_stroke_speed"},
    {"taper", "Taper", "", "ic_stroke_taper"},
};

const ToolEntry* penEntry(const QByteArray& kind) {
    for (const ToolEntry& p : kPens)
        if (kind == p.id) return &p;
    return nullptr;
}

/* Pens whose width can follow the pressure. */
bool hasPressure(const QByteArray& tool) { return tool == "pen" || tool == "calligraphy" || tool == "speed" || tool == "taper"; }

struct FaceEntry {
    const char* id;
    const char* label;
};

const FaceEntry kFaces[] = {{"mono", "Monospace"}, {"sans", "Sans-serif"}, {"serif", "Serif"}, {"hand", "Handwriting"}};

struct ShapeEntry {
    const char* id;
    const char* label;
    const char* icon;
};

const ShapeEntry kShapes[] = {
    {"line", "Line", "shapeLine"}, {"arrow", "Arrow", "shapeArrow"}, {"rectangle", "Rectangle", "shapeRect"},
    {"ellipse", "Ellipse", "shapeEllipse"}, {"circle", "Circle", "shapeCircle"}, {"triangle", "Triangle", "shapeTriangle"},
};

/* The app's ink presets (InkPalette) plus black and blue for light paper. */
const QList<QColor> kSwatches = {
    QColor(0, 230, 118), QColor(236, 236, 236), QColor(255, 92, 92), QColor(255, 199, 64),
    QColor(88, 196, 255), QColor(199, 134, 255), QColor(128, 128, 128), QColor(28, 28, 28), QColor(33, 150, 243),
};

/* Tools whose ink width the toolbar sets. */
/* The width slider's range per tool, as on Android (ToolDefaults.widthRange); the eraser's is its radius. */
std::pair<double, double> widthRange(const QByteArray& tool) {
    if (tool == "highlighter") return {4.0, 40.0};
    if (tool == "eraser") return {1.0, 80.0};
    return {1.0, 20.0};
}

/* Tools whose ink width (the eraser: its radius) the toolbar sets. */
bool hasWidth(const QByteArray& tool) { return penEntry(tool) || tool == "highlighter" || tool == "shape" || tool == "eraser"; }

QString takeError(char* error) {
    QString message = error ? QString::fromUtf8(error) : QStringLiteral("unknown error");
    xn_free_string(error);
    return message;
}

bool isSystemDark() {
    static const bool initialDark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) return true;
    if (scheme == Qt::ColorScheme::Light) return false;
#endif
    return initialDark;
}

QPalette buildUiPalette(bool dark) {
    QPalette p;
    if (dark) {
        p.setColor(QPalette::Window, QColor(0x11, 0x11, 0x11));
        p.setColor(QPalette::WindowText, QColor(0xc8, 0xc8, 0xc8));
        p.setColor(QPalette::Base, QColor(0x0d, 0x0d, 0x0d));
        p.setColor(QPalette::AlternateBase, QColor(0x16, 0x16, 0x16));
        p.setColor(QPalette::ToolTipBase, QColor(0x24, 0x24, 0x24));
        p.setColor(QPalette::ToolTipText, QColor(0xc8, 0xc8, 0xc8));
        p.setColor(QPalette::Text, QColor(0xc8, 0xc8, 0xc8));
        p.setColor(QPalette::PlaceholderText, QColor(0x6f, 0x6f, 0x6f));
        p.setColor(QPalette::Button, QColor(0x16, 0x16, 0x16));
        p.setColor(QPalette::ButtonText, QColor(0xc8, 0xc8, 0xc8));
        p.setColor(QPalette::BrightText, Qt::white);
        p.setColor(QPalette::Mid, QColor(0x2c, 0x2c, 0x2c));
        p.setColor(QPalette::Highlight, QColor(0x00, 0xe6, 0x76));
        p.setColor(QPalette::HighlightedText, QColor(0x0a, 0x0a, 0x0a));
    } else {
        p.setColor(QPalette::Window, QColor(0xf4, 0xf4, 0xf4));
        p.setColor(QPalette::WindowText, QColor(0x1c, 0x1c, 0x1c));
        p.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::AlternateBase, QColor(0xe8, 0xe8, 0xe8));
        p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::ToolTipText, QColor(0x1c, 0x1c, 0x1c));
        p.setColor(QPalette::Text, QColor(0x1c, 0x1c, 0x1c));
        p.setColor(QPalette::PlaceholderText, QColor(0x6f, 0x6f, 0x6f));
        p.setColor(QPalette::Button, QColor(0xfb, 0xfb, 0xfb));
        p.setColor(QPalette::ButtonText, QColor(0x1c, 0x1c, 0x1c));
        p.setColor(QPalette::BrightText, Qt::black);
        p.setColor(QPalette::Mid, QColor(0xc4, 0xc4, 0xc4));
        p.setColor(QPalette::Highlight, QColor(0x00, 0xa1, 0x52));
        p.setColor(QPalette::HighlightedText, Qt::white);
    }
    return p;
}

}  // namespace

MainWindow::MainWindow() {
    // Capture initial system palette lightness before custom palettes are applied.
    (void)isSystemDark();

    // The canvas with scroll bars beside and under it (device pixels, as the core counts).
    auto* central = new QWidget(this);
    auto* grid = new QGridLayout(central);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    canvas_ = new CanvasWidget(central);
    vbar_ = new QScrollBar(Qt::Vertical, central);
    hbar_ = new QScrollBar(Qt::Horizontal, central);
    vbar_->hide();
    hbar_->hide();
    grid->addWidget(canvas_, 0, 0);
    grid->addWidget(vbar_, 0, 1);
    grid->addWidget(hbar_, 1, 0);
    setCentralWidget(central);
    auto scrolled = [this] {
        if (syncingBars_ || !canvas_->editor()) return;
        xn_editor_scroll_to(canvas_->editor(), hbar_->value(), vbar_->value());
    };
    connect(vbar_, &QScrollBar::valueChanged, this, scrolled);
    connect(hbar_, &QScrollBar::valueChanged, this, scrolled);
    vbar_->hide();
    hbar_->hide();
    // Page thumbnails on the left (the Android side panel), shown with notes, not canvases.
    pages_ = new PagesPanel(this);
    pagesDock_ = new QDockWidget(QStringLiteral("Pages"), this);
    pagesDock_->setObjectName(QStringLiteral("pagesDock"));
    pagesDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    pagesDock_->setTitleBarWidget(new QWidget(pagesDock_));
    pagesDock_->setWidget(pages_);
    pagesDock_->hide();
    addDockWidget(Qt::LeftDockWidgetArea, pagesDock_);
    buildUi();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (theme_ == Theme::System) applyTheme();
    });
#endif
    QSettings settings(QStringLiteral("xnotes"), QStringLiteral("xnotes"));
    const int saved = settings.value(QStringLiteral("theme"), int(Theme::System)).toInt();
    setTheme(saved == int(Theme::Light) ? Theme::Light : saved == int(Theme::Dark) ? Theme::Dark : Theme::System);

    connect(canvas_, &CanvasWidget::contentChanged, pages_, &PagesPanel::contentChanged);
    connect(canvas_, &CanvasWidget::viewChanged, pages_, &PagesPanel::viewChanged);
    connect(canvas_, &CanvasWidget::contentChanged, this, &MainWindow::refresh);
    connect(canvas_, &CanvasWidget::viewChanged, this, &MainWindow::refreshView);
    connect(canvas_, &CanvasWidget::notice, this, [this](int code) {
        statusBar()->showMessage(code == XN_NOTICE_KEEP_ONE_PAGE ? QStringLiteral("The note must contain at least one page")
                                 : code == XN_NOTICE_PAGE_ALREADY_EMPTY ? QStringLiteral("The page is already empty")
                                                                        : QString(), 4000);
    });
    statusBar()->hide();
    // Input and speed, for checking a tablet: whether Qt reports it as a pen with pressure.
    inputLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(inputLabel_);
    auto* stats = new QTimer(this);
    connect(stats, &QTimer::timeout, this, [this] {
        const QString input = canvas_->lastInput();
        const double interval = canvas_->frameIntervalMs();
        const int b = canvas_->penButtons();
        const QString buttons = b == 0 ? QString() : b == 1 ? QStringLiteral("pen button 1    ") : b == 2 ? QStringLiteral("pen button 2    ") : QStringLiteral("pen buttons 1+2    ");
        inputLabel_->setText(buttons + (input.isEmpty() ? QString() : QStringLiteral("input: %1, pressure %2    ").arg(input).arg(canvas_->lastPressure(), 0, 'f', 2)) +
                             QStringLiteral("%1: frame %2 ms").arg(canvas_->render() == CanvasWidget::Render::Gl ? QStringLiteral("GPU") : QStringLiteral("CPU"))
                                 .arg(canvas_->lastFrameMs(), 0, 'f', 1) +
                             (interval > 0 ? QStringLiteral(", %1 fps").arg(int(std::lround(1000 / interval))) : QString()));
    });
    stats->start(250);
    resize(1200, 900);
}

MainWindow::~MainWindow() {
    pages_->setDocument(nullptr, nullptr);
    canvas_->setNote(nullptr);
    if (note_) xn_note_close(note_);
}

void MainWindow::buildUi() {
    QToolBar* bar = addToolBar(QStringLiteral("Main"));
    bar->setMovable(false);
    pagesAction_ = bar->addAction(xnIcon(QStringLiteral("sidebar")), QStringLiteral("Pages"));
    actionIcons_[pagesAction_] = QStringLiteral("sidebar");
    pagesAction_->setCheckable(true);
    pagesAction_->setChecked(false);
    pagesAction_->setShortcut(QKeySequence(Qt::Key_F9));
    connect(pagesAction_, &QAction::toggled, this, [this](bool on) {
        pagesDock_->setVisible(on && !(note_ && xn_note_is_canvas(note_)));
    });
    bar->addSeparator();
    auto* create = new QToolButton(this);
    createButton_ = create;
    create->setText(QStringLiteral("New"));
    create->setIcon(xnIcon(QStringLiteral("file")));
    create->setToolTip(QStringLiteral("New"));
    create->setPopupMode(QToolButton::InstantPopup);
    auto* createMenu = new QMenu(create);
    QAction* newNoteAct = createMenu->addAction(xnIcon(QStringLiteral("file")), QStringLiteral("Note"), QKeySequence::New, this, [this] { if (confirmDiscard()) newNote(); });
    QAction* newCanvasAct = createMenu->addAction(xnIcon(QStringLiteral("canvas")), QStringLiteral("Canvas"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this, [this] { if (confirmDiscard()) newCanvas(); });
    actionIcons_[newNoteAct] = QStringLiteral("file");
    actionIcons_[newCanvasAct] = QStringLiteral("canvas");
    create->setMenu(createMenu);
    bar->addWidget(create);
    bar->addAction(QStringLiteral("Open…"), QKeySequence::Open, this, &MainWindow::chooseOpen);
    bar->addAction(QStringLiteral("Save"), QKeySequence::Save, this, [this] { save(false); });
    bar->addAction(QStringLiteral("Save As…"), QKeySequence::SaveAs, this, [this] { save(true); });
    exportAction_ = bar->addAction(QStringLiteral("Export to PDF…"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::exportPdf);
    bar->addSeparator();

    undo_ = bar->addAction(QStringLiteral("↶"), QKeySequence::Undo, this, [this] { if (canvas_->editor()) xn_editor_undo(canvas_->editor()); });
    undo_->setToolTip(QStringLiteral("Undo (Ctrl+Z)"));
    redo_ = bar->addAction(QStringLiteral("↷"), this, [this] { if (canvas_->editor()) xn_editor_redo(canvas_->editor()); });
    redo_->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)});
    redo_->setToolTip(QStringLiteral("Redo (Ctrl+Shift+Z)"));
    bar->addSeparator();

    pageActions_ << bar->addAction(QStringLiteral("←"), QKeySequence(Qt::Key_PageUp), this, [this] {
        if (auto* e = canvas_->editor()) xn_editor_go_to_page(e, std::max(0, xn_editor_current_page(e) - 1));
    });
    pageLabel_ = new QLabel(this);
    pageLabel_->setMinimumWidth(60);
    pageLabel_->setAlignment(Qt::AlignCenter);
    pageActions_ << bar->addWidget(pageLabel_);
    pageActions_ << bar->addAction(QStringLiteral("→"), QKeySequence(Qt::Key_PageDown), this, [this] {
        if (auto* e = canvas_->editor()) xn_editor_go_to_page(e, std::min(xn_note_page_count(note_) - 1, xn_editor_current_page(e) + 1));
    });
    bar->addAction(QStringLiteral("−"), QKeySequence::ZoomOut, this, [this] { if (auto* e = canvas_->editor()) xn_editor_zoom_step(e, 0); });
    zoomLabel_ = new QLabel(this);
    zoomLabel_->setMinimumWidth(50);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    bar->addWidget(zoomLabel_);
    bar->addAction(QStringLiteral("+"), QKeySequence(Qt::CTRL | Qt::Key_Equal), this, [this] { if (auto* e = canvas_->editor()) xn_editor_zoom_step(e, 1); });
    fitAction_ = bar->addAction(QStringLiteral("Fit Width"), this, [this] { if (auto* e = canvas_->editor()) xn_editor_fit_width(e); });
    pageActions_ << bar->addAction(QStringLiteral("+ Page"), this, [this] { if (auto* e = canvas_->editor()) xn_editor_add_page(e); });
    pageActions_ << bar->addAction(QStringLiteral("− Page"), this, [this] {
        if (!canvas_->editor()) return;
        if (QMessageBox::question(this, QStringLiteral("Delete Page"), QStringLiteral("Delete current page?")) == QMessageBox::Yes)
            xn_editor_delete_current_page(canvas_->editor());
    });
    bar->addSeparator();
    bar->addAction(QStringLiteral("Background…"), this, [this] {
        if (!canvas_->editor()) return;
        if (!pageStyle_) {
            pageStyle_ = new PageStylePopup(canvas_->editor(), xn_note_is_canvas(note_) != 0, this);
            pageStyle_->setAttribute(Qt::WA_DeleteOnClose);
        }
        pageStyle_->show();
        pageStyle_->raise();
    });

    themeButton_ = new QToolButton(this);
    themeButton_->setText(QStringLiteral("Theme"));
    themeButton_->setIcon(xnIcon(QStringLiteral("view")));
    themeButton_->setToolTip(QStringLiteral("Theme (System / Light / Dark)"));
    themeButton_->setPopupMode(QToolButton::InstantPopup);
    auto* themeMenu = new QMenu(themeButton_);
    auto* themeGroup = new QActionGroup(themeMenu);
    const struct {
        const char* label;
        Theme value;
    } kThemes[] = {
        {"System", Theme::System},
        {"Light", Theme::Light},
        {"Dark", Theme::Dark},
    };
    for (const auto& entry : kThemes) {
        QAction* a = themeMenu->addAction(QString::fromUtf8(entry.label));
        a->setCheckable(true);
        a->setData(int(entry.value));
        if (entry.value == theme_) a->setChecked(true);
        themeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, v = entry.value] { setTheme(v); });
    }
    themeButton_->setMenu(themeMenu);
    bar->addWidget(themeButton_);

    // Second row: tools and their style.
    QToolBar* tools = new QToolBar(QStringLiteral("Tools"), this);
    addToolBarBreak();
    addToolBar(tools);
    tools->setMovable(false);
    tools_ = new QActionGroup(this);
    for (const ToolEntry& t : kTools) {
        QAction* a = tools->addAction(QString::fromUtf8(t.label));
        a->setCheckable(true);
        a->setShortcut(QKeySequence(QString::fromLatin1(t.key)));
        a->setData(QByteArray(t.id));
        tools_->addAction(a);
        a->setIcon(xnIcon(QString::fromLatin1(t.icon)));
        actionIcons_[a] = QString::fromLatin1(t.icon);
        if (qstrcmp(t.id, "pen") == 0) a->setChecked(true);
        if (qstrcmp(t.id, "pen") == 0) {
            // The pen button draws with the kind of pen picked on its menu.
            penAction_ = a;
            auto* menu = new QMenu(this);
            auto* kinds = new QActionGroup(menu);
            for (const ToolEntry& k : kPens) {
                QAction* ka = menu->addAction(xnIcon(QString::fromLatin1(k.icon)), QString::fromUtf8(k.label));
                actionIcons_[ka] = QString::fromLatin1(k.icon);
                ka->setCheckable(true);
                ka->setChecked(qstrcmp(k.id, "pen") == 0);
                kinds->addAction(ka);
                penKinds_.insert(k.id, ka);
                connect(ka, &QAction::triggered, this, [this, k] {
                    setPenKind(k.id);
                    penAction_->trigger();
                });
            }
            menu->addSeparator();
            QAction* penSettingsAct = menu->addAction(xnIcon(QStringLiteral("sliders")), QStringLiteral("Pen Settings…"), this, &MainWindow::showPenStyle);
            actionIcons_[penSettingsAct] = QStringLiteral("sliders");
            a->setMenu(menu);
            if (auto* button = qobject_cast<QToolButton*>(tools->widgetForAction(a))) button->setPopupMode(QToolButton::MenuButtonPopup);
        }
        if (qstrcmp(t.id, "shape") == 0) {
            // The shape tool's button also opens what it draws.
            auto* menu = new QMenu(this);
            auto* kinds = new QActionGroup(menu);
            for (const ShapeEntry& k : kShapes) {
                QAction* ka = menu->addAction(xnIcon(QString::fromLatin1(k.icon)), QString::fromUtf8(k.label));
                actionIcons_[ka] = QString::fromLatin1(k.icon);
                ka->setCheckable(true);
                ka->setChecked(shapeKind_ == k.id);
                kinds->addAction(ka);
                connect(ka, &QAction::triggered, this, [this, a, k] {
                    shapeKind_ = k.id;
                    applyShape();
                    a->trigger();
                });
            }
            menu->addSeparator();
            QAction* fill = menu->addAction(QStringLiteral("Fill"));
            fill->setCheckable(true);
            connect(fill, &QAction::toggled, this, [this](bool on) { shapeFill_ = on; applyShape(); });
            QAction* dashed = menu->addAction(QStringLiteral("Dashed"));
            dashed->setCheckable(true);
            connect(dashed, &QAction::toggled, this, [this](bool on) { shapeDashed_ = on; applyShape(); });
            a->setMenu(menu);
            if (auto* button = qobject_cast<QToolButton*>(tools->widgetForAction(a))) button->setPopupMode(QToolButton::MenuButtonPopup);
        }
    }
    connect(tools_, &QActionGroup::triggered, this, [this](QAction* a) {
        canvas_->setTool(a->data().toByteArray().constData());
        syncWidth();
    });
    tools->addSeparator();

    widthLabel_ = new QLabel(this);
    widthActions_ << tools->addWidget(widthLabel_);
    // A slider in half-pixel steps (its value is twice the width), with the width beside it.
    width_ = new QSlider(Qt::Horizontal, this);
    width_->setFixedWidth(160);
    width_->setFocusPolicy(Qt::ClickFocus);
    widthValue_ = new QLabel(this);
    widthValue_->setMinimumWidth(36);
    connect(width_, &QSlider::valueChanged, this, [this](int v) {
        const double w = v / 2.0;
        widthValue_->setText(QString::number(w, 'f', 1));
        const QByteArray tool = currentTool();
        if (!hasWidth(tool) || syncingWidth_) return;
        widths_[tool] = w;
        if (tool == "shape") applyShape();
        else if (auto* e = canvas_->editor()) xn_editor_set_tool_width(e, tool.constData(), w);
    });
    widthActions_ << tools->addWidget(width_) << tools->addWidget(widthValue_);

    // Pressure: off, a line keeps its width however hard the pen presses (every pen at once).
    pressure_ = new QCheckBox(QStringLiteral("Pressure"), this);
    pressure_->setChecked(true);
    pressure_->setFocusPolicy(Qt::ClickFocus);
    pressure_->setToolTip(QStringLiteral("Stroke width follows pen pressure. Turn off for a uniform line"));
    connect(pressure_, &QCheckBox::toggled, this, &MainWindow::setPressure);
    pressureActions_ << tools->addWidget(pressure_);

    // The eraser: whole strokes or just the ink under it.
    eraseMode_ = new QComboBox(this);
    eraseMode_->addItem(QStringLiteral("Whole Stroke"), int(XN_ERASE_STROKE));
    eraseMode_->addItem(QStringLiteral("Area"), int(XN_ERASE_AREA));
    eraseMode_->setToolTip(QStringLiteral("Eraser removes intersecting strokes completely or only the ink underneath"));
    eraseMode_->setFocusPolicy(Qt::ClickFocus);
    connect(eraseMode_, &QComboBox::currentIndexChanged, this, [this] {
        if (auto* e = canvas_->editor()) xn_editor_set_erase_mode(e, eraseMode_->currentData().toInt());
    });
    eraserActions_ << tools->addWidget(eraseMode_);

    // Text box style: the box being edited or selected, and the next one.
    face_ = new QComboBox(this);
    for (const FaceEntry& f : kFaces) face_->addItem(QString::fromUtf8(f.label), QByteArray(f.id));
    face_->setFocusPolicy(Qt::ClickFocus);
    connect(face_, &QComboBox::currentIndexChanged, this, [this] {
        if (auto* e = canvas_->editor()) xn_editor_set_text_face(e, face_->currentData().toByteArray().constData());
    });
    textSize_ = new QSpinBox(this);
    textSize_->setRange(6, 96);
    textSize_->setValue(13);
    textSize_->setSuffix(QStringLiteral(" pt"));
    textSize_->setFocusPolicy(Qt::ClickFocus);
    connect(textSize_, &QSpinBox::valueChanged, this, [this](int pt) { if (auto* e = canvas_->editor()) xn_editor_set_text_size(e, pt); });
    textActions_ << tools->addWidget(face_) << tools->addWidget(textSize_);
    tools->addSeparator();

    for (const QColor& c : kSwatches) {
        QAction* a = tools->addAction(swatchIcon(c), QString());
        a->setToolTip(c.name());
        connect(a, &QAction::triggered, this, [this, c] { setInk(c); });
    }
    colorAction_ = tools->addAction(QStringLiteral("Color…"), this, &MainWindow::chooseColor);
    tools->addSeparator();
    tools->addAction(QStringLiteral("Image…"), this, &MainWindow::chooseImage);
    colorAction_->setToolTip(QStringLiteral("Current color; click to choose another"));

    // Icons from the Android app's set; the words stay as tooltips (and as the actions' names).
    const QHash<QString, QString> icons = {
        {QStringLiteral("Open…"), QStringLiteral("folder")},
        {QStringLiteral("Save"), QStringLiteral("save")},
        {QStringLiteral("Save As…"), QStringLiteral("download")},
        {QStringLiteral("Export to PDF…"), QStringLiteral("exportDoc")},
        {QStringLiteral("↶"), QStringLiteral("undo")},
        {QStringLiteral("↷"), QStringLiteral("redo")},
        {QStringLiteral("←"), QStringLiteral("prev")},
        {QStringLiteral("→"), QStringLiteral("next")},
        {QStringLiteral("−"), QStringLiteral("zoomOut")},
        {QStringLiteral("+"), QStringLiteral("zoomIn")},
        {QStringLiteral("Fit Width"), QStringLiteral("fit")},
        {QStringLiteral("+ Page"), QStringLiteral("plus")},
        {QStringLiteral("− Page"), QStringLiteral("minus")},
        {QStringLiteral("Background…"), QStringLiteral("sliders")},
        {QStringLiteral("Image…"), QStringLiteral("image")},
        {QStringLiteral("Pages"), QStringLiteral("sidebar")},
    };
    const QHash<QString, QString> tips = {
        {QStringLiteral("↶"), QStringLiteral("Undo")}, {QStringLiteral("↷"), QStringLiteral("Redo")},
        {QStringLiteral("←"), QStringLiteral("Previous Page")}, {QStringLiteral("→"), QStringLiteral("Next Page")},
        {QStringLiteral("−"), QStringLiteral("Zoom Out")}, {QStringLiteral("+"), QStringLiteral("Zoom In")},
        {QStringLiteral("+ Page"), QStringLiteral("Add Page")}, {QStringLiteral("− Page"), QStringLiteral("Delete Page")},
        {QStringLiteral("Pages"), QStringLiteral("Pages Panel")},
    };
    for (QToolBar* tb : {bar, tools}) {
        tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
        tb->setIconSize(QSize(22, 22));
        for (QAction* a : tb->actions()) {
            if (auto it = icons.constFind(a->text()); it != icons.constEnd()) {
                a->setIcon(xnIcon(*it));
                actionIcons_[a] = *it;
            }
            const QString tip = tips.value(a->text(), a->text());
            a->setToolTip(a->shortcut().isEmpty() ? tip : QStringLiteral("%1 (%2)").arg(tip, a->shortcut().toString(QKeySequence::NativeText)));
        }
    }

    selectionBar_ = new SelectionBar(canvas_);
    selectionBar_->setSwatches(kSwatches);
    connect(canvas_, &CanvasWidget::selectionMenu, this, [this](bool shown, QRectF) { selectionBar_->onMenu(shown); });
    connect(canvas_, &CanvasWidget::viewChanged, selectionBar_, &SelectionBar::refresh);
    connect(canvas_, &CanvasWidget::contentChanged, selectionBar_, &SelectionBar::refresh);
    connect(canvas_, &CanvasWidget::contextMenu, this, &MainWindow::showContextMenu);
    connect(canvas_, &CanvasWidget::toolChanged, this, [this](const QString& id) {
        if (installing_) return;
        if (penEntry(id.toUtf8())) setPenKind(id.toUtf8());
        for (QAction* a : tools_->actions())
            if (a->data().toByteArray() == id.toUtf8()) a->setChecked(true);
        syncWidth();
    });

    // Canvas commands without buttons.
    auto bind = [this](const QKeySequence& key, auto fn) {
        auto* a = new QAction(this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
    };
    bind(QKeySequence(Qt::Key_Escape), [this] { if (canvas_->editor()) xn_editor_escape(canvas_->editor()); });
    bind(QKeySequence(Qt::Key_Delete), [this] { if (canvas_->editor()) xn_editor_delete_selection(canvas_->editor()); });

    bind(QKeySequence::Copy, [this] { if (canvas_->editor()) xn_editor_copy(canvas_->editor()); clipboardIsNewer_ = false; });
    bind(QKeySequence::Cut, [this] { if (canvas_->editor()) xn_editor_cut(canvas_->editor()); clipboardIsNewer_ = false; });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] { clipboardIsNewer_ = true; });
    connect(canvas_, &CanvasWidget::filesDropped, this, [this](const QStringList& files, QPointF at) {
        for (const QString& f : files) insertImageFile(f, canvas_->toDevice(at), true);
    });
    bind(QKeySequence::Paste, [this] { pasteAt(canvas_->mapFromGlobal(QCursor::pos())); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_D), [this] { if (canvas_->editor()) xn_editor_duplicate(canvas_->editor()); });
    bind(QKeySequence::SelectAll, [this] { selectAll(); });
    bind(QKeySequence::Quit, [this] { close(); });
    setInk(kSwatches.first());
    syncWidth();
}

void MainWindow::setTheme(Theme theme) {
    theme_ = theme;
    if (themeButton_ && themeButton_->menu()) {
        for (QAction* a : themeButton_->menu()->actions())
            if (a->data().toInt() == int(theme)) a->setChecked(true);
    }
    QSettings settings(QStringLiteral("xnotes"), QStringLiteral("xnotes"));
    settings.setValue(QStringLiteral("theme"), int(theme_));
    applyTheme();
}

void MainWindow::applyTheme() {
    if (applyingTheme_) return;
    applyingTheme_ = true;
    dark_ = (theme_ == Theme::System) ? isSystemDark() : (theme_ == Theme::Dark);

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(
        theme_ == Theme::System ? Qt::ColorScheme::Unknown
        : dark_                 ? Qt::ColorScheme::Dark
                                : Qt::ColorScheme::Light);
#endif

    QApplication::setPalette(buildUiPalette(dark_));
    refreshIcons();
    canvas_->setDark(dark_);
    pages_->setPaper(paper());
    for (QWidget* w : findChildren<QWidget*>()) w->update();
    applyingTheme_ = false;
}

void MainWindow::refreshIcons() {
    for (auto it = actionIcons_.cbegin(); it != actionIcons_.cend(); ++it)
        if (it.key()) it.key()->setIcon(xnIcon(it.value()));
    if (createButton_) createButton_->setIcon(xnIcon(QStringLiteral("file")));
    if (themeButton_) themeButton_->setIcon(xnIcon(QStringLiteral("view")));
    if (penAction_) {
        if (const ToolEntry* entry = penEntry(penAction_->data().toByteArray()))
            penAction_->setIcon(xnIcon(QString::fromLatin1(entry->icon)));
    }
    if (colorAction_ && colorAction_->data().isValid())
        colorAction_->setIcon(swatchIcon(colorAction_->data().value<QColor>()));
}

QByteArray MainWindow::currentTool() const {
    QAction* a = tools_->checkedAction();
    return a ? a->data().toByteArray() : QByteArray();
}

void MainWindow::syncWidth() {
    const QByteArray tool = currentTool();
    for (QAction* a : std::as_const(widthActions_)) a->setVisible(tool != "text_box");
    for (QAction* a : std::as_const(textActions_)) a->setVisible(tool == "text_box");
    for (QAction* a : std::as_const(eraserActions_)) a->setVisible(tool == "eraser");
    for (QAction* a : std::as_const(pressureActions_)) a->setVisible(hasPressure(tool));
    width_->setEnabled(hasWidth(tool));
    widthLabel_->setText(tool == "eraser" ? QStringLiteral(" Size ") : QStringLiteral(" Width "));
    if (!hasWidth(tool)) return;
    double w = widths_.value(tool, 0);
    if (w <= 0) w = tool == "shape" ? 3.0 : canvas_->editor() ? xn_editor_tool_width(canvas_->editor(), tool.constData()) : 3.0;
    const auto [lo, hi] = widthRange(tool);
    syncingWidth_ = true;
    width_->setRange(int(lo * 2), int(hi * 2));
    width_->setValue(int(std::lround(std::clamp(w, lo, hi) * 2)));
    widthValue_->setText(QString::number(width_->value() / 2.0, 'f', 1));
    syncingWidth_ = false;
}

void MainWindow::applyShape() {
    auto* e = canvas_->editor();
    if (!e) return;
    const xn_shape_style style{shapeKind_.constData(), widths_.value("shape", 3.0), shapeFill_ ? 1 : 0, 0.25, shapeDashed_ ? 1 : 0};
    xn_editor_set_shape_style(e, &style);
}

/* The pen button draws with kind from now on; its icon and tip say which. */
void MainWindow::setPenKind(const QByteArray& kind) {
    const ToolEntry* entry = penEntry(kind);
    if (!entry || !penAction_) return;
    penAction_->setData(kind);
    penAction_->setIcon(xnIcon(QString::fromLatin1(entry->icon)));
    actionIcons_[penAction_] = QString::fromLatin1(entry->icon);
    penAction_->setText(QString::fromUtf8(entry->label));
    penAction_->setToolTip(QStringLiteral("%1 (%2)").arg(QString::fromUtf8(entry->label), penAction_->shortcut().toString(QKeySequence::NativeText)));
    if (QAction* k = penKinds_.value(kind)) k->setChecked(true);
}

xn_pen_style MainWindow::penStyle(const QByteArray& kind) const {
    if (auto it = penStyles_.constFind(kind); it != penStyles_.constEnd()) return *it;
    xn_pen_style s{1, 65, 2, 74, 30, 10, 8};  // the core's defaults, should there be no editor
    if (auto* e = canvas_->editor()) xn_editor_pen_style(e, kind.constData(), &s);
    return s;
}

void MainWindow::setPenStyle(const QByteArray& kind, xn_pen_style style) {
    if (hasPressure(kind)) style.pressure = pressure_->isChecked() ? 1 : 0;
    penStyles_.insert(kind, style);
    if (auto* e = canvas_->editor()) xn_editor_set_pen_style(e, kind.constData(), &style);
}

void MainWindow::setPressure(bool) {
    for (const ToolEntry& p : kPens)
        if (hasPressure(p.id)) setPenStyle(p.id, penStyle(p.id));
}

/* The current pen's own settings, under the pen button. */
void MainWindow::showPenStyle() {
    const QByteArray kind = penAction_->data().toByteArray();
    xn_pen_style style = penStyle(kind);
    if (hasPressure(kind)) style.pressure = pressure_->isChecked() ? 1 : 0;
    auto* popup = new PenStylePopup(kind, style, [this, kind](const xn_pen_style& s) { setPenStyle(kind, s); }, this);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    QWidget* anchor = nullptr;
    for (QToolBar* tb : findChildren<QToolBar*>())
        if (QWidget* w = tb->widgetForAction(penAction_)) anchor = w;
    popup->adjustSize();
    popup->move(anchor ? anchor->mapToGlobal(QPoint(0, anchor->height())) : QCursor::pos());
    popup->show();
}

/* Settings the toolbar holds, handed to a fresh editor. */
void MainWindow::applyToolSettings() {
    auto* e = canvas_->editor();
    if (!e) return;
    for (auto it = widths_.cbegin(); it != widths_.cend(); ++it)
        if (it.key() != "shape") xn_editor_set_tool_width(e, it.key().constData(), it.value());
    applyShape();
    for (auto it = penStyles_.cbegin(); it != penStyles_.cend(); ++it) xn_editor_set_pen_style(e, it.key().constData(), &it.value());
    xn_editor_set_erase_mode(e, eraseMode_->currentData().toInt());
    xn_editor_set_text_face(e, face_->currentData().toByteArray().constData());
    xn_editor_set_text_size(e, textSize_->value());
}

/*
 * Paste what was copied last: items copied in the editor, or an image another program put on the
 * system clipboard since.
 */
void MainWindow::pasteAt(QPointF widget) {
    auto* e = canvas_->editor();
    if (!e) return;
    if (!canvas_->rect().contains(widget.toPoint())) widget = QPointF(canvas_->width() / 2.0, canvas_->height() / 3.0);
    const QPointF at = canvas_->toDevice(widget);
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    const bool image = mime && mime->hasImage();
    if (image && (clipboardIsNewer_ || !xn_editor_can_paste(e))) {
        const QImage picture = qvariant_cast<QImage>(mime->imageData());
        const QString file = workFile(QStringLiteral("png"));
        if (!picture.isNull() && !file.isEmpty() && picture.save(file, "PNG")) insertImage(file, at, true);
        return;
    }
    if (!xn_editor_can_paste(e)) return;
    xn_editor_paste_at(e, at.x(), at.y());
    selectionBar_->refresh();
}

/* A fresh file name in the note's work folder, which lives as long as the note. */
QString MainWindow::workFile(const QString& suffix) {
    if (!workDir_ || !workDir_->isValid()) workDir_ = std::make_unique<QTemporaryDir>();
    if (!workDir_->isValid()) return {};
    return workDir_->filePath(QStringLiteral("inserted-%1.%2").arg(QUuid::createUuid().toString(QUuid::Id128), suffix));
}

void MainWindow::chooseImage() {
    if (!canvas_->editor()) return;
    QStringList patterns;
    for (const QByteArray& f : QImageReader::supportedImageFormats()) patterns << QStringLiteral("*.") + QString::fromLatin1(f);
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Insert Image"), QString(),
                                                      QStringLiteral("Images (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (!path.isEmpty()) insertImageFile(path, QPointF(), false);
}

/* Copy an image file into the work folder and put it on the page (at a device point, if given). */
bool MainWindow::insertImageFile(const QString& path, QPointF at, bool atPoint) {
    const QString copy = workFile(QFileInfo(path).suffix().isEmpty() ? QStringLiteral("img") : QFileInfo(path).suffix().toLower());
    if (copy.isEmpty() || !QFile::copy(path, copy)) {
        QMessageBox::warning(this, QStringLiteral("Image"), QStringLiteral("Failed to copy %1").arg(path));
        return false;
    }
    return insertImage(copy, at, atPoint);
}

bool MainWindow::insertImage(const QString& file, QPointF at, bool atPoint) {
    auto* e = canvas_->editor();
    if (!e) return false;
    if (!xn_editor_insert_image(e, QFile::encodeName(file).constData(), atPoint ? 1 : 0, at.x(), at.y())) {
        QFile::remove(file);
        QMessageBox::warning(this, QStringLiteral("Image"), QStringLiteral("Failed to read image"));
        return false;
    }
    return true;
}

void MainWindow::selectAll() {
    auto* e = canvas_->editor();
    if (!e) return;
    xn_editor_select_all(e);
    selectionBar_->refresh();
}

void MainWindow::showContextMenu(QPointF at, bool onLocked) {
    auto* e = canvas_->editor();
    if (!e) return;
    QMenu menu(this);
    QAction* paste = menu.addAction(xnIcon(QStringLiteral("paste")), QStringLiteral("Paste"), this, [this, at] { pasteAt(at); });
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    paste->setEnabled(xn_editor_can_paste(e) || (mime && mime->hasImage()));
    if (onLocked) menu.addAction(xnIcon(QStringLiteral("unlock")), QStringLiteral("Unlock"), this, [e] { xn_editor_unlock_pressed(e); });
    menu.addAction(xnIcon(QStringLiteral("select")), QStringLiteral("Select All"), this, &MainWindow::selectAll);
    menu.exec(canvas_->mapToGlobal(at.toPoint()));
}

void MainWindow::install(xn_note* note, std::unique_ptr<QTemporaryDir> workDir, const QString& path) {
    suggestedName_.clear();
    if (pageStyle_) pageStyle_->close();  // it edits the editor going away
    pages_->setDocument(nullptr, nullptr);
    canvas_->setNote(nullptr);
    if (note_) xn_note_close(note_);
    note_ = note;
    workDir_ = std::move(workDir);
    path_ = path;
    installing_ = true;
    canvas_->setNote(note_);
    installing_ = false;
    applyKind();
    canvas_->setDark(dark_);
    pages_->setPaper(paper());
    pages_->setDocument(note_, canvas_->editor());
    if (QAction* tool = tools_->checkedAction()) canvas_->setTool(tool->data().toByteArray().constData());
    if (colorAction_->data().isValid()) setInk(colorAction_->data().value<QColor>());
    applyToolSettings();
    syncWidth();
    refresh();
    refreshView();
}

void MainWindow::newNote() { install(xn_note_new(1), std::make_unique<QTemporaryDir>(), QString()); }

void MainWindow::newCanvas() { install(xn_canvas_new(), std::make_unique<QTemporaryDir>(), QString()); }

/* An infinite canvas has no pages, text boxes or PDF export: their controls step aside. */
void MainWindow::applyKind() {
    const bool canvas = note_ && xn_note_is_canvas(note_);
    for (QAction* a : std::as_const(pageActions_)) a->setVisible(!canvas);
    pagesAction_->setVisible(!canvas);
    pagesDock_->setVisible(!canvas && pagesAction_->isChecked());
    exportAction_->setEnabled(!canvas);
    fitAction_->setText(canvas ? QStringLiteral("Show All") : QStringLiteral("Fit Width"));
    fitAction_->setToolTip(fitAction_->text());
    for (QAction* a : tools_->actions()) {
        if (a->data().toByteArray() != "text_box") continue;
        a->setVisible(!canvas);
        if (canvas && a->isChecked()) {
            for (QAction* pen : tools_->actions())
                if (pen->data().toByteArray() == "pen") pen->trigger();
        }
    }
}

bool MainWindow::open(const QString& path) {
    auto workDir = std::make_unique<QTemporaryDir>();
    if (!workDir->isValid()) {
        QMessageBox::warning(this, QStringLiteral("Open"), QStringLiteral("Failed to create temporary folder"));
        return false;
    }
    if (path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
        // A note made from this PDF before is usually what is wanted, rather than a fresh one.
        const QStringList notes = notesForPdf(path);
        if (!notes.isEmpty()) {
            bool cancelled = false;
            const QString note = chooseExistingNote(path, notes, &cancelled);
            if (cancelled) return false;
            if (!note.isEmpty()) return open(note);
        }
        return importPdf(path, std::move(workDir));
    }
    char* error = nullptr;
    const QByteArray file = QFile::encodeName(QFileInfo(path).absoluteFilePath());
    const QByteArray dir = QFile::encodeName(workDir->path());
    xn_note* note = xn_note_open(file.constData(), dir.constData(), canvas_->host(), &error);
    if (!note) {
        QMessageBox::warning(this, QStringLiteral("Open"), QStringLiteral("Failed to open %1:\n%2").arg(path, takeError(error)));
        return false;
    }
    install(note, std::move(workDir), QFileInfo(path).absoluteFilePath());
    return true;
}

/* Which of the notes made from the PDF to open; empty for a new one. */
QString MainWindow::chooseExistingNote(const QString& pdf, const QStringList& notes, bool* cancelled) {
    const QString pdfName = QFileInfo(pdf).fileName();
    if (notes.size() == 1) {
        QMessageBox box(QMessageBox::Question, QStringLiteral("Open PDF"),
                        QStringLiteral("A note for \"%1\" already exists (\"%2\"). Open it?").arg(pdfName, QFileInfo(notes.first()).fileName()),
                        QMessageBox::NoButton, this);
        QPushButton* openNote = box.addButton(QStringLiteral("Open Note"), QMessageBox::AcceptRole);
        QPushButton* fresh = box.addButton(QStringLiteral("Create New"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(openNote);
        box.exec();
        if (box.clickedButton() == openNote) return notes.first();
        if (box.clickedButton() == fresh) return {};
        *cancelled = true;
        return {};
    }
    QStringList items;
    for (const QString& n : notes)
        items << QStringLiteral("%1 — modified %2").arg(QFileInfo(n).fileName(), QFileInfo(n).lastModified().toString(QStringLiteral("MM/dd/yyyy HH:mm")));
    const QString freshItem = QStringLiteral("Create new note");
    items << freshItem;
    bool ok = false;
    const QString picked = QInputDialog::getItem(this, QStringLiteral("Open PDF"),
                                                 QStringLiteral("Notes already exist for \"%1\":").arg(pdfName), items, 0, false, &ok);
    if (!ok) {
        *cancelled = true;
        return {};
    }
    const qsizetype index = items.indexOf(picked);
    return picked == freshItem || index < 0 ? QString() : notes.at(index);
}

/* A new note over a copy of the PDF (the note embeds it when saved); saving asks for a name. */
bool MainWindow::importPdf(const QString& path, std::unique_ptr<QTemporaryDir> workDir) {
    const QString copy = workDir->filePath(QStringLiteral("source.pdf"));
    if (!QFile::copy(path, copy)) {
        QMessageBox::warning(this, QStringLiteral("Open"), QStringLiteral("Failed to copy %1").arg(path));
        return false;
    }
    char* error = nullptr;
    xn_note* note = xn_note_import_pdf(QFile::encodeName(copy).constData(), canvas_->host(), &error);
    if (!note) {
        QMessageBox::warning(this, QStringLiteral("Open"), QStringLiteral("Failed to open PDF %1:\n%2").arg(path, takeError(error)));
        return false;
    }
    install(note, std::move(workDir), QString());
    suggestedName_ = QFileInfo(path).absolutePath() + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QStringLiteral(".xnote");
    refresh();
    return true;
}

void MainWindow::chooseOpen() {
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open"), QFileInfo(path_).absolutePath(),
                                                      QStringLiteral("Notes, Canvases, and PDFs (*.xnote *.xcanvas *.pdf);;Notes (*.xnote);;Canvases (*.xcanvas);;PDF (*.pdf)"));
    if (!path.isEmpty()) open(path);
}

/*
 * The note as a PDF, a page per page at its own size: ink, shapes and text stay vector; a PDF
 * background is rendered at 300 dpi; paper is the screen's paper, so light ink stays visible.
 */
void MainWindow::exportPdf() {
    if (!note_) return;
    QString base = !path_.isEmpty() ? path_ : suggestedName_;
    base = base.isEmpty() ? QStringLiteral("note.pdf") : QFileInfo(base).absolutePath() + QLatin1Char('/') + QFileInfo(base).completeBaseName() + QStringLiteral(".pdf");
    QString out = QFileDialog::getSaveFileName(this, QStringLiteral("Export to PDF"), base, QStringLiteral("PDF (*.pdf)"));
    if (out.isEmpty()) return;
    if (!out.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) out += QLatin1String(".pdf");
    const QString error = writePdf(note_, out, paper(), [this](int done, int total) {
        statusBar()->showMessage(QStringLiteral("Exporting: page %1 of %2").arg(done).arg(total));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    });
    if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Export to PDF"), error);
    else statusBar()->showMessage(QStringLiteral("Exported: %1").arg(out), 4000);
}

bool MainWindow::save(bool askPath) {
    if (!note_) return false;
    QString path = path_;
    if (askPath || path.isEmpty()) {
        // A canvas is saved as .xcanvas, a note as .xnote: the format follows the document.
        const bool canvas = xn_note_is_canvas(note_);
        const QString ext = canvas ? QStringLiteral(".xcanvas") : QStringLiteral(".xnote");
        const QString start = !path.isEmpty() ? path : !suggestedName_.isEmpty() ? suggestedName_ : (canvas ? QStringLiteral("canvas") : QStringLiteral("note")) + ext;
        path = QFileDialog::getSaveFileName(this, canvas ? QStringLiteral("Save Canvas") : QStringLiteral("Save Note"), start,
                                            canvas ? QStringLiteral("xnotes Canvases (*.xcanvas)") : QStringLiteral("xnotes Notes (*.xnote)"));
        if (path.isEmpty()) return false;
        if (!path.endsWith(ext)) path += ext;
    }
    char* error = nullptr;
    if (!xn_note_save(note_, QFile::encodeName(path).constData(), canvas_->host(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Save"), QStringLiteral("Failed to save %1:\n%2").arg(path, takeError(error)));
        return false;
    }
    path_ = path;
    statusBar()->showMessage(QStringLiteral("Saved: %1").arg(path), 3000);
    refresh();
    return true;
}

bool MainWindow::confirmDiscard() {
    if (!note_ || !xn_note_is_dirty(note_)) return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("Unsaved Changes"),
                                              QStringLiteral("Save changes to note?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) return save(false);
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (confirmDiscard()) e->accept(); else e->ignore();
}

void MainWindow::chooseColor() {
    const QColor c = QColorDialog::getColor(colorAction_->data().value<QColor>(), this, QStringLiteral("Ink Color"));
    if (c.isValid()) setInk(c);
}

void MainWindow::setInk(const QColor& c) {
    colorAction_->setData(c);
    colorAction_->setIcon(swatchIcon(c));
    if (auto* e = canvas_->editor())
        xn_editor_set_ink_color(e, (xn_rgba(c.red()) << 24) | (xn_rgba(c.green()) << 16) | (xn_rgba(c.blue()) << 8) | 0xFF);
}

void MainWindow::refresh() {
    auto* e = canvas_->editor();
    undo_->setEnabled(e && xn_editor_can_undo(e));
    redo_->setEnabled(e && xn_editor_can_redo(e));
    const bool canvas = note_ && xn_note_is_canvas(note_);
    const QString name = !path_.isEmpty() ? QFileInfo(path_).fileName() : canvas ? QStringLiteral("New Canvas") : QStringLiteral("New Note");
    const bool dirty = note_ && xn_note_is_dirty(note_);
    setWindowTitle((dirty ? QStringLiteral("• ") : QString()) + name + QStringLiteral(" — xnotes"));
    refreshView();
}

void MainWindow::syncScrollBars() {
    auto* e = canvas_->editor();
    double sc[6] = {0, 0, 0, 0, 0, 0};
    if (e) xn_editor_scroll_info(e, sc);
    syncingBars_ = true;
    vbar_->setRange(0, int(std::lround(sc[3])));
    vbar_->setPageStep(std::max(1, int(sc[5])));
    vbar_->setSingleStep(std::max(1, int(sc[5] / 20)));
    vbar_->setValue(int(std::lround(sc[1])));
    hbar_->setRange(0, int(std::lround(sc[2])));
    hbar_->setPageStep(std::max(1, int(sc[4])));
    hbar_->setSingleStep(std::max(1, int(sc[4] / 20)));
    hbar_->setValue(int(std::lround(sc[0])));
    hbar_->setVisible(false);
    vbar_->setVisible(false);
    syncingBars_ = false;
}

void MainWindow::refreshView() {
    syncScrollBars();
    auto* e = canvas_->editor();
    if (!e || !note_) return;
    if (!xn_note_is_canvas(note_))
        pageLabel_->setText(QStringLiteral("%1 / %2").arg(xn_editor_current_page(e) + 1).arg(xn_note_page_count(note_)));
    zoomLabel_->setText(QStringLiteral("%1%").arg(int(xn_editor_zoom(e) * 100)));
}
