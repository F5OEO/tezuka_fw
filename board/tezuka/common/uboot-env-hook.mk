# Generate U-Boot env config fragment with absolute path resolved at build time
define UBOOT_SET_ENV_PATH
    echo 'CONFIG_USE_DEFAULT_ENV_FILE=y' > \
        $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/common/uboot-env.config
    echo 'CONFIG_DEFAULT_ENV_FILE="$(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/common/uboot-env.txt"' >> \
        $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/common/uboot-env.config
endef

UBOOT_POST_EXTRACT_HOOKS += UBOOT_SET_ENV_PATH