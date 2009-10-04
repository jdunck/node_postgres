include("/utils.js");
var postgres = require("postgres.js");

var c = postgres.createConnection("host=localhost dbname=ryan");

c.addListener("connect", function () {
  puts("connected");
  puts(c.readyState);
});

c.addListener("close", function (e) {
  puts("connection closed.");
  if (e) {
    puts("error: " + e.message);
  }
});

c.query("select * from test;").addCallback(function (rows) {
  puts("result1:");
  p(rows);
});

c.query("select * from test limit 1;").addCallback(function (rows) {
  puts("result2:");
  p(rows);
});

c.query("select ____ from test limit 1;").addCallback(function (rows) {
  puts("result3:");
  p(rows);
}).addErrback(function (e) {
  puts("error! "+ e.message);
  puts("full: "+ e.full);
  puts("severity: "+ e.severity);
  c.close();
});
