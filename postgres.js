var binding = require("binding.node");

var Connection = binding.Connection;

// postgres cannot handle multiple queries at the same time.
// thus we must queue them internally and dispatch them as
// others come in. 
Connection.prototype.maybeDispatchQuery = function () {
  if (!this._queries) return;
  // If not connected, do not dispatch. 
  if (this.readyState != "OK") return;
  if (this._queries.length > 0) this.dispatchQuery(this._queries[0]);
};

Connection.prototype.query = function (sql) {
  if (!this._queries) this._queries = [];
  this._queries.push(sql);
  this.maybeDispatchQuery();
};

exports.createConnection = function (conninfo) {
  var c = new Connection;

  c.addListener("connect", function () {
    c.maybeDispatchQuery();
  });

  c.addListener("result", function () {
    c._queries.shift();
  });

  c.connect(conninfo);

  return c;
};
