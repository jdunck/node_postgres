var ext = require("binding.node");

exports.createConnection = function (conninfo) {
  var c = new ext.Connection;
  c.connect(conninfo);
  return c;
};

var c = exports.createConnection("host=/var/run/postgresql dbname=test");

c.addListener("connect", function () {
  puts("connected");
  puts(c.readyState);
});

c.addListener("error", function () {
  puts("error");
});
