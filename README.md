# Bakalarka
Pro kompilaci je prikaz vsechny kodu jsou abslutni , adresa kompilatoru je takhle z duvodu vice nainstalovanych kompilatoru.


sysroot https://github.com/analogdevicesinc/plutosdr-fw/releases/download/v0.39/sysroot-v0.39.tar.gz

./../../../usr/local/bin/gcc-linaro-7.2.1-2017.11-i686_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc-7.2.1 -mfloat-abi=hard  --sysroot=$HOME/sysroot-v0.39/staging -std=gnu99 -Iinclude -Wno-unused-parameter -Wno-unused-variable -Wno-duplicate-decl-specifier -g -o pluto_stream /home/vojta/MQTT-C-master12/examples/simple_subscriber.c /home/vojta/MQTT-C-master12/src/mqtt.c /home/vojta/MQTT-C-master12/src/mqtt_pal.c -lpthread -liio -lm -lrt -Wall -Wextra -O3

Manipulace s plutem:

sudo ssh-keygen -f "/root/.ssh/known_hosts" -R "192.168.2.1"

sudo ssh -t root@192.168.2.1 

sudo scp pluto_stream  root@192.168.2.1:/root/


Testovani mqtt serveru

mosquitto_sub -h localhost -t /test

mosquitto_sub -h localhost -t /# -T /data/out -v

mosquitto_pub -h localhost -t /message -m "MESSAGE"

mosquitto_pub -h localhost -t /frequency/tx -m "435005"

mosquitto_pub -h localhost -t /frequency/rx -m "435005"

mosquitto_pub -h localhost -t /paris_speed -m "15"

mosquitto_pub -h localhost -t /sine_tone -m "1000"

mosquitto_pub -h localhost -t /space_btw -m "4"

mosquitto_pub -h localhost -t /refresh -n



makefile v pluto program patri k mqtt.h

