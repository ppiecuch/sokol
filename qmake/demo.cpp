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
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QApplication>

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <alloca.h>
#include <math.h>

#include <qopengl.h>

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


// == Qt Window ==

class Window : public QWindow, protected QOpenGLFunctions
{
    Q_OBJECT

# define SOKOL_CLASS_IMPL
# define SOKOL_NO_FUNC_PROTO
# define SOKOL_GLCORE33
# include <sokol_gfx.h>

private:
    bool m_done, m_update_pending, m_resize_pending, m_auto_refresh;
    QOpenGLContext *m_context;

    sg_pipeline pip;
    /* resource bindings */
    sg_bindings binds;
    /* default pass action (clear to grey) */
    sg_pass_action pass_action;

public:
    QPoint cursorPos;
public:
    Window(QWindow *parent = 0) : QWindow(parent)
    , m_update_pending(false)
    , m_resize_pending(false)
    , m_auto_refresh(true)
    , m_context(0)
    , m_done(false) {
        setSurfaceType(QWindow::OpenGLSurface);
    }
    ~Window() {
        /* cleanup */
        sg_shutdown();
    }
    void setAutoRefresh(bool a) { m_auto_refresh = a; }

    void initialize() {
        qDebug() << "OpenGL infos with gl functions:";
        qDebug() << "-------------------------------";
        qDebug() << " Renderer:" << (const char*)glGetString(GL_RENDERER);
        qDebug() << " Vendor:" << (const char*)glGetString(GL_VENDOR);
        qDebug() << " OpenGL Version:" << (const char*)glGetString(GL_VERSION);
        qDebug() << " GLSL Version:" << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
        setTitle(QString("Qt %1 - %2 (%3)").arg(QT_VERSION_STR).arg((const char*)glGetString(GL_VERSION)).arg((const char*)glGetString(GL_RENDERER)));

        /* setup sokol_gfx */
        sg_desc desc = {0}; sg_setup(&desc);

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
        };
        sg_buffer vbuf = sg_make_buffer(&buffer_desc);

        /* a shader */
        sg_shader_desc shader_desc = {
            .vs.source =
                "#version 330\n"
                "in vec4 position;\n"
                "in vec4 color0;\n"
                "out vec4 color;\n"
                "void main() {\n"
                "  gl_Position = position;\n"
                "  color = color0;\n"
                "}\n",
            .fs.source =
                "#version 330\n"
                "in vec4 color;\n"
                "out vec4 frag_color;\n"
                "void main() {\n"
                "  frag_color = color;\n"
                "}\n"
        };
        sg_shader shd = sg_make_shader(&shader_desc);

        /* a pipeline state object (default render states are fine for triangle) */
        sg_pipeline_desc pipeline_desc = {
            .shader = shd,
            .layout = {
                .attrs = {
                    [0] = { .name="position", .format=SG_VERTEXFORMAT_FLOAT3 },
                    [1] = { .name="color0", .format=SG_VERTEXFORMAT_FLOAT4 }
                }
            }
        };
        pip = sg_make_pipeline(&pipeline_desc);

        /* resource bindings */
        binds = (sg_bindings){
            .vertex_buffers[0] = vbuf
        };

        /* default pass action (clear to grey) */
        pass_action = {0};
    }
    void update() { renderLater(); }
    void render() {
        /* draw loop */
        sg_begin_default_pass(&pass_action, width(), height(), devicePixelRatio());
        sg_apply_pipeline(pip);
        sg_apply_bindings(&binds);
        sg_draw(0, 3, 1);
        sg_end_pass();
        sg_commit();
    }
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
            case Qt::Key_Escape: close(); break;
            default: event->ignore();
            break;
        }
    }
    void quit() { m_done = true; }
    bool done() const { return m_done; }
    protected:
    void closeEvent(QCloseEvent *event) { quit(); }
    bool event(QEvent *event) {
        switch (event->type()) {
            case QEvent::UpdateRequest:
                m_update_pending = false;
                renderNow();
                return true;
            default:
                return QWindow::event(event);
        }
    }
    void exposeEvent(QExposeEvent *event) {
        Q_UNUSED(event);
        if (isExposed()) renderNow();
    }
    void resizeEvent(QResizeEvent *event)
    {
        renderLater();
    }
    public slots:
    void renderLater() {
        if (!m_update_pending) {
            m_update_pending = true;
            QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest));
        }
    }
    void renderNow() {
        if (!isExposed()) return;
        bool needsInitialize = false;
        if (!m_context) {
            m_context = new QOpenGLContext(this);
            m_context->setFormat(requestedFormat());
            m_context->create();
            needsInitialize = true;
        }
        m_context->makeCurrent(this);
        if (needsInitialize) {
            initializeOpenGLFunctions();
            initialize();
        }
        render();
        m_context->swapBuffers(this);
        if (m_auto_refresh) renderLater();
    }
private:
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

    QApplication app(argc, argv);
    Window w;
    w.resize(800,600);
    w.show();
    app.exec();
}

#include "demo.moc"
