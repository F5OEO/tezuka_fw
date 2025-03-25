################################################################################
#
# srt
#
################################################################################


SRT_VERSION = master
SRT_SITE = $(call github,Haivision,srt,$(SRT_VERSION))
SRT_INSTALL_STAGING = YES
SRT_CONF_OPTS = -DENABLE_ENCRYPTION=OFF -DENABLE_STATIC=OFF

$(eval $(cmake-package))


