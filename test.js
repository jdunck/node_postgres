var postgres = require("postgres.js");

var c = postgres.createConnection("host=/var/run/postgresql dbname=test");

c.addListener("connect", function () {
  puts("connected");
  puts(c.readyState);
});

c.addListener("close", function () {
  puts("connection closed.");
});

c.addListener("error", function () {
  puts("error");
});

c.query("select * from xxx;").addCallback(function (rows) {
  puts("result1:");
  p(rows);
});

c.query("select * from xxx limit 1;").addCallback(function (rows) {
  puts("result2:");
  p(rows);
});

c.query("select ____ from xxx limit 1;").addCallback(function (rows) {
  puts("result3:");
  p(rows);
}).addErrback(function (e) {
  puts("error! "+ e.message);
  puts("full: "+ e.full);
  puts("severity: "+ e.severity);
});
