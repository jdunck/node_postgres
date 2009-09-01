var postgres = require("postgres.js");

var c = postgres.createConnection("host=/var/run/postgresql dbname=test");

c.addListener("connect", function () {
  puts("connected");
  puts(c.readyState);
});

c.addListener("error", function () {
  puts("error");
});

c.query("select * from xxx;");
