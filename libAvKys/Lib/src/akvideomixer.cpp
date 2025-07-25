/* Webcamoid, webcam capture application.
 * Copyright (C) 2022  Gonzalo Exequiel Pedone
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

#ifdef OPENMP_ENABLED
#include <omp.h>
#endif

#include <QQmlEngine>

#include "akalgorithm.h"
#include "akcpufeatures.h"
#include "akfrac.h"
#include "aksimd.h"
#include "akvideocaps.h"
#include "akvideoformatspec.h"
#include "akvideomixer.h"
#include "akvideopacket.h"

enum DrawType
{
    DrawType_1_component,
    DrawType_3_components,
};

enum DrawDataTypes
{
    DrawDataTypes_8,
    DrawDataTypes_16,
    DrawDataTypes_32,
};

// Optimized fumctions for un-packed formats

using CreateDrawParametersType =
    void *(*)();
using FreeDrawParametersType =
    void (*)(void *drawParameters);
using DrawSIMDFast8bits1AType =
    void (*)(void *drawParameters,
             int oWidth,
             const int *srcWidthOffsetX,
             const int *srcWidthOffsetA,
             const int *dstWidthOffsetX,
             const int *dstWidthOffsetA,
             const quint8 *src_line_x,
             const quint8 *src_line_a,
             quint8 *dst_line_x,
             quint8 *dst_line_a,
             int *x);
using DrawSIMDFast8bits3AType =
    void (*)(void *drawParameters,
             int oWidth,
             const int *srcWidthOffsetX,
             const int *srcWidthOffsetY,
             const int *srcWidthOffsetZ,
             const int *srcWidthOffsetA,
             const int *dstWidthOffsetX,
             const int *dstWidthOffsetY,
             const int *dstWidthOffsetZ,
             const int *dstWidthOffsetA,
             const quint8 *src_line_x,
             const quint8 *src_line_y,
             const quint8 *src_line_z,
             const quint8 *src_line_a,
             quint8 *dst_line_x,
             quint8 *dst_line_y,
             quint8 *dst_line_z,
             quint8 *dst_line_a,
             int *x);
using DrawSIMDFastLc8bits1AType =
    void (*)(void *drawParameters,
             int oWidth,
             int iDiffX,
             int oDiffX,
             int oMultX,
             size_t xiWidthDiv,
             size_t aiWidthDiv,
             size_t xiStep,
             size_t aiStep,
             const quint8 *src_line_x,
             const quint8 *src_line_a,
             quint8 *dst_line_x,
             quint8 *dst_line_a,
             int *x);
using DrawSIMDFastLc8bits3AType =
    void (*)(void *drawParameters,
             int oWidth,
             int iDiffX,
             int oDiffX,
             int oMultX,
             size_t xiWidthDiv,
             size_t yiWidthDiv,
             size_t ziWidthDiv,
             size_t aiWidthDiv,
             size_t xiStep,
             size_t yiStep,
             size_t ziStep,
             size_t aiStep,
             const quint8 *src_line_x,
             const quint8 *src_line_y,
             const quint8 *src_line_z,
             const quint8 *src_line_a,
             quint8 *dst_line_x,
             quint8 *dst_line_y,
             quint8 *dst_line_z,
             quint8 *dst_line_a,
             int *x);

class CommonDrawParameters
{
    public:
        AkVideoCaps outputCaps;

        AkVideoMixer::MixerFlags flags {AkVideoMixer::MixerFlagNone};
        bool lightweightCache {false};
        DrawType drawType {DrawType_1_component};
        DrawDataTypes drawDataTypes {DrawDataTypes_8};
        bool fastDraw {false};
        bool optimizedFor8bits {false};
        bool fastFormat {false};

        int endianness {Q_BYTE_ORDER};

        int planeXi {0};
        int planeYi {0};
        int planeZi {0};
        int planeAi {0};

        AkColorComponent compXi;
        AkColorComponent compYi;
        AkColorComponent compZi;
        AkColorComponent compAi;

        size_t xiOffset {0};
        size_t yiOffset {0};
        size_t ziOffset {0};
        size_t aiOffset {0};

        size_t xiShift {0};
        size_t yiShift {0};
        size_t ziShift {0};
        size_t aiShift {0};

        size_t xiStep {0};
        size_t yiStep {0};
        size_t ziStep {0};
        size_t aiStep {0};

        size_t xiWidthDiv {0};
        size_t yiWidthDiv {0};
        size_t ziWidthDiv {0};
        size_t aiWidthDiv {0};

        quint64 maxXi {0};
        quint64 maxYi {0};
        quint64 maxZi {0};
        quint64 maxAi {0};
        quint64 maxAi2 {0};

        quint64 maskXo {0};
        quint64 maskYo {0};
        quint64 maskZo {0};
        quint64 maskAo {0};

        size_t depthAi {0};
        qint64 *aiMultTable {nullptr};
        qint64 *aoMultTable {nullptr};
        qint64 *alphaTable {nullptr};

        void *simdDrawParameters {nullptr};

        CreateDrawParametersType  createSIMDDrawParameters {nullptr};
        FreeDrawParametersType    freeSIMDDrawParameters   {nullptr};
        DrawSIMDFast8bits1AType   drawSIMDFast8bits1A      {nullptr};
        DrawSIMDFast8bits3AType   drawSIMDFast8bits3A      {nullptr};
        DrawSIMDFastLc8bits1AType drawSIMDFastLc8bits1A    {nullptr};
        DrawSIMDFastLc8bits3AType drawSIMDFastLc8bits3A    {nullptr};

        size_t parallelizationThreshold {0};

        CommonDrawParameters();
        CommonDrawParameters(const CommonDrawParameters &other);
        ~CommonDrawParameters();
        CommonDrawParameters &operator =(const CommonDrawParameters &other);
        inline void allocateBuffers(size_t alphaDepth);
        inline void clearBuffers();
        void configure(const AkVideoCaps &caps);
        void reset();
};

class DrawParameters
{
    public:
        AkVideoCaps inputCaps;
        AkVideoCaps outputCaps;

        bool canDraw {false};

        int x {0};
        int y {0};

        int iX {0};
        int iY {0};
        int iWidth {0};
        int iHeight {0};

        int oX {0};
        int oY {0};
        int oWidth {0};
        int oHeight {0};

        int iDiffX {0};
        int iDiffY {0};
        int oDiffX {0};
        int oDiffY {0};
        int oMultX {0};
        int oMultY {0};

        int *srcWidthOffsetX {nullptr};
        int *srcWidthOffsetY {nullptr};
        int *srcWidthOffsetZ {nullptr};
        int *srcWidthOffsetA {nullptr};
        int *srcHeight {nullptr};

        int *dstWidthOffsetX {nullptr};
        int *dstWidthOffsetY {nullptr};
        int *dstWidthOffsetZ {nullptr};
        int *dstWidthOffsetA {nullptr};

        bool paralelize {false};

        DrawParameters();
        DrawParameters(const DrawParameters &other);
        ~DrawParameters();
        DrawParameters &operator =(const DrawParameters &other);
        inline void allocateBuffers(const AkVideoCaps &caps);
        inline void clearBuffers();
        void configure(int x, int y,
                       const AkVideoCaps &icaps,
                       const AkVideoCaps &ocaps, const CommonDrawParameters &cdp);
        void reset();
};

class AkVideoMixerPrivate
{
    public:
        AkVideoMixer::MixerFlags m_flags {AkVideoMixer::MixerFlagNone};
        AkVideoPacket *m_baseFrame {nullptr};
        CommonDrawParameters m_cdp;
        DrawParameters *m_dp {nullptr};
        size_t m_dpSize {0};
        int m_cacheIndex {0};

        /* Drawing functions */

        template <typename DataType>
        void draw8bits3A(const DrawParameters &dp,
                         const AkVideoPacket &src,
                         AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto &ys = dp.srcHeight[y];

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_y = src.constLine(this->m_cdp.planeYi, ys) + this->m_cdp.yiOffset;
                auto src_line_z = src.constLine(this->m_cdp.planeZi, ys) + this->m_cdp.ziOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_y = dst.line(this->m_cdp.planeYi, y) + this->m_cdp.yiOffset;
                auto dst_line_z = dst.line(this->m_cdp.planeZi, y) + this->m_cdp.ziOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                #pragma omp simd if(dp.paralelize)
                for (int x = dp.oX; x < dp.oWidth; ++x) {
                    int &xs_x = dp.srcWidthOffsetX[x];
                    int &xs_y = dp.srcWidthOffsetY[x];
                    int &xs_z = dp.srcWidthOffsetZ[x];
                    int &xs_a = dp.srcWidthOffsetA[x];

                    auto xi = *reinterpret_cast<const DataType *>(src_line_x + xs_x);
                    auto yi = *reinterpret_cast<const DataType *>(src_line_y + xs_y);
                    auto zi = *reinterpret_cast<const DataType *>(src_line_z + xs_z);
                    auto ai = *reinterpret_cast<const DataType *>(src_line_a + xs_a);

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xi = AkAlgorithm::swapBytes(DataType(xi));
                        yi = AkAlgorithm::swapBytes(DataType(yi));
                        zi = AkAlgorithm::swapBytes(DataType(zi));
                        ai = AkAlgorithm::swapBytes(DataType(ai));
                    }

                    xi = (xi >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    yi = (yi >> this->m_cdp.yiShift) & this->m_cdp.maxYi;
                    zi = (zi >> this->m_cdp.ziShift) & this->m_cdp.maxZi;
                    ai = (ai >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    int &xd_x = dp.dstWidthOffsetX[x];
                    int &xd_y = dp.dstWidthOffsetY[x];
                    int &xd_z = dp.dstWidthOffsetZ[x];
                    int &xd_a = dp.dstWidthOffsetA[x];

                    auto xop = reinterpret_cast<DataType *>(dst_line_x + xd_x);
                    auto yop = reinterpret_cast<DataType *>(dst_line_y + xd_y);
                    auto zop = reinterpret_cast<DataType *>(dst_line_z + xd_z);
                    auto aop = reinterpret_cast<DataType *>(dst_line_a + xd_a);

                    DataType xo = 0;
                    DataType yo = 0;
                    DataType zo = 0;
                    DataType ao = 0;

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xo = AkAlgorithm::swapBytes(DataType(*xop));
                        yo = AkAlgorithm::swapBytes(DataType(*yop));
                        zo = AkAlgorithm::swapBytes(DataType(*zop));
                        ao = AkAlgorithm::swapBytes(DataType(*aop));
                    } else {
                        xo = *xop;
                        yo = *yop;
                        zo = *zop;
                        ao = *aop;
                    }

                    xo = (xo >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    yo = (yo >> this->m_cdp.yiShift) & this->m_cdp.maxYi;
                    zo = (zo >> this->m_cdp.ziShift) & this->m_cdp.maxZi;
                    ao = (ao >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    auto alphaMask = (size_t(ai) << this->m_cdp.depthAi) | size_t(ao);
                    qint64 xt = (qint64(xi) * this->m_cdp.aiMultTable[alphaMask] + qint64(xo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    qint64 yt = (qint64(yi) * this->m_cdp.aiMultTable[alphaMask] + qint64(yo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    qint64 zt = (qint64(zi) * this->m_cdp.aiMultTable[alphaMask] + qint64(zo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    qint64 &at = this->m_cdp.alphaTable[alphaMask];

                    *xop = (*xop & DataType(this->m_cdp.maskXo)) | (DataType(xt) << this->m_cdp.xiShift);
                    *yop = (*yop & DataType(this->m_cdp.maskYo)) | (DataType(yt) << this->m_cdp.yiShift);
                    *zop = (*zop & DataType(this->m_cdp.maskZo)) | (DataType(zt) << this->m_cdp.ziShift);
                    *aop = (*aop & DataType(this->m_cdp.maskAo)) | (DataType(at) << this->m_cdp.aiShift);
                }
            }
        }

        void drawFast8bits3A(const DrawParameters &dp,
                             const AkVideoPacket &src,
                             AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto &ys = dp.srcHeight[y];

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_y = src.constLine(this->m_cdp.planeYi, ys) + this->m_cdp.yiOffset;
                auto src_line_z = src.constLine(this->m_cdp.planeZi, ys) + this->m_cdp.ziOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_y = dst.line(this->m_cdp.planeYi, y) + this->m_cdp.yiOffset;
                auto dst_line_z = dst.line(this->m_cdp.planeZi, y) + this->m_cdp.ziOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                int x = dp.oX;

                if (this->m_cdp.drawSIMDFast8bits3A) {
                    this->m_cdp.drawSIMDFast8bits3A(this->m_cdp.simdDrawParameters,
                                                    dp.oWidth,
                                                    dp.srcWidthOffsetX,
                                                    dp.srcWidthOffsetY,
                                                    dp.srcWidthOffsetZ,
                                                    dp.srcWidthOffsetA,
                                                    dp.dstWidthOffsetX,
                                                    dp.dstWidthOffsetY,
                                                    dp.dstWidthOffsetZ,
                                                    dp.dstWidthOffsetA,
                                                    src_line_x,
                                                    src_line_y,
                                                    src_line_z,
                                                    src_line_a,
                                                    dst_line_x,
                                                    dst_line_y,
                                                    dst_line_z,
                                                    dst_line_a,
                                                    &x);
                }

                #pragma omp simd if(dp.paralelize)
                for (int i = x; i < dp.oWidth; ++i) {
                    auto &xi = src_line_x[dp.srcWidthOffsetX[i]];
                    auto &yi = src_line_y[dp.srcWidthOffsetY[i]];
                    auto &zi = src_line_z[dp.srcWidthOffsetZ[i]];
                    auto &ai = src_line_a[dp.srcWidthOffsetA[i]];

                    auto &xo = dst_line_x[dp.dstWidthOffsetX[i]];
                    auto &yo = dst_line_y[dp.dstWidthOffsetY[i]];
                    auto &zo = dst_line_z[dp.dstWidthOffsetZ[i]];
                    auto &ao = dst_line_a[dp.dstWidthOffsetA[i]];

                    auto alphaMask = (size_t(ai) << this->m_cdp.depthAi) | size_t(ao);
                    xo = (qint64(xi) * this->m_cdp.aiMultTable[alphaMask] + qint64(xo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    yo = (qint64(yi) * this->m_cdp.aiMultTable[alphaMask] + qint64(yo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    zo = (qint64(zi) * this->m_cdp.aiMultTable[alphaMask] + qint64(zo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    ao = qint8(this->m_cdp.alphaTable[alphaMask]);
                }
            }
        }

        template <typename DataType>
        void draw3A(const DrawParameters &dp,
                    const AkVideoPacket &src,
                    AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto &ys = dp.srcHeight[y];

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_y = src.constLine(this->m_cdp.planeYi, ys) + this->m_cdp.yiOffset;
                auto src_line_z = src.constLine(this->m_cdp.planeZi, ys) + this->m_cdp.ziOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_y = dst.line(this->m_cdp.planeYi, y) + this->m_cdp.yiOffset;
                auto dst_line_z = dst.line(this->m_cdp.planeZi, y) + this->m_cdp.ziOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                #pragma omp simd if(dp.paralelize)
                for (int x = dp.oX; x < dp.oWidth; ++x) {
                    int &xs_x = dp.srcWidthOffsetX[x];
                    int &xs_y = dp.srcWidthOffsetY[x];
                    int &xs_z = dp.srcWidthOffsetZ[x];
                    int &xs_a = dp.srcWidthOffsetA[x];

                    auto xi = *reinterpret_cast<const DataType *>(src_line_x + xs_x);
                    auto yi = *reinterpret_cast<const DataType *>(src_line_y + xs_y);
                    auto zi = *reinterpret_cast<const DataType *>(src_line_z + xs_z);
                    auto ai = *reinterpret_cast<const DataType *>(src_line_a + xs_a);

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xi = AkAlgorithm::swapBytes(DataType(xi));
                        yi = AkAlgorithm::swapBytes(DataType(yi));
                        zi = AkAlgorithm::swapBytes(DataType(zi));
                        ai = AkAlgorithm::swapBytes(DataType(ai));
                    }

                    xi = (xi >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    yi = (yi >> this->m_cdp.yiShift) & this->m_cdp.maxYi;
                    zi = (zi >> this->m_cdp.ziShift) & this->m_cdp.maxZi;
                    ai = (ai >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    int &xd_x = dp.dstWidthOffsetX[x];
                    int &xd_y = dp.dstWidthOffsetY[x];
                    int &xd_z = dp.dstWidthOffsetZ[x];
                    int &xd_a = dp.dstWidthOffsetA[x];

                    auto xop = reinterpret_cast<DataType *>(dst_line_x + xd_x);
                    auto yop = reinterpret_cast<DataType *>(dst_line_y + xd_y);
                    auto zop = reinterpret_cast<DataType *>(dst_line_z + xd_z);
                    auto aop = reinterpret_cast<DataType *>(dst_line_a + xd_a);

                    DataType xo = 0;
                    DataType yo = 0;
                    DataType zo = 0;
                    DataType ao = 0;

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xo = AkAlgorithm::swapBytes(DataType(*xop));
                        yo = AkAlgorithm::swapBytes(DataType(*yop));
                        zo = AkAlgorithm::swapBytes(DataType(*zop));
                        ao = AkAlgorithm::swapBytes(DataType(*aop));
                    } else {
                        xo = *xop;
                        yo = *yop;
                        zo = *zop;
                        ao = *aop;
                    }

                    xo = (xo >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    yo = (yo >> this->m_cdp.yiShift) & this->m_cdp.maxYi;
                    zo = (zo >> this->m_cdp.ziShift) & this->m_cdp.maxZi;
                    ao = (ao >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    auto diffAi = qint64(this->m_cdp.maxAi) - qint64(ai);
                    auto a = qint64(this->m_cdp.maxAi2) - (qint64(this->m_cdp.maxAi) - qint64(ao)) * diffAi;

                    qint64 xt = 0;
                    qint64 yt = 0;
                    qint64 zt = 0;
                    qint64 at = 0;

                    if (a != 0) {
                        auto mi = qint64(ai) * qint64(this->m_cdp.maxAi);
                        auto mo = qint64(ao) * diffAi;
                        xt = (qint64(xi) * mi + qint64(xo) * mo) / a;
                        yt = (qint64(yi) * mi + qint64(yo) * mo) / a;
                        zt = (qint64(zi) * mi + qint64(zo) * mo) / a;
                        at = a / qint64(this->m_cdp.maxAi);
                    }

                    *xop = (*xop & DataType(this->m_cdp.maskXo)) | (DataType(xt) << this->m_cdp.xiShift);
                    *yop = (*yop & DataType(this->m_cdp.maskYo)) | (DataType(yt) << this->m_cdp.yiShift);
                    *zop = (*zop & DataType(this->m_cdp.maskZo)) | (DataType(zt) << this->m_cdp.ziShift);
                    *aop = (*aop & DataType(this->m_cdp.maskAo)) | (DataType(at) << this->m_cdp.aiShift);
                }
            }
        }

        template <typename DataType>
        void draw8bits1A(const DrawParameters &dp,
                         const AkVideoPacket &src,
                         AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto &ys = dp.srcHeight[y];

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                #pragma omp simd if(dp.paralelize)
                for (int x = dp.oX; x < dp.oWidth; ++x) {
                    int &xs_x = dp.srcWidthOffsetX[x];
                    int &xs_a = dp.srcWidthOffsetA[x];

                    auto xi = *reinterpret_cast<const DataType *>(src_line_x + xs_x);
                    auto ai = *reinterpret_cast<const DataType *>(src_line_a + xs_a);

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xi = AkAlgorithm::swapBytes(DataType(xi));
                        ai = AkAlgorithm::swapBytes(DataType(ai));
                    }

                    xi = (xi >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    ai = (ai >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    int &xd_x = dp.dstWidthOffsetX[x];
                    int &xd_a = dp.dstWidthOffsetA[x];

                    auto xop = reinterpret_cast<DataType *>(dst_line_x + xd_x);
                    auto aop = reinterpret_cast<DataType *>(dst_line_a + xd_a);

                    DataType xo = 0;
                    DataType ao = 0;

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xo = AkAlgorithm::swapBytes(DataType(*xop));
                        ao = AkAlgorithm::swapBytes(DataType(*aop));
                    } else {
                        xo = *xop;
                        ao = *aop;
                    }

                    xo = (xo >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    ao = (ao >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    auto alphaMask = (size_t(ai) << this->m_cdp.depthAi) | size_t(ao);
                    qint64 xt = (qint64(xi) * this->m_cdp.aiMultTable[alphaMask] + qint64(xo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    qint64 &at = this->m_cdp.alphaTable[alphaMask];

                    *xop = (*xop & DataType(this->m_cdp.maskXo)) | (DataType(xt) << this->m_cdp.xiShift);
                    *aop = (*aop & DataType(this->m_cdp.maskAo)) | (DataType(at) << this->m_cdp.aiShift);
                }
            }
        }

        void drawFast8bits1A(const DrawParameters &dp,
                             const AkVideoPacket &src,
                             AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto &ys = dp.srcHeight[y];

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                int x = dp.oX;

                if (this->m_cdp.drawSIMDFast8bits1A) {
                    this->m_cdp.drawSIMDFast8bits1A(this->m_cdp.simdDrawParameters,
                                                    dp.oWidth,
                                                    dp.srcWidthOffsetX,
                                                    dp.srcWidthOffsetA,
                                                    dp.dstWidthOffsetX,
                                                    dp.dstWidthOffsetA,
                                                    src_line_x,
                                                    src_line_a,
                                                    dst_line_x,
                                                    dst_line_a,
                                                    &x);
                }

                #pragma omp simd if(dp.paralelize)
                for (int i = x; i < dp.oWidth; ++i) {
                    auto &xi = src_line_x[dp.srcWidthOffsetX[i]];
                    auto &ai = src_line_a[dp.srcWidthOffsetA[i]];

                    auto &xo = dst_line_x[dp.dstWidthOffsetX[i]];
                    auto &ao = dst_line_a[dp.dstWidthOffsetA[i]];

                    auto alphaMask = (size_t(ai) << this->m_cdp.depthAi) | size_t(ao);
                    xo = (qint64(xi) * this->m_cdp.aiMultTable[alphaMask] + qint64(xo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    ao = qint8(this->m_cdp.alphaTable[alphaMask]);
                }
            }
        }

        template <typename DataType>
        void draw1A(const DrawParameters &dp,
                    const AkVideoPacket &src,
                    AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto &ys = dp.srcHeight[y];

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                #pragma omp simd if(dp.paralelize)
                for (int x = dp.oX; x < dp.oWidth; ++x) {
                    int &xs_x = dp.srcWidthOffsetX[x];
                    int &xs_a = dp.srcWidthOffsetA[x];

                    auto xi = *reinterpret_cast<const DataType *>(src_line_x + xs_x);
                    auto ai = *reinterpret_cast<const DataType *>(src_line_a + xs_a);

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xi = AkAlgorithm::swapBytes(DataType(xi));
                        ai = AkAlgorithm::swapBytes(DataType(ai));
                    }

                    xi = (xi >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    ai = (ai >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    int &xd_x = dp.dstWidthOffsetX[x];
                    int &xd_a = dp.dstWidthOffsetA[x];

                    auto xop = reinterpret_cast<DataType *>(dst_line_x + xd_x);
                    auto aop = reinterpret_cast<DataType *>(dst_line_a + xd_a);

                    DataType xo = 0;
                    DataType ao = 0;

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xo = AkAlgorithm::swapBytes(DataType(*xop));
                        ao = AkAlgorithm::swapBytes(DataType(*aop));
                    } else {
                        xo = *xop;
                        ao = *aop;
                    }

                    xo = (xo >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    ao = (ao >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    auto diffAi = qint64(this->m_cdp.maxAi) - qint64(ai);
                    auto a = qint64(this->m_cdp.maxAi2) - (qint64(this->m_cdp.maxAi) - qint64(ao)) * diffAi;

                    qint64 xt = 0;
                    qint64 at = 0;

                    if (a != 1) {
                        auto mi = qint64(ai) * qint64(this->m_cdp.maxAi);
                        auto mo = qint64(ao) * diffAi;
                        xt = (qint64(xi) * mi + qint64(xo) * mo) / a;
                        at = a / qint64(this->m_cdp.maxAi);
                    }

                    *xop = (*xop & DataType(this->m_cdp.maskXo)) | (DataType(xt) << this->m_cdp.xiShift);
                    *aop = (*aop & DataType(this->m_cdp.maskAo)) | (DataType(at) << this->m_cdp.aiShift);
                }
            }
        }

        /* Lightweight cache drawing functions */

        template <typename DataType>
        void drawLc8bits3A(const DrawParameters &dp,
                           const AkVideoPacket &src,
                           AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto ys = (y * dp.iDiffY + dp.oMultY) / dp.oDiffY;

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_y = src.constLine(this->m_cdp.planeYi, ys) + this->m_cdp.yiOffset;
                auto src_line_z = src.constLine(this->m_cdp.planeZi, ys) + this->m_cdp.ziOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_y = dst.line(this->m_cdp.planeYi, y) + this->m_cdp.yiOffset;
                auto dst_line_z = dst.line(this->m_cdp.planeZi, y) + this->m_cdp.ziOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                #pragma omp simd if(dp.paralelize)
                for (int x = dp.oX; x < dp.oWidth; ++x) {
                    auto xs = (x * dp.iDiffX + dp.oMultX) / dp.oDiffX;

                    int xs_x = (xs >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xs_y = (xs >> this->m_cdp.yiWidthDiv) * this->m_cdp.yiStep;
                    int xs_z = (xs >> this->m_cdp.ziWidthDiv) * this->m_cdp.ziStep;
                    int xs_a = (xs >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto xi = *reinterpret_cast<const DataType *>(src_line_x + xs_x);
                    auto yi = *reinterpret_cast<const DataType *>(src_line_y + xs_y);
                    auto zi = *reinterpret_cast<const DataType *>(src_line_z + xs_z);
                    auto ai = *reinterpret_cast<const DataType *>(src_line_a + xs_a);

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xi = AkAlgorithm::swapBytes(DataType(xi));
                        yi = AkAlgorithm::swapBytes(DataType(yi));
                        zi = AkAlgorithm::swapBytes(DataType(zi));
                        ai = AkAlgorithm::swapBytes(DataType(ai));
                    }

                    xi = (xi >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    yi = (yi >> this->m_cdp.yiShift) & this->m_cdp.maxYi;
                    zi = (zi >> this->m_cdp.ziShift) & this->m_cdp.maxZi;
                    ai = (ai >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    int xd_x = (x >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xd_y = (x >> this->m_cdp.yiWidthDiv) * this->m_cdp.yiStep;
                    int xd_z = (x >> this->m_cdp.ziWidthDiv) * this->m_cdp.ziStep;
                    int xd_a = (x >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto xop = reinterpret_cast<DataType *>(dst_line_x + xd_x);
                    auto yop = reinterpret_cast<DataType *>(dst_line_y + xd_y);
                    auto zop = reinterpret_cast<DataType *>(dst_line_z + xd_z);
                    auto aop = reinterpret_cast<DataType *>(dst_line_a + xd_a);

                    DataType xo = 0;
                    DataType yo = 0;
                    DataType zo = 0;
                    DataType ao = 0;

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xo = AkAlgorithm::swapBytes(DataType(*xop));
                        yo = AkAlgorithm::swapBytes(DataType(*yop));
                        zo = AkAlgorithm::swapBytes(DataType(*zop));
                        ao = AkAlgorithm::swapBytes(DataType(*aop));
                    } else {
                        xo = *xop;
                        yo = *yop;
                        zo = *zop;
                        ao = *aop;
                    }

                    xo = (xo >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    yo = (yo >> this->m_cdp.yiShift) & this->m_cdp.maxYi;
                    zo = (zo >> this->m_cdp.ziShift) & this->m_cdp.maxZi;
                    ao = (ao >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    auto alphaMask = (size_t(ai) << this->m_cdp.depthAi) | size_t(ao);
                    qint64 xt = (qint64(xi) * this->m_cdp.aiMultTable[alphaMask] + qint64(xo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    qint64 yt = (qint64(yi) * this->m_cdp.aiMultTable[alphaMask] + qint64(yo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    qint64 zt = (qint64(zi) * this->m_cdp.aiMultTable[alphaMask] + qint64(zo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    qint64 &at = this->m_cdp.alphaTable[alphaMask];

                    *xop = (*xop & DataType(this->m_cdp.maskXo)) | (DataType(xt) << this->m_cdp.xiShift);
                    *yop = (*yop & DataType(this->m_cdp.maskYo)) | (DataType(yt) << this->m_cdp.yiShift);
                    *zop = (*zop & DataType(this->m_cdp.maskZo)) | (DataType(zt) << this->m_cdp.ziShift);
                    *aop = (*aop & DataType(this->m_cdp.maskAo)) | (DataType(at) << this->m_cdp.aiShift);
                }
            }
        }

        void drawFastLc8bits3A(const DrawParameters &dp,
                               const AkVideoPacket &src,
                               AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto ys = (y * dp.iDiffY + dp.oMultY) / dp.oDiffY;

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_y = src.constLine(this->m_cdp.planeYi, ys) + this->m_cdp.yiOffset;
                auto src_line_z = src.constLine(this->m_cdp.planeZi, ys) + this->m_cdp.ziOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_y = dst.line(this->m_cdp.planeYi, y) + this->m_cdp.yiOffset;
                auto dst_line_z = dst.line(this->m_cdp.planeZi, y) + this->m_cdp.ziOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                int x = dp.oX;

                if (this->m_cdp.drawSIMDFastLc8bits3A)
                    this->m_cdp.drawSIMDFastLc8bits3A(this->m_cdp.simdDrawParameters,
                                                      dp.oWidth,
                                                      dp.iDiffX,
                                                      dp.oDiffX,
                                                      dp.oMultX,
                                                      this->m_cdp.xiWidthDiv,
                                                      this->m_cdp.yiWidthDiv,
                                                      this->m_cdp.ziWidthDiv,
                                                      this->m_cdp.aiWidthDiv,
                                                      this->m_cdp.xiStep,
                                                      this->m_cdp.yiStep,
                                                      this->m_cdp.ziStep,
                                                      this->m_cdp.aiStep,
                                                      src_line_x,
                                                      src_line_y,
                                                      src_line_z,
                                                      src_line_a,
                                                      dst_line_x,
                                                      dst_line_y,
                                                      dst_line_z,
                                                      dst_line_a,
                                                      &x);

                #pragma omp simd if(dp.paralelize)
                for (int i = x; i < dp.oWidth; ++i) {
                    auto xs = (i * dp.iDiffX + dp.oMultX) / dp.oDiffX;

                    int xs_x = (xs >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xs_y = (xs >> this->m_cdp.yiWidthDiv) * this->m_cdp.yiStep;
                    int xs_z = (xs >> this->m_cdp.ziWidthDiv) * this->m_cdp.ziStep;
                    int xs_a = (xs >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto &xi = src_line_x[xs_x];
                    auto &yi = src_line_y[xs_y];
                    auto &zi = src_line_z[xs_z];
                    auto &ai = src_line_a[xs_a];

                    int xd_x = (i >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xd_y = (i >> this->m_cdp.yiWidthDiv) * this->m_cdp.yiStep;
                    int xd_z = (i >> this->m_cdp.ziWidthDiv) * this->m_cdp.ziStep;
                    int xd_a = (i >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto &xo = dst_line_x[xd_x];
                    auto &yo = dst_line_y[xd_y];
                    auto &zo = dst_line_z[xd_z];
                    auto &ao = dst_line_a[xd_a];

                    auto alphaMask = (size_t(ai) << this->m_cdp.depthAi) | size_t(ao);
                    xo = (qint64(xi) * this->m_cdp.aiMultTable[alphaMask] + qint64(xo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    yo = (qint64(yi) * this->m_cdp.aiMultTable[alphaMask] + qint64(yo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    zo = (qint64(zi) * this->m_cdp.aiMultTable[alphaMask] + qint64(zo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    ao = qint8(this->m_cdp.alphaTable[alphaMask]);
                }
            }
        }

        template <typename DataType>
        void drawLc3A(const DrawParameters &dp,
                      const AkVideoPacket &src,
                      AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto ys = (y * dp.iDiffY + dp.oMultY) / dp.oDiffY;

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_y = src.constLine(this->m_cdp.planeYi, ys) + this->m_cdp.yiOffset;
                auto src_line_z = src.constLine(this->m_cdp.planeZi, ys) + this->m_cdp.ziOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_y = dst.line(this->m_cdp.planeYi, y) + this->m_cdp.yiOffset;
                auto dst_line_z = dst.line(this->m_cdp.planeZi, y) + this->m_cdp.ziOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                #pragma omp simd if(dp.paralelize)
                for (int x = dp.oX; x < dp.oWidth; ++x) {
                    auto xs = (x * dp.iDiffX + dp.oMultX) / dp.oDiffX;

                    int xs_x = (xs >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xs_y = (xs >> this->m_cdp.yiWidthDiv) * this->m_cdp.yiStep;
                    int xs_z = (xs >> this->m_cdp.ziWidthDiv) * this->m_cdp.ziStep;
                    int xs_a = (xs >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto xi = *reinterpret_cast<const DataType *>(src_line_x + xs_x);
                    auto yi = *reinterpret_cast<const DataType *>(src_line_y + xs_y);
                    auto zi = *reinterpret_cast<const DataType *>(src_line_z + xs_z);
                    auto ai = *reinterpret_cast<const DataType *>(src_line_a + xs_a);

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xi = AkAlgorithm::swapBytes(DataType(xi));
                        yi = AkAlgorithm::swapBytes(DataType(yi));
                        zi = AkAlgorithm::swapBytes(DataType(zi));
                        ai = AkAlgorithm::swapBytes(DataType(ai));
                    }

                    xi = (xi >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    yi = (yi >> this->m_cdp.yiShift) & this->m_cdp.maxYi;
                    zi = (zi >> this->m_cdp.ziShift) & this->m_cdp.maxZi;
                    ai = (ai >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    int xd_x = (x >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xd_y = (x >> this->m_cdp.yiWidthDiv) * this->m_cdp.yiStep;
                    int xd_z = (x >> this->m_cdp.ziWidthDiv) * this->m_cdp.ziStep;
                    int xd_a = (x >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto xop = reinterpret_cast<DataType *>(dst_line_x + xd_x);
                    auto yop = reinterpret_cast<DataType *>(dst_line_y + xd_y);
                    auto zop = reinterpret_cast<DataType *>(dst_line_z + xd_z);
                    auto aop = reinterpret_cast<DataType *>(dst_line_a + xd_a);

                    DataType xo = 0;
                    DataType yo = 0;
                    DataType zo = 0;
                    DataType ao = 0;

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xo = AkAlgorithm::swapBytes(DataType(*xop));
                        yo = AkAlgorithm::swapBytes(DataType(*yop));
                        zo = AkAlgorithm::swapBytes(DataType(*zop));
                        ao = AkAlgorithm::swapBytes(DataType(*aop));
                    } else {
                        xo = *xop;
                        yo = *yop;
                        zo = *zop;
                        ao = *aop;
                    }

                    xo = (xo >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    yo = (yo >> this->m_cdp.yiShift) & this->m_cdp.maxYi;
                    zo = (zo >> this->m_cdp.ziShift) & this->m_cdp.maxZi;
                    ao = (ao >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    auto diffAi = qint64(this->m_cdp.maxAi) - qint64(ai);
                    auto a = qint64(this->m_cdp.maxAi2) - (qint64(this->m_cdp.maxAi) - qint64(ao)) * diffAi;

                    qint64 xt = 0;
                    qint64 yt = 0;
                    qint64 zt = 0;
                    qint64 at = 0;

                    if (a != 0) {
                        auto mi = qint64(ai) * qint64(this->m_cdp.maxAi);
                        auto mo = qint64(ao) * diffAi;
                        xt = (qint64(xi) * mi + qint64(xo) * mo) / a;
                        yt = (qint64(yi) * mi + qint64(yo) * mo) / a;
                        zt = (qint64(zi) * mi + qint64(zo) * mo) / a;
                        at = a / qint64(this->m_cdp.maxAi);
                    }

                    *xop = (*xop & DataType(this->m_cdp.maskXo)) | (DataType(xt) << this->m_cdp.xiShift);
                    *yop = (*yop & DataType(this->m_cdp.maskYo)) | (DataType(yt) << this->m_cdp.yiShift);
                    *zop = (*zop & DataType(this->m_cdp.maskZo)) | (DataType(zt) << this->m_cdp.ziShift);
                    *aop = (*aop & DataType(this->m_cdp.maskAo)) | (DataType(at) << this->m_cdp.aiShift);
                }
            }
        }

        template <typename DataType>
        void drawLc8bits1A(const DrawParameters &dp,
                           const AkVideoPacket &src,
                           AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto ys = (y * dp.iDiffY + dp.oMultY) / dp.oDiffY;

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                #pragma omp simd if(dp.paralelize)
                for (int x = dp.oX; x < dp.oWidth; ++x) {
                    auto xs = (x * dp.iDiffX + dp.oMultX) / dp.oDiffX;

                    int xs_x = (xs >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xs_a = (xs >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto xi = *reinterpret_cast<const DataType *>(src_line_x + xs_x);
                    auto ai = *reinterpret_cast<const DataType *>(src_line_a + xs_a);

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xi = AkAlgorithm::swapBytes(DataType(xi));
                        ai = AkAlgorithm::swapBytes(DataType(ai));
                    }

                    xi = (xi >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    ai = (ai >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    int xd_x = (x >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xd_a = (x >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto xop = reinterpret_cast<DataType *>(dst_line_x + xd_x);
                    auto aop = reinterpret_cast<DataType *>(dst_line_a + xd_a);

                    DataType xo = 0;
                    DataType ao = 0;

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xo = AkAlgorithm::swapBytes(DataType(*xop));
                        ao = AkAlgorithm::swapBytes(DataType(*aop));
                    } else {
                        xo = *xop;
                        ao = *aop;
                    }

                    xo = (xo >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    ao = (ao >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    auto alphaMask = (size_t(ai) << this->m_cdp.depthAi) | size_t(ao);
                    qint64 xt = (qint64(xi) * this->m_cdp.aiMultTable[alphaMask] + qint64(xo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    qint64 &at = this->m_cdp.alphaTable[alphaMask];

                    *xop = (*xop & DataType(this->m_cdp.maskXo)) | (DataType(xt) << this->m_cdp.xiShift);
                    *aop = (*aop & DataType(this->m_cdp.maskAo)) | (DataType(at) << this->m_cdp.aiShift);
                }
            }
        }

        void drawFastLc8bits1A(const DrawParameters &dp,
                               const AkVideoPacket &src,
                               AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto ys = (y * dp.iDiffY + dp.oMultY) / dp.oDiffY;

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                int x = dp.oX;

                if (this->m_cdp.drawSIMDFastLc8bits1A)
                    this->m_cdp.drawSIMDFastLc8bits1A(this->m_cdp.simdDrawParameters,
                                                      dp.oWidth,
                                                      dp.iDiffX,
                                                      dp.oDiffX,
                                                      dp.oMultX,
                                                      this->m_cdp.xiWidthDiv,
                                                      this->m_cdp.aiWidthDiv,
                                                      this->m_cdp.xiStep,
                                                      this->m_cdp.aiStep,
                                                      src_line_x,
                                                      src_line_a,
                                                      dst_line_x,
                                                      dst_line_a,
                                                      &x);

                #pragma omp simd if(dp.paralelize)
                for (int i = x; i < dp.oWidth; ++i) {
                    auto xs = (i * dp.iDiffX + dp.oMultX) / dp.oDiffX;

                    int xs_x = (xs >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xs_a = (xs >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto &xi = src_line_x[xs_x];
                    auto &ai = src_line_a[xs_a];

                    int xd_x = (i >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xd_a = (i >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto &xo = dst_line_x[xd_x];
                    auto &ao = dst_line_a[xd_a];

                    auto alphaMask = (size_t(ai) << this->m_cdp.depthAi) | size_t(xo);
                    xo = (qint64(xi) * this->m_cdp.aiMultTable[alphaMask] + qint64(xo) * this->m_cdp.aoMultTable[alphaMask]) >> this->m_cdp.depthAi;
                    ao = qint8(this->m_cdp.alphaTable[alphaMask]);
                }
            }
        }

        template <typename DataType>
        void drawLc1A(const DrawParameters &dp,
                      const AkVideoPacket &src,
                      AkVideoPacket &dst) const
        {
            #pragma omp parallel for schedule(static) if(dp.paralelize)
            for (int y = dp.oY; y < dp.oHeight; ++y) {
                auto ys = (y * dp.iDiffY + dp.oMultY) / dp.oDiffY;

                auto src_line_x = src.constLine(this->m_cdp.planeXi, ys) + this->m_cdp.xiOffset;
                auto src_line_a = src.constLine(this->m_cdp.planeAi, ys) + this->m_cdp.aiOffset;

                auto dst_line_x = dst.line(this->m_cdp.planeXi, y) + this->m_cdp.xiOffset;
                auto dst_line_a = dst.line(this->m_cdp.planeAi, y) + this->m_cdp.aiOffset;

                #pragma omp simd if(dp.paralelize)
                for (int x = dp.oX; x < dp.oWidth; ++x) {
                    auto xs = (x * dp.iDiffX + dp.oMultX) / dp.oDiffX;

                    int xs_x = (xs >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xs_a = (xs >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto xi = *reinterpret_cast<const DataType *>(src_line_x + xs_x);
                    auto ai = *reinterpret_cast<const DataType *>(src_line_a + xs_a);

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xi = AkAlgorithm::swapBytes(DataType(xi));
                        ai = AkAlgorithm::swapBytes(DataType(ai));
                    }

                    xi = (xi >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    ai = (ai >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    int xd_x = (x >> this->m_cdp.xiWidthDiv) * this->m_cdp.xiStep;
                    int xd_a = (x >> this->m_cdp.aiWidthDiv) * this->m_cdp.aiStep;

                    auto xop = reinterpret_cast<DataType *>(dst_line_x + xd_x);
                    auto aop = reinterpret_cast<DataType *>(dst_line_a + xd_a);

                    DataType xo = 0;
                    DataType ao = 0;

                    if (this->m_cdp.endianness != Q_BYTE_ORDER) {
                        xo = AkAlgorithm::swapBytes(DataType(*xop));
                        ao = AkAlgorithm::swapBytes(DataType(*aop));
                    } else {
                        xo = *xop;
                        ao = *aop;
                    }

                    xo = (xo >> this->m_cdp.xiShift) & this->m_cdp.maxXi;
                    ao = (ao >> this->m_cdp.aiShift) & this->m_cdp.maxAi;

                    auto diffAi = qint64(this->m_cdp.maxAi) - qint64(ai);
                    auto a = qint64(this->m_cdp.maxAi2) - (qint64(this->m_cdp.maxAi) - qint64(ao)) * diffAi;

                    qint64 xt = 0;
                    qint64 at = 0;

                    if (a != 1) {
                        auto mi = qint64(ai) * qint64(this->m_cdp.maxAi);
                        auto mo = qint64(ao) * diffAi;
                        xt = (qint64(xi) * mi + qint64(xo) * mo) / a;
                        at = a / qint64(this->m_cdp.maxAi);
                    }

                    *xop = (*xop & DataType(this->m_cdp.maskXo)) | (DataType(xt) << this->m_cdp.xiShift);
                    *aop = (*aop & DataType(this->m_cdp.maskAo)) | (DataType(at) << this->m_cdp.aiShift);
                }
            }
        }

        void drawBlit(const DrawParameters &dp,
                      const AkVideoPacket &src,
                      AkVideoPacket &dst) const
        {
            auto diffX = dp.oWidth - dp.oX;
            auto diffY = dp.oHeight - dp.oY;

            for (int plane = 0; plane < src.planes(); ++plane) {
                auto bytesUsed = src.bytesUsed(plane);
                auto srcOffset = dp.iX * bytesUsed / src.caps().width();
                auto dstOffset = dp.oX * bytesUsed / src.caps().width();
                auto copyBytes = diffX * bytesUsed / src.caps().width();
                auto srcLineOffset = src.lineSize(plane);
                auto dstLineOffset = dst.lineSize(plane);
                auto srcLine = src.constLine(plane, dp.iY) + srcOffset;
                auto dstLine = dst.line(plane, dp.oY) + dstOffset;
                auto maxY = diffY >> src.heightDiv(plane);

                for (int y = 0; y < maxY; ++y) {
                    memcpy(dstLine, srcLine, copyBytes);
                    srcLine += srcLineOffset;
                    dstLine += dstLineOffset;
                }
            }
        }

#define DRAW_FUNC(components) \
        template <typename DataType> \
        inline void drawFrame##components(const DrawParameters &dp, \
                                          const AkVideoPacket &src, \
                                          AkVideoPacket &dst) const \
        { \
            if (this->m_cdp.optimizedFor8bits) { \
                if (this->m_cdp.lightweightCache) \
                    this->drawLc8bits##components##A<DataType>(dp, src, dst); \
                else \
                    this->draw8bits##components##A<DataType>(dp, src, dst); \
            } else { \
                if (this->m_cdp.lightweightCache) \
                    this->drawLc##components##A<DataType>(dp, src, dst); \
                else \
                    this->draw##components##A<DataType>(dp, src, dst); \
            } \
        }

        DRAW_FUNC(1)
        DRAW_FUNC(3)

        template <typename DataType>
        inline void draw(const DrawParameters &dp,
                         const AkVideoPacket &src,
                         AkVideoPacket &dst)
        {
            switch (this->m_cdp.drawType) {
            case DrawType_3_components:
                this->drawFrame3<DataType>(dp, src, dst);
                break;
            case DrawType_1_component:
                this->drawFrame1<DataType>(dp, src, dst);
                break;
            default:
                break;
            }
        }

        inline void draw(int x, int y, const AkVideoPacket &packet);
};

AkVideoMixer::AkVideoMixer(QObject *parent):
    QObject(parent)
{
    this->d = new AkVideoMixerPrivate();
}

AkVideoMixer::AkVideoMixer(const AkVideoMixer &other):
    QObject()
{
    this->d = new AkVideoMixerPrivate();
    this->d->m_baseFrame = other.d->m_baseFrame;
}

AkVideoMixer::~AkVideoMixer()
{
    if (this->d->m_dp) {
        delete [] this->d->m_dp;
        this->d->m_dp = nullptr;
    }

    this->d->m_cdp.clearBuffers();

    delete this->d;
}

AkVideoMixer &AkVideoMixer::operator =(const AkVideoMixer &other)
{
    if (this != &other) {
        this->d->m_baseFrame = other.d->m_baseFrame;
    }

    return *this;
}

QObject *AkVideoMixer::create()
{
    return new AkVideoMixer();
}

AkVideoMixer::MixerFlags AkVideoMixer::flags() const
{
    return this->d->m_flags;
}

bool AkVideoMixer::begin(AkVideoPacket *packet)
{
    this->d->m_baseFrame = packet;
    this->d->m_cacheIndex = 0;

    if (packet->caps() != this->d->m_cdp.outputCaps
        || this->d->m_flags != this->d->m_cdp.flags) {
        this->d->m_cdp.outputCaps = packet->caps();
        this->d->m_cdp.flags = this->d->m_flags;
        this->d->m_cdp.configure(packet->caps());
    }

    return true;
}

void AkVideoMixer::end()
{
    this->d->m_baseFrame = nullptr;
    this->d->m_cacheIndex = 0;
}

void AkVideoMixer::draw(const AkVideoPacket &packet)
{
    this->draw(0, 0, packet);
}

bool AkVideoMixer::draw(int x, int y, const AkVideoPacket &packet)
{
    if (!this->d->m_baseFrame
        || !*this->d->m_baseFrame
        || !packet
        || packet.caps().format() != this->d->m_baseFrame->caps().format()) {
        return false;
    }

    this->d->draw(x, y, packet);

    return true;
}

void AkVideoMixer::setCacheIndex(int index)
{
    this->d->m_cacheIndex = index;
}

void AkVideoMixer::setFlags(const AkVideoMixer::MixerFlags &flags)
{
    if (this->d->m_flags == flags)
        return;

    this->d->m_flags = flags;
    emit this->flagsChanged(this->d->m_flags);
}

void AkVideoMixer::resetFlags()
{
    this->setFlags(MixerFlagNone);
}

void AkVideoMixer::reset()
{
    if (this->d->m_dp) {
        delete [] this->d->m_dp;
        this->d->m_dp = nullptr;
    }

    this->d->m_cdp.reset();
    this->d->m_dpSize = 0;
}

void AkVideoMixer::registerTypes()
{
    qRegisterMetaType<AkVideoMixer>("AkVideoMixer");
    qRegisterMetaType<MixerFlag>("MixerFlag");
    qRegisterMetaType<MixerFlags>("MixerFlags");
    qmlRegisterSingletonType<AkVideoMixer>("Ak", 1, 0, "AkVideoMixer",
                                           [] (QQmlEngine *qmlEngine,
                                               QJSEngine *jsEngine) -> QObject * {
        Q_UNUSED(qmlEngine)
        Q_UNUSED(jsEngine)

        return new AkVideoMixer();
    });
}

#define DEFINE_DRAW_FUNC(bitsSize) \
    case DrawDataTypes_##bitsSize: \
        this->draw<quint##bitsSize>(dp, packet, *this->m_baseFrame); \
        \
        if (this->m_cdp.endianness != Q_BYTE_ORDER) \
            AkAlgorithm::swapDataBytes(reinterpret_cast<quint##bitsSize *>(this->m_baseFrame->data()), this->m_baseFrame->size()); \
        \
        break;

void AkVideoMixerPrivate::draw(int x, int y, const AkVideoPacket &packet)
{
    static const int maxCacheAlloc = 1 << 16;
    int cacheIndex;

    if (this->m_cdp.lightweightCache) {
        cacheIndex = 0;

        if (this->m_dpSize != 1) {
            if (this->m_dp)
                delete [] this->m_dp;

            this->m_dp = new DrawParameters[1];
            this->m_dpSize = 1;
        }
    } else {
        cacheIndex = this->m_cacheIndex;

        if (this->m_cacheIndex >= this->m_dpSize) {
            static const int cacheBlockSize = 8;
            auto newSize = qBound(cacheBlockSize, this->m_cacheIndex + cacheBlockSize, maxCacheAlloc);
            auto dp = new DrawParameters[newSize];

            if (this->m_dp) {
                for (int i = 0; i < this->m_dpSize; ++i)
                    dp[i] = this->m_dp[i];

                delete [] this->m_dp;
            }

            this->m_dp = dp;
            this->m_dpSize = newSize;
        }
    }

    if (cacheIndex >= maxCacheAlloc)
        return;

    auto &dp = this->m_dp[cacheIndex];

    if (!packet.caps().isSameFormat(dp.inputCaps)
        || !this->m_baseFrame->caps().isSameFormat(dp.outputCaps)
        || x != dp.x
        || y != dp.y) {
        dp.inputCaps = packet.caps();
        dp.outputCaps = this->m_baseFrame->caps();
        dp.x = x;
        dp.y = y;
        dp.configure(x,
                     y,
                     packet.caps(),
                     this->m_baseFrame->caps(),
                     this->m_cdp);
    }

    if (dp.canDraw) {
        if (this->m_cdp.fastDraw) {
            this->drawBlit(dp, packet, *this->m_baseFrame);
        } else if (this->m_cdp.fastFormat && this->m_cdp.depthAi == 8) {
            if (this->m_cdp.lightweightCache) {
                if (this->m_cdp.drawType == DrawType_3_components)
                    this->drawFastLc8bits3A(dp, packet, *this->m_baseFrame);
                else
                    this->drawFastLc8bits1A(dp, packet, *this->m_baseFrame);
            } else {
                if (this->m_cdp.drawType == DrawType_3_components)
                    this->drawFast8bits3A(dp, packet, *this->m_baseFrame);
                else
                    this->drawFast8bits1A(dp, packet, *this->m_baseFrame);
            }
        } else {
            switch (this->m_cdp.drawDataTypes) {
            DEFINE_DRAW_FUNC(8)
            DEFINE_DRAW_FUNC(16)
            DEFINE_DRAW_FUNC(32)
            }
        }
    }

    if (!this->m_cdp.lightweightCache)
        this->m_cacheIndex++;
}

CommonDrawParameters::CommonDrawParameters()
{

}

CommonDrawParameters::CommonDrawParameters(const CommonDrawParameters &other):
    outputCaps(other.outputCaps),
    flags(other.flags),
    drawType(other.drawType),
    drawDataTypes(other.drawDataTypes),
    fastDraw(other.fastDraw),
    optimizedFor8bits(other.optimizedFor8bits),
    fastFormat(other.fastFormat),
    endianness(other.endianness),
    planeXi(other.planeXi),
    planeYi(other.planeYi),
    planeZi(other.planeZi),
    planeAi(other.planeAi),
    compXi(other.compXi),
    compYi(other.compYi),
    compZi(other.compZi),
    compAi(other.compAi),
    xiOffset(other.xiOffset),
    yiOffset(other.yiOffset),
    ziOffset(other.ziOffset),
    aiOffset(other.aiOffset),
    xiShift(other.xiShift),
    yiShift(other.yiShift),
    ziShift(other.ziShift),
    aiShift(other.aiShift),
    xiStep(other.xiStep),
    yiStep(other.yiStep),
    ziStep(other.ziStep),
    aiStep(other.aiStep),
    xiWidthDiv(other.xiWidthDiv),
    yiWidthDiv(other.yiWidthDiv),
    ziWidthDiv(other.ziWidthDiv),
    aiWidthDiv(other.aiWidthDiv),
    maxXi(other.maxXi),
    maxYi(other.maxYi),
    maxZi(other.maxZi),
    maxAi(other.maxAi),
    maxAi2(other.maxAi2),
    maskXo(other.maskXo),
    maskYo(other.maskYo),
    maskZo(other.maskZo),
    maskAo(other.maskAo),
    depthAi(other.depthAi),
    drawSIMDFast8bits1A(other.drawSIMDFast8bits1A),
    drawSIMDFast8bits3A(other.drawSIMDFast8bits3A),
    drawSIMDFastLc8bits1A(other.drawSIMDFastLc8bits1A),
    drawSIMDFastLc8bits3A(other.drawSIMDFastLc8bits3A)
{
    auto alphaMult = 1 << (2 * this->depthAi);
    size_t alphaMultSize = sizeof(qint64) * alphaMult;

    if (other.aiMultTable) {
        this->aiMultTable = AkSimd::amallocT<qint64>(alphaMult);
        memcpy(this->aiMultTable, other.aiMultTable, alphaMultSize);
    }

    if (other.aoMultTable) {
        this->aoMultTable = AkSimd::amallocT<qint64>(alphaMult);
        memcpy(this->aoMultTable, other.aoMultTable, alphaMultSize);
    }

    if (other.alphaTable) {
        this->alphaTable = AkSimd::amallocT<qint64>(alphaMult);
        memcpy(this->alphaTable, other.alphaTable, alphaMultSize);
    }
}

CommonDrawParameters::~CommonDrawParameters()
{
    this->clearBuffers();

    if (this->freeSIMDDrawParameters && this->simdDrawParameters)
        this->freeSIMDDrawParameters(this->simdDrawParameters);
}

CommonDrawParameters &CommonDrawParameters::operator =(const CommonDrawParameters &other)
{
    if (this != &other) {
        this->outputCaps = other.outputCaps;
        this->flags = other.flags;
        this->drawType = other.drawType;
        this->drawDataTypes = other.drawDataTypes;
        this->fastDraw = other.fastDraw;
        this->optimizedFor8bits = other.optimizedFor8bits;
        this->fastFormat = other.fastFormat;
        this->endianness = other.endianness;
        this->planeXi = other.planeXi;
        this->planeYi = other.planeYi;
        this->planeZi = other.planeZi;
        this->planeAi = other.planeAi;
        this->compXi = other.compXi;
        this->compYi = other.compYi;
        this->compZi = other.compZi;
        this->compAi = other.compAi;
        this->xiOffset = other.xiOffset;
        this->yiOffset = other.yiOffset;
        this->ziOffset = other.ziOffset;
        this->aiOffset = other.aiOffset;
        this->xiShift = other.xiShift;
        this->yiShift = other.yiShift;
        this->ziShift = other.ziShift;
        this->aiShift = other.aiShift;
        this->xiStep = other.xiStep;
        this->yiStep = other.yiStep;
        this->ziStep = other.ziStep;
        this->aiStep = other.aiStep;
        this->xiWidthDiv = other.xiWidthDiv;
        this->yiWidthDiv = other.yiWidthDiv;
        this->ziWidthDiv = other.ziWidthDiv;
        this->aiWidthDiv = other.aiWidthDiv;
        this->maxXi = other.maxXi;
        this->maxYi = other.maxYi;
        this->maxZi = other.maxZi;
        this->maxAi = other.maxAi;
        this->maxAi2 = other.maxAi2;
        this->maskXo = other.maskXo;
        this->maskYo = other.maskYo;
        this->maskZo = other.maskZo;
        this->maskAo = other.maskAo;
        this->depthAi = other.depthAi;
        this->drawSIMDFast8bits1A = other.drawSIMDFast8bits1A;
        this->drawSIMDFast8bits3A = other.drawSIMDFast8bits3A;
        this->drawSIMDFastLc8bits1A = other.drawSIMDFastLc8bits1A;
        this->drawSIMDFastLc8bits3A = other.drawSIMDFastLc8bits3A;

        this->clearBuffers();

        auto alphaMult = 1 << (2 * this->depthAi);
        size_t alphaMultSize = sizeof(qint64) * alphaMult;

        if (other.aiMultTable) {
            this->aiMultTable = AkSimd::amallocT<qint64>(alphaMult);
            memcpy(this->aiMultTable, other.aiMultTable, alphaMultSize);
        }

        if (other.aoMultTable) {
            this->aoMultTable = AkSimd::amallocT<qint64>(alphaMult);
            memcpy(this->aoMultTable, other.aoMultTable, alphaMultSize);
        }

        if (other.alphaTable) {
            this->alphaTable = AkSimd::amallocT<qint64>(alphaMult);
            memcpy(this->alphaTable, other.alphaTable, alphaMultSize);
        }
    }

    return *this;
}

void CommonDrawParameters::allocateBuffers(size_t alphaDepth)
{
    this->clearBuffers();

    auto alphaMult = 1 << (2 * alphaDepth);
    this->aiMultTable = AkSimd::amallocT<qint64>(alphaMult);
    this->aoMultTable = AkSimd::amallocT<qint64>(alphaMult);
    this->alphaTable = AkSimd::amallocT<qint64>(alphaMult);
}

void CommonDrawParameters::clearBuffers()
{
    if (this->aiMultTable) {
        AkSimd::afree(this->aiMultTable);
        this->aiMultTable = nullptr;
    }

    if (this->aoMultTable) {
        AkSimd::afree(this->aoMultTable);
        this->aoMultTable = nullptr;
    }

    if (this->alphaTable) {
        AkSimd::afree(this->alphaTable);
        this->alphaTable = nullptr;
    }
}

#define DEFINE_DRAW_TYPES(size) \
    if (ispecs.depth() == size) \
        this->drawDataTypes = DrawDataTypes_##size;

void CommonDrawParameters::configure(const AkVideoCaps &caps)
{
    auto ispecs = AkVideoCaps::formatSpecs(caps.format());

    DEFINE_DRAW_TYPES(8);
    DEFINE_DRAW_TYPES(16);
    DEFINE_DRAW_TYPES(32);

    switch (ispecs.mainComponents()) {
    case 1:
        this->drawType = DrawType_1_component;

        break;
    case 3:
        this->drawType = DrawType_3_components;

        break;
    default:
        break;
    }

    this->endianness = ispecs.endianness();

    switch (ispecs.type()) {
    case AkVideoFormatSpec::VFT_RGB:
        this->planeXi = ispecs.componentPlane(AkColorComponent::CT_R);
        this->planeYi = ispecs.componentPlane(AkColorComponent::CT_G);
        this->planeZi = ispecs.componentPlane(AkColorComponent::CT_B);

        this->compXi = ispecs.component(AkColorComponent::CT_R);
        this->compYi = ispecs.component(AkColorComponent::CT_G);
        this->compZi = ispecs.component(AkColorComponent::CT_B);

        break;

    case AkVideoFormatSpec::VFT_YUV:
        this->planeXi = ispecs.componentPlane(AkColorComponent::CT_Y);
        this->planeYi = ispecs.componentPlane(AkColorComponent::CT_U);
        this->planeZi = ispecs.componentPlane(AkColorComponent::CT_V);

        this->compXi = ispecs.component(AkColorComponent::CT_Y);
        this->compYi = ispecs.component(AkColorComponent::CT_U);
        this->compZi = ispecs.component(AkColorComponent::CT_V);

        break;

    default:
        break;
    }

    this->planeAi = ispecs.componentPlane(AkColorComponent::CT_A);
    this->compAi = ispecs.component(AkColorComponent::CT_A);

    this->xiOffset = this->compXi.offset();
    this->yiOffset = this->compYi.offset();
    this->ziOffset = this->compZi.offset();
    this->aiOffset = this->compAi.offset();

    this->xiShift = this->compXi.shift();
    this->yiShift = this->compYi.shift();
    this->ziShift = this->compZi.shift();
    this->aiShift = this->compAi.shift();

    this->xiStep = this->compXi.step();
    this->yiStep = this->compYi.step();
    this->ziStep = this->compZi.step();
    this->aiStep = this->compAi.step();

    this->xiWidthDiv = this->compXi.widthDiv();
    this->yiWidthDiv = this->compYi.widthDiv();
    this->ziWidthDiv = this->compZi.widthDiv();
    this->aiWidthDiv = this->compAi.widthDiv();

    this->maxXi = this->compXi.max<quint64>();
    this->maxYi = this->compYi.max<quint64>();
    this->maxZi = this->compZi.max<quint64>();
    this->maxAi = this->compAi.max<quint64>();
    this->maxAi2 = this->maxAi * this->maxAi;

    this->maskXo = ~(this->compXi.max<quint64>() << this->compXi.shift());
    this->maskYo = ~(this->compYi.max<quint64>() << this->compYi.shift());
    this->maskZo = ~(this->compZi.max<quint64>() << this->compZi.shift());
    this->maskAo = ~(this->compAi.max<quint64>() << this->compAi.shift());

    this->depthAi = this->compAi.depth();
    this->fastDraw = (this->flags & AkVideoMixer::MixerFlagForceBlit)
                     || !ispecs.contains(AkColorComponent::CT_A);
    this->lightweightCache =
            this->flags & AkVideoMixer::MixerFlagLightweightCache;
    this->optimizedFor8bits =  this->compXi.depth() == 8
                               && this->compYi.depth() == 8
                               && this->compZi.depth() == 8
                               && this->compAi.depth() == 8;
    this->fastFormat = ispecs.isFast();

    if (!this->fastDraw) {
        this->allocateBuffers(this->depthAi);
        auto aBitLen = 1 << this->depthAi;

        for (int ai = 0; ai < aBitLen; ai++)
            for (int ao = 0; ao < aBitLen; ao++) {
                auto alphaMask = (size_t(ai) << this->depthAi) | size_t(ao);
                auto aiMult = qint64(ai) * qint64(this->maxAi);
                auto aoMult = qint64(ao) * (qint64(this->maxAi) - qint64(ai));
                auto a = aiMult + aoMult;
                this->aiMultTable[alphaMask] = a? aBitLen * aiMult / a: 0;
                this->aoMultTable[alphaMask] = a? aBitLen * aoMult / a: 0;
                this->alphaTable[alphaMask] = a / this->maxAi;
            }
    }

    AkSimd simd("Core");

    this->createSIMDDrawParameters = reinterpret_cast<CreateDrawParametersType>(simd.resolve("createDrawParameters"));
    this->freeSIMDDrawParameters = reinterpret_cast<FreeDrawParametersType>(simd.resolve("freeDrawParameters"));
    this->drawSIMDFast8bits1A = reinterpret_cast<DrawSIMDFast8bits1AType>(simd.resolve("drawFast8bits1A"));
    this->drawSIMDFast8bits3A = reinterpret_cast<DrawSIMDFast8bits3AType>(simd.resolve("drawFast8bits3A"));
    this->drawSIMDFastLc8bits1A = reinterpret_cast<DrawSIMDFastLc8bits1AType>(simd.resolve("drawFastLc8bits1A"));
    this->drawSIMDFastLc8bits3A = reinterpret_cast<DrawSIMDFastLc8bits3AType>(simd.resolve("drawFastLc8bits3A"));

    if (this->freeSIMDDrawParameters && this->simdDrawParameters)
        this->freeSIMDDrawParameters(this->simdDrawParameters);

    if (this->createSIMDDrawParameters) {
        this->simdDrawParameters =
                this->createSIMDDrawParameters();
    }

    // Configure the minimum threshold for paralellizing the frame convertion.

    int operationsPerByte = 0;

    if (this->fastDraw) {
        operationsPerByte = 2;
    } else if (this->optimizedFor8bits) {
        if (ispecs.mainComponents() == 3)
            operationsPerByte = 20;
        else
            operationsPerByte = 12;
    } else {
        if (ispecs.mainComponents() == 3)
            operationsPerByte = 28;
        else
            operationsPerByte = 16;
    }

    this->parallelizationThreshold =
            AkCpuFeatures::paralellizableBytesThreshold(operationsPerByte,
                                                        simd.loadedInstructionSet());
}

void CommonDrawParameters::reset()
{
    this->clearBuffers();

    this->outputCaps = AkVideoCaps();

    this->drawType = DrawType_1_component;
    this->drawDataTypes = DrawDataTypes_8;
    this->fastDraw = false;
    this->optimizedFor8bits = false;
    this->fastFormat = false;

    this->endianness = Q_BYTE_ORDER;

    this->clearBuffers();

    this->planeXi = 0;
    this->planeYi = 0;
    this->planeZi = 0;
    this->planeAi = 0;

    this->compXi = {};
    this->compYi = {};
    this->compZi = {};
    this->compAi = {};

    this->xiOffset = 0;
    this->yiOffset = 0;
    this->ziOffset = 0;
    this->aiOffset = 0;

    this->xiShift = 0;
    this->yiShift = 0;
    this->ziShift = 0;
    this->aiShift = 0;

    this->xiStep = 0;
    this->yiStep = 0;
    this->ziStep = 0;
    this->aiStep = 0;

    this->xiWidthDiv = 0;
    this->yiWidthDiv = 0;
    this->ziWidthDiv = 0;
    this->aiWidthDiv = 0;

    this->maxXi = 0;
    this->maxYi = 0;
    this->maxZi = 0;
    this->maxAi = 0;
    this->maxAi2 = 0;

    this->maskXo = 0;
    this->maskYo = 0;
    this->maskZo = 0;
    this->maskAo = 0;

    this->depthAi = 0;
    this->optimizedFor8bits = false;

    this->simdDrawParameters = nullptr;
    this->createSIMDDrawParameters = nullptr;
    this->freeSIMDDrawParameters = nullptr;
    this->drawSIMDFast8bits1A = nullptr;
    this->drawSIMDFast8bits3A = nullptr;
    this->drawSIMDFastLc8bits1A = nullptr;
    this->drawSIMDFastLc8bits3A = nullptr;

    this->parallelizationThreshold = 0;
}

DrawParameters::DrawParameters()
{
}

DrawParameters::DrawParameters(const DrawParameters &other):
    inputCaps(other.inputCaps),
    x(other.x),
    y(other.y),
    iX(other.iX),
    iY(other.iY),
    iWidth(other.iWidth),
    iHeight(other.iHeight),
    oX(other.oX),
    oY(other.oY),
    oWidth(other.oWidth),
    oHeight(other.oHeight),
    iDiffX(other.iDiffX),
    iDiffY(other.iDiffY),
    oDiffX(other.oDiffX),
    oDiffY(other.oDiffY),
    oMultX(other.oMultX),
    oMultY(other.oMultY)
{
    auto width = this->outputCaps.width();
    auto height = this->outputCaps.height();

    size_t widthDataSize = sizeof(int) * width;

    if (other.srcWidthOffsetX) {
        this->srcWidthOffsetX = new int [width];
        memcpy(this->srcWidthOffsetX, other.srcWidthOffsetX, widthDataSize);
    }

    if (other.srcWidthOffsetY) {
        this->srcWidthOffsetY = new int [width];
        memcpy(this->srcWidthOffsetY, other.srcWidthOffsetY, widthDataSize);
    }

    if (other.srcWidthOffsetZ) {
        this->srcWidthOffsetZ = new int [width];
        memcpy(this->srcWidthOffsetZ, other.srcWidthOffsetZ, widthDataSize);
    }

    if (other.srcWidthOffsetA) {
        this->srcWidthOffsetA = new int [width];
        memcpy(this->srcWidthOffsetA, other.srcWidthOffsetA, widthDataSize);
    }

    if (other.srcHeight) {
        this->srcHeight = new int [height];
        memcpy(this->srcHeight, other.srcHeight, sizeof(int) * height);
    }

    if (other.dstWidthOffsetX) {
        this->dstWidthOffsetX = new int [width];
        memcpy(this->dstWidthOffsetX, other.dstWidthOffsetX, widthDataSize);
    }

    if (other.dstWidthOffsetY) {
        this->dstWidthOffsetY = new int [width];
        memcpy(this->dstWidthOffsetY, other.dstWidthOffsetY, widthDataSize);
    }

    if (other.dstWidthOffsetZ) {
        this->dstWidthOffsetZ = new int [width];
        memcpy(this->dstWidthOffsetZ, other.dstWidthOffsetZ, widthDataSize);
    }

    if (other.dstWidthOffsetA) {
        this->dstWidthOffsetA = new int [width];
        memcpy(this->dstWidthOffsetA, other.dstWidthOffsetA, widthDataSize);
    }
}

DrawParameters::~DrawParameters()
{
    this->clearBuffers();
}

DrawParameters &DrawParameters::operator =(const DrawParameters &other)
{
    if (this != &other) {
        this->inputCaps = other.inputCaps;
        this->outputCaps = other.outputCaps;
        this->x = other.x;
        this->y = other.y;
        this->iX = other.iX;
        this->iY = other.iY;
        this->iWidth = other.iWidth;
        this->iHeight = other.iHeight;
        this->oX = other.oX;
        this->oY = other.oY;
        this->oWidth = other.oWidth;
        this->oHeight = other.oHeight;
        this->iDiffX = other.iDiffX;
        this->iDiffY = other.iDiffY;
        this->oDiffX = other.oDiffX;
        this->oDiffY = other.oDiffY;
        this->oMultX = other.oMultX;
        this->oMultY = other.oMultY;

        this->clearBuffers();

        auto width = this->outputCaps.width();
        auto height = this->outputCaps.height();

        size_t widthDataSize = sizeof(int) * width;

        if (other.srcWidthOffsetX) {
            this->srcWidthOffsetX = new int [width];
            memcpy(this->srcWidthOffsetX, other.srcWidthOffsetX, widthDataSize);
        }

        if (other.srcWidthOffsetY) {
            this->srcWidthOffsetY = new int [width];
            memcpy(this->srcWidthOffsetY, other.srcWidthOffsetY, widthDataSize);
        }

        if (other.srcWidthOffsetZ) {
            this->srcWidthOffsetZ = new int [width];
            memcpy(this->srcWidthOffsetZ, other.srcWidthOffsetZ, widthDataSize);
        }

        if (other.srcWidthOffsetA) {
            this->srcWidthOffsetA = new int [width];
            memcpy(this->srcWidthOffsetA, other.srcWidthOffsetA, widthDataSize);
        }

        if (other.srcHeight) {
            this->srcHeight = new int [height];
            memcpy(this->srcHeight, other.srcHeight, sizeof(int) * height);
        }

        if (other.dstWidthOffsetX) {
            this->dstWidthOffsetX = new int [width];
            memcpy(this->dstWidthOffsetX, other.dstWidthOffsetX, widthDataSize);
        }

        if (other.dstWidthOffsetY) {
            this->dstWidthOffsetY = new int [width];
            memcpy(this->dstWidthOffsetY, other.dstWidthOffsetY, widthDataSize);
        }

        if (other.dstWidthOffsetZ) {
            this->dstWidthOffsetZ = new int [width];
            memcpy(this->dstWidthOffsetZ, other.dstWidthOffsetZ, widthDataSize);
        }

        if (other.dstWidthOffsetA) {
            this->dstWidthOffsetA = new int [width];
            memcpy(this->dstWidthOffsetA, other.dstWidthOffsetA, widthDataSize);
        }
    }

    return *this;
}

void DrawParameters::allocateBuffers(const AkVideoCaps &caps)
{
    this->clearBuffers();

    this->srcWidthOffsetX = new int [caps.width()];
    this->srcWidthOffsetY = new int [caps.width()];
    this->srcWidthOffsetZ = new int [caps.width()];
    this->srcWidthOffsetA = new int [caps.width()];
    this->srcHeight = new int [caps.height()];

    this->dstWidthOffsetX = new int [caps.width()];
    this->dstWidthOffsetY = new int [caps.width()];
    this->dstWidthOffsetZ = new int [caps.width()];
    this->dstWidthOffsetA = new int [caps.width()];
}

void DrawParameters::clearBuffers()
{
    if (this->srcWidthOffsetX) {
        delete [] this->srcWidthOffsetX;
        this->srcWidthOffsetX = nullptr;
    }

    if (this->srcWidthOffsetY) {
        delete [] this->srcWidthOffsetY;
        this->srcWidthOffsetY = nullptr;
    }

    if (this->srcWidthOffsetZ) {
        delete [] this->srcWidthOffsetZ;
        this->srcWidthOffsetZ = nullptr;
    }

    if (this->srcWidthOffsetA) {
        delete [] this->srcWidthOffsetA;
        this->srcWidthOffsetA = nullptr;
    }

    if (this->srcHeight) {
        delete [] this->srcHeight;
        this->srcHeight = nullptr;
    }

    if (this->dstWidthOffsetX) {
        delete [] this->dstWidthOffsetX;
        this->dstWidthOffsetX = nullptr;
    }

    if (this->dstWidthOffsetY) {
        delete [] this->dstWidthOffsetY;
        this->dstWidthOffsetY = nullptr;
    }

    if (this->dstWidthOffsetZ) {
        delete [] this->dstWidthOffsetZ;
        this->dstWidthOffsetZ = nullptr;
    }

    if (this->dstWidthOffsetA) {
        delete [] this->dstWidthOffsetA;
        this->dstWidthOffsetA = nullptr;
    }
}

void DrawParameters::configure(int x, int y,
                               const AkVideoCaps &icaps,
                               const AkVideoCaps &ocaps,
                               const CommonDrawParameters &cdp)
{
    if (x < 0) {
        this->iX = -x;
        this->oX = 0;
    } else {
        this->iX = 0;
        this->oX = x;
    }

    if (x + icaps.width() <= ocaps.width()) {
        this->iWidth = icaps.width();
        this->oWidth = icaps.width() + x;
    } else {
        this->iWidth = ocaps.width() - x;
        this->oWidth = ocaps.width();
    }

    if (y < 0) {
        this->iY = -y;
        this->oY = 0;
    } else {
        this->iY = 0;
        this->oY = y;
    }

    if (y + icaps.height() <= ocaps.height()) {
        this->iHeight = icaps.height();
        this->oHeight = icaps.height() + y;
    } else {
        this->iHeight = ocaps.height() - y;
        this->oHeight = ocaps.height();
    }

    this->canDraw = this->iX >= 0 && this->iX < icaps.width()
                 && this->iY >= 0 && this->iY < icaps.height()
                 && this->oX >= 0 && this->oX < ocaps.width()
                 && this->oY >= 0 && this->oY < ocaps.height()
                 && this->iWidth >= 0 && this->iWidth <= icaps.width()
                 && this->iHeight >= 0 && this->iHeight <= icaps.height()
                 && this->oWidth >= 0 && this->oWidth <= ocaps.width()
                 && this->oHeight >= 0 && this->oHeight <= ocaps.height();

    this->iDiffX = this->iWidth - this->iX - 1;
    this->oDiffX = this->oWidth - this->oX - 1;

    if (this->oDiffX < 1)
        this->oDiffX = 1;

    this->oMultX = this->iX * this->oDiffX - this->oX * this->iDiffX;

    this->iDiffY = this->iHeight - this->iY - 1;
    this->oDiffY = this->oHeight - this->oY - 1;

    if (this->oDiffY < 1)
        this->oDiffY = 1;

    this->oMultY = this->iY * this->oDiffY - this->oY * this->iDiffY;

    if (!cdp.lightweightCache) {
        this->allocateBuffers(ocaps);

        for (int x = 0; x < ocaps.width(); ++x) {
            auto xs = (x * this->iDiffX + this->oMultX) / this->oDiffX;

            this->srcWidthOffsetX[x] = (xs >> cdp.xiWidthDiv) * cdp.xiStep;
            this->srcWidthOffsetY[x] = (xs >> cdp.yiWidthDiv) * cdp.yiStep;
            this->srcWidthOffsetZ[x] = (xs >> cdp.ziWidthDiv) * cdp.ziStep;
            this->srcWidthOffsetA[x] = (xs >> cdp.aiWidthDiv) * cdp.aiStep;

            this->dstWidthOffsetX[x] = (x >> cdp.xiWidthDiv) * cdp.xiStep;
            this->dstWidthOffsetY[x] = (x >> cdp.yiWidthDiv) * cdp.yiStep;
            this->dstWidthOffsetZ[x] = (x >> cdp.ziWidthDiv) * cdp.ziStep;
            this->dstWidthOffsetA[x] = (x >> cdp.aiWidthDiv) * cdp.aiStep;
        }

        for (int y = 0; y < ocaps.height(); ++y) {
            auto ys = (y * this->iDiffY + this->oMultY) / this->oDiffY;
            this->srcHeight[y] = ys;
        }
    }

    this->paralelize =
            AkVideoCaps(ocaps.format(),
                        this->oWidth - this->oX,
                        this->oHeight - this->oY,
                        {}).dataSize() > cdp.parallelizationThreshold;
}

void DrawParameters::reset()
{
    this->inputCaps = AkVideoCaps();
    this->outputCaps = AkVideoCaps();

    this->iX = 0;
    this->iY = 0;
    this->iWidth = 0;
    this->iHeight = 0;

    this->oX = 0;
    this->oY = 0;
    this->oWidth = 0;
    this->oHeight = 0;
    this->iDiffX = 0;
    this->iDiffY = 0;
    this->oDiffX = 0;
    this->oDiffY = 0;
    this->oMultX = 0;
    this->oMultY = 0;

    this->clearBuffers();
}

#include "moc_akvideomixer.cpp"
