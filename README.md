# ngx_mongodb
#编译
./configure --with-ld-opt="-L./mongo/ -lbson -lmongoc" --with-cc-opt="--std=c99" --add-module=./mongo/

#ngx mongodb 功能
1：mongodb 无阻塞模式集成在nginx中，现在只是一个简单的module 有待进一步测试
2：终极目标为mongodb 实现一个简单http rest 接口， 通过前端可以直接操作mongodb 减少中间环节，提高系统吞吐率
3：使用改动过的mongodb master 库， 在mongodb 中集成协程，只改动了mongo recv send connect 接口，以及关闭是的接口， 其他代码未改变， 毕竟不是去直接分析mongodb的代码
