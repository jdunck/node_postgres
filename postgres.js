var ext = require("binding.node");

puts(ext.hello);

exports.createConnection = function (conninfo) {
  var c = new ext.Connection;
  c.connect(conninfo);
  return c;
};

exports.createConnection("host=localhost");
