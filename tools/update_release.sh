release=0.1.9
cd /tmp
#e200
wget https://github.com/F5OEO/tezuka_fw/releases/download/"$release"/antsdr_e200.zip -O antsdr_e200.zip && mkdir -p antsdr_e200 && unzip -o antsdr_e200.zip -d antsdr_e200 \
&& sshpass -p 'analog' scp -r antsdr_e200/sdimg/* root@antsdre200.local:/mnt/sd && sshpass -p 'analog' ssh root@antsdre200.local "reboot"
#plutoplus
wget https://github.com/F5OEO/tezuka_fw/releases/download/"$release"/plutoplus.zip -O plutoplus.zip && mkdir -p plutoplus && unzip -o plutoplus.zip -d plutoplus \
&& sshpass -p 'analog' scp -r plutoplus/sdimg/* root@plutoplus.local:/mnt/sd && sshpass -p 'analog' ssh root@plutoplus.local "reboot"
#Libre
wget https://github.com/F5OEO/tezuka_fw/releases/download/"$release"/libresdr.zip -O libresdr.zip && mkdir -p libresdr && unzip -o libresdr.zip -d libresdr \
&& sshpass -p 'analog' scp -r libresdr/sdimg/* root@libresdr.local:/mnt/sd && sshpass -p 'analog' ssh root@libresdr.local "reboot"
#fishball
wget https://github.com/F5OEO/tezuka_fw/releases/download/"$release"/fishball.zip -O fishball.zip && mkdir -p fishball && unzip -o fishball.zip -d fishball \
&& sshpass -p 'analog' scp -r fishball/sdimg/* root@fishball.local:/mnt/sd && sshpass -p 'analog' ssh root@fishball.local "reboot"

