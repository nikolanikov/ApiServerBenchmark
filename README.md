#Task documentation and notes:

To achieve the task requirements, to handle as much requests as possible I had to decide which technology to use.
My choices for the test are Node.JS as modern pure JavaScript and pure C server. I'm giving a chance also to PHP 5.4 without opcode caching.

Because I don't have 12 core server, but some 4 shared cores VPS with 1GB RAM. I have to make tests for comparing the server performance.
My simple recursive fibonacci function is:
// number starting from 34 (if we start from 0 it will be much faster, but however we are comparing )
```
 unsigned fibonacci(unsigned number) {
    if (number < 2) return number;
    return fibonacci(number - 1) + fibonacci(number - 2);
}
```

I have created pure Node.js and C app to test the results:

##pure C:

 ~/wiki/purecalc # gcc test.c -o test
 ~/wiki/purecalc # time ./test

real    0m8.568s
user    0m8.560s
sys     0m0.003s
 ~/wiki/purecalc # gcc -O2 test.c -o testO2
 ~/wiki/purecalc # time ./testO2

real    0m4.396s
user    0m4.387s
sys     0m0.003s

##pure NodeJS v0.10.29:

 ~/wiki/purecalcnodejs # time nodejs test.js 
done

real    0m14.515s
user    0m14.714s
sys     0m0.027s

##And PHP example just for reference :)

~/wiki/tests/purecalcphp # time php test.php 

real    6m4.350s
user    6m4.104s
sys     0m0.069s


As the results shows, the compiled with optimizations C code is more than 3 times faster.

##HTTP Server performance

Also I must test the HTTP request performance, so I created the simplest possible Node.js multithreaded server without using any frameworks.
The transactions may be a less than in a production environment, because my test client is on the same machine as the server and it takes a lot of performance to create 10 connections threads all the time and to record the results.
But because the testing environment is the same to all of the servers, it is not of significance. 
I'm using siege to test the performance, the results are recorded in results file:

siege -b -c10 -t60S http://127.0.0.1:8000

Transactions:               1407 hits
Availability:             100.00 %
Elapsed time:              59.92 secs
Data transferred:           0.01 MB
Response time:              0.42 secs
Transaction rate:          23.48 trans/sec
Throughput:             0.00 MB/sec
Concurrency:                9.94
Successful transactions:        1407
Failed transactions:               0
Longest transaction:            1.47
Shortest transaction:           0.14

#C API server:

Node.JS is good choice because you are able to write in pure JavaScript and a lot of engineers are used to with JS, but because we want the fastest possible performance I will choose C.
By making the server on pure C, We have control on every piece of the execution process.

Because the main task is to create an API server, I gathered pieces of existing code and created a pure C web server that is doing the job:

To compile it:
Just 'make'. If you have troubles, please check the Makefile and edit for your environment. 
mkdir -p /tmp/data/Latest_plane_crash
I didn't pay much attention about making it easy to compile on every possible environment. However it don't have any 3rd party requirements.


Firstly I have created a pure C server that is parsing the requests, invokes the fibonacci function and returns the result as response.
This was thread per connection response server and the siege results are below:

Lifting the server siege...-^H      done.

Transactions:               4668 hits
Availability:             100.00 %
Elapsed time:              59.51 secs
Data transferred:           0.04 MB
Response time:              0.13 secs
Transaction rate:          78.44 trans/sec
Throughput:             0.00 MB/sec
Concurrency:                9.98
Successful transactions:        4668
Failed transactions:               0
Longest transaction:            0.38
Shortest transaction:           0.04

Even without thread pool it is still 3 times faster :)
I will modify the source a little to use posix threads pool + poll (epoll is better but is only for linux)

And the result is:

Lifting the server siege...-^H      done.

Transactions:               4477 hits
Availability:             100.00 %
Elapsed time:              59.56 secs
Data transferred:           0.03 MB
Response time:              0.13 secs
Transaction rate:          75.17 trans/sec
Throughput:             0.00 MB/sec
Concurrency:                9.98
Successful transactions:        4477
Failed transactions:               0
Longest transaction:            0.25
Shortest transaction:           0.05

It shows a little decrease in the performance (we are not able to compare exactly the difference on the current stage, because the bottle neck is in the fibonacci and the current load of the server
if 100 fibonacci calculations in the C server is taking 5 secs per thread so for 4 threads 400 fibonacci calculations are taking 5seconds so the 4477 hits are taking 55.96secs just for the calculation.)
There are a lot of TODOs for optimizations, also if we go to epoll it will show significant improvement.
But the most important is that we are able to limit the performance by setting the number of the threads and provide ability for future optimizations.


#API calls part:

### GET /article/Latest_plane_crash (just an example currently every GET returns the Latest_plane_crash)
This call provide read only version of the latest version of the article.
The main reason for doing it this way, because most of the requests are from desktop users or read only users.
We must send the content as fast as possible with less checks as possible.
We are sending the file not by reading it from the disk, but from the latest mapped version in the memory.
If we are reading it for first time, or don't have it mapped in the memory we are mapping it and the on every other request we are sending it from the RAM.
The reason for doing it because the I/O of the RAM is pretty fast compared to the hard drives. The latest data is get from /tmp/data/Latest_plane_crash/

Without the fibonacci, just serving the file the results are:
It is real world example with serving the static content of Latest_plane_crash, so I decided to create a benchmark to show that 20k requests are almost achievable even on my slow VPS that is missing I/O performance.

Transactions:             270639 hits
Availability:             100.00 %
Elapsed time:              59.28 secs
Data transferred:          15.49 MB
Response time:              0.00 secs
Transaction rate:        4565.44 trans/sec
Throughput:             0.26 MB/sec
Concurrency:                2.68
Successful transactions:      270639
Failed transactions:               0
Longest transaction:            0.05
Shortest transaction:           0.00

The Latest_plane_crash content is:
~ # cat /tmp/data/Latest_plane_crash 
<html>
<body>
warning!!!

plane crashed again

be careful when you fly :)
</body>
</html>
----------------------------

However, on every call we are calculating fibonacci(34) as required.


### POST /article/Latest_plane_crash/Version (currently every post upgrades the latest version by creating a new one)
By using POST we are able to upload new version of the file, by providing the current Version.
This way we know what old version has been modified, so we are able to inform that it is not the latest possible version.
The new versions are stored in 
/tmp/data/Latest_plane_crash/4 where 4 is the version

Test:
telnet 127.0.0.1 8080

POST / HTTP/1.1
Host: test.com
Content-Length: 18

new file content

##API calls dynamic:
The idea behind dynamic calls is that we are getting JSON input on request and provide JSON output on the response.
This way we are able to create easily many different dynamic calls. It must be urlencoded or base64(not implemented)

/?{"actions":{"article.get_version":{"name":"Latest_plane_crash"}}}
It provides the latest version of the article in JSON format.

Example response:
{"version":4}

Test:
telnet 127.0.0.1 8080
GET /?%7B%22actions%22%3A%7B%22article.get_version%22%3A%7B%7D%7D%7D HTTP/1.1
Host: rethrthrtht

HTTP/1.1 200 OK
Server: test/1.0
Content-Length: 18
Date: Sat, 09 May 2015 18:17:21 GMT

{"version": 4}



/?{"actions":{"example.hello_world":{}}}
This is example action that returns "Hello World !"
The idea is to show how can be created additional dynamic API calls.

Test:
telnet 127.0.0.1 8080
GET /?%7B%22actions%22%3A%7B%22example.hello_world%22%3A%7B%7D%7D%7D HTTP/1.1
Host: rethrthrtht

HTTP/1.1 200 OK
Server: test/1.0
Content-Length: 13
Date: Sat, 09 May 2015 18:15:48 GMT

Hello world!

Source:
```
int example_hello_world(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options)
{
        struct string entity = string("Hello world!\n");
        response->code = OK;
        if (!response_headers_send(&resources->stream, request, response, entity.length))
                return -1;
        response_entity_send(&resources->stream, response, entity.data, entity.length);
        return 0;
}
```


The dynamic calls are located in the actions folder.


==============================================
#Code structure description:
http_parse.[ch], http.[ch] // the http protocol parser
http_response.[ch], main.c // the main part of the code, where the magic happens
storage.[ch] // Latest_plane_crash related storage handling
json.[ch] //JSON parser cson
arch.[ch],dictionary.c,format.[ch],log.[ch],stream.[ch],vector.c // contains helping functionalities

actions.h // the definition of the dynamic actions
actions/ //the dynamic actions source

==============================================
#Modifications for production use:
There are a lot of TODOs that must be fixed in the code, also the call examples are pretty basic, just to show the basic functionality
with upload and download of the content. There have to be written a lot of synchronization stuff, not this basic versioning.
Also the API is just for example, the serialization/deserialization methods can be changed.
Currently also it is made just for the Latest_plane_crash.



