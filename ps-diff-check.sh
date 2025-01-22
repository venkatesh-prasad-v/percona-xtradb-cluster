#!/bin/bash

# Create a temporary directory to fetch the PS source tarball.
PS_84_PATH=`mktemp -d`
SCRIPT_PATH=${BASH_SOURCE[0]}
BASE_DIR=$(dirname $SCRIPT_PATH)
RET=1

# Get the MySQL Version to download to compare the sources.
source MYSQL_VERSION
PS_VERSION=$MYSQL_VERSION_MAJOR.$MYSQL_VERSION_MINOR.$MYSQL_VERSION_PATCH$MYSQL_VERSION_EXTRA

echo "Using PS Version: $PS_VERSION"


# Get the source tarball from Percona downloads
pushd $PS_84_PATH
curl https://downloads.percona.com/downloads/Percona-Server-8.4/Percona-Server-$PS_VERSION/source/tarball/percona-server-$PS_VERSION.tar.gz -o percona-server-$PS_VERSION.tar.gz
if [[ $? -ne 0 ]]; then
  echo "Downloading source reference failed"
  exit 1 
fi

tar -xf percona-server-$PS_VERSION.tar.gz
if [[ $? -ne 0 ]]; then
  echo "Unpacking source reference failed"
  exit 1 
fi

popd

# Run the diff checker tool
echo "Running Diff Checker..."
$BASE_DIR/check_src/run.sh $PS_84_PATH/percona-server-$PS_VERSION/ 2>&1 > $PS_84_PATH/diff-check.log
if [[ $? -ne 0 ]]; then
  echo "Running check_src tool failed"
  exit 1 
fi

echo "Running Diff Checker...DONE"

if [[ -s $PS_84_PATH/diff-check.log ]]; then
  echo "Diff checker failed with below error. Please add them to whitelist if they are expected"
  echo ""
  cat $PS_84_PATH/diff-check.log
  RET=1
else
  RET=0
fi


# Cleanup
rm -rf $PS_84_PATH
echo "Diff Checker exits with status code ${RET}"
exit $RET
