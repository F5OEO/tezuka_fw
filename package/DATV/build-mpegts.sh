#!/bin/sh
# Builds the latest mpegts.js (https://github.com/xqq/mpegts.js, Apache-2.0) from its main branch.
#   build-mpegts.sh <work dir> <output dir> [repo url] [branch]
# Needs git, node and npm on the build host (the Dashboard build already needs node) and network access.
# Writes <output dir>/mpegts.js (webpack production build, minified) and <output dir>/VERSION.
# Fails with a non-zero status on any problem: the caller then keeps the vendored copy.
set -e
WORK=$1
OUT=$2
REPO=${3:-https://github.com/xqq/mpegts.js.git}
BRANCH=${4:-master}

mkdir -p "$WORK" "$OUT"
if [ -d "$WORK/src/.git" ]; then
	git -C "$WORK/src" fetch --depth 1 origin "$BRANCH"
	git -C "$WORK/src" reset --hard FETCH_HEAD
else
	rm -rf "$WORK/src"
	git clone --depth 1 --branch "$BRANCH" "$REPO" "$WORK/src"
fi
cd "$WORK/src"
npm ci --no-audit --no-fund --ignore-scripts
npx webpack --mode=production
test -s dist/mpegts.js
cp dist/mpegts.js "$OUT/mpegts.js"
{
	echo "mpegts.js $(node -p "require('./package.json').version")"
	echo "commit $(git rev-parse HEAD) ($(git log -1 --format=%cd --date=short))"
	echo "source $REPO $BRANCH"
} > "$OUT/VERSION"
