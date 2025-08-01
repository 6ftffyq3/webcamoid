#!/bin/bash

# Webcamoid, webcam capture application.
# Copyright (C) 2023  Gonzalo Exequiel Pedone
#
# Webcamoid is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Webcamoid is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Webcamoid. If not, see <http://www.gnu.org/licenses/>.
#
# Web-Site: http://webcamoid.github.io/

set -e

if [[ "${UPLOAD}" != 1 || "${DAILY_BUILD}" = 1 || "${ENABLE_ADS}" != 1 ]]; then
    exit 0
fi

pacman --noconfirm --needed -S \
    p7zip

verMaj=$(grep VER_MAJ libAvKys/cmake/ProjectCommons.cmake | awk '{print $2}' | tr -d ')' | head -n 1)
verMin=$(grep VER_MIN libAvKys/cmake/ProjectCommons.cmake | awk '{print $2}' | tr -d ')' | head -n 1)
verPat=$(grep VER_PAT libAvKys/cmake/ProjectCommons.cmake | awk '{print $2}' | tr -d ')' | head -n 1)
releaseVer=${verMaj}.${verMin}.${verPat}

7z a -mx1 -mmt4 "webcamoid-packages/webcamoid-android-with-ads-${releaseVer}.7z" webcamoid-packages/android
rm -vf webcamoid-packages/android/*
mv -vf webcamoid-packages/*.7z webcamoid-packages/android/
