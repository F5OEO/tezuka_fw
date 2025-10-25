cd buildroot
make clean
mkdir -p build
make pluto_maiasdr_defconfig && make && mv output/images/tezuka.zip build/pluto.zip
        rm output/images/sdimg/* -r
        make plutoplus_maiasdr_defconfig && make uboot-dirclean && make linux-reconfigure && make maia-kmod-reconfigure && make && mv output/images/tezuka.zip build/plutoplus.zip
        rm output/images/sdimg/* -r
        make e200_maiasdr_defconfig && make uboot-dirclean && make linux-reconfigure && make maia-kmod-reconfigure && make && mv output/images/tezuka.zip build/antsdr_e200.zip
        rm output/images/sdimg/* -r
        make libre_maiasdr_defconfig  && make uboot-dirclean && make linux-reconfigure && make maia-kmod-reconfigure && make && mv output/images/tezuka.zip build/libresdr.zip
        rm output/images/sdimg/* -r
        make fishball_maiasdr_defconfig  && make uboot-dirclean && make linux-reconfigure && make maia-kmod-reconfigure &&  make && mv output/images/tezuka.zip build/fishball.zip
        rm output/images/sdimg/* -r
        make fishball_maiasdr_7020_defconfig  && make uboot-dirclean && make linux-reconfigure && make maia-kmod-reconfigure && make && mv output/images/tezuka.zip build/fishball7020.zip
