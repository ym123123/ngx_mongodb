# ngx_mongodb

#编译

./configure --with-ld-opt="-L./mongo/ -lbson -lmongoc" --with-cc-opt="--std=c99" --add-module=./mongo/

#ngx mongodb 功能

1：mongodb 无阻塞模式集成在nginx, 利用了协程， 只是实现了一个先当简单的协程

2：终极目标为mongodb 实现一个简单http rest 接口， 通过前端可以直接操作mongodb 减少中间环节，提高系统吞吐率

3：使用改动过的mongodb master 库， 在mongodb 中集成协程，只改动了mongo recv send connect 接口，以及关闭是的接口， 其他代码未改变， 毕竟不是去直接分析mongodb的代码

4: 第一阶段目标完成， nginx通过mongodb 的接口，成功的和mongodb通信

==================================================================

#ngx mongodb 性能
1: 环境有限， 只进行了简单的测试， 4核机器，centos6，每个请求完整的便利一边mongodb数据库, test.test库中有数据183596条数据， 每个请求便利一边平均话费20ms, 看下面: 
[root@localhost sbin]# ab -n 10000 -c 100 http://127.0.0.1/mongo
This is ApacheBench, Version 2.3 <$Revision: 655654 $>
Copyright 1996 Adam Twiss, Zeus Technology Ltd, http://www.zeustech.net/
Licensed to The Apache Software Foundation, http://www.apache.org/

Benchmarking 127.0.0.1 (be patient)
Completed 1000 requests
Completed 2000 requests
Completed 3000 requests
Completed 4000 requests
Completed 5000 requests
Completed 6000 requests
Completed 7000 requests
Completed 8000 requests
Completed 9000 requests
Completed 10000 requests
Finished 10000 requests


Server Software:        nginx/1.13.1
Server Hostname:        127.0.0.1
Server Port:            80

Document Path:          /mongo
Document Length:        2 bytes

Concurrency Level:      100
Time taken for tests:   198.393 seconds
Complete requests:      10000
Failed requests:        0
Write errors:           0
Total transferred:      1440000 bytes
HTML transferred:       20000 bytes
Requests per second:    50.41 [#/sec] (mean)
Time per request:       1983.929 [ms] (mean)
Time per request:       19.839 [ms] (mean, across all concurrent requests)
Transfer rate:          7.09 [Kbytes/sec] received

Connection Times (ms)
              min  mean[+/-sd] median   max
Connect:        0    0   0.2      0       3
Processing:    49 1977 491.8   1965    3830
Waiting:       46 1977 491.9   1964    3830
Total:         49 1978 491.8   1965    3830

Percentage of the requests served within a certain time (ms)
  50%   1965
  66%   2137
  75%   2252
  80%   2336
  90%   2608
  95%   2879
  98%   3121
  99%   3260
 100%   3830 (longest request)

效果还是不错的

#ngx mongodb rest 接口的实现


