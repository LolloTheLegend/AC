#!/bin/sh

# CUBE_DIR should refer to the directory in which AssaultCube is placed.
#CUBE_DIR=~/assaultcube
#CUBE_DIR=/usr/local/assaultcube
#CUBE_DIR=./
CUBE_DIR=$(dirname "$(readlink -f "${0}")")

# CUBE_OPTIONS starts AssaultCube with any command line options you choose.
#CUBE_OPTIONS="-f"
CUBE_OPTIONS="--home=${HOME}/.assaultcube_v1.2 --init"

# SYSTEM_NAME should be set to the name of your operating system.
#SYSTEM_NAME=Linux
SYSTEM_NAME=`uname -s`

# MACHINE_NAME should be set to the architecture of your processor.
#MACHINE_NAME=i686
MACHINE_NAME=`uname -m`

case ${SYSTEM_NAME} in
Linux)
  SYSTEM_NAME=linux_
  ;;
*)
  SYSTEM_NAME=unknown_
  ;;
esac

case ${MACHINE_NAME} in
i486|i586|i686)
  MACHINE_NAME=
  ;;
x86_64)
  MACHINE_NAME=64_
  ;;
*)
  if [ ${SYSTEM_NAME} != native_ ]
  then
    SYSTEM_NAME=native_
  fi
  MACHINE_NAME=
  ;;
esac

if [ -x "${CUBE_DIR}/client" ]
then
  SYSTEM_NAME=native_
  MACHINE_NAME=
fi

if [ -x "/sbin/ldconfig" ]; then
  if [ -z "$(/sbin/ldconfig -p | grep "libX11")" ]; then
    echo "To run AssaultCube, please ensure X11 libraries are installed."
    exit 1
  fi
  if [ -z "$(/sbin/ldconfig -p | grep "libSDL-1.2")" ]; then
    echo "To run AssaultCube, please ensure SDL v1.2 libraries are installed."
    exit 1
  fi
  if [ -z "$(/sbin/ldconfig -p | grep "libSDL_image")" ]; then
    echo "To run AssaultCube, please ensure SDL_image libraries are installed."
    exit 1
  fi
  if [ -z "$(/sbin/ldconfig -p | grep "libz")" ]; then
    echo "To run AssaultCube, please ensure z libraries are installed."
    exit 1
  fi
  if [ -z "$(/sbin/ldconfig -p | grep "libogg")" ]; then
    echo "To run AssaultCube, please ensure ogg libraries are installed."
    exit 1
  fi
  if [ -z "$(/sbin/ldconfig -p | grep "libvorbis")" ]; then
    echo "To run AssaultCube, please ensure vorbis libraries are installed."
    exit 1
  fi
  if [ -z "$(/sbin/ldconfig -p | grep "libopenal")" ]; then
    echo "To run AssaultCube, please ensure OpenAL-Soft libraries are installed."
    exit 1
  fi
  if [ -z "$(/sbin/ldconfig -p | grep "libcurl")" ]; then
    echo "To run AssaultCube, please ensure Curl libraries are installed."
    exit 1
  fi
fi

if [ -x "${CUBE_DIR}/bin_unix/${SYSTEM_NAME}${MACHINE_NAME}client" ]; then
  cd "${CUBE_DIR}"
  exec "${CUBE_DIR}/bin_unix/${SYSTEM_NAME}${MACHINE_NAME}client" ${CUBE_OPTIONS} "$@"
elif [ -e "${CUBE_DIR}/bin_unix/${SYSTEM_NAME}${MACHINE_NAME}client" ]; then
  echo "Insufficient permissons to run AssaultCube."
  echo "Please change (chmod) the AssaultCube client in the bin_unix folder to be readable/executable."
else
  echo "Your platform does not have a pre-compiled AssaultCube client."
  echo "Please follow the following steps to build a native client:"
  echo "1) Ensure you have the following DEVELOPMENT libraries installed:"
  echo "   OpenGL, SDL, SDL_image, zlib, libogg, libvorbis, OpenAL Soft, libcurl"
  echo "2) Ensure clang++ and any other required build tools are installed."
  echo "3) Change directory to ./source/src/ and type \"make install\"."
  echo "4) If the compile succeeds, return to this directory and re-run this script."
  exit 1
fi
