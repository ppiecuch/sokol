#include <QtMath>
#include <QDebug>
#include <QVector3D>
#include <QMatrix4x4>
#include <QDirIterator>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QWaitCondition>
#include <QPainter>
#include <QWindow>
#include <QOpenGLWindow>
#include <QOpenGLContext>

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <alloca.h>
#include <math.h>

#define HANDMADE_MATH_IMPLEMENTATION
#define HANDMADE_MATH_NO_SSE
#include "HandmadeMath.h"
#define SOKOL_IMPL
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <ui/dbgui.h>

class SleepSimulator {
    QMutex localMutex;
    QWaitCondition sleepSimulator;
public:
    SleepSimulator() { localMutex.lock(); }
    void sleep(unsigned long sleepMS) { sleepSimulator.wait(&localMutex, sleepMS); }
    void CancelSleep() { sleepSimulator.wakeAll(); }
};

double qtGetTime() {
    static QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();
    return timer.elapsed() / 1000.;
}

void qtDelay(long ms) {
    SleepSimulator s;
    s.sleep(ms);
}

// ----- Basic shaders -----

#if defined(SOKOL_GLCORE33)
static constexpr const char* vs_src =
    "#version 330\n"
    "in vec4 position;\n"
    "in vec4 color0;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  gl_Position = position;\n"
    "  color = color0;\n"
    "}\n";
static constexpr const char* fs_src =
    "#version 330\n"
    "in vec4 color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "  frag_color = color;\n"
    "}\n";
#elif defined(SOKOL_GLES3) || defined(SOKOL_GLES2)
static constexpr const char* vs_src =
    "attribute vec4 position;\n"
    "attribute vec4 color0;\n"
    "varying vec4 color;\n"
    "void main() {\n"
    "  gl_Position = position;\n"
    "  color = color0;\n"
    "}\n";
static constexpr const char* fs_src =
    "precision mediump float;\n"
    "varying vec4 color;\n"
    "void main() {\n"
    "  gl_FragColor = color;\n"
    "}\n";
#elif defined(SOKOL_METAL)
static constexpr const char* vs_src =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct vs_in {\n"
    "  float4 position [[attribute(0)]];\n"
    "  float4 color [[attribute(1)]];\n"
    "};\n"
    "struct vs_out {\n"
    "  float4 position [[position]];\n"
    "  float4 color;\n"
    "};\n"
    "vertex vs_out _main(vs_in inp [[stage_in]]) {\n"
    "  vs_out outp;\n"
    "  outp.position = inp.position;\n"
    "  outp.color = inp.color;\n"
    "  return outp;\n"
    "}\n";
static constexpr const char* fs_src =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "fragment float4 _main(float4 color [[stage_in]]) {\n"
    "  return color;\n"
    "};\n";
#elif defined(SOKOL_D3D11)
static constexpr const char* vs_src =
    "struct vs_in {\n"
    "  float4 pos: POS;\n"
    "  float4 color: COLOR;\n"
    "};\n"
    "struct vs_out {\n"
    "  float4 color: COLOR0;\n"
    "  float4 pos: SV_Position;\n"
    "};\n"
    "vs_out main(vs_in inp) {\n"
    "  vs_out outp;\n"
    "  outp.pos = inp.pos;\n"
    "  outp.color = inp.color;\n"
    "  return outp;\n"
    "}\n";
static constexpr const char* fs_src =
    "float4 main(float4 color: COLOR0): SV_Target0 {\n"
    "  return color;\n"
    "}\n";
#endif


// ----- Qt render window -----

class GLWindow : public QOpenGLWindow
{
    Q_OBJECT

  private:
    bool m_done, m_update_pending, m_resize_pending, m_auto_refresh;

    sg_pipeline pip;
    sg_bindings bind;
    sg_pass_action pass_action;

  public:
    QPoint cursorPos;
  public:
    GLWindow(QOpenGLWindow::UpdateBehavior updateBehavior = NoPartialUpdate,
             QWindow *parent = 0) : QOpenGLWindow(updateBehavior, parent)
    , m_update_pending(false)
    , m_resize_pending(false)
    , m_auto_refresh(true)
    , m_done(false) {
    }
    ~GLWindow() {
        makeCurrent();
        sg_shutdown();
    }

    void setAutoRefresh(bool a) { m_auto_refresh = a; }
    bool isDone() const { return m_done; }
    void quit() { m_done = true; }

  protected:
    virtual void initializeGL() Q_DECL_OVERRIDE {
        qDebug() << "OpenGL informations:";
        qDebug() << "--------------------";
        qDebug() << " Renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        qDebug() << " Vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        qDebug() << " OpenGL Version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
        qDebug() << " GLSL Version:" << reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
        setTitle(QString("Qt %1 - %2 (%3)")
            .arg(QT_VERSION_STR)
            .arg((const char*)glGetString(GL_VERSION))
            .arg((const char*)glGetString(GL_RENDERER)));

        QString glType, glVersion, glProfile;

        // Get version information
        glType = (context()->isOpenGLES()) ? "OpenGL ES" : "OpenGL";
        glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        // Get profile information
        #define CASE(c) case QSurfaceFormat::c: glProfile = #c; break
        switch (format().profile())
        {
            CASE(NoProfile);
            CASE(CoreProfile);
            CASE(CompatibilityProfile);
        }
        #undef CASE

        qDebug() << "Context summary:";
        qDebug() << "----------------";
        qDebug()
            << qPrintable(glType)
            << qPrintable(glVersion)
            << "(" << qPrintable(glProfile) << ")";


        /* setup sokol_gfx */
        sg_desc desc{0}; sg_setup(&desc);
        __dbgui_setup(1, devicePixelRatio());

        /* a vertex buffer */
        const float vertices[] = {
            // positions           // colors
            0.0f,  0.5f, 0.5f,     1.0f, 0.0f, 0.0f, 1.0f,
            0.5f, -0.5f, 0.5f,     0.0f, 1.0f, 0.0f, 1.0f,
            -0.5f, -0.5f, 0.5f,    0.0f, 0.0f, 1.0f, 1.0f
        };
        sg_buffer_desc buffer_desc = {
            .size = sizeof(vertices),
            .content = vertices,
            .label = "triangle-vertices"
        };

        bind = (sg_bindings){
            .vertex_buffers[0] = sg_make_buffer(&buffer_desc)
        };

        /* a shader */
        sg_shader_desc shader_desc = {
            .attrs = {
                [0] = { .name="position", .sem_name="POS" },
                [1] = { .name="color0", .sem_name="COLOR" }
            },
            .vs.source = vs_src,
            .fs.source = fs_src,
            .label = "triangle-shader"
        };
        sg_shader shd = sg_make_shader(&shader_desc);

        /* a pipeline state object (default render states are fine for triangle) */
        sg_pipeline_desc pipeline_desc = {
            /* if the vertex layout doesn't have gaps, don't need to provide strides and offsets */
            .shader = shd,
            .layout = {
                .attrs = {
                    [0].format=SG_VERTEXFORMAT_FLOAT3,
                    [1].format=SG_VERTEXFORMAT_FLOAT4
                }
            },
            .label = "triangle-pipeline"
        };
        pip = sg_make_pipeline(&pipeline_desc);

        /* default pass action (clear to grey) */
        pass_action = { 0 };

        /* validate sapp state */
        _sapp.desc = {
            .event_cb = __dbgui_event
        };
        _sapp.init_called = true;
        _sapp.valid = true;
    }
    virtual void resizeGL(int width, int height) Q_DECL_OVERRIDE {
        _sapp.window_width = width;
        _sapp.window_height = height;
        _sapp.dpi_scale = devicePixelRatio();
        _sapp.framebuffer_width = _sapp.window_width * _sapp.dpi_scale;
        _sapp.framebuffer_height = _sapp.window_height * _sapp.dpi_scale;
    }
    virtual void paintGL() Q_DECL_OVERRIDE {
        if (!isExposed()) return;
        if (isDone()) return;
        updateEvents();
        render();
        if (m_auto_refresh) update();
    }

  protected:
    void closeEvent(QCloseEvent *event) { quit(); }
    void mousePressEvent(QMouseEvent *event) {
        cursorPos = QPoint(event->x(), event->y());
        Qt::KeyboardModifiers modifiers = event->modifiers();
        if (event->buttons() & Qt::LeftButton) { }
    }
    void mouseReleaseEvent(QMouseEvent *event) {
        cursorPos = QPoint(event->x(), event->y());
        Qt::KeyboardModifiers modifiers = event->modifiers();
        if (event->button() == Qt::LeftButton) { }
    }
    void mouseMoveEvent(QMouseEvent *event) {
        cursorPos = QPoint(event->x(), event->y());
    }
    void keyPressEvent(QKeyEvent* event) {
        switch(event->key()) {
            case Qt::Key_Escape:
            case Qt::Key_Q: close(); break;
            default: event->ignore();
            break;
        }
    }

  private:
    void updateEvents() {
        /*while( const EventDelivery *ev = Input::dequeueRecordedEvent()) {

        }*/
    }
    void render() {
        /* draw loop */
        sg_begin_default_pass(&pass_action, sapp_width(), sapp_height());
        sg_apply_pipeline(pip);
        sg_apply_bindings(&bind);
        sg_draw(0, 3, 1);
        __dbgui_draw();
        sg_end_pass();
        sg_commit();
    }
};


int main(int argc, char *argv[])
{
    QSurfaceFormat surface_format = QSurfaceFormat::defaultFormat();
    surface_format.setAlphaBufferSize( 8 );
    surface_format.setDepthBufferSize( 24 );
    // surface_format.setRedBufferSize( 8 );
    // surface_format.setBlueBufferSize( 8 );
    // surface_format.setGreenBufferSize( 8 );
    // surface_format.setOption( QSurfaceFormat::DebugContext );
    // surface_format.setProfile( QSurfaceFormat::NoProfile );
    // surface_format.setRenderableType( QSurfaceFormat::OpenGLES );
    // surface_format.setSamples( 4 );
    // surface_format.setStencilBufferSize( 8 );
    // surface_format.setSwapBehavior( QSurfaceFormat::DefaultSwapBehavior );
    // surface_format.setSwapInterval( 1 );
    surface_format.setVersion( 3, 3 );
    surface_format.setProfile( QSurfaceFormat::CoreProfile );
    QSurfaceFormat::setDefaultFormat( surface_format );

    SokolQtApplication app(argc, argv);
    GLWindow w;
    w.resize(800,600);
    w.show();
    app.exec();
}

#include "demo.moc"
