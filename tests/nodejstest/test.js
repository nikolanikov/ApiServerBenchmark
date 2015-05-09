var http = require("http");
var cluster = require("cluster");
var cpus = 4;
var port = 8000;

function fibonacci(number)
{
 if (number < 2) return number;
 return fibonacci(number - 1) + fibonacci(number - 2);
}

if (cluster.isMaster) {
    for(var i = 0; i < cpus; i++) cluster.fork();
}
else http.createServer(function(req,res) {

    res.writeHeader(200, {"Content-Type": "text/plain"});
    res.write(fibonacci(34).toString());
    res.end();

}).listen(port);

