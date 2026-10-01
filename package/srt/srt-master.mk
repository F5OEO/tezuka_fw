################################################################################
#
# srt
#
################################################################################


SRT_VERSION = 899348d8318eb9a3c5a5b6ec43c4a1114288773a # master, pinned 2026-08-29
SRT_SITE = $(call github,Haivision,srt,$(SRT_VERSION))
SRT_INSTALL_STAGING = YES
SRT_CONF_OPTS = -DENABLE_ENCRYPTION=OFF -DENABLE_STATIC=OFF

$(eval $(cmake-package))


