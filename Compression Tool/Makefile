vzip: serial.c
	gcc serial.c -lz -o vzip

debug: serial.c
	gcc serial.c -lz -g -o vzip

test:
	rm -f video.vzip
	./vzip frames
	./check.sh

clean:
	rm -f vzip video.vzip

