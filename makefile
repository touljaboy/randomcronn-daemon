randomcronn: randomcronn.c
	gcc -o randomcronn randomcronn.c
	touch outfile taskfile
# too many tasks test
test1:
	echo "$$(date +%H:%M);$$(date -d '+1 minute' +%H:%M)" > taskfile
	echo "ls:2" >> taskfile
	echo "cat /home/ern/randomcronn-daemon/README.md:0" >> taskfile
	echo "cat niemamnie.txt:1" >> taskfile
	echo "ls:2" >> taskfile
	echo "ls:2" >> taskfile
	echo "ls:2" >> taskfile
	echo "ls:2" >> taskfile
	echo "ls:2" >> taskfile
	echo "ls:2" >> taskfile
	echo "ls:2" >> taskfile
	echo "ls:2" >> taskfile
	echo "ls:2" >> taskfile
	echo "ls:2" >> taskfile
	
	./randomcronn /home/ern/randomcronn-daemon/taskfile /home/ern/randomcronn-daemon/outfile
	journalctl -t randomcronn | tail
# normal tasks test and SIGINT test
test2:
	echo "$$(date +%H:%M);$$(date -d '+1 minute' +%H:%M)" > taskfile
	echo "ls:2" >> taskfile
	echo "cat /home/ern/randomcronn-daemon/README.md:0" >> taskfile
	echo "cat niemamnie.txt:1" >> taskfile
	./randomcronn /home/ern/randomcronn-daemon/taskfile /home/ern/randomcronn-daemon/outfile
	sleep 35
	journalctl -t randomcronn | tail
	/usr/bin/pidof ./randomcronn | xargs kill -SIGINT
	journalctl -t randomcronn | tail
# reload tasks test SIGUSR1
test3:
	echo "$$(date +%H:%M);$$(date -d '+1 minute' +%H:%M)" > taskfile
	echo "cat /home/ern/randomcronn-daemon/README.md:0" >> taskfile
	echo "cat niemamnie.txt:1" >> taskfile
	./randomcronn /home/ern/randomcronn-daemon/taskfile /home/ern/randomcronn-daemon/outfile
	journalctl -t randomcronn | tail -n 3
	echo "$$(date +%H:%M);$$(date -d '+1 minute' +%H:%M)" > taskfile
	echo "ls -la:2" >> taskfile
	/usr/bin/pidof ./randomcronn | xargs kill -SIGUSR1
	journalctl -t randomcronn | tail -n 3
	/usr/bin/pidof ./randomcronn | xargs kill -SIGINT
# remaining tasks test SIGUSR2
test4:
	echo "$$(date +%H:%M);$$(date -d '+1 minute' +%H:%M)" > taskfile
	echo "cat /home/ern/randomcronn-daemon/README.md:0" >> taskfile
	echo "ls -la:2" >> taskfile
	echo "cat niemamnie.txt:1" >> taskfile
	./randomcronn /home/ern/randomcronn-daemon/taskfile /home/ern/randomcronn-daemon/outfile
	/usr/bin/pidof ./randomcronn | xargs kill -SIGUSR2
	journalctl -t randomcronn | tail -n 3
	sleep 10
	/usr/bin/pidof ./randomcronn | xargs kill -SIGUSR2
	journalctl -t randomcronn | tail -n 3
	/usr/bin/pidof ./randomcronn | xargs kill -SIGINT
#
clean:
	rm -rf randomcronn outfile taskfile
