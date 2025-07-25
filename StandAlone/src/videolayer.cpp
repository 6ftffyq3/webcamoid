/* Webcamoid, webcam capture application.
 * Copyright (C) 2016  Gonzalo Exequiel Pedone
 *
 * Webcamoid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Webcamoid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Webcamoid. If not, see <http://www.gnu.org/licenses/>.
 *
 * Web-Site: http://webcamoid.github.io/
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>

#ifdef Q_OS_ANDROID
#include <QJniObject>

#define PERMISSION_GRANTED  0
#define PERMISSION_DENIED  -1
#endif

#include <ak.h>
#include <akaudiocaps.h>
#include <akcaps.h>
#include <akpacket.h>
#include <akvideocaps.h>
#include <akplugininfo.h>
#include <akpluginmanager.h>
#include <iak/akelement.h>

#ifdef Q_OS_WIN32
#include <windows.h>
#endif

#include "videolayer.h"
#include "updates.h"

#define DUMMY_OUTPUT_DEVICE ":dummyout:"

#ifdef Q_OS_WIN32
    #define DEFAULT_VCAM_DRIVER "VideoSink/VirtualCamera/Impl/AkVirtualCameraDShow"
#elif defined(Q_OS_MACOS)
    #define DEFAULT_VCAM_DRIVER "VideoSink/VirtualCamera/Impl/AkVirtualCameraCMIO"
#elif defined(Q_OS_LINUX)
    #define DEFAULT_VCAM_DRIVER "VideoSink/VirtualCamera/Impl/AkVCam"
#else
    #define DEFAULT_VCAM_DRIVER ""
#endif

using ObjectPtr = QSharedPointer<QObject>;

class VideoLayerPrivate
{
    public:
        VideoLayer *self;
        QQmlApplicationEngine *m_engine {nullptr};
        QString m_videoInput;
        QStringList m_videoOutput;
        QStringList m_inputs;
        QMap<QString, QString> m_images;
        QMap<QString, QString> m_streams;
        QStringList m_supportedFileFormats;
        QStringList m_supportedImageFormats;
        QMap<QString, QString> m_formatsDescription;
        AkAudioCaps m_inputAudioCaps;
        AkVideoCaps m_inputVideoCaps;
        AkElementPtr m_cameraCapture {akPluginManager->create<AkElement>("VideoSource/CameraCapture")};
        AkElementPtr m_desktopCapture {akPluginManager->create<AkElement>("VideoSource/DesktopCapture")};
        AkElementPtr m_imageCapture {akPluginManager->create<AkElement>("VideoSource/ImageSrc")};
        AkElementPtr m_uriCapture {akPluginManager->create<AkElement>("MultimediaSource/MultiSrc")};
        AkElementPtr m_cameraOutput {akPluginManager->create<AkElement>("VideoSink/VirtualCamera")};
        QString m_vcamDriver;
        QThreadPool m_threadPool;
        AkElement::ElementState m_state {AkElement::ElementStateNull};
        QString m_latestVersion;
        bool m_playOnStart {true};
        bool m_outputsAsInputs {false};
        bool m_currentVCamInstalled {false};
        bool m_isPassThroughVCam {false};

        explicit VideoLayerPrivate(VideoLayer *self);
        static bool canAccessStorage();
        void connectSignals();
        AkElementPtr sourceElement(const QString &stream) const;
        QString sourceId(const QString &stream) const;
        QStringList cameras() const;
        QStringList desktops() const;
        QString cameraDescription(const QString &camera) const;
        QString desktopDescription(const QString &desktop) const;
        bool embedControls(const QString &where,
                           const AkElementPtr &element,
                           const QString &pluginId,
                           const QString &name) const;
        void setInputAudioCaps(const AkAudioCaps &audioCaps);
        void setInputVideoCaps(const AkVideoCaps &videoCaps);
        void loadProperties();
        void loadVideoOutputControls();
        static QString sanitizeKey(const QString &key);
        void saveVideoInput(const QString &videoInput);
        void saveVideoOutput(const QString &videoOutput);
        void saveVideoOutputControls();
        void saveStreams(const QMap<QString, QString> &streams);
        void savePlayOnStart(bool playOnStart);
        void saveOutputsAsInputs(bool outputsAsInputs);
        inline QString vcamDownloadUrl() const;
};

VideoLayer::VideoLayer(QQmlApplicationEngine *engine, QObject *parent):
    QObject(parent)
{
    this->d = new VideoLayerPrivate(this);
    this->d->canAccessStorage();
    this->setQmlEngine(engine);
    this->d->connectSignals();
    this->d->loadProperties();
    this->d->m_latestVersion = this->currentVCamVersion();
    this->d->m_vcamDriver =
            akPluginManager->defaultPlugin("VideoSink/VirtualCamera/Impl/*").id();
    QObject::connect(akPluginManager,
                     &AkPluginManager::linksChanged,
                     this,
                     [this] (const AkPluginLinks &links) {
        if (links.contains("VideoSink/VirtualCamera/Impl/*")
            && links["VideoSink/VirtualCamera/Impl/*"] != this->d->m_vcamDriver) {
            this->d->m_vcamDriver = links["VideoSink/VirtualCamera/Impl/*"];
            emit this->vcamDriverChanged(this->d->m_vcamDriver);

            if (this->d->m_cameraOutput) {
                auto version =
                        this->d->m_cameraOutput->property("driverVersion").toString();
                emit this->currentVCamVersionChanged(version);
                auto installed =
                    this->d->m_cameraOutput->property("driverInstalled").toBool();

                if (this->d->m_currentVCamInstalled != installed) {
                    this->d->m_currentVCamInstalled = installed;
                    emit this->currentVCamInstalledChanged(installed);
                }

                bool isPassThrough =
                        this->d->m_cameraOutput->property("isPassThrough").toBool();

                if (isPassThrough != this->d->m_isPassThroughVCam) {
                    this->d->m_isPassThroughVCam = isPassThrough;
                    emit this->isPassThroughVCamChanged(isPassThrough);
                }
            } else {
                if (this->d->m_isPassThroughVCam) {
                    this->d->m_isPassThroughVCam = false;
                    emit this->isPassThroughVCamChanged(false);
                }
            }
        }
    });

    if (this->d->m_cameraOutput) {
        this->d->m_currentVCamInstalled =
            this->d->m_cameraOutput->property("driverInstalled").toBool();

        if (!this->d->m_currentVCamInstalled) {
            QString pluginId;
            auto plugins =
                    akPluginManager->listPlugins("VideoSink/VirtualCamera/Impl/*",
                                                 {"VirtualCameraImpl"},
                                                 AkPluginManager::FilterEnabled);
            for (auto &plugin: plugins) {
                auto pluginInstance = akPluginManager->create<QObject>(plugin);

                if (pluginInstance && pluginInstance->property("isInstalled").toBool()) {
                    if (pluginId.isEmpty())
                        pluginId = plugin;

                    if (plugin == DEFAULT_VCAM_DRIVER) {
                        pluginId = plugin;

                        break;
                    }
                }
            }

            if (pluginId.isEmpty())
                pluginId = DEFAULT_VCAM_DRIVER;

            akPluginManager->link("VideoSink/VirtualCamera/Impl/*", pluginId);
        }

        this->d->m_isPassThroughVCam =
                this->d->m_cameraOutput->property("isPassThrough").toBool();
    }
}

VideoLayer::~VideoLayer()
{
    this->setState(AkElement::ElementStateNull);
    this->d->saveVideoOutputControls();
    delete this->d;
}

QStringList VideoLayer::videoSourceFileFilters() const
{
    QStringList filters;

    /* Android's file selection dialog seems to be ignoring the file filters,
     * so allow selecting any type of file.
     */
#ifndef Q_OS_ANDROID
    //  Alternative extension names
    static const QMap<QString, QString> videoLayerFormatsMapping {
        {"jp2" , "jpg" },
        {"jpeg", "jpg" },
        {"svgz", "svg" },
        {"tif" , "tiff"},
        {"m4v" , "mp4" },
        {"mpeg", "mpg" },
    };

    QString extensions =
            "*." + this->d->m_supportedFileFormats.join(" *.");

    filters << tr("All Image and Video Files")
               + QString(" (%1)").arg(extensions);

    QStringList formats;

    for (auto &format: this->d->m_supportedFileFormats) {
        QString fmt;

        if (videoLayerFormatsMapping.contains(format))
            fmt = videoLayerFormatsMapping[format];
        else
            fmt = format;

        if (!formats.contains(fmt))
            formats << fmt;
    }

    QStringList fileFilters;

    for (auto &format: formats) {
        QString filter;
        QStringList extensions = QStringList {format}
                                 + videoLayerFormatsMapping.keys(format);
        QString extensionsFilter = "*." + extensions.join(" *.");

        if (this->d->m_formatsDescription.contains(format))
            filter = format.toUpper() + " - " + this->d->m_formatsDescription[format];
        else
            filter = format.toUpper();

        fileFilters << filter + QString(" (%1)").arg(extensionsFilter);
    }

    fileFilters.sort();
    filters << fileFilters;
#endif

    filters << tr("All Files") + " (*)";

    return filters;
}

QString VideoLayer::videoInput() const
{
    return this->d->m_videoInput;
}

QStringList VideoLayer::videoOutput() const
{
    return this->d->m_videoOutput;
}

QStringList VideoLayer::inputs() const
{
    return this->d->m_inputs;
}

QStringList VideoLayer::outputs() const
{
    QStringList outputs;

    if (this->d->m_cameraOutput) {
        auto outs = this->d->m_cameraOutput->property("medias").toStringList();

        if (!outs.isEmpty())
            outputs << DUMMY_OUTPUT_DEVICE << outs;
    }

    return outputs;
}

AkAudioCaps VideoLayer::inputAudioCaps() const
{
    return this->d->m_inputAudioCaps;
}

AkVideoCaps VideoLayer::inputVideoCaps() const
{
    return this->d->m_inputVideoCaps;
}

QStringList VideoLayer::supportedFileFormats() const
{
    return this->d->m_supportedFileFormats;
}

AkVideoCaps::PixelFormatList VideoLayer::supportedOutputPixelFormats() const
{
    if (!this->d->m_cameraOutput)
        return {};

    AkVideoCaps::PixelFormatList pixelFormats;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "supportedOutputPixelFormats",
                              Q_RETURN_ARG(AkVideoCaps::PixelFormatList, pixelFormats));

    return pixelFormats;
}

AkVideoCaps::PixelFormat VideoLayer::defaultOutputPixelFormat() const
{
    if (!this->d->m_cameraOutput)
        return AkVideoCaps::Format_none;

    AkVideoCaps::PixelFormat pixelFormat;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "defaultOutputPixelFormat",
                              Q_RETURN_ARG(AkVideoCaps::PixelFormat, pixelFormat));

    return pixelFormat;
}

AkVideoCapsList VideoLayer::supportedOutputVideoCaps(const QString &device) const
{
    if (!this->d->m_cameraOutput)
        return {};

    AkVideoCapsList caps;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "outputCaps",
                              Q_RETURN_ARG(AkVideoCapsList, caps),
                              Q_ARG(QString, device));

    return caps;
}

AkElement::ElementState VideoLayer::state() const
{
    return this->d->m_state;
}

bool VideoLayer::isTorchSupported() const
{
    if (!this->d->m_cameraCapture)
        return false;

    return this->d->m_cameraCapture->property("isTorchSupported").toBool();
}

VideoLayer::TorchMode VideoLayer::torchMode() const
{
    if (!this->d->m_cameraCapture)
        return Torch_Off;

    return this->d->m_cameraCapture->property("torchMode").value<TorchMode>();
}

VideoLayer::PermissionStatus VideoLayer::cameraPermissionStatus() const
{
    if (!this->d->m_cameraCapture)
        return PermissionStatus_Granted;

    return this->d->m_cameraCapture->property("permissionStatus").value<PermissionStatus>();
}

bool VideoLayer::playOnStart() const
{
    return this->d->m_playOnStart;
}

bool VideoLayer::outputsAsInputs() const
{
    return this->d->m_outputsAsInputs;
}

VideoLayer::InputType VideoLayer::deviceType(const QString &device) const
{
    if (this->d->cameras().contains(device))
        return InputCamera;

    if (this->d->desktops().contains(device))
        return InputDesktop;

    if (this->d->m_images.contains(device))
        return InputImage;

    if (this->d->m_streams.contains(device))
        return InputStream;

    return InputUnknown;
}

QStringList VideoLayer::devicesByType(InputType type) const
{
    switch (type) {
    case InputCamera:
        return this->d->cameras();

    case InputDesktop:
        return this->d->desktops();

    case InputImage:
        return this->d->m_images.keys();

    case InputStream:
        return this->d->m_streams.keys();

    default:
        break;
    }

    return {};
}

QString VideoLayer::description(const QString &device) const
{
    if (device == DUMMY_OUTPUT_DEVICE)
        //: Disable video output, don't send the video to the output device.
        return tr("No Output");

    if (this->d->m_cameraOutput) {
        auto outputs = this->d->m_cameraOutput->property("medias").toStringList();

        if (outputs.contains(device)) {
            QString description;
            QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                                      "description",
                                      Q_RETURN_ARG(QString, description),
                                      Q_ARG(QString, device));

            return description;
        }
    }

    if (this->d->cameras().contains(device))
        return this->d->cameraDescription(device);

    if (this->d->desktops().contains(device))
        return this->d->desktopDescription(device);

    if (this->d->m_images.contains(device))
        return this->d->m_images.value(device);

    if (this->d->m_streams.contains(device))
        return this->d->m_streams.value(device);

    return {};
}

QString VideoLayer::createOutput(VideoLayer::OutputType type,
                                 const QString &description,
                                 const AkVideoCapsList &formats)
{
    if (!this->d->m_cameraOutput || type != OutputVirtualCamera)
        return {};

    QString deviceId;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "createWebcam",
                              Q_RETURN_ARG(QString, deviceId),
                              Q_ARG(QString, description),
                              Q_ARG(AkVideoCapsList, formats));

    return deviceId;
}

QString VideoLayer::createOutput(VideoLayer::OutputType type,
                                 const QString &description,
                                 const QVariantList &formats)
{
    AkVideoCapsList fmts;

    for (auto &format: formats)
        fmts << format.value<AkVideoCaps>();

    return this->createOutput(type, description, fmts);
}

bool VideoLayer::editOutput(const QString &output,
                            const QString &description,
                            const AkVideoCapsList &formats)
{
    if (!this->d->m_cameraOutput)
        return {};

    bool result;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "editWebcam",
                              Q_RETURN_ARG(bool, result),
                              Q_ARG(QString, output),
                              Q_ARG(QString, description),
                              Q_ARG(AkVideoCapsList, formats));

    return result;
}

bool VideoLayer::removeOutput(const QString &output)
{
    if (!this->d->m_cameraOutput)
        return {};

    bool result;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "removeWebcam",
                              Q_RETURN_ARG(bool, result),
                              Q_ARG(QString, output));

    return result;
}

bool VideoLayer::removeAllOutputs()
{
    if (!this->d->m_cameraOutput)
        return {};

    bool result;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "removeAllWebcams",
                              Q_RETURN_ARG(bool, result));

    return result;
}

QString VideoLayer::inputError() const
{
    auto source = this->d->sourceElement(this->d->m_videoInput);

    if (!source)
        return {};

    QString error;
    QMetaObject::invokeMethod(source.data(),
                              "error",
                              Q_RETURN_ARG(QString, error));

    return error;
}

QString VideoLayer::outputError() const
{
    if (!this->d->m_cameraOutput)
        return {};

    QString error;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "error",
                              Q_RETURN_ARG(QString, error));

    return error;
}

bool VideoLayer::embedInputControls(const QString &where,
                                    const QString &device,
                                    const QString &name) const
{
    auto element = this->d->sourceElement(device);
    auto id = this->d->sourceId(device);

    return this->d->embedControls(where, element, id, name);
}

bool VideoLayer::embedOutputControls(const QString &where,
                                     const QString &device,
                                     const QString &name) const
{
    AkElementPtr element;

    if (this->d->m_cameraOutput) {
        auto outs = this->d->m_cameraOutput->property("medias").toStringList();

        if (outs.contains(device))
            element = this->d->m_cameraOutput;
   }

    return this->d->embedControls(where,
                                  element,
                                  "VideoSink/VirtualCamera",
                                  name);
}

void VideoLayer::removeInterface(const QString &where) const
{
    if (!this->d->m_engine)
        return;

    for (auto &obj: this->d->m_engine->rootObjects()) {
        auto item = obj->findChild<QQuickItem *>(where);

        if (!item)
            continue;

        auto childItems = item->childItems();

        for (auto &child: childItems) {
            child->setParentItem(nullptr);
            child->setParent(nullptr);
            delete child;
        }
    }
}

QList<quint64> VideoLayer::clientsPids() const
{
    if (!this->d->m_cameraOutput)
        return {};

    auto pids = this->d->m_cameraOutput->property("clientsPids");

    return pids.value<QList<quint64>>();
}

QString VideoLayer::clientExe(quint64 pid) const
{
    if (!this->d->m_cameraOutput)
        return {};

    QString exe;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "clientExe",
                              Q_RETURN_ARG(QString, exe),
                              Q_ARG(quint64, pid));

    return exe;
}

bool VideoLayer::driverInstalled() const
{
    if (!this->d->m_cameraOutput)
        return false;

    return this->d->m_cameraOutput->property("driverInstalled").toBool();
}

QString VideoLayer::picture() const
{
    if (!this->d->m_cameraOutput)
        return {};

    return this->d->m_cameraOutput->property("picture").toString();
}

QString VideoLayer::rootMethod() const
{
    if (!this->d->m_cameraOutput)
        return {};

    return this->d->m_cameraOutput->property("rootMethod").toString();
}

QStringList VideoLayer::availableRootMethods() const
{
    if (!this->d->m_cameraOutput)
        return {};

    return this->d->m_cameraOutput->property("availableRootMethods").toStringList();
}

bool VideoLayer::isVCamSupported() const
{
#if defined(Q_OS_WIN32) \
    || defined(Q_OS_MACOS) \
    || defined(FAKE_APPLE) \
    || (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) \
    || (defined(Q_OS_BSD4) && !defined(Q_OS_DARWIN)))
    return true;
#else
    return false;
#endif
}

VideoLayer::VCamStatus VideoLayer::vcamInstallStatus() const
{
    bool akvcamInstalled = false;
    bool otherInstalled = false;
    auto plugins = akPluginManager->listPlugins("VideoSink/VirtualCamera/Impl/*",
                                                {"VirtualCameraImpl"},
                                                AkPluginManager::FilterEnabled);

    for (auto &plugin: plugins) {
        auto pluginInstance = akPluginManager->create<QObject>(plugin);

        if (pluginInstance && pluginInstance->property("isInstalled").toBool()) {
            if (plugin == DEFAULT_VCAM_DRIVER) {
                akvcamInstalled = true;

                break;
            }

            otherInstalled = true;
        }
    }

    if (akvcamInstalled)
        return VCamInstalled;

    if (otherInstalled)
        return VCamInstalledOther;

    return VCamNotInstalled;
}

QString VideoLayer::vcamDriver() const
{
    return this->d->m_vcamDriver;
}

QString VideoLayer::currentVCamVersion() const
{
    if (this->d->m_cameraOutput)
        return this->d->m_cameraOutput->property("driverVersion").toString();

    return {};
}

bool VideoLayer::isCurrentVCamInstalled() const
{
    return this->d->m_currentVCamInstalled;
}

bool VideoLayer::canEditVCamDescription() const
{
    if (this->d->m_cameraOutput)
        return this->d->m_cameraOutput->property("canEditVCamDescription").toBool();

    return false;
}

QString VideoLayer::vcamUpdateUrl() const
{
#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS) || defined(FAKE_APPLE)
    return {"https://api.github.com/repos/webcamoid/akvirtualcamera/releases/latest"};
#elif defined(Q_OS_LINUX)
    return {"https://api.github.com/repos/webcamoid/akvcam/releases/latest"};
#else
    return {};
#endif
}

QString VideoLayer::vcamDownloadUrl() const
{
#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS) || defined(FAKE_APPLE)
    return {"https://github.com/webcamoid/akvirtualcamera/releases/latest"};
#elif defined(Q_OS_LINUX)
    return {"https://github.com/webcamoid/akvcam/releases/latest"};
#else
    return {};
#endif
}

QString VideoLayer::defaultVCamDriver() const
{
    return {DEFAULT_VCAM_DRIVER};
}

bool VideoLayer::isPassThroughVCam() const
{
    return this->d->m_isPassThroughVCam;
}

bool VideoLayer::applyPicture()
{
    if (!this->d->m_cameraOutput)
        return {};

    bool ok = false;
    QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                              "applyPicture",
                              Q_RETURN_ARG(bool, ok));

    return ok;
}

void VideoLayer::setLatestVCamVersion(const QString &version)
{
    this->d->m_latestVersion = version;
}

bool VideoLayer::downloadVCam()
{
    if (!Updates::isOnline())
        return false;

    auto installerUrl = this->d->vcamDownloadUrl();

    if (installerUrl.isEmpty())
        return false;

    auto cacheLocation =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    if (cacheLocation.isEmpty())
        return false;

    auto outFile = QDir(cacheLocation).filePath(QUrl(installerUrl).fileName());

    emit this->startVCamDownload(tr("Virtual Camera"),
                                    installerUrl,
                                    outFile);

    return true;
}

bool VideoLayer::executeVCamInstaller(const QString &installer)
{
    if (installer.isEmpty())
        return false;

    QFile(installer).setPermissions(QFileDevice::ReadOwner
                                    | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner
                                    | QFileDevice::ReadUser
                                    | QFileDevice::WriteUser
                                    | QFileDevice::ExeUser
                                    | QFileDevice::ReadGroup
                                    | QFileDevice::ExeGroup
                                    | QFileDevice::ReadOther
                                    | QFileDevice::ExeOther);

    auto result =
            QtConcurrent::run(&this->d->m_threadPool, [this, installer] () {
        qDebug() << "Executing installer:" << installer;
        int exitCode = -1;
        QString errorString = "Can't execute installer";

#ifdef Q_OS_WIN32
        SHELLEXECUTEINFOA execInfo;
        memset(&execInfo, 0, sizeof(SHELLEXECUTEINFOA));
        execInfo.cbSize = sizeof(SHELLEXECUTEINFOA);
        execInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
        execInfo.hwnd = nullptr;
        execInfo.lpVerb = "runas";
        execInfo.lpFile = installer.toStdString().c_str();
        execInfo.lpParameters = "";
        execInfo.lpDirectory = "";
        execInfo.nShow = SW_SHOWNORMAL;
        execInfo.hInstApp = nullptr;
        ShellExecuteExA(&execInfo);

        if (execInfo.hProcess) {
            WaitForSingleObject(execInfo.hProcess, INFINITE);

            DWORD dExitCode;
            GetExitCodeProcess(execInfo.hProcess, &dExitCode);
            CloseHandle(execInfo.hProcess);

            if (dExitCode == 0)
                errorString = "";
            else
                errorString = QString("Installer failed with code %1").arg(exitCode);

            exitCode = int(dExitCode);
        }
#elif defined(Q_OS_MACOS)
        QProcess proc;
        proc.start("open", QStringList {"-W", installer});
        proc.waitForFinished(-1);
        exitCode = proc.exitCode();
        errorString = proc.errorString();
#else
        QProcess proc;

    #ifdef Q_PROCESSOR_X86
        if (Ak::isFlatpak())
            proc.start("flatpak-spawn", QStringList {"--host", installer});
        else
            proc.start(installer, QStringList {});
    #else
        auto readLine = [this, &proc] () {
            while (proc.canReadLine())
                emit this->vcamCliInstallLineReady(proc.readLine());
        };

        QObject::connect(&proc,
                         &QProcess::started,
                         this,
                         &VideoLayer::vcamCliInstallStarted);
        QObject::connect(&proc,
                         &QProcess::readyReadStandardOutput,
                         this,
                         readLine,
                         Qt::DirectConnection);
        QObject::connect(&proc,
                         &QProcess::readyReadStandardError,
                         this,
                         readLine,
                         Qt::DirectConnection);

        if (Ak::isFlatpak())
            proc.start("flatpak-spawn",
                       QStringList {"--host",
                                    "pkexec",
                                    "/bin/sh",
                                    "-c",
                                    "yes | " + installer});
        else
            proc.start("pkexec",
                       QStringList {"/bin/sh",
                                    "-c",
                                    "yes | " + installer});
    #endif

        proc.waitForFinished(-1);
        exitCode = proc.exitCode();
        errorString = proc.errorString();

    #ifndef Q_PROCESSOR_X86
        emit this->vcamCliInstallFinished();
    #endif
#endif

        if (exitCode != 0)
            qDebug() << "Failed to run virtual camera installer:"
                     << exitCode
                     << ":"
                     << errorString;

        emit this->vcamInstallFinished(exitCode, errorString);
    });
    Q_UNUSED(result)

    return true;
}

void VideoLayer::checkVCamDownloadReady(const QString &url,
                                        const QString &filePath,
                                        DownloadManager::DownloadStatus status,
                                        const QString &error)
{
    auto installerUrl = this->d->vcamDownloadUrl();

    if (installerUrl.isEmpty())
        return;

    if (installerUrl != url)
        return;

    switch (status) {
    case DownloadManager::DownloadStatusFinished:
        emit this->vcamDownloadReady(filePath);

        break;

    case DownloadManager::DownloadStatusFailed:
        emit this->vcamDownloadFailed(error);

        break;

    default:
        break;
    }
}

void VideoLayer::setInputStream(const QString &stream,
                                const QString &description)
{
    if (stream.isEmpty()
        || description.isEmpty()
        || this->d->m_streams.value(stream) == description
        || this->d->m_images.value(stream) == description)
        return;

    QFileInfo fileInfo(stream);

    if (!fileInfo.exists())
        return;

    auto suffix = fileInfo.suffix().toLower();

    if (!this->d->m_supportedFileFormats.contains(suffix))
        return;

    if (this->d->m_supportedImageFormats.contains(suffix))
        this->d->m_images[stream] = description;
    else
        this->d->m_streams[stream] = description;

    this->updateInputs();
    auto streams = this->d->m_streams;
    streams.insert(this->d->m_images);
    this->d->saveStreams(streams);
}

void VideoLayer::removeInputStream(const QString &stream)
{
    if (stream.isEmpty()
        || (!this->d->m_images.contains(stream)
            && !this->d->m_streams.contains(stream)))
        return;

    this->d->m_images.remove(stream);
    this->d->m_streams.remove(stream);
    this->updateInputs();
    auto streams = this->d->m_streams;
    streams.insert(this->d->m_images);
    this->d->saveStreams(streams);

#ifdef Q_OS_ANDROID
    if (QFile::exists(stream))
        QFile::remove(stream);
#endif
}

void VideoLayer::setVideoInput(const QString &videoInput)
{
    if (this->d->m_videoInput == videoInput)
        return;

    this->d->m_videoInput = videoInput;
    emit this->videoInputChanged(videoInput);
    this->d->saveVideoInput(videoInput);
    this->updateCaps();
}

void VideoLayer::setVideoOutput(const QStringList &videoOutput)
{
    if (this->d->m_videoOutput == videoOutput)
        return;

    auto output = videoOutput.value(0);

    if (this->d->m_cameraOutput) {
        this->d->m_cameraOutput->setState(AkElement::ElementStateNull);
        this->d->saveVideoOutputControls();

        if (videoOutput.contains(DUMMY_OUTPUT_DEVICE)) {
            this->d->m_cameraOutput->setProperty("media", QString());
        } else {
            this->d->m_cameraOutput->setProperty("media", output);
            this->d->loadVideoOutputControls();

            if (!output.isEmpty())
                this->d->m_cameraOutput->setState(this->d->m_state);
        }
    }

    this->d->m_videoOutput = videoOutput;
    emit this->videoOutputChanged(videoOutput);
    this->d->saveVideoOutput(output);
}

void VideoLayer::setState(AkElement::ElementState state)
{
    if (this->d->m_state == state)
        return;

    AkElementPtr source;

    if (this->d->cameras().contains(this->d->m_videoInput)) {
        if (this->d->m_desktopCapture)
            this->d->m_desktopCapture->setState(AkElement::ElementStateNull);

        if (this->d->m_imageCapture)
            this->d->m_imageCapture->setState(AkElement::ElementStateNull);

        if (this->d->m_uriCapture)
            this->d->m_uriCapture->setState(AkElement::ElementStateNull);

        source = this->d->m_cameraCapture;
    } else if (this->d->desktops().contains(this->d->m_videoInput)) {
        if (this->d->m_cameraCapture)
            this->d->m_cameraCapture->setState(AkElement::ElementStateNull);

        if (this->d->m_imageCapture)
            this->d->m_imageCapture->setState(AkElement::ElementStateNull);

        if (this->d->m_uriCapture)
            this->d->m_uriCapture->setState(AkElement::ElementStateNull);

        source = this->d->m_desktopCapture;
    } else if (this->d->m_images.contains(this->d->m_videoInput)) {
        if (this->d->m_cameraCapture)
            this->d->m_cameraCapture->setState(AkElement::ElementStateNull);

        if (this->d->m_desktopCapture)
            this->d->m_desktopCapture->setState(AkElement::ElementStateNull);

        if (this->d->m_uriCapture)
            this->d->m_uriCapture->setState(AkElement::ElementStateNull);

        source = this->d->m_imageCapture;
    } else if (this->d->m_streams.contains(this->d->m_videoInput)) {
        if (this->d->m_cameraCapture)
            this->d->m_cameraCapture->setState(AkElement::ElementStateNull);

        if (this->d->m_desktopCapture)
            this->d->m_desktopCapture->setState(AkElement::ElementStateNull);

        if (this->d->m_imageCapture)
            this->d->m_imageCapture->setState(AkElement::ElementStateNull);

        source = this->d->m_uriCapture;
    }

    if (source) {
        if (source->setState(state)
            || source->state() != this->d->m_state) {
            auto state = source->state();
            this->d->m_state = state;
            emit this->stateChanged(state);

            if (this->d->m_cameraOutput) {
                if (this->d->m_videoOutput.isEmpty()
                    || this->d->m_videoOutput.contains(DUMMY_OUTPUT_DEVICE))
                    this->d->m_cameraOutput->setState(AkElement::ElementStateNull);
                else
                    this->d->m_cameraOutput->setState(state);
            }
        }
    } else {
        if (this->d->m_state != AkElement::ElementStateNull) {
            this->d->m_state = AkElement::ElementStateNull;
            emit this->stateChanged(AkElement::ElementStateNull);

            if (this->d->m_cameraOutput)
                this->d->m_cameraOutput->setState(AkElement::ElementStateNull);
        }
    }
}

void VideoLayer::setTorchMode(TorchMode mode)
{
    if (this->d->m_cameraCapture)
        this->d->m_cameraCapture->setProperty("torchMode", mode);
}

void VideoLayer::setPlayOnStart(bool playOnStart)
{
    if (this->d->m_playOnStart == playOnStart)
        return;

    this->d->m_playOnStart = playOnStart;
    emit this->playOnStartChanged(playOnStart);
    this->d->savePlayOnStart(playOnStart);
}

void VideoLayer::setOutputsAsInputs(bool outputsAsInputs)
{
    if (this->d->m_outputsAsInputs == outputsAsInputs)
        return;

    this->d->m_outputsAsInputs = outputsAsInputs;
    emit this->outputsAsInputsChanged(this->d->m_outputsAsInputs);
    this->updateInputs();
}

void VideoLayer::setPicture(const QString &picture)
{
    if (this->d->m_cameraOutput)
        this->d->m_cameraOutput->setProperty("picture", picture);
}

void VideoLayer::setRootMethod(const QString &rootMethod)
{
    if (this->d->m_cameraOutput)
        this->d->m_cameraOutput->setProperty("rootMethod", rootMethod);
}

void VideoLayer::resetVideoInput()
{
    this->setVideoInput({});
}

void VideoLayer::resetVideoOutput()
{
    this->setVideoOutput({});
}

void VideoLayer::resetState()
{
    this->setState(AkElement::ElementStateNull);
}

void VideoLayer::resetTorchMode()
{
    this->setTorchMode(Torch_Off);
}

void VideoLayer::resetPlayOnStart()
{
    this->setPlayOnStart(true);
}

void VideoLayer::resetOutputsAsInputs()
{
    this->setOutputsAsInputs(false);
}

void VideoLayer::resetPicture()
{
    if (this->d->m_cameraOutput)
        QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                                  "resetPicture");
}

void VideoLayer::resetRootMethod()
{
    if (this->d->m_cameraOutput)
        QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                                  "resetRootMethod");
}

void VideoLayer::setQmlEngine(QQmlApplicationEngine *engine)
{
    if (this->d->m_engine == engine)
        return;

    this->d->m_engine = engine;

    if (engine) {
        engine->rootContext()->setContextProperty("videoLayer", this);
        qRegisterMetaType<InputType>("VideoInputType");
        qRegisterMetaType<OutputType>("VideoOutputType");
        qRegisterMetaType<VCamStatus>("VCamStatus");
        qRegisterMetaType<TorchMode>("TorchMode");
        qRegisterMetaType<PermissionStatus>("PermissionStatus");
        qmlRegisterType<VideoLayer>("Webcamoid", 1, 0, "VideoLayer");
    }
}

void VideoLayer::updateCaps()
{
    auto state = this->state();
    this->setState(AkElement::ElementStateNull);
    auto source = this->d->sourceElement(this->d->m_videoInput);

    if (this->d->m_cameraOutput)
        this->d->m_cameraOutput->setState(AkElement::ElementStateNull);

    AkCaps audioCaps;
    AkCaps videoCaps;

    if (source) {
        // Set the resource to play.
        source->setProperty("media", this->d->m_videoInput);

        // Update output caps.
        QList<int> streams;
        QMetaObject::invokeMethod(source.data(),
                                  "streams",
                                  Q_RETURN_ARG(QList<int>, streams));

        if (streams.isEmpty()) {
            int audioStream = -1;
            int videoStream = -1;

            // Find the defaults audio and video streams.
            QMetaObject::invokeMethod(source.data(),
                                      "defaultStream",
                                      Q_RETURN_ARG(int, audioStream),
                                      Q_ARG(AkCaps::CapsType, AkCaps::CapsAudio));
            QMetaObject::invokeMethod(source.data(),
                                      "defaultStream",
                                      Q_RETURN_ARG(int, videoStream),
                                      Q_ARG(AkCaps::CapsType, AkCaps::CapsVideo));

            // Read streams caps.
            if (audioStream >= 0)
                QMetaObject::invokeMethod(source.data(),
                                          "caps",
                                          Q_RETURN_ARG(AkCaps, audioCaps),
                                          Q_ARG(int, audioStream));

            if (videoStream >= 0)
                QMetaObject::invokeMethod(source.data(),
                                          "caps",
                                          Q_RETURN_ARG(AkCaps, videoCaps),
                                          Q_ARG(int, videoStream));
        } else {
            for (auto &stream: streams) {
                AkCaps caps;
                QMetaObject::invokeMethod(source.data(),
                                          "caps",
                                          Q_RETURN_ARG(AkCaps, caps),
                                          Q_ARG(int, stream));

                if (caps.type() == AkCaps::CapsAudio)
                    audioCaps = caps;
                else if (caps.type() == AkCaps::CapsVideo)
                    videoCaps = caps;
            }
        }
    }

    if (this->d->m_cameraOutput) {
        QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                                  "clearStreams");
        QMetaObject::invokeMethod(this->d->m_cameraOutput.data(),
                                  "addStream",
                                  Q_ARG(int, 0),
                                  Q_ARG(AkCaps, videoCaps));

        if (!this->d->m_videoOutput.isEmpty() &&
            !this->d->m_videoOutput.contains(DUMMY_OUTPUT_DEVICE))
            this->d->m_cameraOutput->setState(state);
    }

    this->setState(state);
    this->d->setInputAudioCaps(audioCaps);
    this->d->setInputVideoCaps(videoCaps);
}

void VideoLayer::updateInputs()
{
    QStringList inputs;

    // Read cameras
    auto cameras = this->d->cameras();
    inputs << cameras;

    // Read desktops
    auto desktops = this->d->desktops();
    inputs << desktops;

    // Read streams
    inputs << this->d->m_images.keys()
           << this->d->m_streams.keys();

    // Remove outputs to prevent self blocking.
    if (this->d->m_cameraOutput && !this->d->m_outputsAsInputs) {
        auto outputs =
                this->d->m_cameraOutput->property("medias").toStringList();

        for (auto &output: outputs)
            inputs.removeAll(output);
    }

    // Update inputs
    if (inputs != this->d->m_inputs) {
        this->d->m_inputs = inputs;
        emit this->inputsChanged(this->d->m_inputs);

        if (!this->d->m_inputs.contains(this->d->m_videoInput))
            this->setVideoInput(this->d->m_inputs.value(0));
    }
}

void VideoLayer::saveVirtualCameraRootMethod(const QString &rootMethod)
{
    QSettings config;
    config.beginGroup("VirtualCamera");
    config.setValue("rootMethod", rootMethod);
    config.endGroup();
}

AkPacket VideoLayer::iStream(const AkPacket &packet)
{
    if (this->d->m_cameraOutput
        && !this->d->m_videoOutput.isEmpty()
        && !this->d->m_videoOutput.contains(DUMMY_OUTPUT_DEVICE)
        && !this->d->m_videoOutput.contains(this->d->m_videoInput))
        this->d->m_cameraOutput->iStream(packet);

    return {};
}

VideoLayerPrivate::VideoLayerPrivate(VideoLayer *self):
    self(self)
{
    this->m_formatsDescription = {
        {"3gp" , QObject::tr("3GP Video")                       },
        {"avi" , QObject::tr("AVI Video")                       },
        {"bmp" , QObject::tr("Windows Bitmap")                  },
        {"cur" , QObject::tr("Microsoft Windows Cursor")        },
        //: Adobe FLV Flash video
        {"flv" , QObject::tr("Flash Video")                     },
        {"gif" , QObject::tr("Animated GIF")                    },
        {"gif" , QObject::tr("Graphic Interchange Format")      },
        {"icns", QObject::tr("Apple Icon Image")                },
        {"ico" , QObject::tr("Microsoft Windows Icon")          },
        {"jpg" , QObject::tr("Joint Photographic Experts Group")},
        {"mkv" , QObject::tr("MKV Video")                       },
        {"mng" , QObject::tr("Animated PNG")                    },
        {"mng" , QObject::tr("Multiple-image Network Graphics") },
        {"mov" , QObject::tr("QuickTime Video")                 },
        {"mp4" , QObject::tr("MP4 Video")                       },
        {"mpg" , QObject::tr("MPEG Video")                      },
        {"ogg" , QObject::tr("Ogg Video")                       },
        {"pbm" , QObject::tr("Portable Bitmap")                 },
        {"pgm" , QObject::tr("Portable Graymap")                },
        {"png" , QObject::tr("Portable Network Graphics")       },
        {"ppm" , QObject::tr("Portable Pixmap")                 },
        //: Don't translate "RealMedia", leave it as is.
        {"rm"  , QObject::tr("RealMedia Video")                 },
        {"svg" , QObject::tr("Scalable Vector Graphics")        },
        {"tga" , QObject::tr("Truevision TGA")                  },
        {"tiff", QObject::tr("Tagged Image File Format")        },
        {"vob" , QObject::tr("DVD Video")                       },
        {"wbmp", QObject::tr("Wireless Bitmap")                 },
        {"webm", QObject::tr("WebM Video")                      },
        {"webp", QObject::tr("WebP")                            },
        //: Also known as WMV, is a video file format.
        {"wmv" , QObject::tr("Windows Media Video")             },
        {"xbm" , QObject::tr("X11 Bitmap")                      },
        {"xpm" , QObject::tr("X11 Pixmap")                      },
    };

    static const QStringList supportedVideoFormats {
        "3gp",
        "avi",
        "flv",
        "gif",
        "mkv",
        "mng",
        "mov",
        "mp4",
        "m4v",
        "mpg",
        "mpeg",
        "ogg",
        "rm",
        "vob",
        "webm",
        "wmv"
    };

    auto supportedImageFormats = QImageReader::supportedImageFormats();
    supportedImageFormats.removeAll("pdf");
    this->m_supportedFileFormats =
        supportedVideoFormats + QStringList(supportedImageFormats.begin(),
                                            supportedImageFormats.end());
}

bool VideoLayerPrivate::canAccessStorage()
{
#ifdef Q_OS_ANDROID
    static bool done = false;
    static bool result = false;

    if (done)
        return result;

    QJniObject context =
        qApp->nativeInterface<QNativeInterface::QAndroidApplication>()->context();

    if (!context.isValid()) {
        done = false;

        return result;
    }

    QStringList permissions;

    if (android_get_device_api_level() >= 33) {
        permissions << "android.permission.READ_MEDIA_IMAGES";
        permissions << "android.permission.READ_MEDIA_VIDEO";
    } else {
        permissions << "android.permission.READ_EXTERNAL_STORAGE";
    }

    QStringList neededPermissions;

    for (auto &permission: permissions) {
        auto permissionStr = QJniObject::fromString(permission);
        auto result =
            context.callMethod<jint>("checkSelfPermission",
                                     "(Ljava/lang/String;)I",
                                     permissionStr.object());

        if (result != PERMISSION_GRANTED)
            neededPermissions << permission;
    }

    if (!neededPermissions.isEmpty()) {
        QJniEnvironment jniEnv;
        jobjectArray permissionsArray =
            jniEnv->NewObjectArray(permissions.size(),
                                   jniEnv->FindClass("java/lang/String"),
                                   nullptr);
        int i = 0;

        for (auto &permission: permissions) {
            auto permissionObject = QJniObject::fromString(permission);
            jniEnv->SetObjectArrayElement(permissionsArray,
                                          i,
                                          permissionObject.object());
            i++;
        }

        context.callMethod<void>("requestPermissions",
                                 "([Ljava/lang/String;I)V",
                                 permissionsArray,
                                 jint(Ak::id()));
        QElapsedTimer timer;
        timer.start();
        static const int timeout = 5000;

        while (timer.elapsed() < timeout) {
            bool permissionsGranted = true;

            for (auto &permission: permissions) {
                auto permissionStr = QJniObject::fromString(permission);
                auto result =
                    context.callMethod<jint>("checkSelfPermission",
                                             "(Ljava/lang/String;)I",
                                             permissionStr.object());

                if (result != PERMISSION_GRANTED) {
                    permissionsGranted = false;

                    break;
                }
            }

            if (permissionsGranted)
                break;

            auto eventDispatcher = QThread::currentThread()->eventDispatcher();

            if (eventDispatcher)
                eventDispatcher->processEvents(QEventLoop::AllEvents);
        }
    }

    done = true;
    result = true;
#endif

    return true;
}

void VideoLayerPrivate::connectSignals()
{
    if (this->m_cameraCapture) {
        QObject::connect(this->m_cameraCapture.data(),
                         SIGNAL(oStream(AkPacket)),
                         self,
                         SIGNAL(oStream(AkPacket)),
                         Qt::DirectConnection);
        QObject::connect(this->m_cameraCapture.data(),
                         SIGNAL(mediasChanged(QStringList)),
                         self,
                         SLOT(updateInputs()));
        QObject::connect(this->m_cameraCapture.data(),
                         SIGNAL(errorChanged(QString)),
                         self,
                         SIGNAL(inputErrorChanged(QString)));
        QObject::connect(this->m_cameraCapture.data(),
                         SIGNAL(streamsChanged(QList<int>)),
                         self,
                         SLOT(updateCaps()));
        QObject::connect(this->m_cameraCapture.data(),
                         SIGNAL(isTorchSupportedChanged(bool)),
                         self,
                         SIGNAL(isTorchSupportedChanged(bool)));
        QObject::connect(this->m_cameraCapture.data(),
                         SIGNAL(torchModeChanged(TorchMode)),
                         self,
                         SIGNAL(torchModeChanged(TorchMode)));
        QObject::connect(this->m_cameraCapture.data(),
                         SIGNAL(permissionStatusChanged(PermissionStatus)),
                         self,
                         SIGNAL(cameraPermissionStatusChanged(PermissionStatus)));
    }

    if (this->m_desktopCapture) {
        QObject::connect(this->m_desktopCapture.data(),
                         SIGNAL(oStream(AkPacket)),
                         self,
                         SIGNAL(oStream(AkPacket)),
                         Qt::DirectConnection);
        QObject::connect(this->m_desktopCapture.data(),
                         SIGNAL(mediasChanged(QStringList)),
                         self,
                         SLOT(updateInputs()));
        QObject::connect(this->m_desktopCapture.data(),
                         SIGNAL(error(QString)),
                         self,
                         SIGNAL(inputErrorChanged(QString)));
        QObject::connect(this->m_desktopCapture.data(),
                         SIGNAL(streamsChanged(QList<int>)),
                         self,
                         SLOT(updateCaps()));
    }

    if (this->m_imageCapture) {
        QObject::connect(this->m_imageCapture.data(),
                         SIGNAL(oStream(AkPacket)),
                         self,
                         SIGNAL(oStream(AkPacket)),
                         Qt::DirectConnection);
        QObject::connect(this->m_imageCapture.data(),
                         SIGNAL(error(QString)),
                         self,
                         SIGNAL(inputErrorChanged(QString)));
        QObject::connect(this->m_imageCapture.data(),
                         SIGNAL(streamsChanged(QList<int>)),
                         self,
                         SLOT(updateCaps()));
        this->m_supportedImageFormats =
                this->m_imageCapture->property("supportedFormats").toStringList();
    }

    if (this->m_uriCapture) {
        this->m_uriCapture->setProperty("loop", true);

        QObject::connect(this->m_uriCapture.data(),
                         SIGNAL(oStream(AkPacket)),
                         self,
                         SIGNAL(oStream(AkPacket)),
                         Qt::DirectConnection);
        QObject::connect(this->m_uriCapture.data(),
                         SIGNAL(error(QString)),
                         self,
                         SIGNAL(inputErrorChanged(QString)));
        QObject::connect(this->m_uriCapture.data(),
                         SIGNAL(streamsChanged(QList<int>)),
                         self,
                         SLOT(updateCaps()));
    }

    if (this->m_cameraOutput) {
        QObject::connect(this->m_cameraOutput.data(),
                         SIGNAL(mediasChanged(QStringList)),
                         self,
                         SIGNAL(outputsChanged(QStringList)));
        QObject::connect(this->m_cameraOutput.data(),
                         SIGNAL(pictureChanged(QString)),
                         self,
                         SIGNAL(pictureChanged(QString)));
        QObject::connect(this->m_cameraOutput.data(),
                         SIGNAL(errorChanged(QString)),
                         self,
                         SIGNAL(outputErrorChanged(QString)));
        QObject::connect(this->m_cameraOutput.data(),
                         SIGNAL(rootMethodChanged(QString)),
                         self,
                         SIGNAL(rootMethodChanged(QString)),
                         Qt::DirectConnection);
        QObject::connect(this->m_cameraOutput.data(),
                         SIGNAL(rootMethodChanged(QString)),
                         self,
                         SLOT(saveVirtualCameraRootMethod(QString)));
    }
}

AkElementPtr VideoLayerPrivate::sourceElement(const QString &stream) const
{
    if (this->cameras().contains(stream))
        return this->m_cameraCapture;

    if (this->desktops().contains(stream))
        return this->m_desktopCapture;

    if (this->m_images.contains(stream))
        return this->m_imageCapture;

    if (this->m_streams.contains(stream))
        return this->m_uriCapture;

    return {};
}

QString VideoLayerPrivate::sourceId(const QString &stream) const
{
    if (this->cameras().contains(stream))
        return {"VideoSource/CameraCapture"};

    if (this->desktops().contains(stream))
        return {"VideoSource/DesktopCapture"};

    if (this->m_images.contains(stream))
        return {"VideoSource/ImageSrc"};

    if (this->m_streams.contains(stream))
        return {"MultimediaSource/MultiSrc"};

    return {};
}

QStringList VideoLayerPrivate::cameras() const
{
    if (!this->m_cameraCapture)
        return {};

    QStringList cameras;
    QMetaObject::invokeMethod(this->m_cameraCapture.data(),
                              "medias",
                              Q_RETURN_ARG(QStringList, cameras));
    return cameras;
}

QStringList VideoLayerPrivate::desktops() const
{
    if (!this->m_desktopCapture)
        return {};

    QStringList desktops;
    QMetaObject::invokeMethod(this->m_desktopCapture.data(),
                              "medias",
                              Q_RETURN_ARG(QStringList, desktops));
    return desktops;
}

QString VideoLayerPrivate::cameraDescription(const QString &camera) const
{
    if (!this->m_cameraCapture)
        return {};

    QString description;
    QMetaObject::invokeMethod(this->m_cameraCapture.data(),
                              "description",
                              Q_RETURN_ARG(QString, description),
                              Q_ARG(QString, camera));

    return description;
}

QString VideoLayerPrivate::desktopDescription(const QString &desktop) const
{
    if (!this->m_desktopCapture)
        return {};

    QString description;
    QMetaObject::invokeMethod(this->m_desktopCapture.data(),
                              "description",
                              Q_RETURN_ARG(QString, description),
                              Q_ARG(QString, desktop));

    return description;
}

bool VideoLayerPrivate::embedControls(const QString &where,
                                      const AkElementPtr &element,
                                      const QString &pluginId,
                                      const QString &name) const
{
    if (!element)
        return false;

    auto controlInterface = element->controlInterface(this->m_engine, pluginId);

    if (!controlInterface)
        return false;

    if (!name.isEmpty())
        controlInterface->setObjectName(name);

    for (auto &obj: this->m_engine->rootObjects()) {
        // First, find where to embed the UI.
        auto item = obj->findChild<QQuickItem *>(where);

        if (!item)
            continue;

        // Create an item with the plugin context.
        auto interfaceItem = qobject_cast<QQuickItem *>(controlInterface);

        // Finally, embed the plugin item UI in the desired place.
        interfaceItem->setParentItem(item);

        return true;
    }

    return false;
}

void VideoLayerPrivate::setInputAudioCaps(const AkAudioCaps &inputAudioCaps)
{
    if (this->m_inputAudioCaps == inputAudioCaps)
        return;

    this->m_inputAudioCaps = inputAudioCaps;
    emit self->inputAudioCapsChanged(inputAudioCaps);
}

void VideoLayerPrivate::setInputVideoCaps(const AkVideoCaps &inputVideoCaps)
{
    if (this->m_inputVideoCaps == inputVideoCaps)
        return;

    this->m_inputVideoCaps = inputVideoCaps;
    emit self->inputVideoCapsChanged(inputVideoCaps);
}

void VideoLayerPrivate::loadProperties()
{
    QSettings config;

    config.beginGroup("StreamConfigs");
    this->m_videoInput = config.value("stream").toString();
    this->m_playOnStart = config.value("playOnStart", true).toBool();

    int size = config.beginReadArray("uris");

    for (int i = 0; i < size; i++) {
        config.setArrayIndex(i);
        auto uri = config.value("uri").toString();
        auto description = config.value("description").toString();

        QFileInfo fileInfo(uri);

        if (!fileInfo.exists())
            continue;

        auto suffix = fileInfo.suffix().toLower();

        if (!this->m_supportedFileFormats.contains(suffix))
            continue;

        if (this->m_supportedImageFormats.contains(suffix))
            this->m_images[uri] = description;
        else
            this->m_streams[uri] = description;
    }

    config.endArray();
    config.endGroup();

    config.beginGroup("VirtualCamera");
    this->m_outputsAsInputs = config.value("loopback", false).toBool();

    if (this->m_cameraOutput) {
        auto rootMethod =
                config.value("rootMethod",
                             this->m_cameraOutput->property("rootMethod")).toString();
        auto availableMethods =
                this->m_cameraOutput->property("availableRootMethods").toStringList();

        if (availableMethods.contains(rootMethod))
            this->m_cameraOutput->setProperty("rootMethod", rootMethod);

        auto streams = this->m_cameraOutput->property("medias").toStringList();
        auto stream = config.value("stream", streams.value(0)).toString();

        if (!streams.contains(stream))
            stream = streams.value(0);

        this->m_videoOutput = QStringList {stream};

        if (stream != DUMMY_OUTPUT_DEVICE)
            this->m_cameraOutput->setProperty("media", stream);
    }

    config.endGroup();

    this->loadVideoOutputControls();
    self->updateInputs();
    self->updateCaps();
}

void VideoLayerPrivate::loadVideoOutputControls()
{
    if (!this->m_cameraOutput)
        return;

    auto output = this->m_cameraOutput->property("media").toString();

    if (output.isEmpty())
        return;

    QSettings config;

    config.beginGroup("VirtualCamera_" + sanitizeKey(output));
    auto controlKeys = config.allKeys();
    QVariantMap controls;

    for (const auto &key: controlKeys)
        controls[key] = config.value(key);

    config.endGroup();

    if (!controls.isEmpty())
        QMetaObject::invokeMethod(this->m_cameraOutput.data(),
                                  "setControls",
                                  Q_ARG(QVariantMap, controls));
}

QString VideoLayerPrivate::sanitizeKey(const QString &key)
{
    QString sanitized(key);

    return sanitized.replace(" ", "_")
                    .replace(".", "_")
                    .replace(",", "_");
}

void VideoLayerPrivate::saveVideoInput(const QString &videoInput)
{
    QSettings config;
    config.beginGroup("StreamConfigs");
    config.setValue("stream", videoInput);
    config.endGroup();
}

void VideoLayerPrivate::saveVideoOutput(const QString &videoOutput)
{
    QSettings config;
    config.beginGroup("VirtualCamera");
    config.setValue("stream", videoOutput);
    config.endGroup();
}

void VideoLayerPrivate::saveVideoOutputControls()
{
    if (!this->m_cameraOutput)
        return;

    auto output = this->m_cameraOutput->property("media").toString();

    if (output.isEmpty())
        return;

    QSettings config;

    config.beginGroup("VirtualCamera_" + sanitizeKey(output));
    QVariantList controls;
    QMetaObject::invokeMethod(this->m_cameraOutput.data(),
                              "controls",
                              Q_RETURN_ARG(QVariantList, controls));

    for (const auto &control: controls) {
        auto controlValues = control.toList();
        config.setValue(sanitizeKey(controlValues[0].toString()), controlValues[7]);
    }

    config.endGroup();
}

void VideoLayerPrivate::saveStreams(const QMap<QString, QString> &streams)
{
    QSettings config;
    config.beginGroup("StreamConfigs");
    config.beginWriteArray("uris");

    int i = 0;

    for (auto it = streams.begin(); it != streams.end(); it++) {
        config.setArrayIndex(i);
        config.setValue("uri", it.key());
        config.setValue("description", it.value());
        i++;
    }

    config.endArray();
    config.endGroup();
}

void VideoLayerPrivate::savePlayOnStart(bool playOnStart)
{
    QSettings config;
    config.beginGroup("StreamConfigs");
    config.setValue("playOnStart", playOnStart);
    config.endGroup();
}

void VideoLayerPrivate::saveOutputsAsInputs(bool outputsAsInputs)
{
    QSettings config;
    config.beginGroup("VirtualCamera");
    config.setValue("loopback", outputsAsInputs);
    config.endGroup();
}

QString VideoLayerPrivate::vcamDownloadUrl() const
{
    if (this->m_latestVersion.isEmpty())
        return {};

#if defined(Q_OS_WIN32)
    return QString("https://github.com/webcamoid/akvirtualcamera/releases/download/%1/akvirtualcamera-windows-%1.exe")
           .arg(this->m_latestVersion);
#elif defined(Q_OS_MACOS)
    return QString("https://github.com/webcamoid/akvirtualcamera/releases/download/%1/akvirtualcamera-mac-%1-%2.pkg")
           .arg(this->m_latestVersion)
           .arg(TARGET_ARCH);
#elif defined(Q_OS_LINUX)
    #ifdef Q_PROCESSOR_X86
        return QString("https://github.com/webcamoid/akvcam/releases/download/%1/akvcam-installer-gui-linux-%1.run")
               .arg(this->m_latestVersion);
    #else
        return QString("https://github.com/webcamoid/akvcam/releases/download/%1/akvcam-installer-cli-linux-%1.run")
               .arg(this->m_latestVersion);
    #endif
#else
    return {};
#endif
}

#include "moc_videolayer.cpp"
