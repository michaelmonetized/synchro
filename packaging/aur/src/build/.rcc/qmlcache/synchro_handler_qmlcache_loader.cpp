#include <QtQml/qqmlprivate.h>
#include <QtCore/qdir.h>
#include <QtCore/qurl.h>
#include <QtCore/qhash.h>
#include <QtCore/qstring.h>

namespace QmlCacheGeneratedCode {
namespace _qt_qml_Synchro_Handler_src_ui_qml_Synchro_Handler_HandlerSurface_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_Synchro_Handler_src_ui_qml_Synchro_Handler_PeekFindBody_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_Synchro_Handler_src_ui_qml_Synchro_Handler_DatabasePeek_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}
namespace _qt_qml_Synchro_Handler_src_ui_qml_Synchro_Handler_ArchivePeek_qml { 
    extern const unsigned char qmlData[];
    extern const QQmlPrivate::AOTCompiledFunction aotBuiltFunctions[];
    const QQmlPrivate::CachedQmlUnit unit = {
        reinterpret_cast<const QV4::CompiledData::Unit*>(&qmlData), &aotBuiltFunctions[0], nullptr
    };
}

}
namespace {
struct Registry {
    Registry();
    ~Registry();
    QHash<QString, const QQmlPrivate::CachedQmlUnit*> resourcePathToCachedUnit;
    static const QQmlPrivate::CachedQmlUnit *lookupCachedUnit(const QUrl &url);
};

Q_GLOBAL_STATIC(Registry, unitRegistry)


Registry::Registry() {
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/Synchro/Handler/src/ui/qml/Synchro/Handler/HandlerSurface.qml"), &QmlCacheGeneratedCode::_qt_qml_Synchro_Handler_src_ui_qml_Synchro_Handler_HandlerSurface_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/Synchro/Handler/src/ui/qml/Synchro/Handler/PeekFindBody.qml"), &QmlCacheGeneratedCode::_qt_qml_Synchro_Handler_src_ui_qml_Synchro_Handler_PeekFindBody_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/Synchro/Handler/src/ui/qml/Synchro/Handler/DatabasePeek.qml"), &QmlCacheGeneratedCode::_qt_qml_Synchro_Handler_src_ui_qml_Synchro_Handler_DatabasePeek_qml::unit);
    resourcePathToCachedUnit.insert(QStringLiteral("/qt/qml/Synchro/Handler/src/ui/qml/Synchro/Handler/ArchivePeek.qml"), &QmlCacheGeneratedCode::_qt_qml_Synchro_Handler_src_ui_qml_Synchro_Handler_ArchivePeek_qml::unit);
    QQmlPrivate::RegisterQmlUnitCacheHook registration;
    registration.structVersion = 0;
    registration.lookupCachedQmlUnit = &lookupCachedUnit;
    QQmlPrivate::qmlregister(QQmlPrivate::QmlUnitCacheHookRegistration, &registration);
}

Registry::~Registry() {
    QQmlPrivate::qmlunregister(QQmlPrivate::QmlUnitCacheHookRegistration, quintptr(&lookupCachedUnit));
}

const QQmlPrivate::CachedQmlUnit *Registry::lookupCachedUnit(const QUrl &url) {
    if (url.scheme() != QLatin1String("qrc"))
        return nullptr;
    QString resourcePath = QDir::cleanPath(url.path());
    if (resourcePath.isEmpty())
        return nullptr;
    if (!resourcePath.startsWith(QLatin1Char('/')))
        resourcePath.prepend(QLatin1Char('/'));
    return unitRegistry()->resourcePathToCachedUnit.value(resourcePath, nullptr);
}
}
int QT_MANGLE_NAMESPACE(qInitResources_qmlcache_synchro_handler)() {
    ::unitRegistry();
    return 1;
}
Q_CONSTRUCTOR_FUNCTION(QT_MANGLE_NAMESPACE(qInitResources_qmlcache_synchro_handler))
int QT_MANGLE_NAMESPACE(qCleanupResources_qmlcache_synchro_handler)() {
    return 1;
}
