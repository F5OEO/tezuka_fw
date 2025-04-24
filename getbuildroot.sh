#!/usr/bin/env bash
wget -O- https://buildroot.org/downloads/buildroot-2024.11.1.tar.gz | tar -xz --one-top-level=buildroot --strip-components=1

patch -p0 << 'EOF'
--- buildroot/support/docker/Dockerfile	2025-01-09 17:43:35.000000000 +0300
+++ Dockerfile	2025-04-24 12:36:38.179855982 +0300
@@ -51,6 +51,11 @@
         subversion \
         unzip \
         wget \
+        # Tezuka packages \
+        zip \
+        curl \
+        libclang-dev \
+        libssl-dev \
         && \
     apt-get -y autoremove && \
     apt-get -y clean
@@ -59,6 +64,17 @@
 RUN sed -i 's/# \(en_US.UTF-8\)/\1/' /etc/locale.gen && \
     /usr/sbin/locale-gen

+# Build genboot
+RUN curl -sfL https://github.com/Xilinx/bootgen/archive/refs/tags/xilinx_v2023.2.tar.gz | \
+    tar -zx -C /tmp && \
+    cd /tmp/bootgen-xilinx_v2023.2 && \
+    make -j$(nproc) && \
+    cp bootgen /usr/bin/ && \
+    rm -rf /tmp/bootgen-xilinx_v2023.2
+
+# Hide git warnings about safety
+RUN git config --global --add safe.directory '*'
+
 RUN useradd -ms /bin/bash br-user && \
     chown -R br-user:br-user /home/br-user

EOF

patch -p0 << 'EOF'
--- buildroot/utils/docker-run	2025-01-09 17:43:35.000000000 +0300
+++ docker-run	2025-04-24 17:23:50.818136982 +0300
@@ -9,11 +9,6 @@
     # Support git-worktree
     GIT_DIR="$(cd "${MAIN_DIR}" && git rev-parse --no-flags --git-common-dir)"
 fi
-if test -z "${IMAGE}" ; then
-    # shellcheck disable=SC2016
-    IMAGE=$(grep ^image: "${MAIN_DIR}/.gitlab-ci.yml" | \
-            sed -e 's,^image: ,,g' | sed -e 's,\$CI_REGISTRY,registry.gitlab.com,g')
-fi

 declare -a docker_opts=(
     -i
@@ -44,6 +39,26 @@
     exit 1
 fi

+# Try to find specific image if not defined or build own
+TEZUKA_IMAGE="br_tezuka:2024.11.1"
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
@@ -90,6 +105,12 @@
     docker_opts+=( --env BR2_DL_DIR )
 fi

+# NOTE only one external dir
+if [ "${BR2_EXTERNAL}" ]; then
+    mountpoints+=( "$(realpath ${BR2_EXTERNAL})" )
+    docker_opts+=( --env BR2_EXTERNAL="$(realpath ${BR2_EXTERNAL})" )
+fi
+
 # shellcheck disable=SC2013 # can't use while-read because of the assignment
 for dir in $(printf '%s\n' "${mountpoints[@]}" |LC_ALL=C sort -u); do
     docker_opts+=( --mount "type=bind,src=${dir},dst=${dir}" )
EOF

