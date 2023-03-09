# demoServer

## webbench
```bash
sudo apt-get install exuberant-ctags
cd webbench-1.5
make && sudo make install
```

## usage
```bash
# websrv ip port
./websrv 192.168.32.208 12345
# new windows test
webbench -c 1000 -t 5 http://192.168.32.208:12345/
```

## result
![test result](./resources/images/result.png)
