#!/usr/bin/env bash
wget -O- https://buildroot.org/downloads/buildroot-2025.08.tar.gz | tar -xz --one-top-level=buildroot --strip-components=1

patch -p0 << 'EOF'
--- buildroot/support/docker/Dockerfile	2025-05-26 20:56:37.937741302 +0300
+++ Dockerfile	2025-05-26 20:58:22.255723815 +0300
@@ -71,6 +71,10 @@
         subversion \
         unzip \
         wget \
+        # Tezuka packages \
+        zip \
+        libclang-dev \
+        libssl-dev \
         && \
     apt-get -y autoremove && \
     apt-get -y clean
@@ -89,6 +93,9 @@
 RUN sed -i 's/# \(en_US.UTF-8\)/\1/' /etc/locale.gen && \
     /usr/sbin/locale-gen

+# Hide git warnings about safety
+RUN git config --global --add safe.directory '*'
+
 RUN useradd -ms /bin/bash br-user && \
     chown -R br-user:br-user /home/br-user

EOF

patch -p0 << 'EOF'
--- buildroot/utils/docker-run	2025-05-19 10:31:22.000000000 +0300
+++ docker-run	2025-05-26 21:08:20.290620140 +0300
@@ -2,18 +2,6 @@
 set -o errexit -o pipefail
 DIR=$(dirname "${0}")
 MAIN_DIR=$(readlink -f "${DIR}/..")
-if [ -L "${MAIN_DIR}/.git/config" ]; then
-    # Support git-new-workdir
-    GIT_DIR="$(dirname "$(realpath "${MAIN_DIR}/.git/config")")"
-else
-    # Support git-worktree
-    GIT_DIR="$(cd "${MAIN_DIR}" && git rev-parse --no-flags --git-common-dir)"
-fi
-if test -z "${IMAGE}" ; then
-    # shellcheck disable=SC2016
-    IMAGE=$(grep ^image: "${MAIN_DIR}/.gitlab-ci.yml" | \
-            sed -e 's,^image: ,,g' | sed -e 's,\$CI_REGISTRY,registry.gitlab.com,g')
-fi

 declare -a docker_opts=(
     -i
@@ -51,6 +39,26 @@
     exit 1
 fi

+# Try to find specific image if not defined or build own
+TEZUKA_IMAGE="br_tezuka:2025.02.3"
+if test -z "${IMAGE}" ; then
+    IMAGE=$(${DOCKER} image ls -q ${TEZUKA_IMAGE})
+fi
+if test -z "${IMAGE}" ; then
+    while true; do
+        read -r -p "Docker image not found. Create new image '${TEZUKA_IMAGE}'? [Yn] " yn
+        yn=${yn,,}
+        if [[ $yn =~ ^(y| ) ]] || [[ -z $yn ]]; then
+            ${DOCKER} build -t "${TEZUKA_IMAGE}" "${MAIN_DIR}/support/docker"
+            IMAGE=$TEZUKA_IMAGE
+            break
+        fi
+        if [[ $yn =~ ^(n) ]]; then
+            exit
+        fi
+    done
+fi
+
 # curl lists (and recognises and uses) other types of *_proxy variables,
 # but only those make sense for Buildroot:
 for env in all_proxy http_proxy https_proxy ftp_proxy no_proxy; do
@@ -92,6 +100,18 @@
     fi
 fi

+if [ "${BR2_EXTERNAL}" ]; then
+    br2_externals=(${BR2_EXTERNAL//:/ })
+    br2_externals=$(printf "%s\n" "${br2_externals[@]}" | sort -u)  # leave only unique paths
+    br2_external_container_env=
+    for br2_external_path in ${br2_externals[@]}; do
+        real_path=$(realpath ${br2_external_path})
+        mountpoints+=( "${real_path}" )
+        br2_external_container_env="${br2_external_container_env}${real_path}:"
+    done
+    docker_opts+=( --env BR2_EXTERNAL="${br2_external_container_env}" )
+fi
+
 if [ "${BR2_DL_DIR}" ]; then
     mountpoints+=( "${BR2_DL_DIR}" )
     docker_opts+=( --env BR2_DL_DIR )

EOF

# Substitute bootgen package with older version. Bootgen 2025.01 produces a broken binary.
# This replaces bootgen 2025.01 with bootgen 2024.2 from Buildroot 2025.05.
echo "Substituting bootgen package with older version"
rm -r buildroot/package/bootgen
cp -a bootgen-alt buildroot/package/bootgen
