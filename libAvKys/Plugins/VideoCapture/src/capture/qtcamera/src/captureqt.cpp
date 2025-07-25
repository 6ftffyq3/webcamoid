/* Webcamoid, webcam capture application.
 * Copyright (C) 2017  Gonzalo Exequiel Pedone
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

#include <QApplication>
#include <QCamera>
#include <QDebug>
#include <QMutex>
#include <QReadWriteLock>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QTimer>
#include <QVideoSink>
#include <QWaitCondition>
#include <QtConcurrent>
#include <ak.h>
#include <akfrac.h>
#include <akcaps.h>
#include <akpacket.h>
#include <akpluginmanager.h>
#include <akvideocaps.h>
#include <akvideoformatspec.h>
#include <akvideopacket.h>
#include <akcompressedvideocaps.h>
#include <akcompressedvideopacket.h>
#include <iak/akelement.h>

#if (defined(Q_OS_ANDROID) || defined(Q_OS_MACOS)) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif

#include "captureqt.h"

using QtFmtToAkFmtMap = QMap<QVideoFrameFormat::PixelFormat, AkVideoCaps::PixelFormat>;

inline QtFmtToAkFmtMap initQtFmtToAkFmt()
{
    QtFmtToAkFmtMap qtFmtToAkFmt {
        {QVideoFrameFormat::Format_ARGB8888 , AkVideoCaps::Format_argb     },
        {QVideoFrameFormat::Format_XRGB8888 , AkVideoCaps::Format_xrgb     },
        {QVideoFrameFormat::Format_BGRA8888 , AkVideoCaps::Format_bgra     },
        {QVideoFrameFormat::Format_BGRX8888 , AkVideoCaps::Format_bgrx     },
        {QVideoFrameFormat::Format_ABGR8888 , AkVideoCaps::Format_abgr     },
        {QVideoFrameFormat::Format_XBGR8888 , AkVideoCaps::Format_xbgr     },
        {QVideoFrameFormat::Format_RGBA8888 , AkVideoCaps::Format_rgba     },
        {QVideoFrameFormat::Format_RGBX8888 , AkVideoCaps::Format_rgbx     },
        {QVideoFrameFormat::Format_AYUV     , AkVideoCaps::Format_ayuv     },
        {QVideoFrameFormat::Format_YUV420P  , AkVideoCaps::Format_yuv420p  },
        {QVideoFrameFormat::Format_YUV422P  , AkVideoCaps::Format_yuv422p  },
        {QVideoFrameFormat::Format_YV12     , AkVideoCaps::Format_yvu420p  },
        {QVideoFrameFormat::Format_UYVY     , AkVideoCaps::Format_uyvy422  },
        {QVideoFrameFormat::Format_YUYV     , AkVideoCaps::Format_yuyv422  },
        {QVideoFrameFormat::Format_NV12     , AkVideoCaps::Format_nv12     },
        {QVideoFrameFormat::Format_NV21     , AkVideoCaps::Format_nv21     },
        {QVideoFrameFormat::Format_Y8       , AkVideoCaps::Format_y8       },
        {QVideoFrameFormat::Format_Y16      , AkVideoCaps::Format_y16      },
        {QVideoFrameFormat::Format_P010     , AkVideoCaps::Format_p010     },
        {QVideoFrameFormat::Format_P016     , AkVideoCaps::Format_p016     },
        {QVideoFrameFormat::Format_YUV420P10, AkVideoCaps::Format_yuv420p10},
    };

    return qtFmtToAkFmt;
}

Q_GLOBAL_STATIC_WITH_ARGS(QtFmtToAkFmtMap, qtFmtToAkFmt, (initQtFmtToAkFmt()))

using QtCompressedFmtToAkFmtMap = QMap<QVideoFrameFormat::PixelFormat, AkCompressedVideoCaps::VideoCodecID>;

inline QtCompressedFmtToAkFmtMap initQtCompressedFmtToAkFmt()
{
    QtCompressedFmtToAkFmtMap qtCompressedFmtToAkFmt {
        {QVideoFrameFormat::Format_Jpeg, AkCompressedVideoCaps::VideoCodecID_jpeg},
    };

    return qtCompressedFmtToAkFmt;
}

Q_GLOBAL_STATIC_WITH_ARGS(QtCompressedFmtToAkFmtMap,
                          qtCompressedFmtToAkFmt,
                          (initQtCompressedFmtToAkFmt()))

using ImageToPixelFormatMap = QMap<QImage::Format, AkVideoCaps::PixelFormat>;

inline ImageToPixelFormatMap initImageToPixelFormatMap()
{
    ImageToPixelFormatMap imageToAkFormat {
        {QImage::Format_RGB32     , AkVideoCaps::Format_xrgbpack},
        {QImage::Format_ARGB32    , AkVideoCaps::Format_argbpack},
        {QImage::Format_RGB16     , AkVideoCaps::Format_rgb565  },
        {QImage::Format_RGB555    , AkVideoCaps::Format_rgb555  },
        {QImage::Format_RGB888    , AkVideoCaps::Format_rgb24   },
        {QImage::Format_RGB444    , AkVideoCaps::Format_rgb444  },
        {QImage::Format_Grayscale8, AkVideoCaps::Format_y8      }
    };

    return imageToAkFormat;
}

Q_GLOBAL_STATIC_WITH_ARGS(ImageToPixelFormatMap,
                          imageToAkFormat,
                          (initImageToPixelFormatMap()))

using CameraPtr = QSharedPointer<QCamera>;

class CaptureQtPrivate
{
    public:
        CaptureQt *self;
        QString m_device;
        QList<int> m_streams;
        QStringList m_devices;
        QMap<QString, QString> m_descriptions;
        QMap<QString, AkCapsList> m_devicesCaps;
        QReadWriteLock m_controlsMutex;
        QReadWriteLock m_frameMutex;
        AkPacket m_videoPacket;
        QWaitCondition m_packetReady;
        QVariantList m_globalImageControls;
        QVariantList m_globalCameraControls;
        QVariantMap m_localImageControls;
        QVariantMap m_localCameraControls;
        CameraPtr m_camera;
        QMediaDevices m_mediaDevices;
        QMediaCaptureSession m_captureSession;
        QVideoSink m_videoSink;

#if (defined(Q_OS_ANDROID) || defined(Q_OS_MACOS)) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QCameraPermission m_cameraPermission;
        bool m_permissionResultReady {false};
#endif

        AkElementPtr m_hslFilter {akPluginManager->create<AkElement>("VideoFilter/AdjustHSL")};
        AkElementPtr m_contrastFilter {akPluginManager->create<AkElement>("VideoFilter/Contrast")};
        AkElementPtr m_gammaFilter {akPluginManager->create<AkElement>("VideoFilter/Gamma")};
        AkElementPtr m_rotateFilter {akPluginManager->create<AkElement>("VideoFilter/Rotate")};
        qint64 m_id {-1};
        AkFrac m_fps;
        bool m_isTorchSupported {false};
        Capture::TorchMode m_torchMode {Capture::Torch_Off};

        explicit CaptureQtPrivate(CaptureQt *self);
        ~CaptureQtPrivate();
        QVariantList imageControls() const;
        bool setImageControls(const QVariantMap &imageControls) const;
        QVariantMap controlStatus(const QVariantList &controls) const;
        QVariantMap mapDiff(const QVariantMap &map1,
                            const QVariantMap &map2) const;
        qreal cameraRotation(const QVideoFrame &frame) const;
        void frameReady(const QVideoFrame &frame);
        void updateDevices();
};

CaptureQt::CaptureQt(QObject *parent):
    Capture(parent)
{
    this->d = new CaptureQtPrivate(this);

#if (defined(Q_OS_ANDROID) || defined(Q_OS_MACOS)) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto permissionStatus = qApp->checkPermission(this->d->m_cameraPermission);

    if (permissionStatus == Qt::PermissionStatus::Granted) {
        qInfo() << "Permission granted for camera capture with Qt Camera";
    } else {
        this->d->m_permissionResultReady = false;
        qApp->requestPermission(this->d->m_cameraPermission,
                                this,
                                [this, permissionStatus] (const QPermission &permission) {
                                    if (permissionStatus != permission.status()) {
                                        PermissionStatus curStatus = PermissionStatus_Granted;

                                        switch (permission.status()) {
                                        case Qt::PermissionStatus::Undetermined:
                                            qInfo() << "Permission undetermined for camera capture with Qt Camera";
                                            curStatus = PermissionStatus_Undetermined;

                                            break;

                                        case Qt::PermissionStatus::Granted:
                                            qInfo() << "Permission granted for camera capture with Qt Camera";
                                            curStatus = PermissionStatus_Granted;

                                            break;

                                        case Qt::PermissionStatus::Denied:
                                            qInfo() << "Permission denied for camera capture with Qt Camera";
                                            curStatus = PermissionStatus_Denied;

                                            break;

                                        default:
                                            break;
                                        }

                                        emit this->permissionStatusChanged(curStatus);
                                    }

                                    this->d->m_permissionResultReady = true;
                                });

        while (!this->d->m_permissionResultReady) {
            auto eventDispatcher = QThread::currentThread()->eventDispatcher();

            if (eventDispatcher)
                eventDispatcher->processEvents(QEventLoop::AllEvents);
        }
    }
#endif

    QObject::connect(&this->d->m_videoSink,
                     &QVideoSink::videoFrameChanged,
                     this,
                     [this] (const QVideoFrame &frame) {
                         this->d->frameReady(frame);
                     });
    QObject::connect(&this->d->m_mediaDevices,
                     &QMediaDevices::videoInputsChanged,
                     this,
                     [this] () {
                         this->d->updateDevices();
                     });

    this->d->updateDevices();
}

CaptureQt::~CaptureQt()
{
    delete this->d;
}

QStringList CaptureQt::webcams() const
{
    return this->d->m_devices;
}

QString CaptureQt::device() const
{
    return this->d->m_device;
}

QList<int> CaptureQt::streams()
{
    if (!this->d->m_streams.isEmpty())
        return this->d->m_streams;

    auto caps = this->d->m_devicesCaps.value(this->d->m_device);

    if (caps.isEmpty())
        return {};

    return {0};
}

QList<int> CaptureQt::listTracks(AkCaps::CapsType type)
{
    if (type != AkCaps::CapsVideo && type != AkCaps::CapsUnknown)
        return {};

    auto caps = this->d->m_devicesCaps.value(this->d->m_device);
    QList<int> streams;

    for (int i = 0; i < caps.count(); i++)
        streams << i;

    return streams;
}

QString CaptureQt::ioMethod() const
{
    return {};
}

int CaptureQt::nBuffers() const
{
    return 0;
}

QString CaptureQt::description(const QString &webcam) const
{
    return this->d->m_descriptions.value(webcam);
}

AkCapsList CaptureQt::caps(const QString &webcam) const
{
    AkCapsList caps;

    for (auto &videoCaps: this->d->m_devicesCaps.value(webcam))
        caps << videoCaps;

    return caps;
}

QVariantList CaptureQt::imageControls() const
{
    return this->d->m_globalImageControls;
}

bool CaptureQt::setImageControls(const QVariantMap &imageControls)
{
    this->d->m_controlsMutex.lockForRead();
    auto globalImageControls = this->d->m_globalImageControls;
    this->d->m_controlsMutex.unlock();

    for (int i = 0; i < globalImageControls.count(); i++) {
        auto control = globalImageControls[i].toList();
        auto controlName = control[0].toString();

        if (imageControls.contains(controlName)) {
            control[6] = imageControls[controlName];
            globalImageControls[i] = control;
        }
    }

    this->d->m_controlsMutex.lockForWrite();

    if (this->d->m_globalImageControls == globalImageControls) {
        this->d->m_controlsMutex.unlock();

        return false;
    }

    this->d->m_globalImageControls = globalImageControls;
    this->d->m_controlsMutex.unlock();

    emit this->imageControlsChanged(imageControls);

    return true;
}

bool CaptureQt::resetImageControls()
{
    QVariantMap controls;

    for (auto &control: this->imageControls()) {
        QVariantList params = control.toList();
        controls[params[0].toString()] = params[5].toInt();
    }

    return this->setImageControls(controls);
}

QVariantList CaptureQt::cameraControls() const
{
    return this->d->m_globalCameraControls;
}

bool CaptureQt::setCameraControls(const QVariantMap &cameraControls)
{
    this->d->m_controlsMutex.lockForRead();
    auto globalCameraControls = this->d->m_globalCameraControls;
    this->d->m_controlsMutex.unlock();

    for (int i = 0; i < globalCameraControls.count(); i++) {
        QVariantList control = globalCameraControls[i].toList();
        QString controlName = control[0].toString();

        if (cameraControls.contains(controlName)) {
            control[6] = cameraControls[controlName];
            globalCameraControls[i] = control;
        }
    }

    this->d->m_controlsMutex.lockForWrite();

    if (this->d->m_globalCameraControls == globalCameraControls) {
        this->d->m_controlsMutex.unlock();

        return false;
    }

    this->d->m_globalCameraControls = globalCameraControls;
    this->d->m_controlsMutex.unlock();
    emit this->cameraControlsChanged(cameraControls);

    return true;
}

bool CaptureQt::resetCameraControls()
{
    QVariantMap controls;

    for (auto &control: this->cameraControls()) {
        auto params = control.toList();
        controls[params[0].toString()] = params[5].toInt();
    }

    return this->setCameraControls(controls);
}

AkPacket CaptureQt::readFrame()
{
    if (!this->d->m_camera)
        return {};

    this->d->m_controlsMutex.lockForRead();
    auto imageControls = this->d->controlStatus(this->d->m_globalImageControls);
    this->d->m_controlsMutex.unlock();

    if (this->d->m_localImageControls != imageControls) {
        auto controls = this->d->mapDiff(this->d->m_localImageControls,
                                         imageControls);
        this->d->setImageControls(controls);
        this->d->m_localImageControls = imageControls;
    }

    if (!this->d->m_videoPacket)
        if (!this->d->m_packetReady.wait(&this->d->m_frameMutex, 1000)) {
            this->d->m_frameMutex.unlock();

            return {};
        }

    auto packet = this->d->m_videoPacket;
    this->d->m_videoPacket = {};
    this->d->m_frameMutex.unlock();

    if (this->d->m_hslFilter)
        packet = this->d->m_hslFilter->iStream(packet);

    if (this->d->m_gammaFilter)
        packet = this->d->m_gammaFilter->iStream(packet);

    if (this->d->m_contrastFilter)
        packet = this->d->m_contrastFilter->iStream(packet);

    return packet;
}

bool CaptureQt::isTorchSupported() const
{
    return this->d->m_isTorchSupported;
}

Capture::TorchMode CaptureQt::torchMode() const
{
    return this->d->m_torchMode;
}

Capture::PermissionStatus CaptureQt::permissionStatus() const
{
#if (defined(Q_OS_ANDROID) || defined(Q_OS_MACOS)) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    switch (qApp->checkPermission(this->d->m_cameraPermission)) {
    case Qt::PermissionStatus::Undetermined:
        return PermissionStatus_Undetermined;

    case Qt::PermissionStatus::Granted:
        return PermissionStatus_Granted;

    case Qt::PermissionStatus::Denied:
        return PermissionStatus_Denied;

    default:
        break;
    }
#endif
    return PermissionStatus_Granted;
}

bool CaptureQt::init()
{
    this->d->m_localImageControls.clear();
    this->d->m_localCameraControls.clear();

    auto streams = this->streams();

    if (streams.isEmpty()) {
        qCritical() << "Qt VideoCapture: No streams available.";

        return false;
    }

    auto supportedCaps = this->caps(this->d->m_device);
    auto caps = supportedCaps[streams[0]];
    QVideoFrameFormat::PixelFormat pixelFormat =
        QVideoFrameFormat::Format_Invalid;
    int width = 0;
    int height = 0;
    AkFrac fps;

    if (caps.type() == AkCaps::CapsVideo) {
        AkVideoCaps videoCaps(caps);
        pixelFormat = qtFmtToAkFmt->key(videoCaps.format(),
                                        QVideoFrameFormat::Format_Invalid);
        width = videoCaps.width();
        height = videoCaps.height();
        fps = videoCaps.fps();
    } else {
        AkCompressedVideoCaps videoCaps(caps);
        pixelFormat = qtCompressedFmtToAkFmt->key(videoCaps.codec(),
                                                  QVideoFrameFormat::Format_Invalid);
        width = videoCaps.rawCaps().width();
        height = videoCaps.rawCaps().height();
        fps = videoCaps.rawCaps().fps();
    }

    QCameraDevice cameraDevice;
    QCameraFormat cameraFormat;
    quint64 k = std::numeric_limits<quint64>::max();

    for (auto &camera: QMediaDevices::videoInputs())
        if (camera.id() == this->d->m_device)
            for (auto &format: camera.videoFormats()) {
                quint64 diffFormat = format.pixelFormat() - pixelFormat;
                quint64 diffWidth = format.resolution().width() - width;
                quint64 diffHeight = format.resolution().height() - height;
                quint64 q = diffFormat * diffFormat
                            + diffWidth * diffWidth
                            + diffHeight * diffHeight;

                if (q < k) {
                    cameraDevice = camera;
                    cameraFormat = format;
                    k = q;
                }
            }

    if (cameraDevice.isNull()) {
        qCritical() << "Qt VideoCapture: A suitable device was not found.";

        return false;
    }

    if (cameraFormat.isNull()) {
        qCritical() << "Qt VideoCapture: A suitable format was not found.";

        return false;
    }

    this->d->m_id = Ak::id();
    this->d->m_fps = fps;

    this->d->m_camera = CameraPtr(new QCamera(cameraDevice));

    if (this->d->m_torchMode == Torch_Off
        && this->d->m_camera->torchMode() == QCamera::TorchOn)
        this->d->m_camera->setTorchMode(QCamera::TorchOff);
    else if (this->d->m_torchMode == Torch_On
             && this->d->m_camera->torchMode() == QCamera::TorchOff)
        this->d->m_camera->setTorchMode(QCamera::TorchOn);

    this->d->m_camera->setCameraFormat(cameraFormat);
    this->d->m_captureSession.setCamera(this->d->m_camera.data());
    this->d->m_captureSession.setVideoSink(&this->d->m_videoSink);
    this->d->m_camera->start();

    return true;
}

void CaptureQt::uninit()
{
    if (this->d->m_camera) {
        this->d->m_camera->stop();
        this->d->m_camera = {};
    }
}

void CaptureQt::setDevice(const QString &device)
{
    if (this->d->m_device == device)
        return;

    this->d->m_device = device;

    if (device.isEmpty()) {
        this->d->m_controlsMutex.lockForWrite();
        this->d->m_globalImageControls.clear();
        this->d->m_globalCameraControls.clear();
        this->d->m_controlsMutex.unlock();
    } else {
        this->d->m_controlsMutex.lockForWrite();

        for (auto &cameraDevice: QMediaDevices::videoInputs())
            if (cameraDevice.id() == device)
                this->d->m_globalImageControls = this->d->imageControls();

        this->d->m_controlsMutex.unlock();
    }

    this->d->m_controlsMutex.lockForRead();
    auto imageStatus = this->d->controlStatus(this->d->m_globalImageControls);
    auto cameraStatus = this->d->controlStatus(this->d->m_globalCameraControls);
    this->d->m_controlsMutex.unlock();

    emit this->deviceChanged(device);
    emit this->imageControlsChanged(imageStatus);
    emit this->cameraControlsChanged(cameraStatus);

    bool isTorchSupported = false;
    Capture::TorchMode torchMode = Capture::Torch_Off;

    for (auto &cameraDevice: QMediaDevices::videoInputs())
        if (cameraDevice.id() == device) {
            QCamera camera(cameraDevice);
            isTorchSupported = camera.isTorchModeSupported(QCamera::TorchOff)
                               && isTorchSupported;
            torchMode = camera.torchMode() == QCamera::TorchOn?
                Capture::Torch_On: Capture::Torch_Off;
        }

    if (this->d->m_isTorchSupported != isTorchSupported) {
        this->d->m_isTorchSupported = isTorchSupported;
        emit this->isTorchSupportedChanged(isTorchSupported);
    }

    if (this->d->m_torchMode != torchMode) {
        this->d->m_torchMode = torchMode;
        emit this->torchModeChanged(torchMode);
    }
}

void CaptureQt::setStreams(const QList<int> &streams)
{
    if (streams.isEmpty())
        return;

    int stream = streams[0];

    if (stream < 0)
        return;

    auto supportedCaps = this->d->m_devicesCaps.value(this->d->m_device);

    if (stream >= supportedCaps.length())
        return;

    QList<int> inputStreams {stream};

    if (this->streams() == inputStreams)
        return;

    this->d->m_streams = inputStreams;
    emit this->streamsChanged(inputStreams);
}

void CaptureQt::setIoMethod(const QString &ioMethod)
{
}

void CaptureQt::setNBuffers(int nBuffers)
{
}

void CaptureQt::setTorchMode(TorchMode mode)
{
    if (this->d->m_torchMode == mode)
        return;

    this->d->m_torchMode = mode;
    emit this->torchModeChanged(mode);

    if (this->d->m_camera) {
        if (this->d->m_torchMode == Torch_Off
            && this->d->m_camera->torchMode() == QCamera::TorchOn)
            this->d->m_camera->setTorchMode(QCamera::TorchOff);
        else if (this->d->m_torchMode == Torch_On
                 && this->d->m_camera->torchMode() == QCamera::TorchOff)
            this->d->m_camera->setTorchMode(QCamera::TorchOn);
    }
}

void CaptureQt::resetDevice()
{
    this->setDevice("");
}

void CaptureQt::resetStreams()
{
    auto supportedCaps = this->d->m_devicesCaps.value(this->d->m_device);
    QList<int> streams;

    if (!supportedCaps.isEmpty())
        streams << 0;

    this->setStreams(streams);
}

void CaptureQt::resetIoMethod()
{
    this->setIoMethod("any");
}

void CaptureQt::resetNBuffers()
{
    this->setNBuffers(32);
}

void CaptureQt::resetTorchMode()
{
    this->setTorchMode(Torch_Off);
}

void CaptureQt::reset()
{
    this->resetStreams();
    this->resetImageControls();
    this->resetCameraControls();
}

CaptureQtPrivate::CaptureQtPrivate(CaptureQt *self):
    self(self)
{
}

CaptureQtPrivate::~CaptureQtPrivate()
{
}

QVariantList CaptureQtPrivate::imageControls() const
{
    QVariantList controlsList;

    if (this->m_hslFilter)
        controlsList << QVariant(QVariantList {
            "Brightness",
            "integer",
            -255,
            255,
            1,
            0,
            this->m_hslFilter->property("luminance").toInt(),
            QStringList()
        });

    if (this->m_contrastFilter)
        controlsList << QVariant(QVariantList {
            "Contrast",
            "integer",
            -255,
            255,
            1,
            0,
            this->m_contrastFilter->property("contrast").toInt(),
            QStringList()
        });

    if (this->m_hslFilter)
        controlsList << QVariant(QVariantList {
            "Saturation",
            "integer",
            -255,
            255,
            1,
            0,
            this->m_hslFilter->property("saturation").toInt(),
            QStringList()
        });

    if (this->m_hslFilter)
        controlsList << QVariant(QVariantList {
            "Hue",
            "integer",
            -359,
            359,
            1,
            0,
            this->m_hslFilter->property("hue").toInt(),
            QStringList()
        });

    if (this->m_gammaFilter)
        controlsList << QVariant(QVariantList {
            "Gamma",
            "integer",
            -255,
            255,
            1,
            0,
            this->m_gammaFilter->property("gamma").toInt(),
            QStringList()
        });

    return controlsList;
}

bool CaptureQtPrivate::setImageControls(const QVariantMap &imageControls) const
{
    for (auto it = imageControls.cbegin(); it != imageControls.cend(); it++) {
        if (it.key() == "Brightness") {
            if (this->m_hslFilter)
                this->m_hslFilter->setProperty("luminance", it.value());
        } else if (it.key() == "Contrast") {
            if (this->m_contrastFilter)
                this->m_contrastFilter->setProperty("contrast", it.value());
        } else if (it.key() == "Saturation") {
            if (this->m_hslFilter)
                this->m_hslFilter->setProperty("saturation", it.value());
        } else if (it.key() == "Hue") {
            if (this->m_hslFilter)
                this->m_hslFilter->setProperty("hue", it.value());
        } else if (it.key() == "Gamma") {
            if (this->m_gammaFilter)
                this->m_gammaFilter->setProperty("gamma", it.value());
        }
    }

    return true;
}

QVariantMap CaptureQtPrivate::controlStatus(const QVariantList &controls) const
{
    QVariantMap controlStatus;

    for (auto &control: controls) {
        auto params = control.toList();
        auto controlName = params[0].toString();
        controlStatus[controlName] = params[6];
    }

    return controlStatus;
}

QVariantMap CaptureQtPrivate::mapDiff(const QVariantMap &map1,
                                      const QVariantMap &map2) const
{
    QVariantMap map;

    for (auto it = map2.cbegin(); it != map2.cend(); it++)
        if (!map1.contains(it.key())
            || map1[it.key()] != it.value()) {
            map[it.key()] = it.value();
        }

    return map;
}

qreal CaptureQtPrivate::cameraRotation(const QVideoFrame &frame) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    return -qreal(frame.rotation());
#else
    return -frame.rotationAngle();
#endif
}

void CaptureQtPrivate::frameReady(const QVideoFrame &frame)
{
    QVideoFrame videoFrame(frame);
    AkPacket videoPacket;

    if (qtFmtToAkFmt->contains(videoFrame.pixelFormat())) {
        AkVideoCaps videoCaps(qtFmtToAkFmt->value(videoFrame.pixelFormat()),
                              videoFrame.width(),
                              videoFrame.height(),
                              this->m_fps);
        AkVideoPacket packet(videoCaps);
        packet.setPts(videoFrame.startTime());
        packet.setDuration(videoFrame.endTime() - videoFrame.startTime());
        packet.setTimeBase({1, 1000000});
        packet.setIndex(0);
        packet.setId(this->m_id);

        videoFrame.map(QVideoFrame::ReadOnly);

        for (int plane = 0; plane < packet.planes(); ++plane) {
            auto line = videoFrame.bits(plane);
            auto srcLineSize = videoFrame.bytesPerLine(plane);
            auto lineSize = qMin<size_t>(packet.lineSize(plane), srcLineSize);
            auto heightDiv = packet.heightDiv(plane);

            for (int y = 0; y < videoFrame.height(); ++y) {
                auto ys = y >> heightDiv;
                memcpy(packet.line(plane, y),
                       line + ys * srcLineSize,
                       lineSize);
            }
        }

        videoFrame.unmap();
        videoPacket = packet;
    } else if (qtCompressedFmtToAkFmt->contains(videoFrame.pixelFormat())) {
        auto image = videoFrame.toImage();

        if (imageToAkFormat->contains(image.format())) {
            AkVideoCaps videoCaps(imageToAkFormat->value(image.format()),
                                  videoFrame.width(),
                                  videoFrame.height(),
                                  this->m_fps);
            AkVideoPacket packet(videoCaps);
            packet.setPts(videoFrame.startTime());
            packet.setDuration(videoFrame.endTime() - videoFrame.startTime());
            packet.setTimeBase({1, 1000000});
            packet.setIndex(0);
            packet.setId(this->m_id);

            auto lineSize = qMin<size_t>(packet.lineSize(0),
                                         image.bytesPerLine());

            for (int y = 0; y < videoFrame.height(); ++y)
                memcpy(packet.line(0, y),
                       image.constScanLine(y),
                       lineSize);

            videoPacket = packet;
        }
    }

    if (videoPacket) {
        auto angle = this->cameraRotation(frame);

        if (this->m_rotateFilter)
            this->m_rotateFilter->setProperty("angle", angle);

        this->m_frameMutex.lockForWrite();
        this->m_videoPacket =
            this->m_rotateFilter?
                this->m_rotateFilter->iStream(videoPacket):
                videoPacket;
        this->m_packetReady.wakeAll();
        this->m_frameMutex.unlock();
    }
}

void CaptureQtPrivate::updateDevices()
{
    decltype(this->m_devices) devices;
    decltype(this->m_descriptions) descriptions;
    decltype(this->m_devicesCaps) devicesCaps;

    for (auto &cameraDevice: QMediaDevices::videoInputs()) {
        QVector<AkVideoCaps> rawFormats;
        QVector<AkCompressedVideoCaps> compressedFormats;

        for (auto &format: cameraDevice.videoFormats()) {
            QVector<AkFrac> frameRates;

            if (format.maxFrameRate() == format.minFrameRate()) {
                frameRates << AkFrac(qRound(format.maxFrameRate()), 1);
            } else {
                static const int nFrameRates = 4;

                for (int i = 0; i < nFrameRates; i++) {
                    auto frameRate =
                        i
                        * (format.minFrameRate() - format.maxFrameRate())
                        / (nFrameRates - 1) + format.maxFrameRate();
                    AkFrac fps(qRound(frameRate), 1);

                    if (!frameRates.contains(fps))
                        frameRates << fps;
                }
            }

            if (frameRates.isEmpty())
                frameRates << AkFrac(30, 1);

            for (auto &fps: frameRates)
                if (qtFmtToAkFmt->contains(format.pixelFormat())) {
                    AkVideoCaps videoCaps(qtFmtToAkFmt->value(format.pixelFormat(), AkVideoCaps::Format_none),
                                          format.resolution().width(),
                                          format.resolution().height(),
                                          fps);
                    rawFormats << videoCaps;
                } else if (qtCompressedFmtToAkFmt->contains(format.pixelFormat())) {
                    AkCompressedVideoCaps videoCaps(qtCompressedFmtToAkFmt->value(format.pixelFormat(),
                                                                                  AkCompressedVideoCaps::VideoCodecID_unknown),
                                                    {AkVideoCaps::Format_yuv420p,
                                                     format.resolution().width(),
                                                     format.resolution().height(),
                                                     fps});
                    compressedFormats << videoCaps;
                }
        }

        std::sort(rawFormats.begin(), rawFormats.begin());
        std::sort(compressedFormats.begin(), compressedFormats.begin());
        AkCapsList caps;

        for (auto &format: compressedFormats)
            caps << format;

        for (auto &format: rawFormats)
            caps << format;

        if (!caps.isEmpty()) {
            auto index =
                    Capture::nearestResolution({DEFAULT_FRAME_WIDTH,
                                                DEFAULT_FRAME_HEIGHT},
                                               DEFAULT_FRAME_FPS,
                                               caps);

            if (index > 0)
                caps.move(index, 0);

            auto deviceId = cameraDevice.id();
            devices << deviceId;
            descriptions[deviceId] = cameraDevice.description();
            devicesCaps[deviceId] = caps;
        }
    }

    if (devicesCaps.isEmpty()) {
        devices.clear();
        descriptions.clear();
    }

    this->m_descriptions = descriptions;
    this->m_devicesCaps = devicesCaps;

    if (this->m_devices != devices) {
        this->m_devices = devices;
        emit self->webcamsChanged(this->m_devices);
    }
}

#include "moc_captureqt.cpp"
