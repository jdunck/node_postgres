var ext = require("binding.node");

puts(node.version);
puts(ext.hello);

exports.createConnection = function (conninfo) {
  var c = new ext.Connection;
  c.connect(conninfo);
  return c;
};

var c = exports.createConnection("host=localhost port=5432 dbname=test");

puts(c.readyState);


c.addListener("connect", function () {
  puts("connected");
});

c.addListener("error", function () {
  puts("error");
});
