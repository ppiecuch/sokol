TEMPLATE = app
QT += widgets
CONFIG += console debug
CONFIG -= app_bundle

INCLUDEPATH += .. ../util ../samples/sapp
DEFINES += SOKOL_GLCORE33 SOKOL_NO_ENTRY SOKOL_TRACE_HOOKS USE_DBG_UI

HEADERS += ../sokol_app.h
SOURCES += demo.cpp ../samples/sapp/ui/dbgui.cc

IMGUI_ROOT=$(HOME)/Private/Projekty/0.shared/common-src-runtime/Gui/imgui
IMGUI_PRI=$$IMGUI_ROOT/examples/contrib_example_qt/imgui.pri

exists($$IMGUI_PRI): include($$IMGUI_PRI)
else:error("Cannot find imgui.pri")
