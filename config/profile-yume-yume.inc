# $Id$
# yume-specific additions to /etc/profile

export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/mrua/lib
export LIBEXTRACTOR_PREFIX=/lib/libextractor

# for anything depends on mrua
cd /usr/local/mrua
. ./run.env
cd

# Initialize dcchd if it exists (only in rfs1)
if [ -d /usr/local/dcchd ]
then

  echo "Initializing dcchd"

  # for dcchd test cases and display_service test cases
  export DFB_CONFIG_DIR=/usr/local/dcchd
  export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${DFB_CONFIG_DIR}/directfb/lib:${DFB_CONFIG_DIR}/dcchd/dcchd:${DFB_CONFIG_DIR}/dcchd/core:${DFB_CONFIG_DIR}/dcchd/mono:${DFB_CONFIG_DIR}/dcchd/dtv:${DFB_CONFIG_DIR}/dcchd/dtv/capture:${DFB_CONFIG_DIR}/dcchd/dtv/network

  cd /usr/local/dcchd
  . ./trun.env
  cd

else

  echo "dcchd not present, skipping initialization"

fi

